#include "aoc/simulation/event/VisibilityEvents.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/ai/AIBlackboard.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/ui/GameNotifications.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>

namespace aoc::sim {

void VisibilityEventBus::dispatch(const aoc::game::GameState& gameState,
                                  const aoc::map::HexGrid& grid,
                                  const aoc::map::FogOfWar& fog,
                                  const Handler& handler) {
    // Dedupe per dispatch: a unit walking N tiles in a single turn used
    // to emit N EnemyUnitSpotted events at the same destination, each
    // fanning out to every player that could see it. Suppress identical
    // (viewer, type, location, actor) tuples so the notification log
    // doesn't spam the same line dozens of times per turn.
    struct Key {
        PlayerId viewer;
        uint8_t  type;
        int32_t  q;
        int32_t  r;
        PlayerId actor;
        bool operator==(const Key& o) const {
            return viewer == o.viewer && type == o.type
                && q == o.q && r == o.r && actor == o.actor;
        }
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept {
            std::size_t h = static_cast<std::size_t>(k.viewer);
            h = h * 131u + k.type;
            h = h * 131u + static_cast<std::size_t>(k.q);
            h = h * 131u + static_cast<std::size_t>(k.r);
            h = h * 131u + static_cast<std::size_t>(k.actor);
            return h;
        }
    };
    std::unordered_set<Key, KeyHash> seen;
    seen.reserve(this->m_queue.size());

    const int32_t playerCount = gameState.playerCount();
    for (const VisibilityEvent& event : this->m_queue) {
        if (!grid.isValid(event.location)) { continue; }
        const int32_t tileIndex = grid.toIndex(event.location);
        for (int32_t p = 0; p < playerCount; ++p) {
            const PlayerId pid = static_cast<PlayerId>(p);
            if (fog.visibility(pid, tileIndex) != aoc::map::TileVisibility::Visible) {
                continue;
            }
            const Key k{pid, static_cast<uint8_t>(event.type),
                        event.location.q, event.location.r, event.actor};
            if (!seen.insert(k).second) { continue; }
            handler(pid, event);
        }
    }
    this->m_queue.clear();
}

namespace {

constexpr std::size_t kMaxTargets = 16;

void pushUnique(std::vector<aoc::hex::AxialCoord>& list, aoc::hex::AxialCoord loc) {
    if (std::find(list.begin(), list.end(), loc) != list.end()) { return; }
    if (list.size() >= kMaxTargets) {
        list.erase(list.begin());
    }
    list.push_back(loc);
}

} // namespace

void processVisibilityEvents(aoc::game::GameState& gameState,
                             const aoc::map::HexGrid& grid,
                             const aoc::map::FogOfWar& fog,
                             VisibilityEventBus& bus) {
    bus.dispatch(gameState, grid, fog,
                 [&gameState](PlayerId viewer, const VisibilityEvent& event) {
        aoc::game::Player* gsViewer = gameState.player(viewer);
        if (gsViewer == nullptr) { return; }
        aoc::sim::ai::AIBlackboard& bb = gsViewer->blackboard();

        // Skip events the viewer generated themselves (no new information).
        if (event.actor == viewer) { return; }

        const std::string locStr = "(" + std::to_string(event.location.q)
                                  + "," + std::to_string(event.location.r) + ")";
        const std::string actorStr = "Player " + std::to_string(event.actor);
        aoc::ui::GameNotification note;
        note.relevantPlayer = viewer;
        note.otherPlayer    = event.actor;

        switch (event.type) {
            case VisibilityEventType::EnemyUnitSpotted:
            case VisibilityEventType::BarbarianCampSighted:
                pushUnique(bb.attackTargets, event.location);
                note.category = aoc::ui::NotificationCategory::Military;
                note.title = (event.type == VisibilityEventType::BarbarianCampSighted)
                    ? "Barbarian Camp Sighted" : "Enemy Unit Spotted";
                note.body = note.title + " at " + locStr + ".";
                note.priority = 4;
                aoc::ui::pushNotification(note);
                break;
            case VisibilityEventType::CityFounded:
                pushUnique(bb.bestCitySites, event.location);
                note.category = aoc::ui::NotificationCategory::City;
                note.title = "New City Discovered";
                note.body = actorStr + " founded a new city at " + locStr + ".";
                note.priority = 3;
                aoc::ui::pushNotification(note);
                break;
            case VisibilityEventType::ResourceRevealed: {
                pushUnique(bb.bestCitySites, event.location);
                note.category = aoc::ui::NotificationCategory::Economy;
                note.title = "Resource Revealed";
                const uint16_t gid = static_cast<uint16_t>(event.payload);
                const std::string_view goodNameSv = aoc::sim::goodDef(gid).name;
                note.body = std::string(goodNameSv) + " visible at " + locStr + ".";
                note.priority = 3;
                aoc::ui::pushNotification(note);
                break;
            }
            case VisibilityEventType::WonderCompleted:
                note.category = aoc::ui::NotificationCategory::City;
                note.title = "Wonder Completed";
                note.body = actorStr + " completed a wonder at " + locStr + ".";
                note.priority = 6;
                aoc::ui::pushNotification(note);
                break;
            case VisibilityEventType::GreatPersonSpawned:
                note.category = aoc::ui::NotificationCategory::City;
                note.title = "Great Person Arrived";
                note.body = actorStr + " recruited a Great Person at " + locStr + ".";
                note.priority = 5;
                aoc::ui::pushNotification(note);
                break;
        }
    });
}

} // namespace aoc::sim

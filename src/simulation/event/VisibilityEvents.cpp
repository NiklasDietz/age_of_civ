#include "aoc/simulation/event/VisibilityEvents.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/ai/AIBlackboard.hpp"

#include <algorithm>

namespace aoc::sim {

void VisibilityEventBus::dispatch(const aoc::game::GameState& gameState,
                                  const aoc::map::HexGrid& grid,
                                  const aoc::map::FogOfWar& fog,
                                  const Handler& handler) {
    const int32_t playerCount = gameState.playerCount();
    for (const VisibilityEvent& event : this->m_queue) {
        if (!grid.isValid(event.location)) { continue; }
        const int32_t tileIndex = grid.toIndex(event.location);
        for (int32_t p = 0; p < playerCount; ++p) {
            const PlayerId pid = static_cast<PlayerId>(p);
            if (fog.visibility(pid, tileIndex) != aoc::map::TileVisibility::Visible) {
                continue;
            }
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

        switch (event.type) {
            case VisibilityEventType::EnemyUnitSpotted:
            case VisibilityEventType::BarbarianCampSighted:
                pushUnique(bb.attackTargets, event.location);
                break;
            case VisibilityEventType::CityFounded:
                // Near-by foreign foundings become contested expansion sites.
                pushUnique(bb.bestCitySites, event.location);
                break;
            case VisibilityEventType::ResourceRevealed:
                pushUnique(bb.bestCitySites, event.location);
                break;
            case VisibilityEventType::WonderCompleted:
            case VisibilityEventType::GreatPersonSpawned:
                // Informational for now; hooks exist once advisors consume them.
                break;
        }
    });
}

} // namespace aoc::sim

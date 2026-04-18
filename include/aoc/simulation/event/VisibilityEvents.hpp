#pragma once

/**
 * @file VisibilityEvents.hpp
 * @brief Visibility-filtered pub/sub so AI subsystems only react to what their
 *        player can actually see.
 *
 * Design:
 *   1. Game systems call VisibilityEventBus::emit() when something observable
 *      happens at a tile (a city is founded, an enemy army enters a region,
 *      a resource is revealed, a wonder is completed, etc.).
 *   2. Once per turn processVisibilityEvents() walks the queue and, for every
 *      (player, event) pair where FogOfWar reports Visible at the event tile,
 *      forwards the event to that player's AIBlackboard / listener hook.
 *   3. The bus is drained at the end of dispatch so events fire exactly once.
 *
 * This keeps emitters cheap (no per-player filtering at emission time) and
 * guarantees AI decisions are driven only by information the player would
 * legitimately have. Genes such as espionagePriority or militaryAggression
 * can then scale the reaction weight without exposing hidden state.
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; class FogOfWar; }

namespace aoc::sim {

enum class VisibilityEventType : uint8_t {
    EnemyUnitSpotted,    ///< Hostile military unit entered a visible tile.
    CityFounded,         ///< New city appeared.
    ResourceRevealed,    ///< Strategic/luxury resource became visible.
    WonderCompleted,     ///< World/national wonder finished.
    GreatPersonSpawned,  ///< A Great Person entity became visible.
    BarbarianCampSighted,
};

struct VisibilityEvent {
    VisibilityEventType  type;
    aoc::hex::AxialCoord location;
    PlayerId             actor = INVALID_PLAYER;  ///< Owner of the observable subject.
    int32_t              payload = 0;             ///< Type-specific (unit type id, good id, wonder id).
};

/// Pub/sub queue for tile-anchored observations.
class VisibilityEventBus {
public:
    using Handler = std::function<void(PlayerId, const VisibilityEvent&)>;

    void emit(VisibilityEvent event) {
        this->m_queue.push_back(event);
    }

    [[nodiscard]] std::size_t pending() const { return this->m_queue.size(); }

    /// Iterate queued events and call `handler(player, event)` for every
    /// (player, event) pair where fog reports the event's tile Visible.
    /// After dispatch, the queue is cleared.
    void dispatch(const aoc::game::GameState& gameState,
                  const aoc::map::HexGrid& grid,
                  const aoc::map::FogOfWar& fog,
                  const Handler& handler);

    void clear() { this->m_queue.clear(); }

private:
    std::vector<VisibilityEvent> m_queue;
};

/// Default dispatcher: pushes visible events onto each player's AIBlackboard.
/// Populates attackTargets, bestCitySites, etc. as a function of event type.
void processVisibilityEvents(aoc::game::GameState& gameState,
                             const aoc::map::HexGrid& grid,
                             const aoc::map::FogOfWar& fog,
                             VisibilityEventBus& bus);

} // namespace aoc::sim

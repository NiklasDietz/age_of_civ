#pragma once

/**
 * @file AISettlerController.hpp
 * @brief AI subsystem for settler management: city founding, location scoring,
 *        and settler movement decisions.
 *
 * Tracking uses axial map coordinates as the stable identity key instead of
 * unit pointer addresses.  This ensures the stuck-turn counter survives when
 * a fresh settler is produced at the same tile as its predecessor, which is
 * the normal case when a city keeps training settlers to expand.
 */

#include "aoc/core/Types.hpp"
#include "aoc/ui/MainMenu.hpp"
#include "aoc/map/HexCoord.hpp"

#include <unordered_map>

namespace aoc::game {
class GameState;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim::ai {

/**
 * @brief Handles settler AI: evaluating city locations, moving settlers,
 *        and founding new cities when a suitable site is reached.
 *
 * Per-settler state is keyed on the settler's current map position rather than
 * its pointer address.  A new settler produced at a city tile inherits the
 * stuck-turn count from the previous settler at that position, so the 1-turn
 * stuck threshold is reached on the very next turn and the city is founded
 * adjacent to (or at) the current tile.
 */
class AISettlerController {
public:
    explicit AISettlerController(PlayerId player, aoc::ui::AIDifficulty difficulty);

    /**
     * @brief Process all settler units for this player for one turn.
     *
     * For each settler: on first activation the best city location within
     * radius 15 is computed and stored as the target.  The settler moves toward
     * it each turn and founds a city upon arrival.  If the target is occupied
     * by an enemy the search is repeated.  A settler stuck for 1 consecutive
     * turn without moving is force-founded at its current position (or the best
     * adjacent passable tile if the current tile is already a city).
     *
     * @param gameState  Game state holding all players, cities and units.
     * @param grid       Hex grid for terrain queries and pathfinding.
     */
    void executeSettlerActions(aoc::game::GameState& gameState, aoc::map::HexGrid& grid);

private:
    PlayerId              m_player;
    aoc::ui::AIDifficulty m_difficulty;

    /// Number of consecutive turns each settler has remained at the same
    /// position without founding a city, keyed by tile coordinate.
    std::unordered_map<aoc::hex::AxialCoord, int32_t> m_settlerStuckTurns;

    /// Pre-computed target city location for each settler, keyed by the
    /// settler's tile coordinate when the target was assigned.  Cleared when
    /// the settler founds a city, force-founds, or the target is stolen.
    std::unordered_map<aoc::hex::AxialCoord, aoc::hex::AxialCoord> m_settlerTargets;
};

} // namespace aoc::sim::ai

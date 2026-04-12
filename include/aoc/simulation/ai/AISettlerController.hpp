#pragma once

/**
 * @file AISettlerController.hpp
 * @brief AI subsystem for settler management: city founding, location scoring,
 *        and settler movement decisions.
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
 * Tracks per-settler movement history to detect settlers that are stuck
 * wandering without finding a suitable site, and forces city founding after
 * a configurable number of turns to prevent indefinite searching.
 */
class AISettlerController {
public:
    explicit AISettlerController(PlayerId player, aoc::ui::AIDifficulty difficulty);

    /**
     * @brief Process all settler units for this player.
     *
     * For each settler: on first activation the best city location within
     * radius 15 is computed and stored as the target. The settler moves toward
     * it each turn and founds a city upon arrival. If the target is taken by an
     * enemy the search is repeated. Settlers stuck for 3+ consecutive turns
     * without moving are force-founded at their current position if the tile is
     * passable.
     *
     * @param gameState  Game state holding all players, cities and units.
     * @param grid       Hex grid for terrain queries and pathfinding.
     */
    void executeSettlerActions(aoc::game::GameState& gameState, aoc::map::HexGrid& grid);

private:
    PlayerId              m_player;
    aoc::ui::AIDifficulty m_difficulty;

    /// Number of consecutive turns each settler has remained at the same
    /// position without founding a city, keyed by unit pointer identity.
    std::unordered_map<uintptr_t, int32_t> m_settlerStuckTurns;

    /// Last known position of each settler, used to detect no-movement turns.
    std::unordered_map<uintptr_t, aoc::hex::AxialCoord> m_settlerLastPosition;

    /// Pre-computed target city location for each settler.  Set on first
    /// activation, cleared when the settler founds a city, becomes stuck, or
    /// the target tile is occupied by an enemy.
    std::unordered_map<uintptr_t, aoc::hex::AxialCoord> m_settlerTargets;
};

} // namespace aoc::sim::ai

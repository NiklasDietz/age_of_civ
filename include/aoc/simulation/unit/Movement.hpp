#pragma once

/**
 * @file Movement.hpp
 * @brief Unit movement logic: path execution and movement point deduction.
 */

#include "aoc/map/HexCoord.hpp"
#include "aoc/core/Types.hpp"

namespace aoc::game {
class GameState;
class Unit;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

/**
 * @brief Move a unit one step along its pending path.
 *
 * Deducts movement points. If the unit runs out of movement, it stops
 * with the remaining path preserved for next turn.
 *
 * @param gameState  Full game state (needed for stacking, ZoC, and city capture checks).
 * @param unit       The unit to move.
 * @param grid       Hex grid for terrain and movement costs.
 * @return true if the unit moved at least one tile, false if it could not move.
 */
bool moveUnitAlongPath(aoc::game::GameState& gameState, aoc::game::Unit& unit,
                       const aoc::map::HexGrid& grid);

/**
 * @brief Set a movement order for a unit (replaces any existing path).
 *
 * Computes an A* path from the unit's current position to goal and stores it
 * in the unit's pending path. The unit will follow the path on subsequent
 * calls to moveUnitAlongPath / executeMovement.
 *
 * @param unit   The unit receiving the order.
 * @param goal   Target tile.
 * @param grid   Hex grid for pathfinding.
 * @return true if a path was found, false if the destination is unreachable.
 */
bool orderUnitMove(aoc::game::Unit& unit,
                   aoc::hex::AxialCoord goal, const aoc::map::HexGrid& grid);

/**
 * @brief Restore movement points for all units belonging to a player.
 *
 * Called at the start of each player's turn.
 */
void refreshMovement(aoc::game::GameState& gameState, PlayerId player);

/**
 * @brief Execute pending movement for all units of a player.
 *
 * Moves each unit along its stored path as far as its movement points allow.
 */
void executeMovement(aoc::game::GameState& gameState, PlayerId player,
                     const aoc::map::HexGrid& grid);

} // namespace aoc::sim

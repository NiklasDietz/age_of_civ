#pragma once

/**
 * @file Movement.hpp
 * @brief Unit movement logic: path execution and movement point deduction.
 */

#include "aoc/map/HexCoord.hpp"
#include "aoc/core/Types.hpp"

namespace aoc::ecs {
class World;
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
 * @return true if the unit moved at least one tile, false if it couldn't move.
 */
bool moveUnitAlongPath(aoc::ecs::World& world, EntityId unitEntity,
                       const aoc::map::HexGrid& grid);

/**
 * @brief Set a movement order for a unit (replaces existing path).
 *
 * Computes A* path from current position to goal and stores it
 * in the unit's pendingPath.
 *
 * @return true if a path was found, false if destination is unreachable.
 */
bool orderUnitMove(aoc::ecs::World& world, EntityId unitEntity,
                   hex::AxialCoord goal, const aoc::map::HexGrid& grid);

/**
 * @brief Restore movement points for all units of a player.
 *
 * Called at the start of each turn.
 */
void refreshMovement(aoc::ecs::World& world, PlayerId player);

/**
 * @brief Execute pending movement for all units of a player.
 *
 * Moves each unit along its path as far as movement points allow.
 */
void executeMovement(aoc::ecs::World& world, PlayerId player,
                     const aoc::map::HexGrid& grid);

} // namespace aoc::sim

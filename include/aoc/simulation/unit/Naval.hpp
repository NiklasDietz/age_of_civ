#pragma once

/**
 * @file Naval.hpp
 * @brief Naval unit movement, embarkation, and transport logic.
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

namespace aoc::ecs {
class World;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

/**
 * @brief Attempt to embark a land unit onto a coast tile.
 *
 * Changes the unit state to Embarked and consumes all remaining movement.
 * The unit must be a land unit adjacent to the target coast tile.
 *
 * @param world      ECS world containing unit components.
 * @param unitEntity Entity ID of the land unit to embark.
 * @param coastTile  Target coast/water tile.
 * @param grid       Hex grid for terrain checks.
 * @return true if embarkation succeeded.
 */
[[nodiscard]] bool tryEmbark(aoc::ecs::World& world,
                              EntityId unitEntity,
                              hex::AxialCoord coastTile,
                              const aoc::map::HexGrid& grid);

/**
 * @brief Attempt to disembark an embarked unit onto a land tile.
 *
 * Changes the unit state back to Idle and consumes all remaining movement.
 * The unit must be Embarked and adjacent to the target land tile.
 *
 * @param world      ECS world containing unit components.
 * @param unitEntity Entity ID of the embarked unit.
 * @param landTile   Target land tile.
 * @param grid       Hex grid for terrain checks.
 * @return true if disembarkation succeeded.
 */
[[nodiscard]] bool tryDisembark(aoc::ecs::World& world,
                                EntityId unitEntity,
                                hex::AxialCoord landTile,
                                const aoc::map::HexGrid& grid);

/**
 * @brief Check if a unit type can traverse water tiles.
 *
 * Returns true for Naval class units. Embarked status is checked separately
 * via the unit's current state.
 *
 * @param typeId The unit type to check.
 * @return true if the unit type is inherently water-capable.
 */
[[nodiscard]] bool canTraverseWater(UnitTypeId typeId);

} // namespace aoc::sim

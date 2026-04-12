#pragma once

/**
 * @file Naval.hpp
 * @brief Naval unit movement, embarkation, and transport logic.
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

namespace aoc::game { class Unit; }
namespace aoc::map  { class HexGrid; }

namespace aoc::sim {

/**
 * @brief Attempt to embark a land unit onto an adjacent coast tile.
 *
 * Changes the unit state to Embarked and consumes all remaining movement.
 * The unit must be a non-naval land unit adjacent to the target coast tile.
 *
 * @param unit       The land unit to embark.
 * @param coastTile  Target coast tile.
 * @param grid       Hex grid for terrain checks.
 * @return true if embarkation succeeded.
 */
[[nodiscard]] bool tryEmbark(aoc::game::Unit& unit,
                              hex::AxialCoord coastTile,
                              const aoc::map::HexGrid& grid);

/**
 * @brief Attempt to disembark an embarked unit onto an adjacent land tile.
 *
 * Changes the unit state back to Idle and consumes all remaining movement.
 * The unit must be Embarked and adjacent to the target land tile.
 *
 * @param unit      The embarked unit.
 * @param landTile  Target land tile.
 * @param grid      Hex grid for terrain checks.
 * @return true if disembarkation succeeded.
 */
[[nodiscard]] bool tryDisembark(aoc::game::Unit& unit,
                                hex::AxialCoord landTile,
                                const aoc::map::HexGrid& grid);

/**
 * @brief Check if a unit type can traverse water tiles natively.
 *
 * Returns true for Naval class units. Embarked status is checked separately
 * via the unit's current state.
 *
 * @param typeId The unit type to check.
 * @return true if the unit type is inherently water-capable.
 */
[[nodiscard]] bool canTraverseWater(UnitTypeId typeId);

} // namespace aoc::sim

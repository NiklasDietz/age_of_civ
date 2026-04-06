#pragma once

/**
 * @file Improvement.hpp
 * @brief Tile improvement definitions and placement logic.
 *
 * Improvements are built by Builder units on valid terrain. Each improvement
 * type has terrain/feature prerequisites and grants a yield bonus to the tile.
 */

#include "aoc/map/HexGrid.hpp"
#include "aoc/core/Types.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace aoc::sim {

/// Static definition of a tile improvement.
struct ImprovementDef {
    aoc::map::ImprovementType type;
    std::string_view          name;
    aoc::map::TileYield       yieldBonus;
    TechId                    requiredTech;   ///< Tech prerequisite (invalid = none)
    int32_t                   buildTurns;     ///< Turns to construct (instant for now)
};

/// Hard-coded improvement definitions.
inline constexpr std::array<ImprovementDef, 14> IMPROVEMENT_DEFS = {{
    {aoc::map::ImprovementType::Farm,         "Farm",          {1, 0, 0, 0, 0, 0}, TechId{},   3},
    {aoc::map::ImprovementType::Mine,         "Mine",          {0, 1, 0, 0, 0, 0}, TechId{0},  3},
    {aoc::map::ImprovementType::Plantation,   "Plantation",    {0, 0, 1, 0, 0, 0}, TechId{},   4},
    {aoc::map::ImprovementType::Quarry,       "Quarry",        {0, 1, 0, 0, 0, 0}, TechId{0},  3},
    {aoc::map::ImprovementType::LumberMill,   "Lumber Mill",   {0, 1, 0, 0, 0, 0}, TechId{},   3},
    {aoc::map::ImprovementType::Camp,         "Camp",          {0, 0, 1, 0, 0, 0}, TechId{},   3},
    {aoc::map::ImprovementType::Pasture,      "Pasture",       {1, 0, 0, 0, 0, 0}, TechId{},   3},
    {aoc::map::ImprovementType::FishingBoats, "Fishing Boats", {1, 0, 1, 0, 0, 0}, TechId{},   4},
    {aoc::map::ImprovementType::Fort,         "Fort",          {0, 0, 0, 0, 0, 0}, TechId{},   5},
    {aoc::map::ImprovementType::Road,         "Road",          {0, 0, 0, 0, 0, 0}, TechId{},   2},
    {aoc::map::ImprovementType::Railway,      "Railway",       {0, 1, 0, 0, 0, 0}, TechId{7},  4}, // +1 prod, requires Steel tech
    {aoc::map::ImprovementType::Highway,      "Highway",       {0, 0, 1, 0, 0, 0}, TechId{16}, 3}, // +1 gold, requires Computers tech
    {aoc::map::ImprovementType::Dam,          "Dam",           {0, 1, 0, 0, 0, 0}, TechId{7},  6}, // +1 prod, river-only
    {aoc::map::ImprovementType::None,         "None",          {0, 0, 0, 0, 0, 0}, TechId{},   0},
}};

/**
 * @brief Check if an improvement can be placed on a tile.
 *
 * Validates terrain/feature prerequisites for the given improvement type.
 *
 * @param grid   The hex grid.
 * @param index  Tile flat index.
 * @param type   The improvement to check.
 * @return true if the improvement can be placed.
 */
[[nodiscard]] bool canPlaceImprovement(const aoc::map::HexGrid& grid,
                                       int32_t index,
                                       aoc::map::ImprovementType type);

/**
 * @brief Auto-pick the best improvement for a tile based on terrain and features.
 *
 * @param grid   The hex grid.
 * @param index  Tile flat index.
 * @return The recommended improvement, or ImprovementType::None if none is suitable.
 */
[[nodiscard]] aoc::map::ImprovementType bestImprovementForTile(
    const aoc::map::HexGrid& grid, int32_t index);

} // namespace aoc::sim

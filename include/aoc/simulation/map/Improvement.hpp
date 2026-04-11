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
inline constexpr std::array<ImprovementDef, 20> IMPROVEMENT_DEFS = {{
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
    {aoc::map::ImprovementType::Railway,      "Railway",       {0, 1, 0, 0, 0, 0}, TechId{7},  4},
    {aoc::map::ImprovementType::Highway,      "Highway",       {0, 0, 1, 0, 0, 0}, TechId{16}, 3},
    {aoc::map::ImprovementType::Dam,          "Dam",           {0, 1, 0, 0, 0, 0}, TechId{7},  6},
    // Cultivated export improvements: produce luxury/strategic goods without natural resources
    {aoc::map::ImprovementType::Vineyard,     "Vineyard",      {0, 0, 2, 0, 0, 0}, TechId{},   4}, // +2 gold (wine exports)
    {aoc::map::ImprovementType::SilkFarm,     "Silk Farm",     {0, 0, 2, 0, 0, 0}, TechId{},   4}, // +2 gold (silk exports)
    {aoc::map::ImprovementType::SpiceFarm,    "Spice Farm",    {0, 0, 2, 0, 0, 0}, TechId{},   4}, // +2 gold (spice exports)
    {aoc::map::ImprovementType::DyeWorks,     "Dye Works",     {0, 0, 2, 0, 0, 0}, TechId{},   4}, // +2 gold (dye exports)
    {aoc::map::ImprovementType::CottonField,  "Cotton Field",  {0, 1, 1, 0, 0, 0}, TechId{},   3}, // +1 prod, +1 gold
    {aoc::map::ImprovementType::Workshop,     "Workshop",      {0, 2, 1, 0, 0, 0}, TechId{0},  5}, // +2 prod, +1 gold (manufactured goods)
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

/**
 * @brief Compute the farm adjacency food bonus for a tile.
 *
 * If the tile has a Farm improvement and 2+ adjacent tiles also have Farms,
 * returns +1 food (Civ 6 Feudalism-style farm triangle bonus).
 * The caller should gate this on the Feudalism civic (CivicId{6}).
 *
 * @param grid   The hex grid.
 * @param index  Tile flat index.
 * @return Extra food from farm adjacency (0 or 1).
 */
[[nodiscard]] int32_t computeFarmAdjacencyBonus(const aoc::map::HexGrid& grid, int32_t index);

/**
 * @brief Check if a tile can be prospected by a Prospector unit.
 *
 * Requires: no existing resource, land tile, not on cooldown from prior survey.
 */
[[nodiscard]] bool canProspect(const aoc::map::HexGrid& grid, int32_t index);

/**
 * @brief Prospect a tile for undiscovered resource deposits.
 *
 * A Prospector unit surveys the terrain. Success depends on terrain type
 * and tech level. On success, a new resource with reserves is placed.
 * On failure, the tile gets a cooldown (cannot be re-prospected for N turns).
 *
 * Terrain probabilities (base, before tech modifiers):
 *   Hills:      25% chance of mineral (iron, copper, gold, silver, tin, coal)
 *   Desert:     15% chance of oil
 *   Plains:     10% chance of oil or niter
 *   Tundra:     15% chance of coal or gems
 *   Other land: 5% chance of stone or clay
 *
 * @param grid       The hex grid (mutated on success + cooldown on failure).
 * @param index      Tile flat index.
 * @param techBonus  Tech-based success rate bonus (0.0 = no bonus, 0.15 = Geology, 0.30 = Seismology).
 * @param rngSeed    Deterministic seed for this prospect attempt.
 * @return true if a resource was discovered.
 */
bool prospectTile(aoc::map::HexGrid& grid, int32_t index,
                  float techBonus, uint32_t rngSeed);

} // namespace aoc::sim

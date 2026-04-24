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
inline constexpr std::array<ImprovementDef, 40> IMPROVEMENT_DEFS = {{
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
    // WP-C7: aligned with TechTree.cpp. Railway/Dam need industrial-era
    // engineering (11 Industrialization), Highway needs mass production (15).
    {aoc::map::ImprovementType::Railway,      "Railway",       {0, 1, 0, 0, 0, 0}, TechId{11}, 4},
    {aoc::map::ImprovementType::Highway,      "Highway",       {0, 0, 1, 0, 0, 0}, TechId{15}, 3},
    {aoc::map::ImprovementType::Dam,          "Dam",           {0, 1, 0, 0, 0, 0}, TechId{11}, 6},
    // Cultivated export improvements: produce luxury/strategic goods without natural resources
    {aoc::map::ImprovementType::Vineyard,     "Vineyard",      {0, 0, 2, 0, 0, 0}, TechId{},   4}, // +2 gold (wine exports)
    {aoc::map::ImprovementType::SilkFarm,     "Silk Farm",     {0, 0, 2, 0, 0, 0}, TechId{},   4}, // +2 gold (silk exports)
    {aoc::map::ImprovementType::SpiceFarm,    "Spice Farm",    {0, 0, 2, 0, 0, 0}, TechId{},   4}, // +2 gold (spice exports)
    {aoc::map::ImprovementType::DyeWorks,     "Dye Works",     {0, 0, 2, 0, 0, 0}, TechId{},   4}, // +2 gold (dye exports)
    {aoc::map::ImprovementType::CottonField,  "Cotton Field",  {0, 1, 1, 0, 0, 0}, TechId{},   3}, // +1 prod, +1 gold
    {aoc::map::ImprovementType::Workshop,     "Workshop",      {0, 2, 1, 0, 0, 0}, TechId{0},  5}, // +2 prod, +1 gold (manufactured goods)
    {aoc::map::ImprovementType::Canal,        "Canal",         {0, 0, 2, 0, 0, 0}, TechId{11}, 0}, // +2 gold (toll revenue). Requires Industrialization. Built via terrain project, not builder.
    {aoc::map::ImprovementType::MountainMine, "Mountain Mine", {0, 1, 0, 0, 0, 0}, TechId{0},  5}, // +1 prod. Mountain + metal resource + adjacent owned land. Requires Mining.
    {aoc::map::ImprovementType::Observatory,  "Observatory",   {0, 0, 0, 2, 0, 0}, TechId{},   5}, // +2 science. Hills only.
    {aoc::map::ImprovementType::Monastery,    "Monastery",     {0, 0, 0, 0, 1, 1}, TechId{},   4}, // +1 culture, +1 faith. Any land.
    {aoc::map::ImprovementType::HeritageSite, "Heritage Site", {0, 0, 0, 0, 2, 0}, TechId{},   5}, // +2 culture. Any land.
    // Modern / industrial improvements (gated by tech). Some consume food to
    // produce production (simulate resource-conversion: biogas, recycling).
    // WP-C7: tech IDs aligned with TechTree.cpp names. Comments describe
    // intended tech gate; earlier defs pointed at incorrect numerical IDs.
    {aoc::map::ImprovementType::TerraceFarm,      "Terrace Farm",      {1, 0, 0, 0, 0, 0}, TechId{2},  4}, // +1 food. Hills. Pottery (Masonry-analog).
    {aoc::map::ImprovementType::BiogasPlant,      "Biogas Plant",      {-1, 2, 0, 0, 0, 0}, TechId{11}, 5}, // +2 prod, -1 food. Industrialization.
    {aoc::map::ImprovementType::SolarFarm,        "Solar Farm",        {0, 0, 2, 1, 0, 0}, TechId{14}, 5}, // +2 gold, +1 sci. Electricity.
    {aoc::map::ImprovementType::WindFarm,         "Wind Farm",         {0, 2, 0, 0, 0, 0}, TechId{14}, 4}, // +2 prod. Electricity.
    {aoc::map::ImprovementType::OffshorePlatform, "Offshore Platform", {0, 2, 2, 0, 0, 0}, TechId{11}, 6}, // Coast + oil. Industrialization.
    {aoc::map::ImprovementType::RecyclingCenter,  "Recycling Center",  {-1, 2, 0, 0, 0, 0}, TechId{14}, 5}, // Any land. +2 prod, -1 food. Electricity.
    // Extended modern set
    {aoc::map::ImprovementType::GeothermalVent,    "Geothermal Vent",    {0, 1, 0, 0, 0, 1}, TechId{0},  5}, // Mountain-adjacent flag. Enables GeothermalPlant building.
    {aoc::map::ImprovementType::DesalinationPlant, "Desalination Plant", {3, 0, 0, 0, 0, 0}, TechId{11}, 6}, // Coast. +3 food. Industrialization.
    {aoc::map::ImprovementType::VerticalFarm,      "Vertical Farm",      {3, -1, 0, 0, 0, 0}, TechId{14}, 6}, // Any land. +3 food, -1 prod. Electricity.
    {aoc::map::ImprovementType::DataCenter,        "Data Center",        {-1, 0, 0, 3, 0, 0}, TechId{16}, 7}, // Any land. +3 sci, -1 food. Computers.
    {aoc::map::ImprovementType::TradingPost,       "Trading Post",       {0, 0, 2, 0, 0, 0}, TechId{5},  3}, // Desert/Plains. +2 gold. Currency.
    {aoc::map::ImprovementType::MangroveNursery,   "Mangrove Nursery",   {1, 0, 0, 0, 1, 0}, TechId{14}, 4}, // Marsh/Coast+marsh. +1 food +1 culture.
    {aoc::map::ImprovementType::KelpFarm,          "Kelp Farm",          {2, 0, 0, 1, 0, 0}, TechId{14}, 4}, // Coast. +2 food +1 sci.
    {aoc::map::ImprovementType::FishFarm,          "Fish Farm",          {2, 0, 1, 0, 0, 0}, TechId{2},  4}, // Coast/ShallowWater. +2 food +1 gold.
    // WP-C4: Greenhouse enables crop growth on off-climate tiles. Gated by
    // Advanced Chemistry (24, our Biology-analog). Flat +2 food for now.
    {aoc::map::ImprovementType::Greenhouse,        "Greenhouse",         {2, 0, 0, 0, 0, 0}, TechId{24}, 5},
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

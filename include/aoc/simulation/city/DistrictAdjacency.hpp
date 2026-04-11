#pragma once

/**
 * @file DistrictAdjacency.hpp
 * @brief District adjacency bonus system and city project definitions.
 *
 * === Adjacency Bonuses ===
 * Districts gain yield bonuses based on what's adjacent to them:
 *   Campus:      +1 science per adjacent Mountain, +0.5 per Rainforest/Reef
 *   Commercial:  +2 gold per adjacent River, +0.5 per adjacent district
 *   Industrial:  +1 production per adjacent Mine, +0.5 per adjacent district
 *   Harbor:      +2 gold per adjacent coastal resource, +1 per adjacent district
 *   Holy Site:   +1 faith per adjacent Mountain, +0.5 per Forest/Wonder
 *   Encampment:  +1 defense per adjacent Hills, no yield adjacency
 *
 * === Appeal System ===
 * Each tile has an appeal rating (affects tourism, housing desirability):
 *   Positive: Coast, Mountain (nearby), Natural Wonder, Forest, Holy Site
 *   Negative: Industrial Zone, Encampment, Rainforest, Marsh, Mine
 *   Appeal determines if a tile can become a National Park or Seaside Resort.
 *
 * === City Projects ===
 * Temporary investments that take production but give one-time or timed bonuses:
 *   Bread & Circuses: +loyalty boost for 10 turns (costs 50 production)
 *   Campus Research: +science burst equal to 2 turns of output
 *   Industrial Surge: +production burst for building currently in queue
 *   Commercial Hub Investment: +gold burst
 *   Harbor Shipyard Rush: -50% production cost for next naval unit
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/simulation/city/District.hpp"

#include <array>
#include <cstdint>

namespace aoc::map { class HexGrid; }
namespace aoc::game { class GameState; }

namespace aoc::sim {

// ============================================================================
// Adjacency bonus computation
// ============================================================================

/// Yield bonus from adjacency for a placed district.
struct AdjacencyBonus {
    float food       = 0.0f;
    float production = 0.0f;
    float gold       = 0.0f;
    float science    = 0.0f;
    float culture    = 0.0f;
    float faith      = 0.0f;
};

/**
 * @brief Compute adjacency bonuses for a district placed at a specific tile.
 *
 * Examines all 6 neighboring tiles and calculates yield bonuses based on
 * the district type and what's adjacent.
 *
 * @param grid       Hex grid.
 * @param world      ECS world (for checking other districts).
 * @param districtType  Type of the district being checked.
 * @param tileIndex     Tile where the district is placed.
 * @return Computed adjacency bonus.
 */
[[nodiscard]] AdjacencyBonus computeAdjacencyBonus(
    const aoc::map::HexGrid& grid,
    const aoc::game::GameState& gameState,
    DistrictType districtType,
    int32_t tileIndex);

// ============================================================================
// Appeal system
// ============================================================================

/**
 * @brief Compute the appeal rating for a tile.
 *
 * Positive appeal: +1 Coast adjacent, +1 Mountain adjacent, +2 Natural Wonder,
 *                  +1 Forest, +1 Holy Site adjacent
 * Negative appeal: -1 Industrial Zone adjacent, -1 Encampment adjacent,
 *                  -1 Rainforest, -1 Marsh, -1 Mine
 *
 * @param grid       Hex grid.
 * @param world      ECS world.
 * @param tileIndex  Tile to compute appeal for.
 * @return Appeal rating (can be negative).
 */
[[nodiscard]] int32_t computeTileAppeal(const aoc::map::HexGrid& grid,
                                        const aoc::game::GameState& gameState,
                                        int32_t tileIndex);

// ============================================================================
// City Projects
// ============================================================================

enum class CityProjectType : uint8_t {
    BreadAndCircuses,    ///< +20 loyalty for 10 turns
    CampusResearch,      ///< Burst of science
    IndustrialSurge,     ///< Burst of production
    CommercialInvestment,///< Burst of gold
    ShipyardRush,        ///< -50% next naval unit cost
    MilitaryTraining,    ///< +XP for all units trained in this city

    Count
};

struct CityProjectDef {
    CityProjectType  type;
    std::string_view name;
    int32_t          productionCost;
    DistrictType     requiredDistrict;
};

inline constexpr std::array<CityProjectDef, 6> CITY_PROJECT_DEFS = {{
    {CityProjectType::BreadAndCircuses,     "Bread and Circuses",   50, DistrictType::CityCenter},
    {CityProjectType::CampusResearch,       "Campus Research Grant", 80, DistrictType::Campus},
    {CityProjectType::IndustrialSurge,      "Industrial Surge",     80, DistrictType::Industrial},
    {CityProjectType::CommercialInvestment, "Commercial Investment", 60, DistrictType::Commercial},
    {CityProjectType::ShipyardRush,         "Shipyard Rush",        60, DistrictType::Harbor},
    {CityProjectType::MilitaryTraining,     "Military Training",    70, DistrictType::Encampment},
}};

/**
 * @brief Complete a city project. Applies the one-time effect.
 */
void completeCityProject(aoc::game::GameState& gameState, EntityId cityEntity,
                         CityProjectType project);

} // namespace aoc::sim

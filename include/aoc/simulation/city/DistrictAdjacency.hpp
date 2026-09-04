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

#include "aoc/map/HexCoord.hpp"

#include <array>
#include <cstdint>
#include <unordered_map>

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
// Shared adjacency primitives
// ============================================================================

/// Traits counted among a tile's six neighbours. One walk serves the district
/// bonus, Campus science, Holy Site faith and Harbor gold, which each used to
/// open-code the same loop over their own subset of these predicates.
struct NeighborTerrainCounts {
    int32_t mountains        = 0;
    int32_t forests          = 0;
    int32_t rainforests      = 0;
    int32_t hills            = 0;
    int32_t mines            = 0;
    int32_t quarries         = 0;
    int32_t wonders          = 0;
    int32_t coastalResources = 0;
};

/// Count the traits of the six tiles around `center`. Off-map neighbours are
/// skipped. Counters are integers, so the result does not depend on the order
/// the neighbours are visited.
[[nodiscard]] NeighborTerrainCounts countNeighborTerrain(const aoc::map::HexGrid& grid,
                                                          aoc::hex::AxialCoord center);

/// Every player's placed districts keyed by tile, so an adjacency check is a
/// hash lookup instead of a walk over every district of every city.
///
/// Determinism: the map is only ever queried by key and never iterated, so its
/// bucket order cannot reach a result. Do not add an iterating accessor without
/// sorting the keys first.
class DistrictIndex {
public:
    /// Districts standing on one tile. Counts, not flags: two districts may
    /// legitimately share a tile and the bonus counts each.
    struct TileDistricts {
        int32_t total      = 0;
        int32_t harbor     = 0;
        int32_t industrial = 0;
        int32_t cityCenter = 0;
        int32_t campus     = 0;
    };

    /// Rebuild from the current world. Cheap enough to call once per city.
    void build(const aoc::game::GameState& gameState);

    /// Districts on `location`; all-zero when none.
    [[nodiscard]] TileDistricts at(aoc::hex::AxialCoord location) const;

private:
    [[nodiscard]] static int64_t key(aoc::hex::AxialCoord c) {
        return (static_cast<int64_t>(c.q) << 32) ^ static_cast<uint32_t>(c.r);
    }
    std::unordered_map<int64_t, TileDistricts> m_byTile;
};

/// Adjacency bonus using a prebuilt index. The four-argument overload builds a
/// throwaway index per call; prefer this one when scoring several districts.
[[nodiscard]] AdjacencyBonus computeAdjacencyBonus(
    const aoc::map::HexGrid& grid,
    const DistrictIndex& districts,
    DistrictType districtType,
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

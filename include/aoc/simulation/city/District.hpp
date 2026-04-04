#pragma once

/**
 * @file District.hpp
 * @brief District and building type definitions.
 *
 * Districts are placed on tiles adjacent to the city center. Each district
 * type enables specific buildings which in turn enable production recipes.
 * The building IDs match the requiredBuilding field in ProductionRecipe.
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace aoc::sim {

// ============================================================================
// District types
// ============================================================================

enum class DistrictType : uint8_t {
    CityCenter,     ///< Automatic, built when city is founded
    Industrial,     ///< Enables smelting, forging, manufacturing
    Commercial,     ///< Enables trade, banking, markets
    Campus,         ///< Science buildings
    HolySite,       ///< Faith buildings
    Harbor,         ///< Coastal trade, fishing
    Encampment,     ///< Military buildings

    Count
};

static constexpr uint8_t DISTRICT_TYPE_COUNT = static_cast<uint8_t>(DistrictType::Count);

[[nodiscard]] constexpr std::string_view districtTypeName(DistrictType type) {
    constexpr std::array<std::string_view, DISTRICT_TYPE_COUNT> NAMES = {{
        "City Center", "Industrial Zone", "Commercial Hub", "Campus",
        "Holy Site", "Harbor", "Encampment"
    }};
    return NAMES[static_cast<uint8_t>(type)];
}

// ============================================================================
// Building definitions
// ============================================================================

struct BuildingDef {
    BuildingId       id;
    std::string_view name;
    DistrictType     requiredDistrict;
    int32_t          productionCost;       ///< Hammers to build
    int32_t          maintenanceCost;      ///< Gold per turn
    int32_t          productionBonus;      ///< Flat bonus to city production
    int32_t          scienceBonus;
    int32_t          goldBonus;
};

// BuildingId values match the requiredBuilding in ProductionRecipe:
//   0 = Forge (smelting, tool-making)
//   1 = Workshop (lumber, bricks, consumer goods)
//   2 = Refinery (oil processing)
//   3 = Factory (steel, machinery)
//   4 = Electronics Plant (advanced electronics)
//   5 = Industrial Complex (industrial equipment)

inline constexpr std::array<BuildingDef, 8> BUILDING_DEFS = {{
    {BuildingId{0}, "Forge",              DistrictType::Industrial, 60,  1, 2, 0, 0},
    {BuildingId{1}, "Workshop",           DistrictType::Industrial, 40,  1, 1, 0, 0},
    {BuildingId{2}, "Refinery",           DistrictType::Industrial, 100, 2, 3, 0, 0},
    {BuildingId{3}, "Factory",            DistrictType::Industrial, 120, 3, 4, 0, 1},
    {BuildingId{4}, "Electronics Plant",  DistrictType::Industrial, 180, 4, 3, 2, 2},
    {BuildingId{5}, "Industrial Complex", DistrictType::Industrial, 250, 5, 6, 0, 3},
    {BuildingId{6}, "Market",             DistrictType::Commercial, 50,  0, 0, 0, 3},
    {BuildingId{7}, "Library",            DistrictType::Campus,     50,  1, 0, 2, 0},
}};

[[nodiscard]] inline constexpr const BuildingDef& buildingDef(BuildingId id) {
    return BUILDING_DEFS[id.value];
}

// ============================================================================
// ECS components
// ============================================================================

/// Attached to a city entity to track which districts and buildings it has.
struct CityDistrictsComponent {
    struct PlacedDistrict {
        DistrictType    type;
        hex::AxialCoord location;
        std::vector<BuildingId> buildings;
    };

    std::vector<PlacedDistrict> districts;

    /// Check if the city has a specific building.
    [[nodiscard]] bool hasBuilding(BuildingId buildingId) const {
        for (const PlacedDistrict& district : this->districts) {
            for (BuildingId bid : district.buildings) {
                if (bid == buildingId) {
                    return true;
                }
            }
        }
        return false;
    }

    /// Check if the city has a specific district type.
    [[nodiscard]] bool hasDistrict(DistrictType type) const {
        for (const PlacedDistrict& district : this->districts) {
            if (district.type == type) {
                return true;
            }
        }
        return false;
    }
};

} // namespace aoc::sim

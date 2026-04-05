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
    float            scienceMultiplier = 1.0f;  ///< Multiplicative science bonus (1.0 = no effect)
};

// BuildingId values match the requiredBuilding in ProductionRecipe:
//   0  = Forge              (smelting, tool-making, glass, bronze)
//   1  = Workshop           (lumber, bricks, consumer goods, construction)
//   2  = Refinery           (oil -> fuel/plastics, rubber)
//   3  = Factory            (steel, machinery, ammunition, armored vehicles, adv. consumer goods)
//   4  = Electronics Plant  (electronics, computers, telecom equipment, adv. machinery)
//   5  = Industrial Complex (industrial equipment, aircraft)
//   6  = Market             (trade)
//   7  = Library            (science)
//   8  = Textile Mill       (textiles, clothing)
//   9  = Food Proc. Plant   (processed food)
//   10 = Precision Workshop (surface plate, precision instruments, interchangeable parts)
//   11 = Semiconductor Fab  (semiconductors, microchips)
//   12 = Research Lab       (software, +50% science)
//   13 = Telecom Hub        (trade speed, gold)
//   14 = Airport            (air trade/military)

inline constexpr std::array<BuildingDef, 24> BUILDING_DEFS = {{
    {BuildingId{0},  "Forge",              DistrictType::Industrial,  60, 1, 2, 0, 0, 1.0f},
    {BuildingId{1},  "Workshop",           DistrictType::Industrial,  40, 1, 1, 0, 0, 1.0f},
    {BuildingId{2},  "Refinery",           DistrictType::Industrial, 100, 2, 3, 0, 0, 1.0f},
    {BuildingId{3},  "Factory",            DistrictType::Industrial, 120, 3, 4, 0, 1, 1.0f},
    {BuildingId{4},  "Electronics Plant",  DistrictType::Industrial, 180, 4, 3, 2, 2, 1.0f},
    {BuildingId{5},  "Industrial Complex", DistrictType::Industrial, 250, 5, 6, 0, 3, 1.0f},
    {BuildingId{6},  "Market",             DistrictType::Commercial,  50, 0, 0, 0, 3, 1.0f},
    {BuildingId{7},  "Library",            DistrictType::Campus,      50, 1, 0, 2, 0, 1.0f},
    {BuildingId{8},  "Textile Mill",       DistrictType::Industrial,  80, 2, 2, 0, 1, 1.0f},
    {BuildingId{9},  "Food Proc. Plant",   DistrictType::Industrial,  90, 2, 1, 0, 1, 1.0f},
    {BuildingId{10}, "Precision Workshop", DistrictType::Industrial, 140, 3, 3, 1, 0, 1.0f},
    {BuildingId{11}, "Semiconductor Fab",  DistrictType::Industrial, 220, 5, 2, 3, 2, 1.0f},
    {BuildingId{12}, "Research Lab",       DistrictType::Campus,     160, 3, 0, 5, 0, 1.5f},
    {BuildingId{13}, "Telecom Hub",        DistrictType::Commercial, 130, 2, 0, 1, 4, 1.0f},
    {BuildingId{14}, "Airport",            DistrictType::Industrial, 200, 4, 2, 0, 3, 1.0f},
    // Expansion buildings
    {BuildingId{15}, "Granary",            DistrictType::CityCenter,  40, 1, 1, 0, 0, 1.0f},
    {BuildingId{16}, "Monument",           DistrictType::CityCenter,  30, 0, 0, 0, 0, 1.0f},
    {BuildingId{17}, "Walls",              DistrictType::Encampment,  60, 0, 0, 0, 0, 1.0f},
    {BuildingId{18}, "Barracks",           DistrictType::Encampment,  70, 1, 0, 0, 0, 1.0f},
    {BuildingId{19}, "University",         DistrictType::Campus,     120, 2, 0, 4, 0, 1.0f},
    {BuildingId{20}, "Bank",               DistrictType::Commercial, 100, 0, 0, 0, 5, 1.0f},
    {BuildingId{21}, "Stock Exchange",     DistrictType::Commercial, 200, 0, 0, 0, 8, 1.0f},
    {BuildingId{22}, "Hospital",           DistrictType::CityCenter, 150, 2, 0, 0, 0, 1.0f},
    {BuildingId{23}, "Shipyard",           DistrictType::Harbor,     120, 2, 3, 0, 2, 1.0f},
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

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
#include "aoc/simulation/wonder/Wonder.hpp"  // WonderAdjacencyReq (alias SpatialReq)

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
    Theatre,        ///< Culture buildings, great works

    Count
};

static constexpr uint8_t DISTRICT_TYPE_COUNT = static_cast<uint8_t>(DistrictType::Count);

[[nodiscard]] constexpr std::string_view districtTypeName(DistrictType type) {
    constexpr std::array<std::string_view, DISTRICT_TYPE_COUNT> NAMES = {{
        "City Center", "Industrial Zone", "Commercial Hub", "Campus",
        "Holy Site", "Harbor", "Encampment", "Theatre Square"
    }};
    return NAMES[static_cast<uint8_t>(type)];
}

/// Per-district-type spatial requirement table. Civ-6 inspired:
///   Harbor    → coast adjacency
///   Encampment, Industrial, Theatre, Commercial, Holy, Campus → no req
[[nodiscard]] inline constexpr aoc::sim::WonderAdjacencyReq districtSpatialReq(DistrictType type) {
    aoc::sim::WonderAdjacencyReq r{};
    if (type == DistrictType::Harbor) {
        r.requiresCoast = true;
    }
    return r;
}

/// Per-building spatial requirement override (separate from
/// BuildingDef::spatial which is set inline; this lookup catches
/// pre-existing entries that haven't been migrated to designated-init).
[[nodiscard]] inline constexpr aoc::sim::WonderAdjacencyReq buildingSpatialReq(BuildingId id) {
    aoc::sim::WonderAdjacencyReq r{};
    switch (id.value) {
        case 28:  // Hydroelectric Dam → must touch river
            r.requiresRiver = true;
            break;
        case 14:  // Airport → flat ground (no mountain/hill)
            r.requiresFlat = true;
            break;
        case 23:  // Shipyard → coast (also enforced via Harbor district)
            r.requiresCoast = true;
            break;
        default:
            break;
    }
    return r;
}

// ============================================================================
// Building definitions
// ============================================================================

/// Resource consumed when constructing a building.
struct BuildingResourceCost {
    uint16_t goodId = 0xFFFF;  ///< 0xFFFF = no requirement
    int32_t  amount = 0;

    [[nodiscard]] constexpr bool isValid() const { return this->goodId != 0xFFFF && this->amount > 0; }
};

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

    /// Resources consumed when constructing this building (up to 2 types).
    BuildingResourceCost resourceCosts[2] = {};

    /// Ongoing fuel: good consumed each turn to keep the building operational.
    /// 0xFFFF = no fuel needed. If fuel is unavailable, building is unpowered.
    uint16_t ongoingFuelGoodId = 0xFFFF;
    int32_t  ongoingFuelPerTurn = 0;

    int32_t faithBonus = 0;    ///< Per-turn faith generated (Shrine/Temple/Cathedral)
    int32_t cultureBonus = 0;  ///< Per-turn culture generated (Theatre buildings)
    uint8_t greatWorksSlots = 0; ///< Capacity for housed great works

    /// Optional civic prerequisite (alongside requiredTech, which lives on
    /// the tech tree's unlockedBuildings list). Default-invalid = no civic
    /// gate.
    CivicId requiredCivic{};

    /// Optional spatial requirement (terrain/feature adjacency). Examples:
    /// Lighthouse → Harbor district (coast); Hydroelectric Plant → river.
    aoc::sim::WonderAdjacencyReq spatial{};

    /// Whether this building requires any resources to construct.
    [[nodiscard]] constexpr bool hasResourceCost() const {
        return this->resourceCosts[0].isValid() || this->resourceCosts[1].isValid();
    }

    /// Whether this building needs ongoing fuel to operate.
    [[nodiscard]] constexpr bool needsFuel() const {
        return this->ongoingFuelGoodId != 0xFFFF && this->ongoingFuelPerTurn > 0;
    }
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
//   24 = Mint               (coins from copper/silver/gold ore)
//   25 = Waste Treatment    (pollution cleanup, recycling)
//   26 = Coal Plant         (power, consumes coal)
//   27 = Oil Plant          (power, consumes oil)
//   28 = Hydroelectric      (power, requires river)
//   29 = Nuclear Plant      (power, consumes uranium, meltdown risk)
//   30 = Solar Array        (power, free, late game)
//   31 = Wind Farm          (power, free, late game)

// Format: {id, name, district, prodCost, maint, prodBonus, sciBonus, goldBonus, sciMult, resourceCosts, fuelGoodId, fuelPerTurn}
// Resource costs and fuel added for mid/late-game buildings per plan Phase 1C/1D.
inline constexpr std::array<BuildingDef, 43> BUILDING_DEFS = {{
    //                                                                                                                     resourceCosts         fuel
    {BuildingId{0},  "Forge",              DistrictType::Industrial,  60, 1, 2, 0, 0, 1.0f},                            // no cost, no fuel
    {BuildingId{1},  "Workshop",           DistrictType::Industrial,  40, 1, 1, 0, 0, 1.0f},
    // Construction-resource costs removed from chain-enabler buildings so
    // civs aren't stuck in a "need Steel to build Refinery, which produces
    // the chain that consumes Steel" loop.  The buildings are still
    // production-expensive (100-250 hammers); only the tile-good prereq is
    // dropped.  Industrial Complex (building 5) keeps its cost because it's
    // a capstone, not a chain entry.
    {BuildingId{2},  "Refinery",           DistrictType::Industrial, 100, 2, 3, 0, 0, 1.0f},
    {BuildingId{3},  "Factory",            DistrictType::Industrial, 120, 2, 4, 0, 1, 1.0f},
    {BuildingId{4},  "Electronics Plant",  DistrictType::Industrial, 180, 3, 3, 2, 2, 1.0f},
    {BuildingId{5},  "Industrial Complex", DistrictType::Industrial, 250, 4, 6, 0, 3, 1.0f},
    {BuildingId{6},  "Market",             DistrictType::Commercial,  50, 0, 0, 0, 6, 1.0f, {{44, 1}}},                // 1 Stone (counter)
    {BuildingId{7},  "Library",            DistrictType::Campus,      90, 1, 0, 3, 0, 1.0f, {{62, 1}}},                // 1 Lumber (shelves)
    {BuildingId{8},  "Textile Mill",       DistrictType::Industrial,  80, 1, 2, 0, 1, 1.0f},
    {BuildingId{9},  "Food Proc. Plant",   DistrictType::Industrial,  90, 1, 1, 0, 1, 1.0f},
    {BuildingId{10}, "Precision Workshop", DistrictType::Industrial, 140, 2, 3, 1, 0, 1.0f,  {{63, 1}}},                // 1 Tools
    // Late-tech buildings cheaper so cities actually finish them before
    // the 700-900-turn victory window closes.
    {BuildingId{11}, "Semiconductor Fab",  DistrictType::Industrial, 160, 4, 2, 3, 2, 1.0f},
    {BuildingId{12}, "Research Lab",       DistrictType::Campus,     280, 3, 0, 10, 0, 1.5f, {{76, 1}}},                // 1 Glass
    {BuildingId{13}, "Telecom Hub",        DistrictType::Commercial, 130, 2, 0, 1, 4, 1.0f},
    {BuildingId{14}, "Airport",            DistrictType::Industrial, 200, 3, 2, 0, 3, 1.0f,  {{64, 2}}},                // 2 Steel
    {BuildingId{15}, "Granary",            DistrictType::CityCenter,  40, 1, 1, 0, 0, 1.0f, {{62, 1}}},                // 1 Lumber (silo)
    {BuildingId{16}, "Monument",           DistrictType::CityCenter,  30, 0, 0, 0, 0, 1.0f, {{44, 1}}},                // 1 Stone
    {BuildingId{17}, "Walls",              DistrictType::Encampment,  60, 0, 0, 0, 0, 1.0f,  {{44, 2}}},                // 2 Stone
    {BuildingId{18}, "Barracks",           DistrictType::Encampment,  70, 2, 0, 0, 0, 1.0f, {{62, 1}}},                // 1 Lumber
    {BuildingId{19}, "University",         DistrictType::Campus,     250, 2, 0, 6, 0, 1.0f, {{44, 1}, {62, 1}}},       // 1 Stone + 1 Lumber
    {BuildingId{20}, "Bank",               DistrictType::Commercial, 100, 0, 0, 0, 10, 1.0f, {{44, 1}}},                // 1 Stone
    {BuildingId{21}, "Stock Exchange",     DistrictType::Commercial, 200, 0, 0, 0, 15, 1.0f, {{44, 2}}},                // 2 Stone
    {BuildingId{22}, "Hospital",           DistrictType::CityCenter, 150, 2, 0, 0, 0, 1.0f, {{44, 2}}},                // 2 Stone
    {BuildingId{23}, "Shipyard",           DistrictType::Harbor,     120, 2, 3, 0, 2, 1.0f,  {{62, 2}}},                // 2 Lumber
    {BuildingId{24}, "Mint",               DistrictType::CityCenter,  70, 1, 0, 0, 4, 1.0f, {{44, 1}}},                // 1 Stone
    {BuildingId{25}, "Waste Treatment",    DistrictType::Industrial, 100, 2, 0, 0, 0, 1.0f},
    {BuildingId{26}, "Coal Plant",         DistrictType::Industrial,  80, 2, 0, 0, 0, 1.0f,  {}, 2, 1},                 // burns 1 Coal/turn
    {BuildingId{27}, "Oil Plant",          DistrictType::Industrial, 120, 3, 0, 0, 0, 1.0f,  {}, 65, 1},                // burns 1 Fuel/turn
    {BuildingId{28}, "Hydroelectric Dam",  DistrictType::Industrial, 150, 1, 0, 0, 0, 1.0f},
    {BuildingId{29}, "Nuclear Plant",      DistrictType::Industrial, 300, 5, 0, 0, 0, 1.0f,  {{64, 2}}, 6, 1},          // 2 Steel to build, 1 Uranium/turn
    {BuildingId{30}, "Solar Array",        DistrictType::Industrial, 200, 1, 0, 1, 0, 1.0f},
    {BuildingId{31}, "Wind Farm",          DistrictType::Industrial, 160, 1, 0, 0, 0, 1.0f},
    // New energy buildings
    {BuildingId{32}, "Gas Plant",          DistrictType::Industrial, 100, 2, 0, 0, 0, 1.0f,  {}, 12, 1},                // burns 1 Natural Gas/turn
    {BuildingId{33}, "Biofuel Plant",      DistrictType::Industrial, 120, 2, 1, 0, 0, 1.0f},                            // enables biofuel recipes
    {BuildingId{34}, "Geothermal Plant",   DistrictType::Industrial, 180, 1, 0, 0, 0, 1.0f},                            // free power, requires volcanic/mountain
    {BuildingId{35}, "Fusion Reactor",     DistrictType::Industrial, 350, 8, 0, 2, 0, 1.0f,  {{64, 3}, {76, 2}}, 80, 1}, // 3 Steel + 2 Glass to build, 1 Deuterium/turn
    // Faith buildings -- gate each city's faith output. Must be built on HolySite district.
    {BuildingId{36}, "Shrine",             DistrictType::HolySite,    40, 1, 0, 0, 0, 1.0f, {}, 0xFFFF, 0, 2},
    {BuildingId{37}, "Temple",             DistrictType::HolySite,   100, 2, 0, 0, 0, 1.0f, {}, 0xFFFF, 0, 4},
    {BuildingId{38}, "Cathedral",          DistrictType::HolySite,   200, 3, 0, 0, 2, 1.0f, {}, 0xFFFF, 0, 6},
    // Culture buildings -- Theatre Square district. cultureBonus per turn, greatWorksSlots for housing works.
    {BuildingId{39}, "Amphitheater",       DistrictType::Theatre,     60, 1, 0, 0, 0, 1.0f, {}, 0xFFFF, 0, 0, 2, 2},
    {BuildingId{40}, "Art Museum",         DistrictType::Theatre,    150, 2, 0, 0, 1, 1.0f, {}, 0xFFFF, 0, 0, 3, 3},
    {BuildingId{41}, "Archaeological Museum", DistrictType::Theatre, 150, 2, 0, 0, 1, 1.0f, {}, 0xFFFF, 0, 0, 3, 3},
    // Housing infrastructure: Aqueduct grants +4 housing. Requires adjacent river
    // or mountain (enforced at production-time, not at def level).
    {BuildingId{42}, "Aqueduct",           DistrictType::CityCenter,  80, 1, 0, 0, 0, 1.0f, {{44, 2}}}, // 2 Stone
}};

[[nodiscard]] inline constexpr const BuildingDef& buildingDef(BuildingId id) {
    return BUILDING_DEFS[id.value];
}

/// Pollution emission per turn (tons of CO2-equivalent). Positive = emits,
/// negative = cleans (e.g. Waste Treatment). 0 = neutral.
[[nodiscard]] inline constexpr int32_t buildingPollutionEmission(BuildingId id) {
    switch (id.value) {
        case 0:  return 1;   // Forge
        case 2:  return 4;   // Refinery
        case 3:  return 5;   // Factory
        case 4:  return 3;   // Electronics Plant
        case 5:  return 6;   // Industrial Complex
        case 11: return 3;   // Semiconductor Fab
        case 25: return -5;  // Waste Treatment (cleans)
        case 26: return 8;   // Coal Plant
        case 27: return 6;   // Oil Plant
        case 29: return 2;   // Nuclear Plant
        case 32: return 4;   // Gas Plant
        case 33: return 2;   // Biofuel Plant
        default: return 0;
    }
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

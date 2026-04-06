#pragma once

/**
 * @file PowerGrid.hpp
 * @brief City power grid: energy generation, demand, and brownouts.
 *
 * Advanced buildings (Tier 3+) require energy each turn. Without sufficient
 * power, buildings operate at reduced efficiency.
 *
 * Power sources (buildings that generate energy):
 *   Coal Plant:     30 energy/turn, requires 2 Coal/turn, +3 emissions
 *   Oil Plant:      40 energy/turn, requires 2 Oil/turn, +2 emissions
 *   Hydroelectric:  25 energy/turn, requires river adjacency, 0 emissions
 *   Nuclear Plant:  60 energy/turn, requires 1 Uranium/turn, meltdown risk
 *   Solar Array:    15 energy/turn, 0 cost, 0 emissions (late game tech)
 *   Wind Farm:      12 energy/turn, 0 cost, 0 emissions (late game tech)
 *
 * Energy demand per building (per turn, regardless of batches executed):
 *   Factory:            5
 *   Electronics Plant: 10
 *   Industrial Complex: 8
 *   Semiconductor Fab: 15
 *   Research Lab:       8
 *   Precision Workshop: 6
 *   Others:             0 (pre-industrial buildings don't need power)
 *
 * If demand > supply: all powered buildings operate at (supply/demand) efficiency.
 * This "brownout" multiplier stacks with other production modifiers.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::ecs { class World; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

// ============================================================================
// Power plant definitions
// ============================================================================

enum class PowerPlantType : uint8_t {
    Coal         = 0,
    Oil          = 1,
    Hydroelectric = 2,
    Nuclear      = 3,
    Solar        = 4,
    Wind         = 5,

    Count
};

struct PowerPlantDef {
    PowerPlantType type;
    BuildingId     buildingId;
    int32_t        energyOutput;     ///< Energy units per turn
    uint16_t       fuelGoodId;       ///< Good consumed as fuel (0xFFFF = none)
    int32_t        fuelPerTurn;      ///< Fuel consumed per turn
    int32_t        emissions;        ///< CO2/pollution per turn
    bool           requiresRiver;    ///< Hydroelectric requires river adjacency
    float          meltdownRisk;     ///< Per-turn meltdown probability (nuclear only)
};

/// Power plant building IDs start at 26.
inline constexpr std::array<PowerPlantDef, 6> POWER_PLANT_DEFS = {{
    {PowerPlantType::Coal,          BuildingId{26}, 30, 2 /*COAL*/,    2, 3, false, 0.0f},
    {PowerPlantType::Oil,           BuildingId{27}, 40, 3 /*OIL*/,     2, 2, false, 0.0f},
    {PowerPlantType::Hydroelectric, BuildingId{28}, 25, 0xFFFF,        0, 0, true,  0.0f},
    {PowerPlantType::Nuclear,       BuildingId{29}, 60, 6 /*URANIUM*/, 1, 0, false, 0.002f},
    {PowerPlantType::Solar,         BuildingId{30}, 15, 0xFFFF,        0, 0, false, 0.0f},
    {PowerPlantType::Wind,          BuildingId{31}, 12, 0xFFFF,        0, 0, false, 0.0f},
}};

// ============================================================================
// Energy demand per building
// ============================================================================

/// Energy required per turn for a building to operate at full efficiency.
[[nodiscard]] constexpr int32_t buildingEnergyDemand(BuildingId id) {
    switch (id.value) {
        case 3:  return 5;   // Factory
        case 4:  return 10;  // Electronics Plant
        case 5:  return 8;   // Industrial Complex
        case 10: return 6;   // Precision Workshop
        case 11: return 15;  // Semiconductor Fab
        case 12: return 8;   // Research Lab
        case 14: return 5;   // Airport
        default: return 0;   // Pre-industrial buildings
    }
}

// ============================================================================
// Per-city power state (ECS component)
// ============================================================================

struct CityPowerComponent {
    int32_t energySupply = 0;    ///< Total energy generated this turn
    int32_t energyDemand = 0;    ///< Total energy demanded by buildings
    bool    hasNuclear = false;  ///< For meltdown risk tracking

    /// Power efficiency: min(1.0, supply/demand). Buildings produce at this rate.
    [[nodiscard]] float powerEfficiency() const {
        if (this->energyDemand <= 0) {
            return 1.0f;
        }
        if (this->energySupply >= this->energyDemand) {
            return 1.0f;
        }
        return static_cast<float>(this->energySupply)
             / static_cast<float>(this->energyDemand);
    }

    /// Whether the city is in a brownout (demand > supply).
    [[nodiscard]] bool isBrownout() const {
        return this->energyDemand > 0 && this->energySupply < this->energyDemand;
    }
};

// ============================================================================
// Power grid functions
// ============================================================================

/**
 * @brief Compute the power state for a city.
 *
 * Sums energy from all power plant buildings, subtracts fuel costs
 * from stockpile, and computes total demand from industrial buildings.
 *
 * @param world       ECS world.
 * @param grid        Hex grid (for river adjacency checks).
 * @param cityEntity  City to compute power for.
 * @return Power state for this turn.
 */
[[nodiscard]] CityPowerComponent computeCityPower(
    aoc::ecs::World& world,
    const aoc::map::HexGrid& grid,
    EntityId cityEntity);

/**
 * @brief Check for nuclear meltdown.
 *
 * Each turn, if the city has a nuclear plant, there is a small chance
 * (0.2%) of meltdown. Meltdown: destroys the nuclear plant building,
 * adds massive pollution (100 waste), -50% population, city is
 * "irradiated" for 20 turns.
 *
 * @param world       ECS world.
 * @param cityEntity  City with nuclear plant.
 * @param turnHash    Deterministic hash for this turn.
 * @return true if meltdown occurred.
 */
bool checkNuclearMeltdown(aoc::ecs::World& world, EntityId cityEntity,
                          uint32_t turnHash);

} // namespace aoc::sim

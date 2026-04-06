#pragma once

/**
 * @file Waste.hpp
 * @brief Production waste, byproducts, and pollution mechanics.
 *
 * Industrial production creates waste as a byproduct:
 *   - Smelting: slag
 *   - Refining: toxic waste
 *   - Semiconductor fab: chemical waste
 *   - Power plants: emissions (coal/oil)
 *
 * Waste accumulates in the city. Effects scale with accumulation:
 *   0-10:  No effect
 *   10-30: -1 food yield, -1 amenity
 *   30-60: -2 food, -2 amenity, -10% population growth
 *   60+:   -3 food, -3 amenity, -25% growth, "polluted" city marker
 *
 * Waste Processing:
 *   - Waste Treatment Plant (building): processes 5 waste/turn into
 *     Construction Materials (recycling).
 *   - Can also just dump waste (no building needed) but pollution doubles.
 *
 * CO2 contribution: waste feeds into the existing Climate system.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"

#include <cstdint>

namespace aoc::ecs { class World; }

namespace aoc::sim {

// ============================================================================
// Waste categories
// ============================================================================

enum class WasteType : uint8_t {
    Slag          = 0,  ///< From smelting (iron, steel, bronze)
    ToxicWaste    = 1,  ///< From refining (oil, chemicals)
    ChemicalWaste = 2,  ///< From semiconductor/electronics manufacturing
    Emissions     = 3,  ///< From power generation (coal, oil plants)

    Count
};

/// Waste produced per recipe execution, by building type.
struct WasteOutput {
    WasteType type;
    int32_t   amount;
};

/// Get the waste produced by a building during one recipe execution.
/// Returns {WasteType::Count, 0} if the building produces no waste.
[[nodiscard]] constexpr WasteOutput buildingWasteOutput(BuildingId id) {
    switch (id.value) {
        case 0:  return {WasteType::Slag, 1};          // Forge
        case 2:  return {WasteType::ToxicWaste, 2};    // Refinery
        case 3:  return {WasteType::Slag, 1};          // Factory
        case 4:  return {WasteType::ChemicalWaste, 1}; // Electronics Plant
        case 5:  return {WasteType::Slag, 2};          // Industrial Complex
        case 11: return {WasteType::ChemicalWaste, 2}; // Semiconductor Fab
        default: return {static_cast<WasteType>(static_cast<uint8_t>(WasteType::Count)), 0};
    }
}

// ============================================================================
// Per-city pollution tracking (ECS component)
// ============================================================================

struct CityPollutionComponent {
    int32_t wasteAccumulated = 0;    ///< Total waste units in the city
    int32_t co2ContributionPerTurn = 0; ///< CO2 emitted per turn (feeds climate system)

    /// Food yield penalty from pollution.
    [[nodiscard]] int32_t foodPenalty() const {
        if (this->wasteAccumulated < 10) { return 0; }
        if (this->wasteAccumulated < 30) { return 1; }
        if (this->wasteAccumulated < 60) { return 2; }
        return 3;
    }

    /// Amenity penalty from pollution.
    [[nodiscard]] int32_t amenityPenalty() const {
        return this->foodPenalty();  // Same scale
    }

    /// Population growth rate modifier (1.0 = normal, <1.0 = reduced).
    [[nodiscard]] float growthModifier() const {
        if (this->wasteAccumulated < 30) { return 1.0f; }
        if (this->wasteAccumulated < 60) { return 0.90f; }
        return 0.75f;
    }

    /// Whether the city is considered "polluted" (UI marker).
    [[nodiscard]] bool isPolluted() const {
        return this->wasteAccumulated >= 60;
    }
};

// ============================================================================
// Waste processing
// ============================================================================

/// Waste Treatment Plant building ID.
inline constexpr BuildingId WASTE_TREATMENT_PLANT{25};

/// Process waste treatment for a city (if it has the building).
/// Converts up to 5 waste/turn into Construction Materials.
void processWasteTreatment(aoc::ecs::World& world, EntityId cityEntity);

/// Process waste generation for all cities (called during production step).
/// Accumulates waste based on recipes executed this turn.
void accumulateWaste(aoc::ecs::World& world, EntityId cityEntity,
                     BuildingId buildingUsed, int32_t batchesExecuted);

/// Get the total CO2 contribution from all cities (for climate system).
[[nodiscard]] int32_t totalIndustrialCO2(const aoc::ecs::World& world);

} // namespace aoc::sim

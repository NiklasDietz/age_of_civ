#pragma once

/**
 * @file WageDemands.hpp
 * @brief Inflation-indexed wage demands and labor market pressure.
 *
 * When inflation is high, workers demand higher wages. If wages don't keep
 * up with prices, productivity drops (workers can't afford food, morale
 * plummets, strikes increase). If wages DO keep up, the employer (city)
 * pays more in maintenance, reducing effective production surplus.
 *
 * This creates a realistic wage-price spiral:
 *   High inflation -> workers demand raises -> costs increase ->
 *   businesses raise prices -> more inflation -> workers demand more raises
 *
 * The player can break the spiral through:
 *   - Central bank raising interest rates (slows economy but tames inflation)
 *   - Government spending cuts (reduces demand-pull inflation)
 *   - Productivity improvements (tech/buildings that increase output per worker)
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::sim {

struct MonetaryStateComponent;

/// Per-city wage pressure state. Tracks the gap between prices and wages.
struct CityWageComponent {
    /// Current wage index (1.0 = base wages, tracks cumulative raises).
    float wageIndex = 1.0f;

    /// Target wage based on price level (workers want wages to match prices).
    float targetWageIndex = 1.0f;

    /// Production penalty from wage gap. 1.0 = no penalty.
    /// Falls below 1.0 when wages lag behind prices (unhappy workers).
    [[nodiscard]] float productionPenalty() const {
        if (this->wageIndex >= this->targetWageIndex * 0.9f) {
            return 1.0f;  // Wages within 10% of target: no penalty
        }
        float gap = (this->targetWageIndex - this->wageIndex) / this->targetWageIndex;
        // Up to 25% penalty when wages severely lag prices
        float penalty = gap * 0.5f;
        return (1.0f - penalty > 0.75f) ? 1.0f - penalty : 0.75f;
    }

    /// Extra maintenance cost from wage increases (ratio above base).
    [[nodiscard]] float maintenanceMultiplier() const {
        return this->wageIndex;
    }
};

/**
 * @brief Update wage demands for a city based on inflation and price level.
 *
 * Workers adjust wage demands toward price level. Wages are sticky downward
 * (workers resist pay cuts) but flexible upward (demand raises quickly).
 *
 * @param wage        City's wage component (mutated).
 * @param priceLevel  Current cumulative price level from monetary state.
 * @param inflationRate Current per-turn inflation rate.
 */
void updateWageDemands(CityWageComponent& wage, float priceLevel, float inflationRate);

} // namespace aoc::sim

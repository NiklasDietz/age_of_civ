/**
 * @file WageDemands.cpp
 * @brief Inflation-indexed wage demands and labor market pressure.
 */

#include "aoc/simulation/economy/WageDemands.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

void updateWageDemands(CityWageComponent& wage, float priceLevel, float inflationRate) {
    // Workers want wages to match the price level
    wage.targetWageIndex = priceLevel;

    // Wage adjustment: sticky downward, flexible upward
    if (wage.wageIndex < wage.targetWageIndex) {
        // Workers demand raises: wages catch up 30% of the gap per turn
        float gap = wage.targetWageIndex - wage.wageIndex;
        wage.wageIndex += gap * 0.30f;

        // High inflation accelerates demands (urgency)
        if (inflationRate > 0.05f) {
            wage.wageIndex += gap * 0.20f;  // Extra 20% catch-up
        }
    } else if (wage.wageIndex > wage.targetWageIndex * 1.05f) {
        // Wages above target by >5%: very slow downward adjustment (sticky)
        float excess = wage.wageIndex - wage.targetWageIndex;
        wage.wageIndex -= excess * 0.05f;  // Only 5% adjustment down per turn
    }

    // Floor: wages never go below 0.5x base
    wage.wageIndex = std::max(0.50f, wage.wageIndex);
}

} // namespace aoc::sim

#pragma once

/**
 * @file Happiness.hpp
 * @brief City happiness/amenity system.
 *
 * Happiness = amenities - population_demand + modifiers
 * Positive: bonuses to growth and production.
 * Zero: neutral.
 * Negative: penalties to growth, eventually unrest and rebellion.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::game {
class Player;
}

namespace aoc::sim {

struct CityHappinessComponent {
    float amenities       = 1.0f;   ///< From luxury resources, buildings, policies
    float demand          = 0.0f;   ///< Based on population (1 per 2 citizens)
    float modifiers       = 0.0f;   ///< From war weariness, inflation, taxes, etc.
    /// WP-A4: persistent unhappiness from climate-driven disasters. Decays
    /// 10%/turn in computeCityHappiness; folded into modifiers each recompute.
    float disasterUnhappiness = 0.0f;
    float happiness       = 1.0f;   ///< Net happiness = amenities - demand + modifiers

    /// Growth multiplier from happiness. Happy cities grow faster.
    [[nodiscard]] float growthMultiplier() const {
        if (this->happiness >= 3.0f) {
            return 1.2f;  // Ecstatic: +20% growth
        }
        if (this->happiness >= 1.0f) {
            return 1.0f;  // Happy: normal
        }
        if (this->happiness >= 0.0f) {
            return 0.85f; // Content: slightly reduced
        }
        if (this->happiness >= -3.0f) {
            return 0.5f;  // Unhappy: halved growth
        }
        return 0.0f;     // Unrest: no growth
    }

    /// Production multiplier from happiness.
    [[nodiscard]] float productionMultiplier() const {
        if (this->happiness >= 3.0f) {
            return 1.1f;
        }
        if (this->happiness >= 0.0f) {
            return 1.0f;
        }
        return 0.85f;  // Unhappy: -15% production
    }
};

/**
 * @brief Recalculate happiness for all cities of a player.
 *
 * Uses GameState object model (Player/City) directly.
 * Considers: luxury resources, buildings, population size,
 * war weariness, inflation penalty, tax penalty, empire size,
 * military unit unhappiness, specialist entertainers.
 */
void computeCityHappiness(aoc::game::Player& player);

} // namespace aoc::sim

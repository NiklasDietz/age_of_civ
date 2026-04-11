#pragma once

/**
 * @file TechUnemployment.hpp
 * @brief Technological unemployment from automation and industrial revolution.
 *
 * Automation buildings (Factory, Electronics Plant, Semiconductor Fab) and
 * industrial revolutions boost production but destroy jobs. This creates
 * a fundamental tension: more efficient economy vs social stability.
 *
 * Effects:
 *   - Each automation building adds +production but generates unemployment
 *   - Unemployment reduces happiness and loyalty
 *   - Unemployed population may migrate to other civs
 *   - Player can mitigate via: education, welfare spending, new industries
 *   - Higher education reduces unemployment (workers retrain faster)
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::game { class GameState; }

namespace aoc::sim {

/// Per-city unemployment tracking.
struct CityUnemploymentComponent {
    /// Unemployment rate (0.0 = full employment, 1.0 = everyone jobless).
    float unemploymentRate = 0.0f;

    /// Jobs displaced by automation this turn.
    int32_t automationDisplacement = 0;

    /// Happiness penalty from unemployment.
    [[nodiscard]] float happinessPenalty() const {
        if (this->unemploymentRate < 0.05f) { return 0.0f; }  // <5% natural rate
        if (this->unemploymentRate < 0.10f) { return -0.5f; }
        if (this->unemploymentRate < 0.20f) { return -1.5f; }
        if (this->unemploymentRate < 0.30f) { return -3.0f; }
        return -5.0f;  // Mass unemployment: severe unrest
    }

    /// Loyalty penalty (unemployed citizens blame the government).
    [[nodiscard]] float loyaltyPenalty() const {
        if (this->unemploymentRate < 0.10f) { return 0.0f; }
        return this->unemploymentRate * -20.0f;  // Up to -20 loyalty at 100%
    }
};

/**
 * @brief Update unemployment for a city based on automation level and education.
 *
 * @param unemployment  City component (mutated).
 * @param automationBuildings Number of automation buildings (Factory, etc.).
 * @param population    City population.
 * @param educationLevel Player's education level (0.0-1.0, higher = faster retraining).
 * @param industrialRevLevel Industrial revolution stage (0-5, higher = more automation).
 */
void updateUnemployment(CityUnemploymentComponent& unemployment,
                         int32_t automationBuildings,
                         int32_t population,
                         float educationLevel,
                         int32_t industrialRevLevel);

/**
 * @brief Process unemployment effects for all cities of a player.
 */
void processUnemployment(aoc::game::GameState& gameState, PlayerId player);

} // namespace aoc::sim

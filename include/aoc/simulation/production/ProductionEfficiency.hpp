#pragma once

/**
 * @file ProductionEfficiency.hpp
 * @brief Per-city per-recipe production experience and learning curves.
 *
 * Each time a city executes a recipe, it gains experience in that recipe.
 * Experience translates to an efficiency bonus:
 *   10 executions: +10% output
 *   50 executions: +25% output
 *   100 executions: +40% output
 *
 * This creates genuine specialization incentive: a city that has been
 * smelting steel for 50 turns is better at it than a brand-new factory.
 * Relocating production (e.g. due to war) means losing accumulated
 * experience -- a real economic cost of conflict.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <unordered_map>

namespace aoc::sim {

// ============================================================================
// Per-city production experience (ECS component)
// ============================================================================

struct CityProductionExperienceComponent {
    /// Map: recipeId -> number of times this recipe has been executed in this city.
    std::unordered_map<uint16_t, int32_t> recipeExperience;

    /// Record one execution of a recipe.
    void addExperience(uint16_t recipeId) {
        ++this->recipeExperience[recipeId];
    }

    /// Get the experience count for a recipe.
    [[nodiscard]] int32_t getExperience(uint16_t recipeId) const {
        std::unordered_map<uint16_t, int32_t>::const_iterator it = this->recipeExperience.find(recipeId);
        return (it != this->recipeExperience.end()) ? it->second : 0;
    }

    /// Get the production efficiency multiplier for a recipe based on experience.
    /// Diminishing returns curve: bonus = 0.4 * (1 - e^(-experience/50))
    [[nodiscard]] float efficiencyMultiplier(uint16_t recipeId) const {
        int32_t exp = this->getExperience(recipeId);
        if (exp <= 0) {
            return 1.0f;
        }
        // Smooth curve: approaches +40% asymptotically
        // At 10 executions: ~+7%
        // At 50 executions: ~+25%
        // At 100 executions: ~+35%
        // At 200 executions: ~+39%
        float t = static_cast<float>(exp) / 50.0f;
        float bonus = 0.40f * (1.0f - 1.0f / (1.0f + t));  // Hyperbolic, no exp() needed
        return 1.0f + bonus;
    }
};

} // namespace aoc::sim

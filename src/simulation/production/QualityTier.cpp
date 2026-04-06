/**
 * @file QualityTier.cpp
 * @brief Quality tier determination for produced goods.
 */

#include "aoc/simulation/production/QualityTier.hpp"

namespace aoc::sim {

QualityTier determineOutputQuality(int32_t buildingLevel,
                                   int32_t recipeExperience,
                                   bool hasPrecisionInstr,
                                   float inputQuality,
                                   uint32_t turnHash) {
    // Base quality chance depends on building level:
    //   Lv1: 100% Standard
    //   Lv2: 70% Standard, 25% High, 5% Premium
    //   Lv3: 40% Standard, 40% High, 20% Premium
    float highChance = 0.0f;
    float premiumChance = 0.0f;

    if (buildingLevel >= 3) {
        highChance = 0.40f;
        premiumChance = 0.20f;
    } else if (buildingLevel >= 2) {
        highChance = 0.25f;
        premiumChance = 0.05f;
    }

    // Experience bonus: up to +15% shift toward higher quality
    float experienceBonus = 0.0f;
    if (recipeExperience > 0) {
        float t = static_cast<float>(recipeExperience) / 100.0f;
        experienceBonus = 0.15f * t / (1.0f + t);  // Asymptotic at 0.15
    }
    highChance += experienceBonus;
    premiumChance += experienceBonus * 0.5f;

    // Precision instruments bonus: +10% chance
    if (hasPrecisionInstr) {
        highChance += 0.10f;
        premiumChance += 0.05f;
    }

    // Input quality bonus: higher quality inputs -> higher quality output
    highChance += inputQuality * 0.10f;
    premiumChance += inputQuality * 0.05f;

    // Deterministic roll using turnHash (0.0 to 1.0 range)
    float roll = static_cast<float>(turnHash % 10000) / 10000.0f;

    if (roll < premiumChance) {
        return QualityTier::Premium;
    }
    if (roll < premiumChance + highChance) {
        return QualityTier::High;
    }
    return QualityTier::Standard;
}

} // namespace aoc::sim

#pragma once

/**
 * @file UtilityScoring.hpp
 * @brief Utility-based AI scoring: each leader scores every possible action
 * differently based on their personality weights.
 */

#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::sim {

/// Score categories for production decisions.
struct ProductionScores {
    float settler;
    float builder;
    float military;
    float navalMilitary;
    float religious;
    float wonder;
    float scienceBuilding;
    float economicBuilding;
    float industrialBuilding;
    float cultureBuilding;
    float militaryBuilding;
    float mintBuilding;
    float powerPlant;
    float district;
};

/// Context that modifies base scores.
struct AIContext {
    int32_t ownedCities;
    int32_t totalPopulation;
    int32_t militaryUnits;
    int32_t builderUnits;
    int32_t settlerUnits;
    bool    isThreatened;
    bool    needsImprovements;
    bool    hasMint;
    bool    hasCoins;
    bool    hasCampus;
    bool    hasCommercial;
    CurrencyAmount treasury;
    int32_t targetMaxCities;
    int32_t desiredMilitary;
};

[[nodiscard]] ProductionScores computeProductionUtility(
    const LeaderBehavior& behavior, const AIContext& context);

[[nodiscard]] float scoreBuildingForLeader(
    const LeaderBehavior& behavior, BuildingId buildingId, const AIContext& context);

[[nodiscard]] float scoreTechForLeader(
    const LeaderBehavior& behavior, TechId techId, const AIContext& context);

} // namespace aoc::sim

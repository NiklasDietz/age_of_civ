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

/// Score categories for production decisions. Defaults make every member
/// well-defined for fresh-zeroed instances; without them, adding a new
/// scorer that forgets to set one field would silently consume garbage.
struct ProductionScores {
    float settler            = 0.0f;
    float builder            = 0.0f;
    float military           = 0.0f;
    float navalMilitary      = 0.0f;
    float religious          = 0.0f;
    float wonder             = 0.0f;
    float scienceBuilding    = 0.0f;
    float economicBuilding   = 0.0f;
    float industrialBuilding = 0.0f;
    float cultureBuilding    = 0.0f;
    float militaryBuilding   = 0.0f;
    float mintBuilding       = 0.0f;
    float powerPlant         = 0.0f;
    float district           = 0.0f;
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

    /// Per-net-devotion science coefficient at the player's current era.
    /// Positive in Ancient/Classical, zero in Medieval, negative from
    /// Renaissance onward.  Used to scale faith-building scores so AIs
    /// invest in religion when it pays and abandon it when the drain hits.
    float religionScienceCoef = 0.0f;
};

[[nodiscard]] ProductionScores computeProductionUtility(
    const LeaderBehavior& behavior, const AIContext& context);

[[nodiscard]] float scoreBuildingForLeader(
    const LeaderBehavior& behavior, BuildingId buildingId, const AIContext& context);

[[nodiscard]] float scoreTechForLeader(
    const LeaderBehavior& behavior, TechId techId, const AIContext& context);

} // namespace aoc::sim

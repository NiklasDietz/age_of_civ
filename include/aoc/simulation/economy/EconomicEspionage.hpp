#pragma once

/**
 * @file EconomicEspionage.hpp
 * @brief Economic espionage missions: steal tech, counterfeit, sabotage.
 *
 * Extends the existing spy system with economy-focused missions:
 *
 *   - Steal Trade Secrets: gain 25% of a tech's research cost instantly
 *   - Counterfeit Currency: inject fake money into target's economy (+inflation)
 *   - Industrial Sabotage: destroy a building in a target city
 *   - Insider Trading: gain gold from target's stock market movements
 *   - Embargo Intelligence: reveal all target's trade routes and partners
 *
 * Success rates depend on spy experience and target's counter-intelligence.
 * Getting caught triggers diplomatic penalties.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"

#include <cstdint>
#include <string_view>

namespace aoc::game { class GameState; }

namespace aoc::sim {

enum class EspionageMissionType : uint8_t {
    StealTradeSecrets,     ///< Gain tech progress from target
    CounterfeitCurrency,   ///< Increase target's money supply (causes inflation)
    IndustrialSabotage,    ///< Destroy a random building in target city
    InsiderTrading,        ///< Earn gold from target's economic activity
    EmbargoIntelligence,   ///< Reveal target's trade network

    Count
};

[[nodiscard]] constexpr std::string_view espionageMissionName(EspionageMissionType type) {
    switch (type) {
        case EspionageMissionType::StealTradeSecrets:   return "Steal Trade Secrets";
        case EspionageMissionType::CounterfeitCurrency: return "Counterfeit Currency";
        case EspionageMissionType::IndustrialSabotage:  return "Industrial Sabotage";
        case EspionageMissionType::InsiderTrading:       return "Insider Trading";
        case EspionageMissionType::EmbargoIntelligence: return "Embargo Intelligence";
        default:                                         return "Unknown";
    }
}

/// Base success rate for each mission type (before modifiers).
[[nodiscard]] constexpr float espionageBaseSuccessRate(EspionageMissionType type) {
    switch (type) {
        case EspionageMissionType::StealTradeSecrets:   return 0.40f;  // Moderate
        case EspionageMissionType::CounterfeitCurrency: return 0.50f;  // Easier
        case EspionageMissionType::IndustrialSabotage:  return 0.30f;  // Hard
        case EspionageMissionType::InsiderTrading:       return 0.60f;  // Easiest
        case EspionageMissionType::EmbargoIntelligence: return 0.55f;
        default:                                         return 0.30f;
    }
}

/// Result of an espionage attempt.
struct EspionageResult {
    bool succeeded = false;
    bool spyCaught = false;          ///< Spy was captured (diplomatic incident)
    CurrencyAmount goldGained = 0;   ///< Gold earned (InsiderTrading)
    float techProgressGained = 0.0f; ///< Tech research boost (StealTradeSecrets)
    float inflationInjected = 0.0f;  ///< Fake money inflation (Counterfeit)
    bool buildingDestroyed = false;  ///< A building was sabotaged
};

/**
 * @brief Execute an economic espionage mission.
 *
 * @param world       ECS world.
 * @param spyOwner    Player running the mission.
 * @param target      Target player.
 * @param mission     Mission type.
 * @param spySkill    Spy experience level (0.0 to 1.0, higher = better).
 * @param rngSeed     Deterministic seed for success roll.
 * @return Result including success/failure and effects.
 */
EspionageResult executeEspionageMission(aoc::game::GameState& gameState,
                                         PlayerId spyOwner,
                                         PlayerId target,
                                         EspionageMissionType mission,
                                         float spySkill,
                                         uint32_t rngSeed);

} // namespace aoc::sim

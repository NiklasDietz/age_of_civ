#pragma once

/**
 * @file Espionage.hpp
 * @brief Spy missions and counter-intelligence.
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <string_view>

namespace aoc::sim {

enum class SpyMission : uint8_t {
    GatherIntelligence,  ///< Reveal enemy city details and nearby units
    StealTechnology,     ///< Chance to gain progress on enemy's researched tech
    SabotageProduction,  ///< Reduce a city's production for several turns
    CounterIntelligence, ///< Defend own city against enemy spies
    Recruit,             ///< Try to flip an enemy city's loyalty
};

struct SpyComponent {
    PlayerId        owner;
    hex::AxialCoord location;         ///< City the spy is assigned to
    SpyMission      currentMission = SpyMission::GatherIntelligence;
    int32_t         turnsRemaining = 0; ///< Turns until mission completes
    int32_t         experience     = 0; ///< Higher = better success chance
    bool            isRevealed     = false; ///< Enemy knows about this spy
};

/// Base success probability per mission type. Modified by spy experience and city defenses.
[[nodiscard]] constexpr float baseMissionSuccessRate(SpyMission mission) {
    switch (mission) {
        case SpyMission::GatherIntelligence:  return 0.85f;
        case SpyMission::StealTechnology:     return 0.40f;
        case SpyMission::SabotageProduction:  return 0.50f;
        case SpyMission::CounterIntelligence: return 0.70f;
        case SpyMission::Recruit:             return 0.20f;
        default:                              return 0.0f;
    }
}

/// Turns needed for each mission type.
[[nodiscard]] constexpr int32_t missionDuration(SpyMission mission) {
    switch (mission) {
        case SpyMission::GatherIntelligence:  return 3;
        case SpyMission::StealTechnology:     return 5;
        case SpyMission::SabotageProduction:  return 4;
        case SpyMission::CounterIntelligence: return 1;  // Passive, always active
        case SpyMission::Recruit:             return 8;
        default:                              return 5;
    }
}

} // namespace aoc::sim

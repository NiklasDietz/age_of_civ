#pragma once

/**
 * @file UtilityScoring.hpp
 * @brief Utility-based action scoring framework for AI decision making.
 *
 * Each possible AI action gets a utility score [0, 1]. The AI picks the
 * highest-scoring action each decision cycle. Scores are computed from
 * weighted factors (military threat, economic need, expansion opportunity, etc.).
 */

#include <cstdint>
#include <string_view>

namespace aoc::sim::ai {

/// Categories of AI actions that can be scored.
enum class ActionCategory : uint8_t {
    BuildUnit,
    BuildBuilding,
    BuildDistrict,
    ResearchTech,
    ResearchCivic,
    MoveUnit,
    AttackUnit,
    FoundCity,
    ProposeTrade,
    DeclareWar,
    MakePeace,
    ChangePolicy,
};

/// A scored action ready for comparison.
struct ScoredAction {
    ActionCategory  category;
    uint32_t        targetId;     ///< Entity ID, tech ID, unit ID, etc.
    float           utility;      ///< [0.0, 1.0] -- higher is better
    std::string_view reason;      ///< Debug: why this score
};

/// Clamp and normalize a raw score to [0, 1].
[[nodiscard]] constexpr float normalizeScore(float raw, float minVal, float maxVal) {
    if (maxVal <= minVal) {
        return 0.0f;
    }
    float clamped = raw;
    if (clamped < minVal) clamped = minVal;
    if (clamped > maxVal) clamped = maxVal;
    return (clamped - minVal) / (maxVal - minVal);
}

/// Weighted combination of factors.
struct UtilityWeights {
    float militaryThreat    = 0.25f;
    float economicNeed      = 0.25f;
    float expansionValue    = 0.20f;
    float diplomaticGain    = 0.15f;
    float scienceValue      = 0.15f;
};

} // namespace aoc::sim::ai

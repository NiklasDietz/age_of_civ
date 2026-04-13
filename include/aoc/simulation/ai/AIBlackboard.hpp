#pragma once

/**
 * @file AIBlackboard.hpp
 * @brief Shared AI blackboard: a single data structure written by advisor subsystems
 *        and read by all other AI decision makers.
 *
 * Each advisor posts a floating-point assessment in [0.0, 1.0] for its domain
 * (0.0 = no concern, 1.0 = critical). The strategic posture is derived from
 * these assessments and biases every production and action decision via
 * multipliers on utility scores.
 */

#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <vector>

namespace aoc::sim::ai {

// ============================================================================
// StrategicPosture
//
// The current high-level stance the AI has adopted based on blackboard readings.
// Changes at most every 20-30 turns to avoid thrashing.
// ============================================================================

/**
 * @brief High-level AI strategic stance that biases all production/action decisions.
 *
 * Selected by evaluateStrategicPosture() after all advisors have posted their
 * assessments.  Posture multipliers are applied to utility scores in
 * executeCityActions().
 */
enum class StrategicPosture : uint8_t {
    Expansion,       ///< Focus on settlers and infrastructure -- empire is too small
    Development,     ///< Focus on science and culture -- default balanced state
    MilitaryBuildup, ///< Build army before engaging -- not at war yet but preparing
    Aggression,      ///< Active or imminent war -- maximize offensive units
    Defense,         ///< Under threat -- fortify and garrison
    Economic,        ///< Focus on gold and trade -- treasury pressure is critical
};

// ============================================================================
// AIBlackboard
// ============================================================================

/**
 * @brief Shared blackboard written by AI advisors and read by all AI subsystems.
 *
 * Advisors run at different frequencies and post their domain assessments here.
 * The strategic posture is evaluated once after all advisors have run.
 * Per-player: stored directly on the Player object so no separate allocation.
 *
 * Field layout is ordered largest-to-smallest to avoid padding.
 */
struct AIBlackboard {
    // -----------------------------------------------------------------------
    // Strategic targets (heap, written by advisors)
    // -----------------------------------------------------------------------

    /// Enemy city or unit coordinates identified as attack targets by MilitaryAdvisor.
    std::vector<aoc::hex::AxialCoord> attackTargets;

    /// Own city coordinates flagged as priority defense sites.
    std::vector<aoc::hex::AxialCoord> defendPriorities;

    /// Top candidate city founding sites scored by ExpansionAdvisor.
    std::vector<aoc::hex::AxialCoord> bestCitySites;

    // -----------------------------------------------------------------------
    // Domain assessments (0.0 = no concern, 1.0 = critical)
    // Posted by individual advisors; read by posture evaluator and scorers.
    // -----------------------------------------------------------------------

    /// Ratio of nearby enemy strength to own military (MilitaryAdvisor).
    float threatLevel            = 0.0f;

    /// Fraction of empire target size not yet reached (ExpansionAdvisor).
    float expansionOpportunity   = 0.0f;

    /// How far behind the tech average this player is (ResearchAdvisor).
    float techGap                = 0.0f;

    /// How strained the gold budget is: maintenance vs income (EconomyAdvisor).
    float goldPressure           = 0.0f;

    /// Number of active wars normalised to [0,1] (DiplomacyAdvisor).
    float diplomaticDanger       = 0.0f;

    /// How much the faith/religion system could benefit this player (placeholder).
    float faithOpportunity       = 0.0f;

    // -----------------------------------------------------------------------
    // Recommended policy values (written by EconomyAdvisor)
    // -----------------------------------------------------------------------

    /// Tax rate the economy advisor recommends to balance the budget.
    float recommendedTaxRate     = 0.15f;

    // -----------------------------------------------------------------------
    // Recommended force size (written by MilitaryAdvisor)
    // -----------------------------------------------------------------------

    /// Number of military units the AI should maintain.
    int32_t desiredMilitaryUnits = 2;

    // -----------------------------------------------------------------------
    // Strategic posture (written by evaluateStrategicPosture)
    // -----------------------------------------------------------------------

    StrategicPosture posture = StrategicPosture::Development;

    // -----------------------------------------------------------------------
    // Last-update turn numbers -- used to throttle advisor frequency.
    // A value of -1 means the advisor has never run.
    // -----------------------------------------------------------------------

    int32_t lastMilitaryUpdate   = -1;  ///< MilitaryAdvisor runs every turn
    int32_t lastEconomyUpdate    = -1;  ///< EconomyAdvisor runs every 5 turns
    int32_t lastExpansionUpdate  = -1;  ///< ExpansionAdvisor runs every 10 turns
    int32_t lastResearchUpdate   = -1;  ///< ResearchAdvisor runs every 10 turns
    int32_t lastDiplomacyUpdate  = -1;  ///< DiplomacyAdvisor runs every 20 turns
};

} // namespace aoc::sim::ai

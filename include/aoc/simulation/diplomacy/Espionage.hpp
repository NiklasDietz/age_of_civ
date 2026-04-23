#pragma once

/**
 * @file Espionage.hpp
 * @brief Comprehensive spy system with economic intelligence, graduated outcomes,
 *        spy levels, and promotions.
 *
 * Espionage is designed to interact deeply with the game's economic simulation:
 * spies can monitor treasury, disrupt supply chains, manipulate currency trust,
 * and steal industrial secrets. This differentiates from Civ 6's district-based
 * spy system by tying missions to the game's unique economic depth.
 *
 * Spy Levels (Civ 6 inspired):
 *   Recruit → Agent → Secret Agent → Master Spy
 *   Each level shifts the success probability one step up the probability scale:
 *   {16%, 25%, 37%, 50%, 63%, 74%, 84%, 90%}
 *
 * Failure Outcomes (graduated, not binary):
 *   1. Escaped undetected — spy returns safely, no diplo penalty
 *   2. Identified — enemy knows who sent the spy, small diplo hit
 *   3. Captured — enemy holds spy (can release/imprison/execute/turn)
 *   4. Killed — spy permanently lost
 *
 * Intelligence Levels (novel mechanic):
 *   Level 0: No info (fog of war)
 *   Level 1: Buildings/districts visible (embassy)
 *   Level 2: Military composition + approximate strength (scout contact)
 *   Level 3: Treasury, income/turn, production queues (active spy)
 *   Level 4: Tech research, trade routes, diplomatic deals (spy network)
 *   Level 5: Exact stockpiles, supply chain state (master spy network)
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace aoc::sim {

// ============================================================================
// Spy missions
// ============================================================================

enum class SpyMission : uint8_t {
    // === Passive intelligence (ongoing while placed) ===
    GatherIntelligence,    ///< Reveal enemy city details and nearby units (Intel Level 3)
    CounterIntelligence,   ///< Defend own city against enemy spies
    MonitorTreasury,       ///< Reveals enemy gold income/expenses per turn
    MonitorResearch,       ///< Reveals what tech enemy is researching + progress

    // === Active offensive (one-shot, then spy returns) ===
    StealTechnology,       ///< Gain 20-50% progress on enemy's current research
    SabotageProduction,    ///< Reduce city production progress by 50%
    SiphonFunds,           ///< Steal % of enemy's Commercial district gold each turn
    MarketManipulation,    ///< Temporarily crash enemy's local market prices
    CurrencyCounterfeit,   ///< Reduce enemy CurrencyTrust by 5-15 points
    SupplyChainDisrupt,    ///< Target a supply chain node, reduce efficiency 50% for 10 turns
    InsiderTrading,        ///< Gain preview of enemy stock market moves for 10 turns
    StealTradeSecrets,     ///< Copy enemy's industrial revolution bonus if they're ahead
    RecruitPartisans,      ///< Spawn hostile militia units near the target city
    FomentUnrest,          ///< Reduce target city loyalty by 20-30 points
    NeutralizeGovernor,    ///< Disable enemy governor in target city for 10 turns

    // === Special ===
    RecruitDoubleAgent,    ///< Turn a captured enemy spy into a double agent
    EstablishEmbassy,      ///< Non-risky: establish diplomatic contact (Intel Level 1)

    Count
};

// ============================================================================
// Spy levels and promotions
// ============================================================================

enum class SpyLevel : uint8_t {
    Recruit      = 0,  ///< Starting level
    Agent        = 1,  ///< First promotion
    SecretAgent  = 2,  ///< Second promotion
    MasterSpy    = 3,  ///< Maximum level
};

/// Success probability scale. Each spy level shifts one step up.
/// Previous distribution (0.16..0.90) left level-0 Recruits at 16% success
/// for the cheapest missions, producing ~85% batch failure.  Shifted the
/// low end up so entry-level spies have an honest shot (≈ 35%) while the
/// top still caps near 0.92 — stronger mission flavour per promotion.
inline constexpr std::array<float, 8> SPY_PROBABILITY_SCALE = {
    0.35f, 0.44f, 0.52f, 0.60f, 0.68f, 0.76f, 0.85f, 0.92f
};

enum class SpyPromotion : uint8_t {
    None = 0,
    // Economic promotions
    Financier,       ///< +2 levels for SiphonFunds, MarketManipulation
    Analyst,         ///< +2 levels for MonitorTreasury, MonitorResearch, InsiderTrading
    Counterfeiter,   ///< +2 levels for CurrencyCounterfeit, SupplyChainDisrupt
    // Military promotions
    Saboteur,        ///< +2 levels for SabotageProduction, SupplyChainDisrupt
    GuerrillaLeader, ///< +2 levels for RecruitPartisans, FomentUnrest
    // Defensive promotions
    Seduction,       ///< +2 levels for CounterIntelligence
    Polygraph,       ///< Enemy spies in your city operate at -1 level
    // Utility promotions
    Linguist,        ///< All missions complete 25% faster
    AceDriver,       ///< +4 levels to escape chance if caught
    Disguise,        ///< Skip the establishment phase (instant mission start)

    Count
};

/// Failure outcome — graduated, not binary.
enum class SpyFailureOutcome : uint8_t {
    EscapedUndetected,  ///< Spy returns to capital, no diplomatic penalty
    Identified,         ///< Enemy knows who sent the spy, small diplo hit (-5)
    Captured,           ///< Enemy holds the spy (can release/imprison/execute/turn)
    Killed,             ///< Spy permanently lost, moderate diplo hit (-15)
};

/// Intelligence level a player has about another player.
enum class IntelligenceLevel : uint8_t {
    None          = 0,  ///< Fog of war — no info
    Basic         = 1,  ///< Buildings/districts visible (embassy)
    Military      = 2,  ///< Military composition + approximate strength
    Economic      = 3,  ///< Treasury, income/turn, production queues
    Comprehensive = 4,  ///< Tech research, trade routes, diplomatic deals
    Complete      = 5,  ///< Exact stockpiles, supply chain state, internal politics
};

// ============================================================================
// Spy component (on Unit)
// ============================================================================

struct SpyComponent {
    PlayerId          owner = INVALID_PLAYER;
    hex::AxialCoord   location;
    SpyMission        currentMission  = SpyMission::GatherIntelligence;
    SpyLevel          level           = SpyLevel::Recruit;
    int32_t           turnsRemaining  = 0;
    int32_t           experience      = 0;  ///< XP toward next level (10 XP per level)
    bool              isRevealed      = false;
    SpyPromotion      promotion1      = SpyPromotion::None;
    SpyPromotion      promotion2      = SpyPromotion::None;
    SpyPromotion      promotion3      = SpyPromotion::None;  ///< Max 3 promotions

    /// Effective level for a specific mission (base + promotion bonuses).
    [[nodiscard]] int32_t effectiveLevel(SpyMission mission) const {
        int32_t lvl = static_cast<int32_t>(this->level);
        // Check promotion bonuses (+2 for matching missions)
        for (SpyPromotion promo : {this->promotion1, this->promotion2, this->promotion3}) {
            if (promo == SpyPromotion::None) { continue; }
            if (promo == SpyPromotion::Financier
                && (mission == SpyMission::SiphonFunds
                    || mission == SpyMission::MarketManipulation)) {
                lvl += 2;
            }
            if (promo == SpyPromotion::Analyst
                && (mission == SpyMission::MonitorTreasury
                    || mission == SpyMission::MonitorResearch
                    || mission == SpyMission::InsiderTrading)) {
                lvl += 2;
            }
            if (promo == SpyPromotion::Counterfeiter
                && (mission == SpyMission::CurrencyCounterfeit
                    || mission == SpyMission::SupplyChainDisrupt)) {
                lvl += 2;
            }
            if (promo == SpyPromotion::Saboteur
                && (mission == SpyMission::SabotageProduction
                    || mission == SpyMission::SupplyChainDisrupt)) {
                lvl += 2;
            }
            if (promo == SpyPromotion::GuerrillaLeader
                && (mission == SpyMission::RecruitPartisans
                    || mission == SpyMission::FomentUnrest)) {
                lvl += 2;
            }
            if (promo == SpyPromotion::Seduction
                && mission == SpyMission::CounterIntelligence) {
                lvl += 2;
            }
        }
        return lvl;
    }

    /// Whether this spy has the Linguist promotion (25% faster missions).
    [[nodiscard]] bool hasLinguist() const {
        return this->promotion1 == SpyPromotion::Linguist
            || this->promotion2 == SpyPromotion::Linguist
            || this->promotion3 == SpyPromotion::Linguist;
    }

    /// Whether this spy has the AceDriver promotion (+4 escape).
    [[nodiscard]] bool hasAceDriver() const {
        return this->promotion1 == SpyPromotion::AceDriver
            || this->promotion2 == SpyPromotion::AceDriver
            || this->promotion3 == SpyPromotion::AceDriver;
    }

    /// Whether this spy has the Disguise promotion (instant start).
    [[nodiscard]] bool hasDisguise() const {
        return this->promotion1 == SpyPromotion::Disguise
            || this->promotion2 == SpyPromotion::Disguise
            || this->promotion3 == SpyPromotion::Disguise;
    }

    /// Number of promotions this spy has.
    [[nodiscard]] int32_t promotionCount() const {
        int32_t count = 0;
        if (this->promotion1 != SpyPromotion::None) { ++count; }
        if (this->promotion2 != SpyPromotion::None) { ++count; }
        if (this->promotion3 != SpyPromotion::None) { ++count; }
        return count;
    }

    /// Add a promotion (up to 3). Returns false if all slots full.
    bool addPromotion(SpyPromotion promo) {
        if (this->promotion1 == SpyPromotion::None) { this->promotion1 = promo; return true; }
        if (this->promotion2 == SpyPromotion::None) { this->promotion2 = promo; return true; }
        if (this->promotion3 == SpyPromotion::None) { this->promotion3 = promo; return true; }
        return false;
    }

    /// Add XP and check for level-up. Returns true if leveled up.
    bool addExperience(int32_t xp) {
        this->experience += xp;
        constexpr int32_t XP_PER_LEVEL = 10;
        if (this->experience >= XP_PER_LEVEL
            && static_cast<uint8_t>(this->level) < static_cast<uint8_t>(SpyLevel::MasterSpy)) {
            this->experience -= XP_PER_LEVEL;
            this->level = static_cast<SpyLevel>(static_cast<uint8_t>(this->level) + 1);
            return true;
        }
        return false;
    }
};

// ============================================================================
// Mission definitions
// ============================================================================

struct SpyMissionDef {
    SpyMission       id;
    std::string_view name;
    int32_t          baseProbabilityIndex;  ///< Index into SPY_PROBABILITY_SCALE
    int32_t          baseDuration;          ///< Turns to complete
    bool             isPassive;             ///< Ongoing effect vs one-shot
    bool             isOffensive;           ///< Targets enemy (vs own city)
};

inline constexpr std::array<SpyMissionDef, static_cast<std::size_t>(SpyMission::Count)> SPY_MISSION_DEFS = {{
    {SpyMission::GatherIntelligence,  "Gather Intelligence",    5, 3, true,  true},
    {SpyMission::CounterIntelligence, "Counter-Intelligence",   6, 1, true,  false},
    {SpyMission::MonitorTreasury,     "Monitor Treasury",       5, 2, true,  true},
    {SpyMission::MonitorResearch,     "Monitor Research",       5, 2, true,  true},
    {SpyMission::StealTechnology,     "Steal Technology",       2, 8, false, true},
    {SpyMission::SabotageProduction,  "Sabotage Production",    2, 6, false, true},
    {SpyMission::SiphonFunds,         "Siphon Funds",           3, 8, false, true},
    {SpyMission::MarketManipulation,  "Market Manipulation",    2, 8, false, true},
    {SpyMission::CurrencyCounterfeit, "Currency Counterfeiting",1, 10, false, true},
    {SpyMission::SupplyChainDisrupt,  "Supply Chain Disruption",1, 10, false, true},
    {SpyMission::InsiderTrading,      "Insider Trading",        3, 6, false, true},
    {SpyMission::StealTradeSecrets,   "Steal Trade Secrets",    2, 8, false, true},
    {SpyMission::RecruitPartisans,    "Recruit Partisans",      0, 8, false, true},
    {SpyMission::FomentUnrest,        "Foment Unrest",          3, 6, false, true},
    {SpyMission::NeutralizeGovernor,  "Neutralize Governor",    2, 8, false, true},
    {SpyMission::RecruitDoubleAgent,  "Recruit Double Agent",   1, 10, false, true},
    {SpyMission::EstablishEmbassy,    "Establish Embassy",      7, 1, false, true},
}};

/// Get the mission definition.
[[nodiscard]] inline constexpr const SpyMissionDef& spyMissionDef(SpyMission mission) {
    return SPY_MISSION_DEFS[static_cast<std::size_t>(mission)];
}

/// Compute success probability for a spy on a specific mission.
/// Takes into account spy level, promotions, and counter-spy presence.
[[nodiscard]] inline float missionSuccessRate(const SpyComponent& spy, SpyMission mission,
                                               int32_t counterSpyLevelBonus = 0) {
    const int32_t effectiveLvl = spy.effectiveLevel(mission);
    const SpyMissionDef& def = spyMissionDef(mission);

    // Base probability index + spy effective level - counter-spy bonus
    int32_t probIndex = def.baseProbabilityIndex + effectiveLvl - counterSpyLevelBonus;
    probIndex = std::max(0, std::min(probIndex, static_cast<int32_t>(SPY_PROBABILITY_SCALE.size()) - 1));

    return SPY_PROBABILITY_SCALE[static_cast<std::size_t>(probIndex)];
}

/// Mission duration, adjusted for Linguist promotion.
[[nodiscard]] inline int32_t adjustedMissionDuration(const SpyComponent& spy, SpyMission mission) {
    int32_t duration = spyMissionDef(mission).baseDuration;
    if (spy.hasLinguist()) {
        duration = std::max(1, duration * 3 / 4);  // 25% faster
    }
    if (spy.hasDisguise()) {
        duration = std::max(1, duration - 1);  // Skip establishment phase
    }
    return duration;
}

/// Determine failure outcome (graduated). Higher spy level = better escape chance.
[[nodiscard]] inline SpyFailureOutcome rollFailureOutcome(const SpyComponent& spy,
                                                           float randomValue) {
    // Escape thresholds based on spy level. Better spies escape more often.
    // AceDriver promotion adds +4 to effective escape level.
    int32_t escapeLvl = static_cast<int32_t>(spy.level);
    if (spy.hasAceDriver()) { escapeLvl += 4; }

    // Probability distribution:
    // Recruit: 30% escape, 30% identified, 25% captured, 15% killed
    // Agent:   45% escape, 25% identified, 20% captured, 10% killed
    // SecretAgent: 55% escape, 25% identified, 15% captured, 5% killed
    // MasterSpy:   70% escape, 20% identified, 8% captured, 2% killed
    const float escapeChance = 0.30f + static_cast<float>(escapeLvl) * 0.10f;
    const float identifyChance = 0.30f - static_cast<float>(escapeLvl) * 0.025f;
    const float captureChance = 0.25f - static_cast<float>(escapeLvl) * 0.05f;
    // Rest = killed

    if (randomValue < escapeChance) { return SpyFailureOutcome::EscapedUndetected; }
    if (randomValue < escapeChance + identifyChance) { return SpyFailureOutcome::Identified; }
    if (randomValue < escapeChance + identifyChance + captureChance) { return SpyFailureOutcome::Captured; }
    return SpyFailureOutcome::Killed;
}

/// Name strings for UI and logging.
[[nodiscard]] inline std::string_view spyLevelName(SpyLevel level) {
    switch (level) {
        case SpyLevel::Recruit:     return "Recruit";
        case SpyLevel::Agent:       return "Agent";
        case SpyLevel::SecretAgent: return "Secret Agent";
        case SpyLevel::MasterSpy:   return "Master Spy";
    }
    return "Unknown";
}

[[nodiscard]] inline std::string_view spyPromotionName(SpyPromotion promo) {
    switch (promo) {
        case SpyPromotion::None:           return "None";
        case SpyPromotion::Financier:      return "Financier";
        case SpyPromotion::Analyst:        return "Analyst";
        case SpyPromotion::Counterfeiter:  return "Counterfeiter";
        case SpyPromotion::Saboteur:       return "Saboteur";
        case SpyPromotion::GuerrillaLeader:return "Guerrilla Leader";
        case SpyPromotion::Seduction:      return "Seduction";
        case SpyPromotion::Polygraph:      return "Polygraph";
        case SpyPromotion::Linguist:       return "Linguist";
        case SpyPromotion::AceDriver:      return "Ace Driver";
        case SpyPromotion::Disguise:       return "Disguise";
        default:                           return "Unknown";
    }
}

[[nodiscard]] inline std::string_view spyFailureOutcomeName(SpyFailureOutcome outcome) {
    switch (outcome) {
        case SpyFailureOutcome::EscapedUndetected: return "Escaped";
        case SpyFailureOutcome::Identified:        return "Identified";
        case SpyFailureOutcome::Captured:          return "Captured";
        case SpyFailureOutcome::Killed:            return "Killed";
    }
    return "Unknown";
}

} // namespace aoc::sim

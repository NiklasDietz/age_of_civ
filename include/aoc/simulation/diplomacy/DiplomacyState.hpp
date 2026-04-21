#pragma once

/**
 * @file DiplomacyState.hpp
 * @brief Pairwise diplomatic relations between players.
 *
 * Relations are stored as an NxN matrix (flat array). Each pair has a
 * score [-100, +100] with active modifiers that decay over time.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/simulation/diplomacy/AllianceTypes.hpp"
#include "aoc/simulation/diplomacy/CasusBelli.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aoc::sim {

/// A time-decaying relation modifier (e.g., "settled near our borders" -5, decays over 20 turns).
struct RelationModifier {
    std::string reason;
    int32_t     amount;          ///< Positive = friendly, negative = hostile
    int32_t     turnsRemaining;  ///< 0 = permanent
};

/// Diplomatic stance derived from the relation score.
enum class DiplomaticStance : uint8_t {
    Hostile,       ///< [-100, -40]
    Unfriendly,    ///< (-40, -10]
    Neutral,       ///< (-10, 10)
    Friendly,      ///< [10, 40)
    Allied,        ///< [40, 100]
};

[[nodiscard]] constexpr std::string_view stanceName(DiplomaticStance stance) {
    switch (stance) {
        case DiplomaticStance::Hostile:    return "Hostile";
        case DiplomaticStance::Unfriendly: return "Unfriendly";
        case DiplomaticStance::Neutral:    return "Neutral";
        case DiplomaticStance::Friendly:   return "Friendly";
        case DiplomaticStance::Allied:     return "Allied";
        default:                           return "Unknown";
    }
}

[[nodiscard]] constexpr DiplomaticStance stanceFromScore(int32_t score) {
    if (score <= -40) return DiplomaticStance::Hostile;
    if (score <= -10) return DiplomaticStance::Unfriendly;
    if (score < 10)   return DiplomaticStance::Neutral;
    if (score < 40)   return DiplomaticStance::Friendly;
    return DiplomaticStance::Allied;
}

/// A time-decaying reputation modifier. Reputation tracks how honorably a player
/// behaves: paying tolls, respecting borders, honoring agreements. AI uses this
/// to decide toll rates, alliance offers, and war declarations. Human players
/// see the score but aren't bound by it.
struct ReputationModifier {
    int32_t amount;          ///< Positive = trustworthy, negative = untrustworthy
    int32_t turnsRemaining;  ///< 0 = permanent
};

/// Pairwise relation data between two players.
struct PairwiseRelation {
    int32_t baseScore = 0;   ///< Base relation score (from modifiers + events)
    bool    hasMet    = false; ///< Whether these players have discovered each other
    int32_t metOnTurn = -1;   ///< Turn when first contact occurred (-1 = never met)
    bool    isAtWar   = false;
    int32_t turnsSincePeace = 100; ///< Turns since last peace treaty (starts high = no cooldown)
    bool    hasOpenBorders     = false;
    bool    hasDefensiveAlliance = false;
    bool    hasMilitaryAlliance  = false;   ///< Share visibility, join wars
    bool    hasResearchAgreement = false;   ///< +10% science for both
    bool    hasEconomicAlliance  = false;   ///< Shared market prices, reduced tariffs
    bool    hasCulturalAlliance  = false;   ///< +25% tourism between allies (L1)
    bool    hasReligiousAlliance = false;   ///< +25% faith on shared holy sites (L1)
    bool    hasEmbargo         = false;

    // -- Alliance level tracking (H1.1) --
    /// Per-type level/turnsActive. Index by AllianceType value. Entry 0 is
    /// reserved for AllianceType::None and unused. The matching boolean
    /// above gates `isActive`; this state only tracks level progression.
    std::array<AllianceState, ALLIANCE_TYPE_COUNT + 1> alliances{};

    /// Turn number on which the most recent alliance of any type was formed.
    /// Used by the 20-turn cooldown (H1.6) to prevent alliance spam.
    /// -1 means no alliance has ever been formed on this pair.
    int32_t lastAllianceFormTurn = -1;

    /// Countdown for auto-break when relations sour (H1.4). Incremented each
    /// turn `totalScore() < -30` while an alliance is active; when it reaches
    /// the auto-break threshold, all alliances are cleared and both sides
    /// take a reputation hit. Reset to 0 as soon as score recovers.
    int32_t allianceBreakWarningTurns = 0;
    std::vector<uint16_t> embargoedGoods; ///< Per-resource embargo list (good IDs)
    std::vector<RelationModifier> modifiers;

    // -- Soft border violation tracking --
    // Units CAN enter foreign territory without Open Borders. The consequences
    // are diplomatic (reputation penalty, casus belli), not mechanical barriers.
    int32_t unitsInTerritory    = 0;   ///< Military units currently in territory (updated per turn)
    int32_t turnsWithViolation  = 0;   ///< Consecutive turns with units present
    bool    casusBelliLand      = false; ///< CB from land border violation
    bool    warningIssued       = false; ///< First warning notification sent

    // -- Naval passage violation tracking (mirrors land border violations) --
    int32_t navalUnitsInWaters    = 0;   ///< Naval military units in owned waters (updated per turn)
    int32_t turnsWithNavalViolation = 0; ///< Consecutive turns with naval units present
    bool    casusBelliNaval       = false; ///< CB from naval passage violation
    bool    navalWarningIssued    = false; ///< First naval warning notification sent

    /// True if any CB source grants war-without-penalty.
    [[nodiscard]] bool casusBelliGranted() const {
        return this->casusBelliLand || this->casusBelliNaval;
    }

    // -- Treaty tracking --
    PlayerId lastWarAggressor = INVALID_PLAYER; ///< Who started the last war (for NonAggression enforcement)

    /// Casus belli claimed for the most recent `declareWar` on this pair (H1.5).
    /// Grievance penalty applied to the war modifier scales with
    /// `casusBelliDef(lastCasusBelli).grievanceMultiplier`.
    CasusBelliType lastCasusBelli = CasusBelliType::SurpriseWar;

    // -- Passive warming --
    /// Accumulated peace-time warming (capped). Sustained peace slowly grows the
    /// relation so civs can reach thresholds for open borders, bilateral deals,
    /// alliances. Increments each turn while hasMet && !isAtWar. Resets to 0 on
    /// war declaration. Included in totalScore().
    int32_t passiveBonus = 0;

    // -- Political reputation (separate from relation score) --
    // Reputation tracks behavioral trustworthiness: paying tolls, respecting
    // borders, honoring agreements. AI reads this when setting toll rates and
    // deciding diplomatic responses. Human players see the score and its
    // effects (e.g., higher tolls) but make their own choices.
    std::vector<ReputationModifier> reputationModifiers;

    /// Compute the total score = baseScore + passiveBonus + sum of active modifiers.
    [[nodiscard]] int32_t totalScore() const {
        int32_t total = this->baseScore + this->passiveBonus;
        for (const RelationModifier& mod : this->modifiers) {
            total += mod.amount;
        }
        if (total < -100) return -100;
        if (total > 100)  return 100;
        return total;
    }

    [[nodiscard]] DiplomaticStance stance() const {
        return stanceFromScore(this->totalScore());
    }

    /// True if any alliance type (Defensive, Military, Research, Economic,
    /// Cultural, Religious) is currently active between the pair. Used by
    /// form*Alliance to reject overlapping alliances (H1.3).
    [[nodiscard]] bool hasAnyAlliance() const {
        return this->hasDefensiveAlliance
            || this->hasMilitaryAlliance
            || this->hasResearchAgreement
            || this->hasEconomicAlliance
            || this->hasCulturalAlliance
            || this->hasReligiousAlliance;
    }

    /// Check if a specific good is embargoed (blanket embargo or per-resource).
    [[nodiscard]] bool isGoodEmbargoed(uint16_t goodId) const {
        if (this->hasEmbargo) { return true; }
        for (uint16_t id : this->embargoedGoods) {
            if (id == goodId) { return true; }
        }
        return false;
    }

    /// Political reputation score: sum of active reputation modifiers, clamped [-100, +100].
    /// Positive = trustworthy (honors agreements, pays tolls, respects borders).
    /// Negative = untrustworthy (violates borders, refuses tolls, breaks deals).
    [[nodiscard]] int32_t reputationScore() const {
        int32_t total = 0;
        for (const ReputationModifier& mod : this->reputationModifiers) {
            total += mod.amount;
        }
        if (total < -100) return -100;
        if (total > 100)  return 100;
        return total;
    }
};

class DiplomacyManager {
public:
    /**
     * @brief Initialize the relation matrix for a given number of players.
     */
    void initialize(uint8_t playerCount);

    /// Get the relation between two players. Order doesn't matter (symmetric).
    [[nodiscard]] PairwiseRelation& relation(PlayerId a, PlayerId b);
    [[nodiscard]] const PairwiseRelation& relation(PlayerId a, PlayerId b) const;

    /// Record first contact between two players.
    void meetPlayers(PlayerId a, PlayerId b, int32_t currentTurn);

    /// Whether two players have met.
    [[nodiscard]] bool haveMet(PlayerId a, PlayerId b) const;

    /// Add a relation modifier between two players.
    void addModifier(PlayerId a, PlayerId b, RelationModifier modifier);

    /// Add a reputation modifier between two players. Reputation is separate from
    /// relation score: it tracks behavioral trustworthiness (toll payment, border
    /// respect, agreement honoring). AI reads it for toll rates and diplomacy.
    void addReputationModifier(PlayerId a, PlayerId b, int32_t amount, int32_t decayTurns);

    /// Declare war between two players, optionally with a Casus Belli
    /// justification. The war modifier penalty is scaled by
    /// `casusBelliDef(cb).grievanceMultiplier` (0.0 = no grievance, 1.0 = full
    /// -50). Default Surprise War applies the full penalty.
    /// If an AllianceObligationTracker is provided, alliance obligations are
    /// generated for the target's allies automatically.
    void declareWar(PlayerId aggressor, PlayerId target,
                    CasusBelliType cb = CasusBelliType::SurpriseWar,
                    struct AllianceObligationTracker* allianceTracker = nullptr);

    /// Make peace between two players.
    void makePeace(PlayerId a, PlayerId b);

    /// Grant open borders between two players.
    void grantOpenBorders(PlayerId a, PlayerId b);

    /// Form a defensive alliance. Returns ErrorCode::AllianceExists if the
    /// pair already has any alliance active or is within the 20-turn post-
    /// formation cooldown (H1.3 / H1.6).
    [[nodiscard]] ErrorCode formDefensiveAlliance(PlayerId a, PlayerId b, int32_t currentTurn);

    /// Form a military alliance (shared visibility, join wars).
    [[nodiscard]] ErrorCode formMilitaryAlliance(PlayerId a, PlayerId b, int32_t currentTurn);

    /// Form a research agreement (+10% science for both).
    [[nodiscard]] ErrorCode formResearchAgreement(PlayerId a, PlayerId b, int32_t currentTurn);

    /// Form an economic alliance (shared market prices, reduced tariffs).
    [[nodiscard]] ErrorCode formEconomicAlliance(PlayerId a, PlayerId b, int32_t currentTurn);

    /// Form a cultural alliance (+25% tourism; L3 shares Great Works slots).
    [[nodiscard]] ErrorCode formCulturalAlliance(PlayerId a, PlayerId b, int32_t currentTurn);

    /// Form a religious alliance (+25% faith on shared holy sites).
    [[nodiscard]] ErrorCode formReligiousAlliance(PlayerId a, PlayerId b, int32_t currentTurn);

    /**
     * @brief Tick all modifiers: decrement turnsRemaining, remove expired ones.
     * Also ticks alliance levels (H1.1) and runs auto-break (H1.4) when
     * relation score stays below -30 for two consecutive turns.
     * Called once per turn during the DiplomacyDecay phase.
     */
    void tickModifiers();

    [[nodiscard]] uint8_t playerCount() const { return this->m_playerCount; }

    /// Check if two players are at war.
    [[nodiscard]] bool isAtWar(PlayerId a, PlayerId b) const;

    /// Set or lift a trade embargo between two players.
    void setEmbargo(PlayerId a, PlayerId b, bool embargo);

    /// Check if a trade embargo exists between two players.
    [[nodiscard]] bool hasEmbargo(PlayerId a, PlayerId b) const;

    /// Set or lift a per-resource embargo between two players.
    void setResourceEmbargo(PlayerId a, PlayerId b, uint16_t goodId, bool embargo);

    /// Check if a specific resource is embargoed between two players.
    [[nodiscard]] bool hasResourceEmbargo(PlayerId a, PlayerId b, uint16_t goodId) const;

    /// Apply a reputation penalty from `violator` to ALL players that have met them.
    /// Used for global dishonor events (breaking treaties, etc.).
    void broadcastReputationPenalty(PlayerId violator, int32_t amount, int32_t decayTurns);

private:
    /// Flat NxN matrix: index = a * playerCount + b.
    std::vector<PairwiseRelation> m_relations;
    uint8_t m_playerCount = 0;
};

} // namespace aoc::sim

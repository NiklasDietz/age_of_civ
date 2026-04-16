#pragma once

/**
 * @file DiplomacyState.hpp
 * @brief Pairwise diplomatic relations between players.
 *
 * Relations are stored as an NxN matrix (flat array). Each pair has a
 * score [-100, +100] with active modifiers that decay over time.
 */

#include "aoc/core/Types.hpp"

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
    bool    hasEmbargo         = false;
    std::vector<RelationModifier> modifiers;

    // -- Soft border violation tracking --
    // Units CAN enter foreign territory without Open Borders. The consequences
    // are diplomatic (reputation penalty, casus belli), not mechanical barriers.
    int32_t unitsInTerritory    = 0;   ///< Military units currently in territory (updated per turn)
    int32_t turnsWithViolation  = 0;   ///< Consecutive turns with units present
    bool    casusBelliGranted   = false; ///< Territory owner can declare war without third-party penalty
    bool    warningIssued       = false; ///< First warning notification sent

    // -- Political reputation (separate from relation score) --
    // Reputation tracks behavioral trustworthiness: paying tolls, respecting
    // borders, honoring agreements. AI reads this when setting toll rates and
    // deciding diplomatic responses. Human players see the score and its
    // effects (e.g., higher tolls) but make their own choices.
    std::vector<ReputationModifier> reputationModifiers;

    /// Compute the total score = baseScore + sum of active modifiers.
    [[nodiscard]] int32_t totalScore() const {
        int32_t total = this->baseScore;
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

    /// Declare war between two players.
    void declareWar(PlayerId aggressor, PlayerId target);

    /// Make peace between two players.
    void makePeace(PlayerId a, PlayerId b);

    /// Grant open borders between two players.
    void grantOpenBorders(PlayerId a, PlayerId b);

    /// Form a defensive alliance.
    void formDefensiveAlliance(PlayerId a, PlayerId b);

    /// Form a military alliance (shared visibility, join wars).
    void formMilitaryAlliance(PlayerId a, PlayerId b);

    /// Form a research agreement (+10% science for both).
    void formResearchAgreement(PlayerId a, PlayerId b);

    /// Form an economic alliance (shared market prices, reduced tariffs).
    void formEconomicAlliance(PlayerId a, PlayerId b);

    /**
     * @brief Tick all modifiers: decrement turnsRemaining, remove expired ones.
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

private:
    /// Flat NxN matrix: index = a * playerCount + b.
    std::vector<PairwiseRelation> m_relations;
    uint8_t m_playerCount = 0;
};

} // namespace aoc::sim

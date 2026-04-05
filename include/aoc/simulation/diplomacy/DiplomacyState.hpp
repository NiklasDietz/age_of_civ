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

/// Pairwise relation data between two players.
struct PairwiseRelation {
    int32_t baseScore = 0;   ///< Base relation score (from modifiers + events)
    bool    isAtWar   = false;
    bool    hasOpenBorders     = false;
    bool    hasDefensiveAlliance = false;
    bool    hasEmbargo         = false;
    std::vector<RelationModifier> modifiers;

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

    /// Add a relation modifier between two players.
    void addModifier(PlayerId a, PlayerId b, RelationModifier modifier);

    /// Declare war between two players.
    void declareWar(PlayerId aggressor, PlayerId target);

    /// Make peace between two players.
    void makePeace(PlayerId a, PlayerId b);

    /// Grant open borders between two players.
    void grantOpenBorders(PlayerId a, PlayerId b);

    /// Form a defensive alliance.
    void formDefensiveAlliance(PlayerId a, PlayerId b);

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

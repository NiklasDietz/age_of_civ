#pragma once

/**
 * @file Grievance.hpp
 * @brief Grievance / casus belli system for diplomacy.
 *
 * Tracks specific negative events between players that affect diplomatic
 * relations and provide justification for wars.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <vector>

namespace aoc::game { class GameState; }

namespace aoc::sim {

enum class GrievanceType : uint8_t {
    SettledNearBorders,        ///< -10, decays over 30 turns
    BrokePromise,              ///< -20, decays over 50 turns
    DeclaredWarOnAlly,         ///< -30, decays over 100 turns (H1.10 cap)
    ConqueredCity,             ///< -15 per city, decays over 40 turns
    TradeEmbargo,              ///< -10, decays over 100 turns; refreshed while active
    ViolatedEmbargo,           ///< -15, decays over 40 turns
    FailedAllianceObligation,  ///< -25, decays over 60 turns
    BrokeNonAggression,        ///< -30, decays over 100 turns (H1.10 cap)
    DMZViolation,              ///< -10, decays over 20 turns
    LostCityToSecession,       ///< -15 per city, decays over 100 turns (H1.10 cap)
    IdeologicalDifference,     ///< -5/turn, capped at -50 per pair
    EspionageCaught,           ///< -20 per caught spy, decays over 40 turns
    BulliedCityState,          ///< -10 per bully, decays over 30 turns
    PriceGouged,               ///< -10, decays over 40 turns; refreshed while squeezed
};

struct Grievance {
    GrievanceType type;
    PlayerId      against;
    int32_t       severity;
    int32_t       turnsRemaining; ///< Always > 0 after H1.10; zero-sentinel retired.
};

/// Generate IdeologicalDifference grievances for every pair of players whose
/// post-industrial governments differ. Call once per turn. Caps the
/// accumulated total per pair at -50.
void accrueIdeologicalGrievances(::aoc::game::GameState& gameState);

/// Per-player grievance tracker (ECS component).
struct PlayerGrievanceComponent {
    PlayerId owner = INVALID_PLAYER;
    std::vector<Grievance> grievances;

    /// Add a grievance of the given type against the specified player.
    void addGrievance(GrievanceType type, PlayerId against);

    /// Called each turn: decrements turnsRemaining, removes expired grievances.
    void tickGrievances();

    /// Sum of all grievance severities against a specific target player.
    [[nodiscard]] int32_t totalGrievanceAgainst(PlayerId target) const;

    /// Grievances arising from something a rival DID, excluding
    /// IdeologicalDifference.
    ///
    /// That one is accrued every turn for every pair of civs holding different
    /// late-game ideologies, and addGrievance refreshes its timer rather than
    /// appending, so it never expires while the ideologies differ. Counting it
    /// made a plain count of grievances read ">= 2" permanently from mid-game
    /// onward, which is why the combined-stress revolt gate -- documented as
    /// war-weariness AND grievances both being high -- had in practice
    /// collapsed to war-weariness alone. Disliking someone's government is not
    /// an injury.
    [[nodiscard]] std::size_t injuryGrievanceCount() const;
};

} // namespace aoc::sim

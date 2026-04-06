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

namespace aoc::sim {

enum class GrievanceType : uint8_t {
    SettledNearBorders,  ///< -10, decays over 30 turns
    BrokePromise,        ///< -20, decays over 50 turns
    DeclaredWarOnAlly,   ///< -30, permanent
    ConqueredCity,       ///< -15 per city, decays over 40 turns
    TradeEmbargo,        ///< -10, while active
};

struct Grievance {
    GrievanceType type;
    PlayerId      against;
    int32_t       severity;
    int32_t       turnsRemaining; ///< 0 = permanent
};

/// Per-player grievance tracker (ECS component).
struct PlayerGrievanceComponent {
    PlayerId owner;
    std::vector<Grievance> grievances;

    /// Add a grievance of the given type against the specified player.
    void addGrievance(GrievanceType type, PlayerId against);

    /// Called each turn: decrements turnsRemaining, removes expired grievances.
    void tickGrievances();

    /// Sum of all grievance severities against a specific target player.
    [[nodiscard]] int32_t totalGrievanceAgainst(PlayerId target) const;
};

} // namespace aoc::sim

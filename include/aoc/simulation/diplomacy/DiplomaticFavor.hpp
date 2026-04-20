#pragma once

/**
 * @file DiplomaticFavor.hpp
 * @brief Diplomatic Favor currency for World Congress voting and proposals.
 *
 * Diplomatic Favor is earned passively and spent on World Congress votes.
 * Sources:
 *   - Government type (Democracy: +4/turn, Merchant Republic: +3/turn)
 *   - Active alliances (+2/turn each)
 *   - City-state suzerainty (+2/turn each)
 *   - Governor promotions (PeaceKeeper: +10/turn, CarbonCredit: +5/turn)
 *
 * Spending:
 *   - First World Congress vote: free
 *   - Each additional vote: +10 Favor (3 extra = 60 Favor)
 *   - Proposing an emergency: 30 Favor
 *
 * High grievances against you reduce Favor generation by up to -10/turn.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::game { class Player; }

namespace aoc::sim {

/// Per-player diplomatic favor state.
struct PlayerDiplomaticFavorComponent {
    PlayerId owner = INVALID_PLAYER;
    int32_t  favor = 0;           ///< Current favor stockpile
    int32_t  favorPerTurn = 0;    ///< Computed each turn

    void addFavor(int32_t amount) { this->favor += amount; }
    bool spendFavor(int32_t amount) {
        if (this->favor < amount) { return false; }
        this->favor -= amount;
        return true;
    }
};

/// Compute per-turn diplomatic favor for a player.
///
/// Contributions:
///   - Government base: Democracy 4, Merchant Republic 3, Monarchy 2, others 1
///   - Active alliances (any type): +2 each
///   - City-state suzerainties: +2 each
///   - Grievances against you: -1 each, clamped to [-10, 0]
[[nodiscard]] int32_t computeDiplomaticFavor(const aoc::game::Player& player,
                                              int32_t allianceCount,
                                              int32_t suzeraintyCount,
                                              int32_t totalGrievancesAgainst);

} // namespace aoc::sim

#pragma once

/**
 * @file AllianceObligations.hpp
 * @brief Tracks and enforces alliance obligations when allies are attacked.
 *
 * When a player with defensive or military alliance partners is attacked,
 * each allied player receives an obligation to respond within 5 turns.
 * Valid responses: declare war on attacker, impose embargo, or transfer
 * 10%+ of treasury to the defender. Failure incurs reputation loss with
 * ALL alliance members.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <vector>

namespace aoc::game { class GameState; }

namespace aoc::sim {

class DiplomacyManager;

/// A pending obligation for one player to respond to an ally being attacked.
struct AllianceObligation {
    PlayerId defender;          ///< The ally who was attacked
    PlayerId attacker;          ///< Who attacked the ally
    PlayerId obligatedPlayer;   ///< Who needs to respond
    int32_t  turnsToRespond;    ///< Countdown (starts at 5)
    bool     fulfilled = false; ///< Whether obligation was met
};

/// Tracks pending alliance obligations across all players.
struct AllianceObligationTracker {
    std::vector<AllianceObligation> pendingObligations;

    /**
     * @brief Generate obligations when war is declared.
     *
     * Scans all players for defensive/military alliances with the target.
     * For each allied player, creates an AllianceObligation with 5-turn countdown.
     *
     * @param aggressor  The player who declared war.
     * @param target     The player who was attacked.
     * @param diplomacy  Diplomacy manager for alliance checks.
     */
    void onWarDeclared(PlayerId aggressor, PlayerId target,
                       const DiplomacyManager& diplomacy);

    /**
     * @brief Check if any pending obligations have been fulfilled.
     *
     * An obligation is fulfilled if the obligated player has:
     *   (a) declared war on the attacker, OR
     *   (b) set embargo on the attacker, OR
     *   (c) is at war with the attacker (joined via other means)
     */
    void checkFulfillment(const DiplomacyManager& diplomacy);

    /**
     * @brief Tick obligation timers and apply penalties for expired unfulfilled ones.
     *
     * Decrements turnsToRespond. When timer reaches 0 and not fulfilled:
     *   - -20 reputation with the defender
     *   - -10 reputation with all other alliance members
     *   - Adds FailedAllianceObligation grievance
     */
    void tickObligations(DiplomacyManager& diplomacy,
                         aoc::game::GameState& gameState);
};

} // namespace aoc::sim

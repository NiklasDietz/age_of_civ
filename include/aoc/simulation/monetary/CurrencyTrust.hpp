#pragma once

/**
 * @file CurrencyTrust.hpp
 * @brief Currency trust scoring for fiat money and reserve currency mechanics.
 *
 * When a player transitions to fiat money, trade partners don't automatically
 * trust their unbacked paper. Trust must be earned through:
 *   - Low inflation (monetary discipline)
 *   - Large GDP (economic weight makes your currency hard to ignore)
 *   - Low debt-to-GDP ratio (fiscal responsibility)
 *   - Military power (sovereign credibility)
 *   - Time on fiat without crises (track record)
 *
 * Trust determines the effective trade efficiency of fiat currency:
 *   - Trust < 0.3: severe penalty, partners demand gold settlement
 *   - Trust 0.3-0.6: moderate penalty, fiat accepted at discount
 *   - Trust 0.6-0.8: near full efficiency
 *   - Trust > 0.8: reserve currency candidate
 *
 * Reserve Currency:
 * The fiat currency with the highest trust score (above 0.8) becomes the
 * global reserve currency. Benefits:
 *   - Seigniorage: other civs holding your currency as reserves = free demand
 *   - Trade premium: +5% trade efficiency with all partners
 *   - Sanctions power: can cut off access to your currency system
 *   - Exorbitant privilege: can run larger deficits without trust loss
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::ecs { class World; }

namespace aoc::sim {

struct MonetaryStateComponent;

// ============================================================================
// Per-player trust score (stored as part of the monetary state or as separate component)
// ============================================================================

struct CurrencyTrustComponent {
    PlayerId owner = INVALID_PLAYER;

    float trustScore = 0.30f;        ///< 0.0 to 1.0. Starts low when transitioning to fiat.
    int32_t turnsOnFiat = 0;         ///< How many turns since fiat adoption
    int32_t turnsStable = 0;         ///< Consecutive turns with inflation < 5%
    bool isReserveCurrency = false;  ///< True if this is the global reserve currency
    int32_t turnsAsReserve = 0;      ///< How long held reserve status

    /// Per-player trust modifiers (bilateral). Players who traded with you
    /// longer trust you more. Indexed by PlayerId.
    static constexpr int32_t MAX_PLAYERS = 16;
    float bilateralTrust[MAX_PLAYERS] = {};
};

// ============================================================================
// Trust computation
// ============================================================================

/**
 * @brief Recompute the trust score for a fiat currency.
 *
 * Called once per turn for each player on fiat money.
 * Updates trustScore based on current economic indicators.
 *
 * @param world     ECS world for reading GDP rankings, military scores.
 * @param state     Player's monetary state.
 * @param trust     Player's trust component (will be mutated).
 * @param playerCount Total number of active players.
 */
void computeCurrencyTrust(const aoc::ecs::World& world,
                          const MonetaryStateComponent& state,
                          CurrencyTrustComponent& trust,
                          int32_t playerCount);

/**
 * @brief Determine the global reserve currency holder.
 *
 * Scans all fiat currencies and selects the one with the highest trust
 * score above 0.8. If no currency qualifies, there is no reserve currency.
 * If the current reserve holder drops below 0.7, they lose status (hysteresis).
 *
 * @param world  ECS world with MonetaryState and CurrencyTrust components.
 */
void updateReserveCurrencyStatus(aoc::ecs::World& world);

/**
 * @brief Get the effective fiat trade efficiency for a player.
 *
 * Combines the base fiat efficiency (1.0) with the trust penalty/bonus.
 * Reserve currency holders get an additional +5% bonus.
 *
 * @param trust  Player's trust component.
 * @return Trade efficiency multiplier (0.4 to 1.05).
 */
[[nodiscard]] float fiatTradeEfficiency(const CurrencyTrustComponent& trust);

/**
 * @brief Get the bilateral trade efficiency between two players.
 *
 * When trading between different monetary systems, the efficiency is
 * the minimum of both players' systems. When a fiat player trades with
 * a gold-standard player, trust affects the fiat side.
 *
 * @param world    ECS world.
 * @param playerA  First player.
 * @param playerB  Second player.
 * @return Combined trade efficiency multiplier.
 */
[[nodiscard]] float bilateralTradeEfficiency(const aoc::ecs::World& world,
                                             PlayerId playerA, PlayerId playerB);

} // namespace aoc::sim

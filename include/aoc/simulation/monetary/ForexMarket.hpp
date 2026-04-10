#pragma once

/**
 * @file ForexMarket.hpp
 * @brief Foreign exchange market: currency trading, exchange rates, and attacks.
 *
 * When multiple players reach fiat money, a forex market emerges where
 * currencies are traded against each other. Exchange rates are determined by:
 *
 *   1. Fundamentals: GDP, trade balance, inflation, interest rate, trust
 *   2. Market forces: active buy/sell orders from players and AI
 *   3. Central bank intervention: spending reserves to defend rates
 *
 * Strategic uses:
 *   - Strong currency: cheap imports, expensive exports (consumer economy)
 *   - Weak currency: expensive imports, cheap exports (export economy)
 *   - Currency attack: dump a rival's currency to crash their economy
 *   - Competitive devaluation: intentionally weaken your own currency
 *   - Reserve accumulation: buy foreign currencies as war chest
 *
 * Exchange rates are relative to a "gold unit" baseline (1.0 = parity).
 * A rate of 1.5 means your currency buys 50% more than baseline.
 * A rate of 0.7 means your currency buys 30% less than baseline.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"

#include <array>
#include <cstdint>

namespace aoc::ecs { class World; }

namespace aoc::sim {

struct MonetaryStateComponent;
struct CurrencyTrustComponent;

// ============================================================================
// Per-player exchange rate state (ECS component)
// ============================================================================

struct CurrencyExchangeComponent {
    PlayerId owner = INVALID_PLAYER;

    /// Exchange rate vs gold baseline. 1.0 = parity. Higher = stronger currency.
    float exchangeRate = 1.0f;

    /// Fundamental (equilibrium) rate based on economic indicators.
    /// The market rate drifts toward this over time.
    float fundamentalRate = 1.0f;

    /// Net buy pressure this turn (positive = buying, negative = selling).
    /// Resets each turn after rate update.
    float netOrderFlow = 0.0f;

    /// Foreign currency reserves held by the central bank for intervention.
    CurrencyAmount foreignReserves = 0;

    /// Trade balance this turn (exports - imports in gold-unit value).
    /// Positive = trade surplus (strengthens currency).
    CurrencyAmount tradeBalance = 0;

    /// Whether the central bank is actively defending the rate.
    bool isDefendingRate = false;

    /// Target rate the central bank is defending (if isDefendingRate).
    float defenseTarget = 1.0f;

    /// How much the central bank has spent defending this turn.
    CurrencyAmount defenseSpending = 0;

    // ========================================================================
    // Derived effects
    // ========================================================================

    /// Import cost modifier. Strong currency = cheaper imports.
    [[nodiscard]] float importCostModifier() const {
        if (this->exchangeRate <= 0.01f) { return 3.0f; }
        return 1.0f / this->exchangeRate;
    }

    /// Export revenue modifier. Weak currency = more competitive exports.
    [[nodiscard]] float exportRevenueModifier() const {
        return this->exchangeRate;
    }
};

// ============================================================================
// Forex market operations
// ============================================================================

/**
 * @brief Compute the fundamental exchange rate from economic indicators.
 *
 * Based on: GDP rank, interest rate differential, inflation differential,
 * trade balance, and currency trust. This is the "fair value" the market
 * rate gravitates toward.
 */
[[nodiscard]] float computeFundamentalRate(const MonetaryStateComponent& state,
                                            const CurrencyTrustComponent& trust,
                                            CurrencyAmount averageGDP);

/**
 * @brief Place a currency buy order (strengthens the target currency).
 *
 * The buyer spends gold from their treasury to buy units of the target currency.
 * This creates buy pressure that pushes the target's exchange rate up.
 *
 * @param world      ECS world.
 * @param buyer      Player placing the buy order.
 * @param target     Player whose currency is being bought.
 * @param goldAmount Amount of gold spent on the purchase.
 * @return Ok if successful.
 */
[[nodiscard]] ErrorCode buyCurrency(aoc::ecs::World& world,
                                     PlayerId buyer, PlayerId target,
                                     CurrencyAmount goldAmount);

/**
 * @brief Place a currency sell order (weakens the target currency).
 *
 * The seller dumps units of the target currency on the market.
 * This creates sell pressure that pushes the target's exchange rate down.
 * Used for currency attacks or to realize profits from prior purchases.
 *
 * @param world         ECS world.
 * @param seller        Player selling the currency.
 * @param target        Player whose currency is being sold.
 * @param currencyAmount Amount of target currency to sell.
 * @return Ok if successful.
 */
[[nodiscard]] ErrorCode sellCurrency(aoc::ecs::World& world,
                                      PlayerId seller, PlayerId target,
                                      CurrencyAmount currencyAmount);

/**
 * @brief Central bank intervention: defend exchange rate.
 *
 * The player's central bank spends foreign reserves to buy their own currency,
 * counteracting sell pressure. Expensive but prevents crashes.
 *
 * @param world    ECS world.
 * @param player   Player whose central bank intervenes.
 * @param amount   Amount of foreign reserves to spend on defense.
 * @return Ok if successful.
 */
[[nodiscard]] ErrorCode defendCurrency(aoc::ecs::World& world,
                                        PlayerId player,
                                        CurrencyAmount amount);

/**
 * @brief Update all exchange rates based on fundamentals and order flow.
 *
 * Called once per turn. For each fiat player:
 * 1. Compute fundamental rate from economic indicators.
 * 2. Apply net order flow (buy/sell pressure) as short-term shock.
 * 3. Drift market rate toward fundamental rate (20% per turn).
 * 4. Apply central bank intervention if active.
 * 5. Clamp rate to reasonable bounds (0.2 to 5.0).
 * 6. Reset order flow for next turn.
 */
void updateExchangeRates(aoc::ecs::World& world);

/**
 * @brief Get the bilateral exchange rate between two fiat players.
 *
 * Rate = playerA.exchangeRate / playerB.exchangeRate.
 * This tells you how many units of B's currency you get for 1 unit of A's.
 *
 * @return Exchange rate A:B. Value > 1.0 means A's currency is stronger.
 */
[[nodiscard]] float bilateralExchangeRate(const CurrencyExchangeComponent& playerA,
                                           const CurrencyExchangeComponent& playerB);

} // namespace aoc::sim

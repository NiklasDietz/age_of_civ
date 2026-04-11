#pragma once

/**
 * @file StockMarket.hpp
 * @brief Inter-player equity investment and stock market mechanics.
 *
 * Players can invest gold in other players' economies by buying "shares."
 * This creates financial interdependence:
 *
 *   - Investor earns dividends each turn (% of target's GDP growth)
 *   - Target gets immediate capital injection (boosts production)
 *   - War between investor and target destroys the investment
 *   - Heavy investment creates "too interconnected to fight" dynamics
 *   - Market crashes can cascade across connected economies
 *
 * The stock market unlocks with Banking tech and scales with monetary system:
 *   - Gold Standard: max 2 investments, 3% dividend rate
 *   - Fiat Money: max 5 investments, 5% dividend rate, margin trading
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"

#include <cstdint>
#include <vector>

namespace aoc::game { class GameState; }

namespace aoc::sim {

// ============================================================================
// Investment definition
// ============================================================================

struct EquityInvestment {
    PlayerId investor = INVALID_PLAYER;     ///< Who invested
    PlayerId target   = INVALID_PLAYER;     ///< Whose economy they invested in
    CurrencyAmount principalInvested = 0;   ///< Original investment amount
    CurrencyAmount currentValue = 0;        ///< Current market value (fluctuates with target GDP)
    CurrencyAmount totalDividends = 0;      ///< Cumulative dividends earned
    int32_t turnsHeld = 0;                  ///< How long the investment has been held
};

// ============================================================================
// Per-player portfolio (ECS component)
// ============================================================================

struct PlayerStockPortfolioComponent {
    PlayerId owner = INVALID_PLAYER;

    /// Investments this player has made in OTHER players' economies.
    std::vector<EquityInvestment> investments;

    /// Foreign investments IN this player's economy (capital inflows).
    std::vector<EquityInvestment> foreignInvestments;

    /// Total foreign capital invested in this economy.
    [[nodiscard]] CurrencyAmount totalForeignCapital() const {
        CurrencyAmount total = 0;
        for (const EquityInvestment& inv : this->foreignInvestments) {
            total += inv.currentValue;
        }
        return total;
    }

    /// Total value of outgoing investments.
    [[nodiscard]] CurrencyAmount totalInvestmentsAbroad() const {
        CurrencyAmount total = 0;
        for (const EquityInvestment& inv : this->investments) {
            total += inv.currentValue;
        }
        return total;
    }

    /// Production bonus from foreign capital inflows (FDI effect).
    /// More foreign investment = more capital = more productivity.
    [[nodiscard]] float foreignCapitalProductionBonus(CurrencyAmount gdp) const {
        if (gdp <= 0) { return 1.0f; }
        float capitalRatio = static_cast<float>(this->totalForeignCapital())
                           / static_cast<float>(gdp);
        // Up to +10% production from foreign capital
        float bonus = capitalRatio * 0.5f;
        return 1.0f + ((bonus > 0.10f) ? 0.10f : bonus);
    }
};

// ============================================================================
// Operations
// ============================================================================

/**
 * @brief Invest gold in another player's economy.
 *
 * Investor pays gold, target gets capital injection.
 * Returns shares that earn dividends from target's GDP growth.
 */
[[nodiscard]] ErrorCode investInEconomy(aoc::game::GameState& gameState,
                                         PlayerId investor, PlayerId target,
                                         CurrencyAmount amount);

/**
 * @brief Divest (sell shares) from another player's economy.
 *
 * Investor gets current value back. May be less than invested if
 * target's economy declined. Creates capital outflow for target.
 */
[[nodiscard]] ErrorCode divestFromEconomy(aoc::game::GameState& gameState,
                                           PlayerId investor, PlayerId target);

/**
 * @brief Process dividends and value updates for all investments.
 *
 * Called once per turn. Updates investment values based on target GDP
 * changes and pays dividends to investors.
 */
void processStockMarket(aoc::game::GameState& gameState);

/**
 * @brief Trigger market crash: all investments in a target lose 30-50% value.
 *
 * Caused by war declaration, sovereign default, or hyperinflation.
 * Cascades to investors who may also crash if heavily exposed.
 */
void triggerMarketCrash(aoc::game::GameState& gameState, PlayerId crashedPlayer);

} // namespace aoc::sim

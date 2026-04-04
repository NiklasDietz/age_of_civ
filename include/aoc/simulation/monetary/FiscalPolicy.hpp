#pragma once

/**
 * @file FiscalPolicy.hpp
 * @brief Government fiscal policy: taxation, spending, and debt management.
 *
 * The fiscal system determines how the government raises and spends money:
 *
 *   Revenue = taxRate * GDP
 *   Deficit = governmentSpending - Revenue
 *   Debt += Deficit  (if deficit > 0)
 *   Debt -= Surplus  (if deficit < 0)
 *
 * High taxes slow economic growth but generate revenue.
 * High spending stimulates growth but may cause deficits.
 * Debt accumulates interest based on the central bank rate.
 *
 * In Fiat systems, the government can monetize debt (print money to pay it),
 * but this directly feeds inflation.
 */

#include "aoc/simulation/monetary/MonetarySystem.hpp"

namespace aoc::sim {

/// Set the tax rate. Clamped to [0.0, 0.60].
void setTaxRate(MonetaryStateComponent& state, Percentage rate);

/// Set per-turn government spending.
void setGovernmentSpending(MonetaryStateComponent& state, CurrencyAmount amount);

/**
 * @brief Execute one turn of fiscal policy.
 *
 * Calculates tax revenue from GDP, applies spending, computes deficit,
 * accrues interest on existing debt.
 *
 * @param state Player's monetary state.
 * @param gdp   This turn's GDP (total production value).
 */
void executeFiscalPolicy(MonetaryStateComponent& state, CurrencyAmount gdp);

/**
 * @brief Monetize debt by printing money to cover it (Fiat only).
 *
 * Reduces government debt but increases money supply, directly
 * causing inflation. A last-resort tool.
 *
 * @param state  Player's monetary state.
 * @param amount Amount of debt to monetize.
 * @return Ok if successful, InvalidMonetaryTransition if not Fiat.
 */
[[nodiscard]] ErrorCode monetizeDebt(MonetaryStateComponent& state, CurrencyAmount amount);

/**
 * @brief Get the happiness modifier from taxation level.
 *
 * Low taxes: slight happiness bonus (citizens keep more income).
 * Moderate taxes: neutral.
 * High taxes (>30%): penalty.
 * Very high taxes (>50%): severe penalty.
 */
[[nodiscard]] float taxHappinessModifier(Percentage taxRate);

/**
 * @brief Get the GDP growth modifier from government spending.
 *
 * Spending stimulates growth (Keynesian multiplier effect).
 * But spending financed by debt has diminishing returns.
 *
 * @param state Player's monetary state.
 * @return Multiplier on GDP growth (1.0 = neutral, >1.0 = stimulus, <1.0 = drag).
 */
[[nodiscard]] float spendingGrowthModifier(const MonetaryStateComponent& state);

} // namespace aoc::sim

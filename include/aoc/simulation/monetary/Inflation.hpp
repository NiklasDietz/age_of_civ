#pragma once

/**
 * @file Inflation.hpp
 * @brief Inflation model based on the Fisher equation (quantity theory of money).
 *
 * Core equation: M * V = P * Y
 *   M = money supply
 *   V = velocity of money (how fast money circulates)
 *   P = price level
 *   Y = real output (GDP in real terms)
 *
 * Per-turn inflation calculation:
 *   inflationRate = (dM/M) + (dV/V) - (dY/Y)
 *
 * In plain terms: inflation rises when money supply grows faster than
 * real economic output. Velocity acts as a multiplier -- when people
 * spend money faster (confidence is high), it amplifies inflation.
 *
 * Different monetary systems have different inflation dynamics:
 *   - Barter: no inflation (no money to inflate)
 *   - Commodity money: inflation only from gold supply increase (mining)
 *   - Gold standard: limited inflation (money supply constrained by gold)
 *   - Fiat: full exposure to inflation from money printing
 */

#include "aoc/simulation/monetary/MonetarySystem.hpp"

namespace aoc::sim {

/**
 * @brief Compute inflation for this turn and update the monetary state.
 *
 * @param state       Player's monetary state (will be mutated with new inflation/priceLevel).
 * @param previousGDP Last turn's GDP.
 * @param currentGDP  This turn's GDP (total production value at base prices).
 * @param previousMoneySupply Last turn's money supply.
 */
void computeInflation(MonetaryStateComponent& state,
                      CurrencyAmount previousGDP,
                      CurrencyAmount currentGDP,
                      CurrencyAmount previousMoneySupply);

/**
 * @brief Apply inflation effects to the player's economy.
 *
 * Adjusts the cumulative price level, which affects:
 *   - Building/unit production costs
 *   - Trade prices
 *   - Government spending effectiveness
 *   - Citizen happiness (high inflation = unhappy)
 */
void applyInflationEffects(MonetaryStateComponent& state);

/**
 * @brief Get the happiness penalty from inflation.
 *
 * Mild inflation (0-3%) is tolerated. Above 5% citizens get unhappy.
 * Above 15% is crisis territory (large penalty).
 * Deflation (negative) is also penalized (unemployment).
 *
 * @return Amenity penalty (0.0 = no effect, positive = penalty).
 */
[[nodiscard]] float inflationHappinessPenalty(Percentage inflationRate);

} // namespace aoc::sim

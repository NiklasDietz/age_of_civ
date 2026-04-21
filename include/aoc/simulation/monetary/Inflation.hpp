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

// ============================================================================
// Price level effects on the real economy
// ============================================================================

/**
 * @brief Maintenance cost multiplier from cumulative price level.
 *
 * High inflation erodes the real value of fixed gold payments, but maintenance
 * workers demand higher wages. Maintenance costs scale with price level.
 *
 * @return Multiplier on gold maintenance costs (1.0 at base price level).
 */
[[nodiscard]] float priceLevelMaintenanceMultiplier(float priceLevel);

/**
 * @brief Production cost multiplier from inflation.
 *
 * High inflation increases the hammers needed for production (supply chain
 * disruption, workers demanding pay raises, material cost increases).
 * Mild inflation (2-3%) actually helps (greases the wheels).
 *
 * @return Multiplier on production costs (1.0 at base, <1.0 for mild inflation).
 */
[[nodiscard]] float inflationProductionModifier(float inflationRate);

// ============================================================================
// Banking and monetary system GDP effects
// ============================================================================

/**
 * @brief Banking system GDP multiplier.
 *
 * More advanced monetary systems amplify economic output through credit
 * creation, fractional reserve banking, and efficient capital allocation.
 *
 * @return Multiplier on GDP calculation (1.0 for Barter, up to 1.3 for Fiat).
 */
[[nodiscard]] float bankingGDPMultiplier(const MonetaryStateComponent& state);

/**
 * @brief Economic stability bonus for science and culture.
 *
 * A stable monetary system (low inflation, manageable debt) creates an
 * environment where scholarship and arts can flourish. Instability
 * disrupts academic institutions and cultural production.
 *
 * @return Multiplier on science/culture yields (0.8 to 1.15).
 */
[[nodiscard]] float economicStabilityMultiplier(const MonetaryStateComponent& state);

/**
 * @brief Seigniorage income for the turn.
 *
 * When a nation's currency is held as reserves by other nations (reserve
 * currency status), the issuer earns seigniorage -- effectively free
 * purchasing power from other nations holding their money.
 *
 * @param state             The reserve currency issuer's monetary state.
 * @param isReserveCurrency Whether this player holds reserve currency status.
 * @param totalForeignGDP   Sum of all other players' GDP.
 * @return Gold income from seigniorage this turn.
 */
[[nodiscard]] CurrencyAmount computeSeigniorage(const MonetaryStateComponent& state,
                                                 bool isReserveCurrency,
                                                 CurrencyAmount totalForeignGDP);

/**
 * @brief Real interest rate = nominal policy rate minus current inflation.
 *
 * Drives the tightening/loosening feedback that makes the central bank tool
 * actually bind. Positive real rates suppress discretionary consumption and
 * pull money into bond-like savings; negative real rates encourage spending.
 */
[[nodiscard]] inline float realInterestRate(const MonetaryStateComponent& state) {
    return state.interestRate - state.inflationRate;
}

/**
 * @brief Demand multiplier for discretionary goods from real interest rates.
 *
 * Applied to elastic categories (consumer goods, advanced consumer goods)
 * during `computePlayerNeeds`. Necessities (wheat, clothing) remain inelastic.
 *
 * High real rates (tight money): multiplier drops toward 0.7 as consumers
 * delay purchases and lenders pull back. Negative real rates (easy money):
 * multiplier rises toward 1.25 as borrowing is cheap and saving is penalized.
 */
[[nodiscard]] float realRateConsumptionMultiplier(float realRate);

} // namespace aoc::sim

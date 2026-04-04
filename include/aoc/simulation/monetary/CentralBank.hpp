#pragma once

/**
 * @file CentralBank.hpp
 * @brief Central bank monetary policy tools.
 *
 * Available in GoldStandard and FiatMoney systems. Controls:
 *   - Interest rate: higher rate slows borrowing, reduces velocity, fights inflation
 *     but also slows economic growth.
 *   - Reserve requirement: fraction of deposits banks must hold. Higher = less
 *     money creation through lending (lower money multiplier).
 *   - Money printing (Fiat only): directly increase money supply. Powerful stimulus
 *     but directly causes inflation.
 *   - Gold purchase/sale (Gold Standard): buy/sell gold to adjust reserves and
 *     money supply within the gold backing constraint.
 */

#include "aoc/simulation/monetary/MonetarySystem.hpp"

namespace aoc::sim {

/// Set the central bank interest rate. Clamped to [0.0, 0.25].
void setInterestRate(MonetaryStateComponent& state, Percentage rate);

/// Set the reserve requirement ratio. Clamped to [0.01, 0.50].
void setReserveRequirement(MonetaryStateComponent& state, Percentage ratio);

/**
 * @brief Print new money (Fiat only). Increases money supply directly.
 *
 * @param state  Player's monetary state.
 * @param amount Amount of new currency to create.
 * @return Ok if successful, InvalidMonetaryTransition if not in Fiat system.
 */
[[nodiscard]] ErrorCode printMoney(MonetaryStateComponent& state, CurrencyAmount amount);

/**
 * @brief Buy gold with currency (increase reserves, decrease money supply).
 *
 * Only meaningful in GoldStandard. In Fiat, gold is just a commodity.
 *
 * @param state  Player's monetary state.
 * @param goldAmount Amount of gold to purchase.
 * @param goldPrice  Current market price of gold.
 * @return Ok if successful, InsufficientResources if not enough currency.
 */
[[nodiscard]] ErrorCode buyGold(MonetaryStateComponent& state,
                                 CurrencyAmount goldAmount,
                                 CurrencyAmount goldPrice);

/**
 * @brief Sell gold for currency (decrease reserves, increase money supply).
 *
 * @param state  Player's monetary state.
 * @param goldAmount Amount of gold to sell.
 * @param goldPrice  Current market price of gold.
 * @return Ok if successful, InsufficientResources if not enough gold.
 */
[[nodiscard]] ErrorCode sellGold(MonetaryStateComponent& state,
                                  CurrencyAmount goldAmount,
                                  CurrencyAmount goldPrice);

/**
 * @brief Compute the money multiplier from reserve requirement.
 *
 * In fractional reserve banking: multiplier = 1 / reserveRequirement
 * This determines how much money banks can create through lending.
 * A 10% reserve requirement means banks can lend 10x their reserves.
 */
[[nodiscard]] float moneyMultiplier(const MonetaryStateComponent& state);

} // namespace aoc::sim

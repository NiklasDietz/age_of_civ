#pragma once

/**
 * @file CentralBank.hpp
 * @brief Central bank monetary policy tools.
 *
 * Available tools depend on the monetary system:
 *
 *   CommodityMoney:
 *     - Debasement: mix cheaper metals into coins to stretch supply.
 *       Short-term stimulus but destroys trust once discovered by partners.
 *
 *   GoldStandard:
 *     - Interest rate, reserve requirement, gold buy/sell.
 *
 *   FiatMoney:
 *     - Interest rate, reserve requirement, money printing.
 */

#include "aoc/simulation/monetary/MonetarySystem.hpp"

namespace aoc::game {
class Player;
}

namespace aoc::sim {

/// Set the central bank interest rate. Clamped to [0.0, 0.25].
void setInterestRate(MonetaryStateComponent& state, Percentage rate);

/// Set the reserve requirement ratio. Clamped to [0.01, 0.50].
void setReserveRequirement(MonetaryStateComponent& state, Percentage ratio);


/**
 * @brief Compute the money multiplier from reserve requirement.
 *
 * In fractional reserve banking: multiplier = 1 / reserveRequirement
 * This determines how much money banks can create through lending.
 */
[[nodiscard]] float moneyMultiplier(const MonetaryStateComponent& state);

/**
 * @brief Debase the currency (Commodity Money only).
 *
 * Mixes cheaper metals into coins, effectively creating new coins from
 * the same metal supply. This increases the money supply immediately
 * but degrades coin quality.
 *
 * @param state  Player's monetary state.
 * @param ratio  How much to debase: 0.1 = 10% base metal mixed in.
 *               Cumulative -- cannot exceed 0.5 (50%).
 * @return Ok if successful, InvalidMonetaryTransition if not CommodityMoney.
 *
 * Effects:
 * - Immediate: coin reserves increase by (ratio * currentReserves) for
 *   the highest-tier coin held. Free money.
 * - After ~5 turns: trade partners discover the debasement via coin
 *   weight/purity checks. Exchange rate penalty applied.
 * - Discovered debasement reduces trade efficiency by up to 25%.
 * - Other civs may refuse debased coins entirely above 40% debasement.
 */
[[nodiscard]] ErrorCode debaseCurrency(MonetaryStateComponent& state, float ratio);

/**
 * @brief Per-turn debasement discovery check.
 *
 * Each turn after debasement, there is a cumulative probability that
 * trade partners discover the coin has been debased. Once discovered,
 * the trade penalty is applied until the player transitions away from
 * Commodity Money (upgrading to Gold Standard resets trust via new paper).
 *
 * @param state  Player's monetary state.
 * @return true if debasement was newly discovered this turn.
 */
bool tickDebasementDiscovery(MonetaryStateComponent& state);

/**
 * @brief Remint currency -- partial escape from a stacked debasement penalty.
 *
 * Before this existed, hitting the 50% debasement cap was a one-way trip:
 * the civ was locked into a permanent trust penalty until it could transition
 * out of CommodityMoney (and many civs never get there). Remint burns 20%
 * of the treasury to melt bad coinage and re-strike at a lower mix ratio.
 *
 * Each call decreases debasementRatio by 0.10 (floor 0.0) and also clears
 * the `discoveredByPartners` flag so partners re-audit from scratch.
 *
 * @param state Player's monetary state.
 * @return Ok if successful, InvalidMonetaryTransition if not in CommodityMoney,
 *         InsufficientResources if treasury can't pay the 20% cost, or
 *         InvalidArgument if already at 0% debasement.
 */
[[nodiscard]] ErrorCode remintCurrency(aoc::game::Player& player);

} // namespace aoc::sim

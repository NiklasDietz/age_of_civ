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

/// Below this measured inflation a fiat bank issues against the money stock.
/// -0.02 sits inside the 3% band that costs no happiness (Inflation.cpp), so
/// the bank leans against a real slide, not noise.
inline constexpr float FIAT_DEFLATION_TRIGGER = -0.02f;

/// Share of a deflation the bank corrects in one turn. At 1.0 the 200-turn
/// seed 42 run swapped 30 deflating fiat turns for 26 above ten percent
/// inflation; half the slide a turn converges without the overshoot.
inline constexpr double FIAT_LEAN_GAIN = 0.5;

/**
 * @brief How much a fiat-class bank should issue this turn, before the cap.
 *
 * Two terms. Under deflation (inflationRate < FIAT_DEFLATION_TRIGGER) it
 * issues the larger of two amounts: the gap to half the money demand the
 * price anchor already assumes, priceAnchorK x population / 2, below which a
 * specie civ's prices would sit at the anchor floor; and FIAT_LEAN_GAIN of the
 * slide's own size, |inflationRate| x money, which lifts next turn's
 * money-growth term by that share. The first rescues a collapsed civ, the
 * second one whose economy has outgrown ample money. The bill term covers what
 * went unpaid last turn in full: arrears disband a unit after five turns, and
 * the old "half the shortfall" rule was austerity against a treasury that can
 * no longer go negative. Neither term is gated on an inflation ceiling: the
 * deflation term cannot fire above zero by construction, and refusing to cover
 * a bill in an inflation is how a state marches its army into desertion.
 * printMoney's share-of-GDP cap still bounds the total.
 *
 * Returns 0 for any regime without a printing press.
 */
[[nodiscard]] CurrencyAmount fiatIssueTarget(const MonetaryStateComponent& state,
                                             CurrencyAmount unpaidLastTurn, int32_t population);

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

#pragma once

/**
 * @file MonetaryActions.hpp
 * @brief Validated player actions on the monetary system.
 *
 * Debasement, reminting and competitive devaluation were fully implemented and
 * had ZERO callers. debaseCurrency and remintCurrency in CentralBank.cpp,
 * devalueCurrency in CurrencyWar.cpp -- all three written, none reachable. The
 * consequences cascaded: because nothing ever debased, debasementRatio stayed
 * at 0, so tickDebasementDiscovery (called every turn) could never return true,
 * and the Gresham's-law tier routing, the discovery curve, the reputational
 * trade penalty and the remint escape valve were all unreachable code. Because
 * nothing ever devalued, isDevalued was never true, so exportPriceMultiplier
 * always returned 1.0 and the race-to-bottom threshold could not be approached.
 *
 * These wrappers put them behind the same validated `requestX` layer the rest
 * of the game uses, so a human, the AI, the REST server and MCP all reach them
 * through one path with one set of rules -- the pattern that stops a capability
 * being reachable by one caller and not another, which is how the AI ended up
 * unable to mine mountains that a human could.
 */

#include "aoc/core/Types.hpp"
#include "aoc/simulation/monetary/CurrencyWar.hpp"

namespace aoc::game { class GameState; }

namespace aoc::sim {

/// Mix base metal into the coinage: more coins from the same bullion, at the
/// cost of a discovery risk and a trade-reputation penalty once found out.
///
/// `ratio` is the base-metal fraction added, clamped by the underlying call.
/// Refused unless the civ is on CommodityMoney -- there is nothing to debase in
/// a fiat system, which is what printing is for.
[[nodiscard]] ErrorCode requestDebaseCurrency(aoc::game::GameState& gameState, PlayerId player,
                                              float ratio);

/// Recall and restrike the coinage at full metal content: clears debasement and
/// its penalty, at the cost of the seigniorage already taken.
[[nodiscard]] ErrorCode requestRemintCurrency(aoc::game::GameState& gameState, PlayerId player);

/// Devalue against rivals to make exports cheaper. Fiat-class only.
///
/// Takes the global currency-war state explicitly: it lives on
/// EconomySimulation rather than GameState, and the race-to-bottom check needs
/// to see how many rivals have already devalued.
[[nodiscard]] ErrorCode requestDevalueCurrency(aoc::game::GameState& gameState, PlayerId player,
                                               GlobalCurrencyWarState& warState);

/// Set the central-bank policy rate. Refused for CommodityMoney and Barter,
/// which have no central bank; the value is clamped to [0, 0.25] inside
/// setInterestRate.
///
/// The rate was previously moved from exactly two places -- the hyperinflation
/// branch of the AI's crisis response, which slams it to 0.25, and a bond
/// default, which adds 0.05 -- so outside those it sat at its 0.05 default for
/// an entire game. Six systems read it: debt service in FiscalPolicy and
/// CurrencyCrisis, bond yields, the forex interest differential, speculative
/// bubble formation and popping, and (since taxableMoneyShare) the tax base.
[[nodiscard]] ErrorCode requestSetInterestRate(aoc::game::GameState& gameState, PlayerId player,
                                               float rate);

/// Choose and apply this turn's policy rate for one civ, balancing the
/// tradeoffs its consumers create. Returns the rate now in force.
///
/// Leaving inflation alone is not free and neither is fighting it: cheap money
/// widens the tax base and lightens debt service, but breeds bubbles (which
/// form only below 0.08) and feeds inflation. This is the decision nobody was
/// making.
///
/// DELIBERATELY NOT CALLED PER TURN YET, pending a balance decision that is the
/// project owner's rather than mine. Enabling it is one line at the top of
/// EconomySimulation::tickMonetaryMechanics, in the per-player loop:
///
///     applyCentralBankPolicy(gameState, playerPtr->id());
///
/// Measured with that line in, over 500 turns: seed 42 is unchanged to slightly
/// better (same winner and victory type, wars 15 -> 14, revolts and secessions
/// identical), while seed 43 keeps its winner but revolts go 611 -> 766,
/// secessions 396 -> 541 and wars 23 -> 31. One seed improves and one degrades,
/// which is exactly the call not to make silently. The mechanism is visible:
/// seed 43 runs hotter than the target, so its banks tighten, tightening slows
/// velocity, a slower velocity narrows the tax base (taxableMoneyShare), and
/// the lost revenue shows up as unrest.
float applyCentralBankPolicy(aoc::game::GameState& gameState, PlayerId player);

/// Issue new fiat money. Refused outside fiat-class systems; the amount is
/// capped inside printMoney at a share of GDP.
[[nodiscard]] ErrorCode requestPrintMoney(aoc::game::GameState& gameState, PlayerId player,
                                          CurrencyAmount amount);

} // namespace aoc::sim

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

/// Issue new fiat money. Refused outside fiat-class systems; the amount is
/// capped inside printMoney at a share of GDP.
[[nodiscard]] ErrorCode requestPrintMoney(aoc::game::GameState& gameState, PlayerId player,
                                          CurrencyAmount amount);

} // namespace aoc::sim

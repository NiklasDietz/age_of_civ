#pragma once

/**
 * @file CommodityExchange.hpp
 * @brief Bilateral commodity barter between players and AI-driven matching.
 *
 * Distinct from the open market: two players swap two different goods in a
 * single atomic trade. Goods are pulled from city stockpiles on each side
 * and deposited into the counterparty's first owned city.
 *
 * The AI matcher runs each turn and pairs up neutral-or-better civs whose
 * stockpile surpluses complement each other's shortages, gated by leader
 * `economicFocus` and relation modifiers (embargoes honored).
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"

#include <cstdint>

namespace aoc::game { class GameState; }

namespace aoc::sim {
class  Market;
class  DiplomacyManager;

/**
 * @brief Execute a bilateral commodity trade between two players.
 *
 * @param gameState      Game state container.
 * @param from           Initiating player.
 * @param to             Counterparty.
 * @param offerGood      Good flowing from -> to.
 * @param offerAmount    Units of offerGood (> 0).
 * @param requestGood    Good flowing to -> from.
 * @param requestAmount  Units of requestGood (> 0).
 * @return Ok on success. InvalidArgument or InsufficientResources on failure.
 */
[[nodiscard]] ErrorCode executeCommodityTrade(aoc::game::GameState& gameState,
                                              PlayerId from, PlayerId to,
                                              uint16_t offerGood, int32_t offerAmount,
                                              uint16_t requestGood, int32_t requestAmount);

/**
 * @brief Scan non-human AI pairs each turn and execute complementary trades.
 *
 * A "trade" requires:
 *   - both sides met, not at war, relation >= Neutral (no embargo on either good)
 *   - the initiator has surplus of goodA; counterparty is short on goodA
 *   - the counterparty has surplus of goodB; initiator is short on goodB
 *   - `economicFocus >= 1.0` on the initiator's leader
 *
 * Trade size is capped and roughly value-balanced against current market prices.
 */
void processAICommodityExchange(aoc::game::GameState& gameState,
                                 Market& market,
                                 DiplomacyManager* diplomacy);

} // namespace aoc::sim

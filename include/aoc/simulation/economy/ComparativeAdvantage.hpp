#pragma once

/**
 * @file ComparativeAdvantage.hpp
 * @brief Comparative advantage and opportunity cost calculations.
 *
 * Comparative advantage ensures that trade is mutually beneficial even
 * when one player can produce everything more efficiently (absolute advantage).
 * The key insight: even if Player A is better at producing BOTH iron and wheat,
 * it's still beneficial for A to specialize in the one where their advantage
 * is GREATEST and trade for the other.
 *
 * opportunityCost(player, good) = units of best-alternative-good forgone
 *                                  per unit of this good produced
 *
 * A player should export goods where their opportunity cost is LOWER than
 * the trading partner's, and import goods where their opportunity cost is HIGHER.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <vector>

namespace aoc::game {
class GameState;
}

namespace aoc::sim {

class Market;

/// Result of comparing two players' production capabilities.
struct TradeRecommendation {
    uint16_t goodId;
    float    opportunityCostA;  ///< Player A's opportunity cost for this good
    float    opportunityCostB;  ///< Player B's opportunity cost for this good
    bool     aShouldExport;     ///< true = A exports this to B, false = B exports to A
    float    tradeGain;         ///< Estimated mutual benefit (higher = stronger incentive)
};

/**
 * @brief Compute comparative advantage between two players.
 *
 * Analyzes each player's production capabilities (what goods they can produce
 * and at what rate) to find mutually beneficial trade opportunities.
 *
 * @param world  ECS world with city and stockpile components.
 * @param market Current market for price context.
 * @param playerA First player.
 * @param playerB Second player.
 * @return Sorted list of trade recommendations (highest gain first).
 */
[[nodiscard]] std::vector<TradeRecommendation> computeComparativeAdvantage(
    const aoc::game::GameState& gameState,
    const Market& market,
    PlayerId playerA,
    PlayerId playerB);

/**
 * @brief Compute the production rate of a good for a player.
 *
 * Sums the potential output of all the player's cities for the given good,
 * considering available resources, buildings, and tile yields.
 *
 * @return Units per turn the player could produce.
 */
[[nodiscard]] float playerProductionRate(
    const aoc::game::GameState& gameState,
    PlayerId player,
    uint16_t goodId);

} // namespace aoc::sim

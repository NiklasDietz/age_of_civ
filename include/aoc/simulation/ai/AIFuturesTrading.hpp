#pragma once

/**
 * @file AIFuturesTrading.hpp
 * @brief Gene-driven AI commodity futures trading.
 *
 * Each turn, every non-barbarian, non-eliminated AI player evaluates whether
 * to open a small futures position:
 *   - Buy futures on goods trading below 0.85x basePrice when the leader has
 *     positive speculationAppetite (bet on mean reversion upward).
 *   - Sell futures on goods trading above 1.20x basePrice when the leader has
 *     elevated riskTolerance (bet on mean reversion downward).
 *
 * Position sizing scales with speculationAppetite / riskTolerance and is
 * capped to a small fraction of the treasury to avoid runaway exposure.
 */

namespace aoc::game { class GameState; }

namespace aoc::sim {
class Market;

void processAIFuturesTrading(aoc::game::GameState& gameState, Market& market);

} // namespace aoc::sim

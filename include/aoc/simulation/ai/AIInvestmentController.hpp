#pragma once

/**
 * @file AIInvestmentController.hpp
 * @brief Gene-driven AI participation in the stock market.
 *
 * Decides, for every AI player, whether to invest in their own or a foreign
 * economy this turn. speculationAppetite sizes bets; riskTolerance gates
 * bubble-era participation.
 */

namespace aoc::game { class GameState; }

namespace aoc::sim::ai {

/// Evaluate and execute one round of stock-market decisions for all AI players.
/// Call once per turn after the stock-market update step.
void runAIInvestmentDecisions(aoc::game::GameState& gameState);

} // namespace aoc::sim::ai

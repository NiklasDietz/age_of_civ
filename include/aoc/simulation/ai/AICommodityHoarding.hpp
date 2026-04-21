#pragma once

/**
 * @file AICommodityHoarding.hpp
 * @brief Gene-driven AI commodity hoarding and release.
 *
 * Each turn, every non-barbarian, non-eliminated, non-human AI player
 * evaluates whether to pull surplus goods from its own city stockpiles
 * into a hoard (betting on future scarcity) or release a held position
 * after price appreciation.
 *
 * Buy/release thresholds scale with `speculationAppetite`; release is
 * profit-driven (current price vs average purchase price).
 */

namespace aoc::game { class GameState; }

namespace aoc::sim {
class Market;

void processAICommodityHoarding(aoc::game::GameState& gameState, Market& market);

} // namespace aoc::sim

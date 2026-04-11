#pragma once

/**
 * @file ProductionSystem.hpp
 * @brief City production queue processing.
 */

#include "aoc/core/Types.hpp"

namespace aoc::game {
class GameState;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

/**
 * @brief Process production queues for all cities of a player.
 */
void processProductionQueues(aoc::game::GameState& gameState,
                              const aoc::map::HexGrid& grid,
                              PlayerId player);

} // namespace aoc::sim

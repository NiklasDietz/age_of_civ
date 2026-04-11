#pragma once

/**
 * @file AISettlerController.hpp
 * @brief AI subsystem for settler management: city founding, location scoring,
 *        and settler movement decisions.
 */

#include "aoc/core/Types.hpp"
#include "aoc/ui/MainMenu.hpp"

namespace aoc::game {
class GameState;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim::ai {

/**
 * @brief Handles settler AI: evaluating city locations, moving settlers,
 *        and founding new cities when a suitable site is reached.
 */
class AISettlerController {
public:
    explicit AISettlerController(PlayerId player, aoc::ui::AIDifficulty difficulty);

    /**
     * @brief Process all settler units for this player.
     *
     * For each settler: score candidate locations in a radius, move toward
     * the best one, and found a city when arrived.
     *
     * @param world  ECS world with all game state.
     * @param grid   Hex grid for terrain queries and pathfinding.
     */
    void executeSettlerActions(aoc::game::GameState& gameState, aoc::map::HexGrid& grid);

private:
    PlayerId              m_player;
    aoc::ui::AIDifficulty m_difficulty;
};

} // namespace aoc::sim::ai

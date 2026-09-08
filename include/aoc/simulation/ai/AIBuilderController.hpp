#pragma once

/**
 * @file AIBuilderController.hpp
 * @brief AI subsystem for builder management: tile improvements and prospecting.
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
 * @brief Manages builder units: auto-improves tiles near owned cities,
 *        and prospects for hidden resources when no improvements are needed.
 */
class AIBuilderController {
public:
    explicit AIBuilderController(PlayerId player, aoc::ui::AIDifficulty difficulty);

    /**
     * @brief Process all builder units for this player.
     *
     * For each builder:
     *   1. If on an unimproved owned tile, build best improvement.
     *   2. Otherwise, find nearest unimproved owned tile near any city and move there.
     *   3. If no unimproved tiles exist, prospect for hidden resources.
     *
     * @param world  ECS world with all game state.
     * @param grid   Hex grid for terrain and improvement queries.
     */
    void manageBuildersAndImprovements(aoc::game::GameState& gameState, aoc::map::HexGrid& grid);

private:
    PlayerId              m_player;
    aoc::ui::AIDifficulty m_difficulty;
};

} // namespace aoc::sim::ai

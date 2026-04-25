#pragma once

/**
 * @file AIMilitaryController.hpp
 * @brief AI subsystem for military unit management: combat, defense, and patrol.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/ui/MainMenu.hpp"

namespace aoc::game {
class GameState;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {
class DiplomacyManager;
}

namespace aoc::sim::ai {

/**
 * @brief Handles military and scout unit AI: ranged/melee combat, city defense,
 *        enemy pursuit, border patrol, fortification, and scouting.
 */
class AIMilitaryController {
public:
    explicit AIMilitaryController(PlayerId player, aoc::ui::AIDifficulty difficulty);

    /**
     * @brief Process all military and scout units for this player.
     *
     * Military units: attack adjacent enemies, defend threatened cities,
     * pursue nearest enemies, patrol borders, and fortify on defensive terrain.
     * Scout units: explore toward unexplored territory.
     *
     * @param world  ECS world with all game state.
     * @param grid   Hex grid for terrain and pathfinding.
     * @param rng    Deterministic PRNG for random patrol movement.
     */
    void executeMilitaryActions(aoc::game::GameState& gameState,
                                aoc::map::HexGrid& grid,
                                aoc::Random& rng,
                                aoc::sim::DiplomacyManager* diplomacy = nullptr);

private:
    PlayerId              m_player;
    aoc::ui::AIDifficulty m_difficulty;

    // WP-D3: persistent war target. Set when AI commits to a campaign;
    // reset when target eliminated or peace declared. Without persistence
    // the AI re-picks weakest neighbour every turn and never sees a war
    // through to completion (capitals captured but not held; armies thin
    // out across rotating targets).
    PlayerId              m_currentWarTarget    = INVALID_PLAYER;
    int32_t               m_warCommitmentTurns  = 0;
};

} // namespace aoc::sim::ai

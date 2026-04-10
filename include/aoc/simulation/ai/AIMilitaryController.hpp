#pragma once

/**
 * @file AIMilitaryController.hpp
 * @brief AI subsystem for military unit management: combat, defense, and patrol.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/ui/MainMenu.hpp"

namespace aoc::ecs {
class World;
}

namespace aoc::map {
class HexGrid;
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
    void executeMilitaryActions(aoc::ecs::World& world,
                                aoc::map::HexGrid& grid,
                                aoc::Random& rng);

private:
    PlayerId              m_player;
    aoc::ui::AIDifficulty m_difficulty;
};

} // namespace aoc::sim::ai

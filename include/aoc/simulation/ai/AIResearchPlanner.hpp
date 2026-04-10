#pragma once

/**
 * @file AIResearchPlanner.hpp
 * @brief AI subsystem for technology and civic research selection.
 */

#include "aoc/core/Types.hpp"
#include "aoc/ui/MainMenu.hpp"

namespace aoc::ecs {
class World;
}

namespace aoc::sim::ai {

/**
 * @brief Selects tech and civic research priorities based on game state,
 *        leader personality, and threat assessment.
 *
 * Priority order for tech:
 *   1. Techs that unlock buildings the player lacks.
 *   2. Techs that unlock military units when threatened.
 *   3. Techs that unlock resources (economic growth).
 *   Tie-break: cheaper techs preferred.
 *
 * Civic research prioritizes governments and policies.
 */
class AIResearchPlanner {
public:
    explicit AIResearchPlanner(PlayerId player, aoc::ui::AIDifficulty difficulty);

    /**
     * @brief Select the next tech and civic research if none is active.
     *
     * @param world  ECS world containing player tech/civic components.
     */
    void selectResearch(aoc::ecs::World& world);

private:
    PlayerId              m_player;
    aoc::ui::AIDifficulty m_difficulty;
};

} // namespace aoc::sim::ai

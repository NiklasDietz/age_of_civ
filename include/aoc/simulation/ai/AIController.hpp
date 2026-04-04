#pragma once

/**
 * @file AIController.hpp
 * @brief Top-level AI decision maker for computer-controlled players.
 *
 * Each AI turn:
 *   1. Evaluate military situation (threats, opportunities).
 *   2. Evaluate economic situation (deficits, trade opportunities).
 *   3. Evaluate diplomatic options (war, peace, alliances).
 *   4. Score all possible actions using utility framework.
 *   5. Execute the highest-utility actions within resource constraints.
 */

#include "aoc/simulation/ai/UtilityScoring.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/core/Random.hpp"

#include <vector>

namespace aoc::ecs {
class World;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {
class DiplomacyManager;
class Market;
}

namespace aoc::sim::ai {

class AIController {
public:
    explicit AIController(PlayerId player);

    /**
     * @brief Execute one full AI turn: evaluate and act.
     *
     * @param world      ECS world with all game state.
     * @param grid       Hex grid for pathfinding and terrain.
     * @param diplomacy  Diplomacy manager for war/peace decisions.
     * @param market     Market for trade/economic decisions.
     * @param rng        Deterministic PRNG for AI randomness.
     */
    void executeTurn(aoc::ecs::World& world,
                     const aoc::map::HexGrid& grid,
                     DiplomacyManager& diplomacy,
                     const Market& market,
                     aoc::Random& rng);

    [[nodiscard]] PlayerId player() const { return this->m_player; }

private:
    /// Score all available actions and return sorted list (highest first).
    [[nodiscard]] std::vector<ScoredAction> evaluateActions(
        const aoc::ecs::World& world,
        const aoc::map::HexGrid& grid,
        const DiplomacyManager& diplomacy,
        const Market& market) const;

    void executeUnitActions(aoc::ecs::World& world,
                            const aoc::map::HexGrid& grid,
                            aoc::Random& rng);

    void executeCityActions(aoc::ecs::World& world,
                            const aoc::map::HexGrid& grid);

    void executeDiplomacyActions(aoc::ecs::World& world,
                                 DiplomacyManager& diplomacy,
                                 const Market& market);

    void selectResearch(aoc::ecs::World& world);

    PlayerId      m_player;
    UtilityWeights m_weights;
};

} // namespace aoc::sim::ai

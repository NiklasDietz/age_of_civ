#pragma once

/**
 * @file AIController.hpp
 * @brief Top-level AI decision maker for computer-controlled players.
 *
 * Orchestrates subsystem controllers for research, city production,
 * builders, military, settlers, diplomacy, economy, and government.
 * Each AI turn delegates to focused subsystems in priority order.
 */

#include "aoc/simulation/ai/AIResearchPlanner.hpp"
#include "aoc/simulation/ai/AISettlerController.hpp"
#include "aoc/simulation/ai/AIBuilderController.hpp"
#include "aoc/simulation/ai/AIMilitaryController.hpp"
#include "aoc/ui/MainMenu.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/core/Random.hpp"

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
    explicit AIController(PlayerId player,
                         aoc::ui::AIDifficulty difficulty = aoc::ui::AIDifficulty::Normal);

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
                     aoc::map::HexGrid& grid,
                     DiplomacyManager& diplomacy,
                     const Market& market,
                     aoc::Random& rng);

    [[nodiscard]] PlayerId player() const { return this->m_player; }

private:
    void executeCityActions(aoc::ecs::World& world,
                            aoc::map::HexGrid& grid);

    void executeDiplomacyActions(aoc::ecs::World& world,
                                 DiplomacyManager& diplomacy,
                                 const Market& market);

    /// Evaluate market prices and manage surplus/deficit goods for trade.
    void manageEconomy(aoc::ecs::World& world,
                       DiplomacyManager& diplomacy,
                       const Market& market);

    /// Assign idle Trader units to trade routes with other players' cities.
    void manageTradeRoutes(aoc::ecs::World& world, aoc::map::HexGrid& grid,
                            const Market& market, const DiplomacyManager& diplomacy);

    /// Manage monetary system: transition when ready, prioritize minting.
    void manageMonetarySystem(aoc::ecs::World& world,
                              const DiplomacyManager& diplomacy);

    void manageGovernment(aoc::ecs::World& world);

    PlayerId              m_player;
    aoc::ui::AIDifficulty m_difficulty;

    AIResearchPlanner     m_researchPlanner;
    AISettlerController   m_settlerController;
    AIBuilderController   m_builderController;
    AIMilitaryController  m_militaryController;
};

} // namespace aoc::sim::ai

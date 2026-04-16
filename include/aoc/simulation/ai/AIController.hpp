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

namespace aoc::game {
class GameState;
}

namespace aoc::map {
class HexGrid;
class FogOfWar;
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
    void executeTurn(aoc::game::GameState& gameState,
                     aoc::map::HexGrid& grid,
                     const aoc::map::FogOfWar* fogOfWar,
                     DiplomacyManager& diplomacy,
                     const Market& market,
                     aoc::Random& rng);

    [[nodiscard]] PlayerId player() const { return this->m_player; }

private:
    void executeCityActions(aoc::game::GameState& gameState,
                            aoc::map::HexGrid& grid);

    void executeDiplomacyActions(aoc::game::GameState& gameState,
                                 DiplomacyManager& diplomacy,
                                 const Market& market);

    /// Evaluate market prices and manage surplus/deficit goods for trade.
    void manageEconomy(aoc::game::GameState& gameState,
                       DiplomacyManager& diplomacy,
                       const Market& market);

    /// Assign idle Trader units to trade routes with other players' cities.
    void manageTradeRoutes(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                            const Market& market, const DiplomacyManager& diplomacy);

    /// Manage monetary system: transition when ready, prioritize minting.
    void manageMonetarySystem(aoc::game::GameState& gameState,
                              const DiplomacyManager& diplomacy);

    void manageGovernment(aoc::game::GameState& gameState);

    /// Consider purchasing units or buildings with gold (ROI-based).
    void considerPurchases(aoc::game::GameState& gameState);

    /// Scan owned tiles for strategic canal opportunities and build when profitable.
    void considerCanalBuilding(aoc::game::GameState& gameState,
                               aoc::map::HexGrid& grid,
                               const aoc::map::FogOfWar* fogOfWar);

    PlayerId              m_player;
    aoc::ui::AIDifficulty m_difficulty;

    AIResearchPlanner     m_researchPlanner;
    AISettlerController   m_settlerController;
    AIBuilderController   m_builderController;
    AIMilitaryController  m_militaryController;
};

} // namespace aoc::sim::ai

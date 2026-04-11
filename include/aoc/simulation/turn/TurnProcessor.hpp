#pragma once

/**
 * @file TurnProcessor.hpp
 * @brief Unified per-turn processing for all players.
 *
 * This is the SINGLE source of truth for what happens each turn.
 * Both the graphical game (Application.cpp) and the headless simulation
 * call the same function, ensuring identical game logic.
 *
 * Turn execution order:
 *   1. AI decisions (for each AI player)
 *   2. Economy simulation (harvest, produce, trade, market)
 *   3. Per-player processing:
 *      a. Maintenance costs (units + buildings)
 *      b. City connections
 *      c. Advanced economics (tariffs, banking, debt)
 *      d. War weariness
 *      e. Golden/Dark age effects
 *      f. City growth
 *      g. City happiness
 *      h. City loyalty
 *      i. Government processing (anarchy, actions)
 *      j. Communication speed
 *      k. Religion (faith, spread, bonuses)
 *      l. Science + research advancement
 *      m. Culture + civic advancement
 *      n. Production queues
 *      o. Border expansion
 *      p. Great people accumulation + recruitment
 *      q. City bombardment
 *      r. City-state bonuses
 *   4. Global processing:
 *      a. Barbarians
 *      b. Natural disasters
 *      c. World events
 *      d. Labor strikes
 *      e. Migration
 *      f. Insurance premiums
 *      g. Futures settlement
 *      h. River flooding
 *      i. Climate / CO2
 *      j. Victory tracking
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aoc::ecs { class World; }
namespace aoc::map { class HexGrid; }
namespace aoc::game { class GameState; }

namespace aoc::sim {

class EconomySimulation;
class DiplomacyManager;
class Market;
class BarbarianController;

namespace ai { class AIController; }

/**
 * @brief Context for turn processing -- all references needed by the turn loop.
 *
 * Created once, passed to processTurn() each turn.
 */
struct TurnContext {
    aoc::ecs::World* world = nullptr;
    aoc::map::HexGrid* grid = nullptr;
    EconomySimulation* economy = nullptr;
    DiplomacyManager* diplomacy = nullptr;
    BarbarianController* barbarians = nullptr;
    aoc::Random* rng = nullptr;

    /// New object model (Phase 3 migration). When non-null, turn processing
    /// reads/writes GameState objects and syncs results back to ECS.
    aoc::game::GameState* gameState = nullptr;

    /// All AI controllers (empty for headless sim that manages AI separately).
    std::vector<ai::AIController*> aiControllers;

    /// All player IDs (human + AI).
    std::vector<PlayerId> allPlayers;

    /// Human player ID (INVALID_PLAYER if headless).
    PlayerId humanPlayer = INVALID_PLAYER;

    /// Current turn number.
    TurnNumber currentTurn = 0;
};

/**
 * @brief Found a new city at the given location with all required components.
 *
 * This is the SINGLE function for city creation. Both human settlers,
 * AI settlers, and the headless sim's starting cities should call this.
 * Ensures all required ECS components are created.
 *
 * @param world     ECS world.
 * @param grid      Hex grid (for tile ownership).
 * @param owner     Player who owns the new city.
 * @param location  Hex coordinate for the city center.
 * @param name      City name.
 * @param isOriginalCapital  True if this is the player's original capital.
 * @param startingPop  Starting population (default 1, capitals get more).
 * @return Entity ID of the new city.
 */
EntityId foundCity(aoc::ecs::World& world,
                   aoc::map::HexGrid& grid,
                   PlayerId owner,
                   aoc::hex::AxialCoord location,
                   const std::string& name,
                   bool isOriginalCapital = false,
                   int32_t startingPop = 1);

/**
 * @brief Process one complete turn for all players.
 *
 * This is the main entry point called by both Application.cpp and
 * HeadlessSimulation.cpp. It runs ALL game systems for ALL players.
 *
 * @param ctx  Turn context with all world references.
 */
void processTurn(TurnContext& ctx);

/**
 * @brief Process per-player systems for a single player.
 *
 * Called internally by processTurn() for each player. Can also be
 * called independently for testing.
 */
void processPlayerTurn(TurnContext& ctx, PlayerId player);

/**
 * @brief Process global systems that aren't per-player.
 *
 * Climate, barbarians, disasters, etc.
 */
void processGlobalSystems(TurnContext& ctx);

/**
 * @brief Sync ECS component state into GameState objects after turn processing.
 *
 * During migration, the ECS functions are authoritative. After they run,
 * this copies key results (treasury, population, food surplus, tech progress)
 * from ECS components into the GameState Player/City/Unit objects so that
 * code reading from GameState sees up-to-date values.
 *
 * @param ctx  Turn context (must have both world and gameState set).
 */
void syncEcsToGameState(TurnContext& ctx);

/**
 * @brief Sync GameState object state back into ECS components.
 *
 * For subsystems that have been migrated to use GameState natively,
 * this copies their results back into ECS so rendering and unmigrated
 * systems still see correct data.
 *
 * @param ctx  Turn context (must have both world and gameState set).
 */
void syncGameStateToEcs(TurnContext& ctx);

} // namespace aoc::sim

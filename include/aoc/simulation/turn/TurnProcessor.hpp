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

namespace aoc::game { class GameState; }
namespace aoc::game { class City; }
namespace aoc::map { class HexGrid; class FogOfWar; }
namespace aoc::core { class DecisionLog; }

namespace aoc::sim {

class EconomySimulation;
class DiplomacyManager;
class Market;
class BarbarianController;
class TurnEventLog;
struct GlobalDealTracker;
struct AllianceObligationTracker;

namespace ai { class AIController; }

/**
 * @brief Context for turn processing -- all references needed by the turn loop.
 *
 * Created once, passed to processTurn() each turn.
 */
struct TurnContext {
    aoc::map::HexGrid* grid = nullptr;
    aoc::map::FogOfWar* fogOfWar = nullptr;  ///< Optional: null in headless mode
    EconomySimulation* economy = nullptr;
    DiplomacyManager* diplomacy = nullptr;
    BarbarianController* barbarians = nullptr;
    GlobalDealTracker* dealTracker = nullptr;
    AllianceObligationTracker* allianceTracker = nullptr;
    aoc::Random* rng = nullptr;

    /// GameState owns all game data.
    aoc::game::GameState* gameState = nullptr;

    /// All AI controllers (empty for headless sim that manages AI separately).
    std::vector<ai::AIController*> aiControllers;

    /// All player IDs (human + AI).
    std::vector<PlayerId> allPlayers;

    /// Human player ID (INVALID_PLAYER if headless).
    PlayerId humanPlayer = INVALID_PLAYER;

    /// Current turn number.
    TurnNumber currentTurn = 0;

    /// Mid-turn event log for ML training. Null if not recording.
    TurnEventLog* eventLog = nullptr;

    /// Per-decision structured binary log. Non-null = recording. Carries the
    /// why behind each AI choice (candidate scores, top alternatives, plus a
    /// per-turn per-player TurnSummary emitted at end-of-turn). Kept off by
    /// default for GA runs; wired on for diagnostic single-sim invocations
    /// via aoc_simulate --trace-file PATH.
    aoc::core::DecisionLog* decisionLog = nullptr;
};

/**
 * @brief Found a new city at the given location for the given player.
 *
 * This is the SINGLE function for city creation. Both human settlers,
 * AI settlers, and the headless sim's starting cities must call this.
 * Creates the City in the GameState object model, assigns initial worked
 * tiles based on yield scoring, places the CityCenter district, sets
 * starting loyalty, and claims adjacent hex tiles on the grid.
 *
 * @param gameState  Top-level game state (player must exist in it).
 * @param grid       Hex grid (for tile ownership and yield queries).
 * @param owner      Player who owns the new city.
 * @param location   Hex coordinate for the city center.
 * @param name       City name.
 * @param isOriginalCapital  True if this is the player's original capital.
 * @param startingPop  Starting population (default 1, capitals may receive more).
 * @return Reference to the newly created City owned by the player.
 */
aoc::game::City& foundCity(aoc::game::GameState& gameState,
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
 * @param turnContext  Turn context with all world references.
 */
void processTurn(TurnContext& turnContext);

/**
 * @brief Process per-player systems for a single player.
 *
 * Called internally by processTurn() for each player. Can also be
 * called independently for testing.
 */
void processPlayerTurn(TurnContext& turnContext, PlayerId player);

/**
 * @brief Process global systems that are not per-player.
 *
 * Covers climate, barbarians, natural disasters, trade routes, speculation
 * bubbles, labor strikes, migration, insurance premiums, futures settlement,
 * river flooding, world events, and victory tracking.
 */
void processGlobalSystems(TurnContext& turnContext);

} // namespace aoc::sim

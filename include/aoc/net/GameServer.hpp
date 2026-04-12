#pragma once

/**
 * @file GameServer.hpp
 * @brief Authoritative game server: owns all game state, processes turns.
 *
 * The GameServer is the single authority for game state. It:
 *   1. Receives commands from clients via ITransport
 *   2. Validates commands (prevents illegal moves)
 *   3. Stores valid commands
 *   4. When all players have ended their turn, processes the turn
 *   5. Generates per-player snapshots (respecting fog of war)
 *   6. Sends snapshots to clients via ITransport
 *
 * For single player: owned by Application, uses LocalTransport.
 * For multiplayer: runs as standalone process, uses NetworkTransport.
 */

#include "aoc/net/Transport.hpp"
#include "aoc/net/GameStateSnapshot.hpp"
#include "aoc/simulation/turn/TurnProcessor.hpp"
#include "aoc/simulation/turn/TurnManager.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/barbarian/BarbarianController.hpp"
#include "aoc/simulation/ai/AIController.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/MapGenerator.hpp"
#include "aoc/core/Random.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace aoc::net {

/// Configuration for starting a new game.
struct GameConfig {
    int32_t mapWidth = 60;
    int32_t mapHeight = 40;
    uint32_t seed = 42;
    aoc::map::MapType mapType = aoc::map::MapType::Realistic;
    int32_t humanPlayerCount = 1;
    int32_t aiPlayerCount = 3;
    int32_t maxTurns = 500;
    std::vector<uint8_t> civAssignments;  ///< CivId per player (human first, then AI)
};

class GameServer {
public:
    GameServer();
    ~GameServer();

    /// Set the transport layer (must be called before initialize).
    void setTransport(ITransport* transport) { this->m_transport = transport; }

    /// Initialize a new game with the given configuration.
    void initialize(const GameConfig& config);

    /**
     * @brief Tick the server. Call once per frame (or once per turn in headless).
     *
     * Processes incoming commands, checks if all players are ready,
     * and if so, executes the turn and broadcasts snapshots.
     *
     * @return true if a turn was processed this tick.
     */
    bool tick();

    /// Access the grid.
    [[nodiscard]] aoc::map::HexGrid& grid() { return this->m_grid; }
    [[nodiscard]] const aoc::map::HexGrid& grid() const { return this->m_grid; }

    /// Access the economy (for UI queries).
    [[nodiscard]] aoc::sim::EconomySimulation& economy() { return this->m_economy; }
    [[nodiscard]] const aoc::sim::EconomySimulation& economy() const { return this->m_economy; }

    /// Current turn number.
    [[nodiscard]] TurnNumber currentTurn() const { return this->m_turnCtx.currentTurn; }

    /// Check if the game is over.
    [[nodiscard]] bool isGameOver() const { return this->m_gameOver; }

private:
    /// Process a single command from a player.
    void executeCommand(PlayerId player, const GameCommand& command);

    /// Validate a command before execution.
    [[nodiscard]] bool validateCommand(PlayerId player, const GameCommand& command) const;

    /// Generate a snapshot for a specific player.
    [[nodiscard]] GameStateSnapshot generateSnapshot(PlayerId player) const;

    /// Broadcast snapshots to all human players.
    void broadcastSnapshots();

    // Game state (owned by server)
    aoc::game::GameState                  m_gameState;
    aoc::map::HexGrid                     m_grid;
    aoc::sim::EconomySimulation           m_economy;
    aoc::sim::DiplomacyManager            m_diplomacy;
    aoc::sim::BarbarianController         m_barbarians;
    aoc::sim::TurnManager                 m_turnManager;
    aoc::Random                           m_rng;

    // Turn processor context
    aoc::sim::TurnContext                  m_turnCtx;

    // AI
    std::vector<aoc::sim::ai::AIController> m_aiControllers;

    // Transport
    ITransport*                            m_transport = nullptr;

    // Player tracking
    std::vector<PlayerId>                  m_humanPlayers;
    std::vector<PlayerId>                  m_allPlayers;
    std::vector<bool>                      m_playerReady;

    // Pending commands (accumulated between turns)
    std::vector<std::pair<PlayerId, GameCommand>> m_pendingCommands;

    // Game state
    bool                                   m_gameOver = false;
    int32_t                                m_maxTurns = 500;
};

} // namespace aoc::net

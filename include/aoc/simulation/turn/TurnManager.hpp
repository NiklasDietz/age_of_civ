#pragma once

/**
 * @file TurnManager.hpp
 * @brief Turn lifecycle management: tracks turn number, player readiness,
 *        and orchestrates phase execution.
 */

#include "aoc/simulation/turn/TurnPhases.hpp"
#include "aoc/core/Types.hpp"

#include <array>
#include <cstdint>

namespace aoc::game {
class GameState;
}

namespace aoc::ecs {
class SystemScheduler;
}

namespace aoc::sim {

class DiplomacyManager;

/// Whether turns are simultaneous (all players act at once) or sequential.
enum class TurnMode : uint8_t {
    Simultaneous,  ///< All players act at once, turn executes when all submit
    Sequential,    ///< Players at war take turns one after another
};

class TurnManager {
public:
    TurnManager();

    /// Current turn number (starts at 1 after first end-turn).
    [[nodiscard]] TurnNumber currentTurn() const { return this->m_currentTurn; }

    /// Current phase within the turn.
    [[nodiscard]] TurnPhase currentPhase() const { return this->m_currentPhase; }

    /// Whether we are currently in the player input phase.
    [[nodiscard]] bool isPlayerInputPhase() const {
        return this->m_currentPhase == TurnPhase::PlayerInput;
    }

    /// Mark a player as having ended their turn.
    void submitEndTurn(PlayerId player);

    /// Check if all human players have ended their turn.
    [[nodiscard]] bool allPlayersReady() const;

    /// Set the number of human players (AI players auto-submit).
    void setPlayerCount(uint8_t humanPlayers, uint8_t aiPlayers);

    /**
     * @brief Advance one full turn: run all phases in order.
     *
     * Should be called when allPlayersReady() returns true.
     * Executes each phase sequentially via the system scheduler.
     */
    void executeTurn(aoc::game::GameState& gameState, aoc::ecs::SystemScheduler& scheduler);

    /// Reset readiness for a new input phase.
    void beginNewTurn();

    /// Restore turn number from a save file.
    void setTurnNumber(TurnNumber turn) { this->m_currentTurn = turn; }

    /// Restore turn phase from a save file.
    void setPhase(TurnPhase phase) { this->m_currentPhase = phase; }

    /// Set the turn mode (simultaneous vs. sequential).
    void setTurnMode(TurnMode mode);

    /// Get the current turn mode.
    [[nodiscard]] TurnMode turnMode() const { return this->m_turnMode; }

    /// In sequential mode during war, which player is currently acting.
    [[nodiscard]] PlayerId activePlayer() const { return this->m_activePlayer; }

    /// Advance to the next player in sequential mode.
    void advanceActivePlayer();

    /// Check if sequential mode should be active (any war exists).
    [[nodiscard]] bool shouldBeSequential(const DiplomacyManager& diplomacy) const;

    /// Total number of players (human + AI).
    [[nodiscard]] uint8_t totalPlayerCount() const {
        return static_cast<uint8_t>(this->m_humanPlayerCount + this->m_aiPlayerCount);
    }

private:
    TurnNumber m_currentTurn = 0;
    TurnPhase  m_currentPhase = TurnPhase::PlayerInput;

    uint8_t m_humanPlayerCount = 1;
    uint8_t m_aiPlayerCount    = 0;
    std::array<bool, MAX_PLAYERS> m_playerReady{};

    TurnMode m_turnMode = TurnMode::Simultaneous;

    /// In sequential mode during war, which player is currently acting.
    PlayerId m_activePlayer = 0;
};

} // namespace aoc::sim

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

namespace aoc::ecs {
class World;
class SystemScheduler;
}

namespace aoc::sim {

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
    void executeTurn(aoc::ecs::World& world, aoc::ecs::SystemScheduler& scheduler);

    /// Reset readiness for a new input phase.
    void beginNewTurn();

private:
    TurnNumber m_currentTurn = 0;
    TurnPhase  m_currentPhase = TurnPhase::PlayerInput;

    uint8_t m_humanPlayerCount = 1;
    uint8_t m_aiPlayerCount    = 0;
    std::array<bool, MAX_PLAYERS> m_playerReady{};
};

} // namespace aoc::sim

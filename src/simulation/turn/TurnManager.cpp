/**
 * @file TurnManager.cpp
 * @brief Turn lifecycle implementation.
 */

#include "aoc/simulation/turn/TurnManager.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/ecs/SystemScheduler.hpp"

#include <cstdio>

namespace aoc::sim {

TurnManager::TurnManager() {
    this->m_playerReady.fill(false);
}

void TurnManager::submitEndTurn(PlayerId player) {
    if (player < MAX_PLAYERS) {
        this->m_playerReady[player] = true;
    }
}

bool TurnManager::allPlayersReady() const {
    for (uint8_t i = 0; i < this->m_humanPlayerCount; ++i) {
        if (!this->m_playerReady[i]) {
            return false;
        }
    }
    return true;
}

void TurnManager::setPlayerCount(uint8_t humanPlayers, uint8_t aiPlayers) {
    this->m_humanPlayerCount = humanPlayers;
    this->m_aiPlayerCount    = aiPlayers;
}

void TurnManager::executeTurn(aoc::ecs::World& world, aoc::ecs::SystemScheduler& scheduler) {
    ++this->m_currentTurn;

    // Execute all registered systems in dependency order.
    // Systems register themselves for specific phases; the scheduler runs all of them.
    ErrorCode result = scheduler.executeTick(world);
    if (result != ErrorCode::Ok) {
        std::fprintf(stderr, "[TurnManager] %s:%d System execution failed: %.*s\n",
                     __FILE__, __LINE__,
                     static_cast<int>(describeError(result).size()),
                     describeError(result).data());
    }

    std::fprintf(stdout, "[TurnManager] Turn %u complete\n", this->m_currentTurn);
}

void TurnManager::beginNewTurn() {
    this->m_currentPhase = TurnPhase::PlayerInput;
    this->m_playerReady.fill(false);

    // AI players are auto-ready (they compute during AIDecisions phase)
    for (uint8_t i = this->m_humanPlayerCount;
         i < this->m_humanPlayerCount + this->m_aiPlayerCount; ++i) {
        this->m_playerReady[i] = true;
    }
}

} // namespace aoc::sim

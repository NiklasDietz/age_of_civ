/**
 * @file TurnManager.cpp
 * @brief Turn lifecycle implementation.
 */

#include "aoc/simulation/turn/TurnManager.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/ecs/SystemScheduler.hpp"

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
        LOG_ERROR("System execution failed: %.*s",
                  static_cast<int>(describeError(result).size()),
                  describeError(result).data());
    }

    LOG_INFO("Turn %u complete", this->m_currentTurn);
}

void TurnManager::beginNewTurn() {
    this->m_currentPhase = TurnPhase::PlayerInput;
    this->m_playerReady.fill(false);
    this->m_activePlayer = 0;

    // AI players are auto-ready (they compute during AIDecisions phase)
    for (uint8_t i = this->m_humanPlayerCount;
         i < this->m_humanPlayerCount + this->m_aiPlayerCount; ++i) {
        this->m_playerReady[i] = true;
    }
}

void TurnManager::setTurnMode(TurnMode mode) {
    this->m_turnMode = mode;
    LOG_INFO("Turn mode set to %s",
             mode == TurnMode::Simultaneous ? "Simultaneous" : "Sequential");
}

void TurnManager::advanceActivePlayer() {
    const uint8_t total = this->totalPlayerCount();
    if (total == 0) {
        return;
    }
    this->m_activePlayer = static_cast<PlayerId>(
        (static_cast<uint8_t>(this->m_activePlayer) + 1) % total);
    LOG_INFO("Sequential turn: active player is now %u",
             static_cast<unsigned>(this->m_activePlayer));
}

bool TurnManager::shouldBeSequential(const DiplomacyManager& diplomacy) const {
    if (this->m_turnMode != TurnMode::Sequential) {
        return false;
    }
    const uint8_t total = this->totalPlayerCount();
    for (uint8_t a = 0; a < total; ++a) {
        for (uint8_t b = static_cast<uint8_t>(a + 1); b < total; ++b) {
            if (diplomacy.isAtWar(a, b)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace aoc::sim

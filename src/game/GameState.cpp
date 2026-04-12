/**
 * @file GameState.cpp
 * @brief Top-level game state container implementation.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"

#include <cassert>

namespace aoc::game {

GameState::GameState() = default;
GameState::~GameState() = default;
GameState::GameState(GameState&&) noexcept = default;
GameState& GameState::operator=(GameState&&) noexcept = default;

void GameState::initialize(int32_t playerCount) {
    assert(playerCount > 0 && playerCount <= MAX_PLAYERS);
    this->m_players.clear();
    this->m_players.reserve(static_cast<std::size_t>(playerCount));
    this->m_currentTurn = 0;

    for (int32_t i = 0; i < playerCount; ++i) {
        this->m_players.push_back(
            std::make_unique<Player>(static_cast<PlayerId>(i)));
    }

    // Player 0 is always the human player
    this->m_players[0]->setHuman(true);
}

Player* GameState::player(PlayerId id) {
    if (id >= static_cast<PlayerId>(this->m_players.size())) {
        return nullptr;
    }
    return this->m_players[static_cast<std::size_t>(id)].get();
}

const Player* GameState::player(PlayerId id) const {
    if (id >= static_cast<PlayerId>(this->m_players.size())) {
        return nullptr;
    }
    return this->m_players[static_cast<std::size_t>(id)].get();
}

Player* GameState::humanPlayer() {
    return this->player(0);
}

const Player* GameState::humanPlayer() const {
    return this->player(0);
}

} // namespace aoc::game

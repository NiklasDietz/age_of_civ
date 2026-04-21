/**
 * @file GameState.cpp
 * @brief Top-level game state container implementation.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/citystate/CityState.hpp"

#include <algorithm>
#include <cassert>

namespace aoc::game {

GameState::GameState() = default;
GameState::~GameState() = default;
GameState::GameState(GameState&&) noexcept = default;
GameState& GameState::operator=(GameState&&) noexcept = default;

void GameState::initialize(int32_t playerCount) {
    assert(playerCount > 0 && playerCount <= MAX_PLAYERS);
    this->m_players.clear();
    this->m_cityStatePlayers.clear();
    this->m_players.reserve(static_cast<std::size_t>(playerCount));
    this->m_currentTurn = 0;

    for (int32_t i = 0; i < playerCount; ++i) {
        this->m_players.push_back(
            std::make_unique<Player>(static_cast<PlayerId>(i)));
    }

    // Player 0 is always the human player
    this->m_players[0]->setHuman(true);

    // Allocate a commodity hoard slot per player so speculation APIs can
    // find an owner-tagged entry without lazy-creating from the sim layer.
    this->m_commodityHoards.clear();
    this->m_commodityHoards.reserve(static_cast<std::size_t>(playerCount));
    for (int32_t i = 0; i < playerCount; ++i) {
        aoc::sim::CommodityHoardComponent h{};
        h.owner = static_cast<PlayerId>(i);
        this->m_commodityHoards.push_back(h);
    }
}

void GameState::initializeCityStateSlots(int32_t count) {
    const int32_t clamped =
        std::min(count, static_cast<int32_t>(aoc::sim::CITY_STATE_COUNT));
    this->m_cityStatePlayers.clear();
    this->m_cityStatePlayers.reserve(static_cast<std::size_t>(clamped));
    for (int32_t i = 0; i < clamped; ++i) {
        const PlayerId id =
            static_cast<PlayerId>(aoc::sim::CITY_STATE_PLAYER_BASE + i);
        this->m_cityStatePlayers.push_back(std::make_unique<Player>(id));
    }
}

Player* GameState::player(PlayerId id) {
    if (id >= aoc::sim::CITY_STATE_PLAYER_BASE) {
        const std::size_t idx =
            static_cast<std::size_t>(id - aoc::sim::CITY_STATE_PLAYER_BASE);
        if (idx >= this->m_cityStatePlayers.size()) { return nullptr; }
        return this->m_cityStatePlayers[idx].get();
    }
    if (id >= static_cast<PlayerId>(this->m_players.size())) {
        return nullptr;
    }
    return this->m_players[static_cast<std::size_t>(id)].get();
}

const Player* GameState::player(PlayerId id) const {
    if (id >= aoc::sim::CITY_STATE_PLAYER_BASE) {
        const std::size_t idx =
            static_cast<std::size_t>(id - aoc::sim::CITY_STATE_PLAYER_BASE);
        if (idx >= this->m_cityStatePlayers.size()) { return nullptr; }
        return this->m_cityStatePlayers[idx].get();
    }
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

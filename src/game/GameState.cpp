/**
 * @file GameState.cpp
 * @brief Top-level game state container implementation.
 */

#include "aoc/game/GameState.hpp"

#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/citystate/CityState.hpp"

#include <algorithm>
#include <limits>
#include <cassert>

namespace aoc::game {

GameState::GameState()                                = default;
GameState::~GameState()                               = default;
GameState::GameState(GameState&&) noexcept            = default;
GameState& GameState::operator=(GameState&&) noexcept = default;

void GameState::initialize(int32_t playerCount) {
    assert(playerCount > 0 && playerCount <= MAX_PLAYERS);
    this->m_players.clear();
    this->m_cityStatePlayers.clear();
    this->m_players.reserve(static_cast<std::size_t>(playerCount));
    this->m_currentTurn = 0;
    // A restarted game must not inherit the previous game's inbox or pacts.
    this->m_pendingProposals.clear();
    this->m_deals.activeDeals.clear();

    for (int32_t i = 0; i < playerCount; ++i) {
        this->m_players.push_back(std::make_unique<Player>(static_cast<PlayerId>(i)));
    }

    // Player 0 is always the human player
    this->m_players[0]->setHuman(true);

    // The barbarian seat exists in every game; BarbarianController spawns
    // camps and units into it from turn 15 (see BARBARIAN_PLAYER).
    this->m_barbarianPlayer = std::make_unique<Player>(BARBARIAN_PLAYER);

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
    const int32_t clamped = std::min(count, static_cast<int32_t>(aoc::sim::CITY_STATE_COUNT));
    this->m_cityStatePlayers.clear();
    this->m_cityStatePlayers.reserve(static_cast<std::size_t>(clamped));
    for (int32_t i = 0; i < clamped; ++i) {
        const PlayerId id = static_cast<PlayerId>(aoc::sim::CITY_STATE_PLAYER_BASE + i);
        this->m_cityStatePlayers.push_back(std::make_unique<Player>(id));
    }
}

Player* GameState::cityHolder(aoc::hex::AxialCoord at) {
    for (const std::unique_ptr<Player>& p : this->m_players) {
        if (p != nullptr && p->cityAt(at) != nullptr) {
            return p.get();
        }
    }
    for (const std::unique_ptr<Player>& p : this->m_cityStatePlayers) {
        if (p != nullptr && p->cityAt(at) != nullptr) {
            return p.get();
        }
    }
    if (this->m_barbarianPlayer != nullptr && this->m_barbarianPlayer->cityAt(at) != nullptr) {
        return this->m_barbarianPlayer.get();
    }
    return nullptr;
}

City* GameState::transferCity(aoc::hex::AxialCoord at, PlayerId newOwner) {
    Player* holder = this->cityHolder(at);
    if (holder == nullptr) {
        return nullptr;
    }
    City* city = holder->cityAt(at);
    if (city == nullptr) {
        return nullptr;
    }
    if (newOwner == holder->id()) {
        city->setOwner(newOwner);
        return city;
    }
    // Trade routes to or from this city are now void. A trader's destOwner is a
    // snapshot from when the route was established, but the arrival path reads
    // the city's live owner, so without this the losing side's trader kept
    // walking here and unloaded into the conqueror's stockpile. Every ownership
    // change -- conquest, loyalty revolt, secession, liberation -- comes through
    // this function, so it is the one place that needs to know.
    aoc::sim::cancelRoutesToCity(*this, at, newOwner);
    Player* destination = this->player(newOwner);
    if (destination == nullptr) {
        // A free city has no seat: leave the object where it is and record
        // only the change of owner. The save writes the holder separately.
        city->setOwner(newOwner);
        return city;
    }
    std::unique_ptr<City> moved = holder->releaseCity(city);
    if (moved == nullptr) {
        return nullptr;
    }
    moved->setOwner(newOwner);
    return &destination->adoptCity(std::move(moved));
}

Player* GameState::player(PlayerId id) {
    if (id == BARBARIAN_PLAYER) {
        return this->m_barbarianPlayer.get();
    }
    if (id >= aoc::sim::CITY_STATE_PLAYER_BASE) {
        const std::size_t idx = static_cast<std::size_t>(id - aoc::sim::CITY_STATE_PLAYER_BASE);
        if (idx >= this->m_cityStatePlayers.size()) {
            return nullptr;
        }
        return this->m_cityStatePlayers[idx].get();
    }
    if (id >= static_cast<PlayerId>(this->m_players.size())) {
        return nullptr;
    }
    return this->m_players[static_cast<std::size_t>(id)].get();
}

const Player* GameState::player(PlayerId id) const {
    if (id == BARBARIAN_PLAYER) {
        return this->m_barbarianPlayer.get();
    }
    if (id >= aoc::sim::CITY_STATE_PLAYER_BASE) {
        const std::size_t idx = static_cast<std::size_t>(id - aoc::sim::CITY_STATE_PLAYER_BASE);
        if (idx >= this->m_cityStatePlayers.size()) {
            return nullptr;
        }
        return this->m_cityStatePlayers[idx].get();
    }
    if (id >= static_cast<PlayerId>(this->m_players.size())) {
        return nullptr;
    }
    return this->m_players[static_cast<std::size_t>(id)].get();
}

Player* GameState::humanPlayer() {
    return this->player(this->m_humanPlayerId);
}

const Player* GameState::humanPlayer() const {
    return this->player(this->m_humanPlayerId);
}

void GameState::setHumanPlayerId(PlayerId id) {
    if (id == this->m_humanPlayerId) {
        return;
    }
    Player* prev = this->player(this->m_humanPlayerId);
    if (prev != nullptr) {
        prev->setHuman(false);
    }
    Player* next = this->player(id);
    if (next != nullptr) {
        next->setHuman(true);
    }
    this->m_humanPlayerId = id;
}

const City* GameState::nearestCity(const aoc::map::HexGrid& grid, hex::AxialCoord at,
                                   int32_t* distOut) const {
    const City* best = nullptr;
    int32_t bestDist = std::numeric_limits<int32_t>::max();
    auto scan = [&](const std::vector<std::unique_ptr<Player>>& seats) {
        for (const std::unique_ptr<Player>& seat : seats) {
            if (seat == nullptr) {
                continue;
            }
            for (const std::unique_ptr<City>& city : seat->cities()) {
                if (city == nullptr) {
                    continue;
                }
                const int32_t d = grid.distance(city->location(), at);
                if (d < bestDist) {
                    bestDist = d;
                    best     = city.get();
                }
            }
        }
    };
    scan(this->m_players);
    scan(this->m_cityStatePlayers);
    if (distOut != nullptr) {
        *distOut = bestDist;
    }
    return best;
}

City* GameState::nearestCity(const aoc::map::HexGrid& grid, hex::AxialCoord at, int32_t* distOut) {
    return const_cast<City*>(std::as_const(*this).nearestCity(grid, at, distOut));
}

} // namespace aoc::game

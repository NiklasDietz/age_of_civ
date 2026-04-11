#pragma once

/**
 * @file GameState.hpp
 * @brief Top-level game state container using object model instead of raw ECS.
 *
 * This is the new architecture replacing scattered ECS components with a
 * clean ownership hierarchy:
 *
 *   GameState
 *   ├── Players[MAX_PLAYERS]  (each player owns their data)
 *   │   ├── Tech, Civics, Economy, Government, Religion
 *   │   ├── Cities[] (production, citizens, buildings, loyalty)
 *   │   └── Units[] (movement, combat, automation)
 *   ├── Map (HexGrid - terrain, resources, improvements)
 *   ├── Market (global trade prices)
 *   ├── Climate (global temperature, CO2)
 *   └── Diplomacy (relations matrix)
 *
 * The ECS World is kept for backward compatibility during migration.
 * New code should use GameState directly. Old code will be migrated gradually.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace aoc::ecs { class World; }
namespace aoc::map { class HexGrid; }

namespace aoc::game {

// Forward declarations for the object model
class Player;
class City;
class Unit;

/**
 * @brief Top-level game state container.
 *
 * Owns all game data. Provides typed access to players, cities, units.
 * Replaces the pattern of querying the ECS World for scattered components.
 */
class GameState {
public:
    GameState();
    ~GameState();

    GameState(const GameState&) = delete;
    GameState& operator=(const GameState&) = delete;
    GameState(GameState&&) noexcept;
    GameState& operator=(GameState&&) noexcept;

    /// Initialize game state for a new game.
    void initialize(int32_t playerCount);

    /// Get a player by ID. Returns nullptr if invalid.
    [[nodiscard]] Player* player(PlayerId id);
    [[nodiscard]] const Player* player(PlayerId id) const;

    /// Get the human player (always player 0).
    [[nodiscard]] Player* humanPlayer();
    [[nodiscard]] const Player* humanPlayer() const;

    /// All active players.
    [[nodiscard]] const std::vector<std::unique_ptr<Player>>& players() const { return this->m_players; }

    /// Number of active players.
    [[nodiscard]] int32_t playerCount() const { return static_cast<int32_t>(this->m_players.size()); }

    /// Current turn number.
    [[nodiscard]] int32_t currentTurn() const { return this->m_currentTurn; }
    void advanceTurn() { ++this->m_currentTurn; }

    /// The legacy ECS World (for backward compatibility during migration).
    /// New code should NOT use this. Use player/city/unit accessors instead.
    [[nodiscard]] aoc::ecs::World& legacyWorld() { return *this->m_legacyWorld; }
    [[nodiscard]] const aoc::ecs::World& legacyWorld() const { return *this->m_legacyWorld; }

private:
    std::vector<std::unique_ptr<Player>> m_players;
    std::unique_ptr<aoc::ecs::World> m_legacyWorld;
    int32_t m_currentTurn = 0;
};

} // namespace aoc::game

#pragma once

/**
 * @file BarbarianController.hpp
 * @brief Barbarian encampment and unit AI controller.
 *
 * Manages spawning of barbarian encampments on unclaimed land,
 * spawning warrior units from encampments, and controlling barbarian
 * unit behavior (patrol and aggression toward nearby players).
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <vector>

namespace aoc::game {
class GameState;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

/// Data for a single barbarian encampment.
struct BarbarianEncampmentComponent {
    hex::AxialCoord location;          ///< Tile where the encampment sits.
    int32_t         spawnCooldown = 0; ///< Turns until the next unit spawns.
    int32_t         unitsSpawned  = 0; ///< Total number of units spawned from this camp.
};

/// Controls barbarian encampments and units each turn.
class BarbarianController {
public:
    /**
     * @brief Execute all barbarian logic for one turn.
     *
     * Spawns new encampments periodically, spawns warriors from existing
     * encampments, and moves/attacks with all barbarian-owned units.
     *
     * @param gameState  Full game state (players, units, cities).
     * @param grid       The hex grid with terrain data.
     * @param rng        Deterministic PRNG for spawn and movement decisions.
     */
    void executeTurn(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid, aoc::Random& rng);

    /// Read-only access to active encampments (used by serialisation and combat).
    [[nodiscard]] const std::vector<BarbarianEncampmentComponent>& encampments() const {
        return this->m_encampments;
    }

    /// Mutable access to active encampments (used by serialisation).
    [[nodiscard]] std::vector<BarbarianEncampmentComponent>& encampments() {
        return this->m_encampments;
    }

private:
    /// Attempt to place new encampments on unowned land far from cities.
    void spawnEncampments(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid, aoc::Random& rng);

    /// Spawn warrior units from existing encampments when their cooldown expires.
    void spawnUnitsFromEncampments(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid, aoc::Random& rng);

    /// Move barbarian units: patrol randomly or pursue nearby non-barbarian units.
    void moveBarbarianUnits(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid, aoc::Random& rng);

    /// Internal turn counter (incremented each call to executeTurn).
    int32_t m_turnCounter = 0;

    /// All currently active barbarian encampments, owned by this controller.
    std::vector<BarbarianEncampmentComponent> m_encampments;
};

} // namespace aoc::sim

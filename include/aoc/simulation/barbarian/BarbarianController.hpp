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

namespace aoc::ecs {
class World;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

/// ECS component attached to barbarian encampment entities.
struct BarbarianEncampmentComponent {
    hex::AxialCoord location;         ///< Tile where the encampment sits.
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
     * @param world  ECS world containing all entities and components.
     * @param grid   The hex grid with terrain data.
     * @param rng    Deterministic PRNG for spawn and movement decisions.
     */
    void executeTurn(aoc::ecs::World& world, const aoc::map::HexGrid& grid, aoc::Random& rng);

private:
    /// Attempt to place new encampments on unowned land far from cities.
    void spawnEncampments(aoc::ecs::World& world, const aoc::map::HexGrid& grid, aoc::Random& rng);

    /// Spawn warrior units from existing encampments when their cooldown expires.
    void spawnUnitsFromEncampments(aoc::ecs::World& world, const aoc::map::HexGrid& grid, aoc::Random& rng);

    /// Move barbarian units: patrol randomly or pursue nearby non-barbarian units.
    void moveBarbarianUnits(aoc::ecs::World& world, const aoc::map::HexGrid& grid, aoc::Random& rng);

    /// Internal turn counter (incremented each call to executeTurn).
    int32_t m_turnCounter = 0;
};

} // namespace aoc::sim

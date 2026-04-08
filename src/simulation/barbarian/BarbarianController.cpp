/**
 * @file BarbarianController.cpp
 * @brief Barbarian encampment spawning, unit spawning, and AI movement.
 */

#include "aoc/simulation/barbarian/BarbarianController.hpp"
#include "aoc/simulation/barbarian/BarbarianClans.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/Combat.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <cassert>
#include <vector>

namespace aoc::sim {

// ============================================================================
// Constants
// ============================================================================

static constexpr int32_t ENCAMPMENT_SPAWN_INTERVAL = 15;
static constexpr int32_t MAX_ENCAMPMENTS           = 3;
static constexpr int32_t MIN_DISTANCE_FROM_CITY    = 7;
static constexpr int32_t SPAWN_COOLDOWN_TURNS      = 3;
static constexpr int32_t MAX_NEARBY_BARBARIAN_UNITS = 3;
static constexpr int32_t AGGRO_RANGE               = 3;

// ============================================================================
// Helpers
// ============================================================================

/// Count barbarian units within a given radius of a position.
static int32_t countBarbarianUnitsNear(const aoc::ecs::World& world,
                                        hex::AxialCoord center,
                                        int32_t radius) {
    const aoc::ecs::ComponentPool<UnitComponent>* pool = world.getPool<UnitComponent>();
    if (pool == nullptr) {
        return 0;
    }

    int32_t count = 0;
    for (uint32_t i = 0; i < pool->size(); ++i) {
        const UnitComponent& unit = pool->data()[i];
        if (unit.owner != BARBARIAN_PLAYER) {
            continue;
        }
        if (hex::distance(unit.position, center) <= radius) {
            ++count;
        }
    }
    return count;
}

/// Find the closest non-barbarian unit within a given range.
static EntityId findNearestTarget(const aoc::ecs::World& world,
                                   hex::AxialCoord position,
                                   int32_t range) {
    const aoc::ecs::ComponentPool<UnitComponent>* pool = world.getPool<UnitComponent>();
    if (pool == nullptr) {
        return NULL_ENTITY;
    }

    EntityId closest = NULL_ENTITY;
    int32_t bestDist = range + 1;

    for (uint32_t i = 0; i < pool->size(); ++i) {
        const UnitComponent& unit = pool->data()[i];
        if (unit.owner == BARBARIAN_PLAYER) {
            continue;
        }
        int32_t dist = hex::distance(unit.position, position);
        if (dist <= range && dist < bestDist) {
            bestDist = dist;
            closest = pool->entities()[i];
        }
    }
    return closest;
}

/// Check if any city from any player is within the given distance of a tile.
static bool isTooCloseToCity(const aoc::ecs::World& world,
                              hex::AxialCoord tile,
                              int32_t minDistance) {
    const aoc::ecs::ComponentPool<CityComponent>* pool = world.getPool<CityComponent>();
    if (pool == nullptr) {
        return false;
    }

    for (uint32_t i = 0; i < pool->size(); ++i) {
        if (hex::distance(pool->data()[i].location, tile) < minDistance) {
            return true;
        }
    }
    return false;
}

/// Count existing barbarian encampments.
static int32_t countEncampments(const aoc::ecs::World& world) {
    const aoc::ecs::ComponentPool<BarbarianEncampmentComponent>* pool =
        world.getPool<BarbarianEncampmentComponent>();
    if (pool == nullptr) {
        return 0;
    }
    return static_cast<int32_t>(pool->size());
}

// ============================================================================
// BarbarianController
// ============================================================================

void BarbarianController::executeTurn(aoc::ecs::World& world,
                                       const aoc::map::HexGrid& grid,
                                       aoc::Random& rng) {
    ++this->m_turnCounter;

    // Refresh movement for all barbarian units
    refreshMovement(world, BARBARIAN_PLAYER);

    this->spawnEncampments(world, grid, rng);
    this->spawnUnitsFromEncampments(world, grid, rng);
    this->moveBarbarianUnits(world, grid, rng);
}

void BarbarianController::spawnEncampments(aoc::ecs::World& world,
                                            const aoc::map::HexGrid& grid,
                                            aoc::Random& rng) {
    if (this->m_turnCounter % ENCAMPMENT_SPAWN_INTERVAL != 0) {
        return;
    }

    if (countEncampments(world) >= MAX_ENCAMPMENTS) {
        return;
    }

    // Try several random positions to find a valid encampment site
    constexpr int32_t MAX_ATTEMPTS = 50;
    for (int32_t attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
        int32_t col = rng.nextInt(0, grid.width() - 1);
        int32_t row = rng.nextInt(0, grid.height() - 1);
        hex::AxialCoord candidate = hex::offsetToAxial({col, row});
        int32_t index = grid.toIndex(candidate);

        // Must be passable land
        aoc::map::TerrainType terrain = grid.terrain(index);
        if (aoc::map::isWater(terrain) || aoc::map::isImpassable(terrain)) {
            continue;
        }

        // Must be unowned
        if (grid.owner(index) != INVALID_PLAYER) {
            continue;
        }

        // Must be far from any city
        if (isTooCloseToCity(world, candidate, MIN_DISTANCE_FROM_CITY)) {
            continue;
        }

        // Place the encampment
        EntityId campEntity = world.createEntity();
        BarbarianEncampmentComponent camp{};
        camp.location = candidate;
        camp.spawnCooldown = 0;
        camp.unitsSpawned = 0;
        world.addComponent<BarbarianEncampmentComponent>(campEntity, std::move(camp));

        // Also spawn an initial warrior at the encampment
        EntityId unitEntity = world.createEntity();
        UnitComponent warrior = UnitComponent::create(BARBARIAN_PLAYER, barbarianSpawnUnit(this->m_turnCounter), candidate);
        world.addComponent<UnitComponent>(unitEntity, std::move(warrior));

        LOG_INFO("Barbarian encampment spawned at (%d,%d)", candidate.q, candidate.r);
        return;
    }
}

void BarbarianController::spawnUnitsFromEncampments(aoc::ecs::World& world,
                                                     const aoc::map::HexGrid& grid,
                                                     aoc::Random& rng) {
    (void)grid;
    (void)rng;

    aoc::ecs::ComponentPool<BarbarianEncampmentComponent>* pool =
        world.getPool<BarbarianEncampmentComponent>();
    if (pool == nullptr) {
        return;
    }

    // Collect encampment entities first to avoid iterator invalidation
    std::vector<EntityId> campEntities;
    campEntities.reserve(pool->size());
    for (uint32_t i = 0; i < pool->size(); ++i) {
        campEntities.push_back(pool->entities()[i]);
    }

    for (EntityId campEntity : campEntities) {
        if (!world.isAlive(campEntity)) {
            continue;
        }
        BarbarianEncampmentComponent& camp =
            world.getComponent<BarbarianEncampmentComponent>(campEntity);

        if (camp.spawnCooldown > 0) {
            --camp.spawnCooldown;
            continue;
        }

        // Check if there are already enough barbarian units near this camp
        if (countBarbarianUnitsNear(world, camp.location, 4) >= MAX_NEARBY_BARBARIAN_UNITS) {
            continue;
        }

        // Spawn a warrior at the encampment location
        EntityId unitEntity = world.createEntity();
        UnitComponent warrior = UnitComponent::create(BARBARIAN_PLAYER, barbarianSpawnUnit(this->m_turnCounter), camp.location);
        world.addComponent<UnitComponent>(unitEntity, std::move(warrior));

        camp.spawnCooldown = SPAWN_COOLDOWN_TURNS;
        ++camp.unitsSpawned;

        LOG_INFO("Barbarian warrior spawned at encampment (%d,%d)",
                 camp.location.q, camp.location.r);
    }
}

void BarbarianController::moveBarbarianUnits(aoc::ecs::World& world,
                                              const aoc::map::HexGrid& grid,
                                              aoc::Random& rng) {
    aoc::ecs::ComponentPool<UnitComponent>* pool = world.getPool<UnitComponent>();
    if (pool == nullptr) {
        return;
    }

    // Collect barbarian unit entities
    std::vector<EntityId> barbarianUnits;
    for (uint32_t i = 0; i < pool->size(); ++i) {
        if (pool->data()[i].owner == BARBARIAN_PLAYER) {
            barbarianUnits.push_back(pool->entities()[i]);
        }
    }

    for (EntityId unitEntity : barbarianUnits) {
        if (!world.isAlive(unitEntity)) {
            continue;
        }

        UnitComponent& unit = world.getComponent<UnitComponent>(unitEntity);
        if (unit.movementRemaining <= 0) {
            continue;
        }

        // Look for a nearby non-barbarian unit to attack
        EntityId target = findNearestTarget(world, unit.position, AGGRO_RANGE);

        if (target.isValid() && world.isAlive(target)) {
            const UnitComponent& targetUnit = world.getComponent<UnitComponent>(target);
            int32_t dist = hex::distance(unit.position, targetUnit.position);

            if (dist == 1) {
                // Adjacent: attack!
                resolveMeleeCombat(world, rng, grid, unitEntity, target);

                // After combat, check if attacker moved onto encampment tile
                // (not applicable for barbarians attacking, but defensive kills
                // are handled in Combat.cpp)
                continue;
            }

            // Move toward the target
            const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(unit.position);
            hex::AxialCoord bestMove = unit.position;
            int32_t bestDist = dist;

            for (const hex::AxialCoord& nbr : nbrs) {
                if (!grid.isValid(nbr)) {
                    continue;
                }
                int32_t cost = grid.movementCost(grid.toIndex(nbr));
                if (cost == 0) {
                    continue;
                }
                int32_t nbrDist = hex::distance(nbr, targetUnit.position);
                if (nbrDist < bestDist) {
                    bestDist = nbrDist;
                    bestMove = nbr;
                }
            }

            if (!(bestMove == unit.position)) {
                int32_t moveCost = grid.movementCost(grid.toIndex(bestMove));
                if (unit.movementRemaining >= moveCost) {
                    unit.position = bestMove;
                    unit.movementRemaining -= moveCost;
                }
            }
        } else {
            // Random patrol: pick a random passable neighbor
            const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(unit.position);
            std::vector<hex::AxialCoord> passableNeighbors;
            for (const hex::AxialCoord& nbr : nbrs) {
                if (!grid.isValid(nbr)) {
                    continue;
                }
                int32_t cost = grid.movementCost(grid.toIndex(nbr));
                if (cost > 0 && unit.movementRemaining >= cost) {
                    passableNeighbors.push_back(nbr);
                }
            }

            if (!passableNeighbors.empty()) {
                int32_t idx = rng.nextInt(0, static_cast<int32_t>(passableNeighbors.size()) - 1);
                hex::AxialCoord chosen = passableNeighbors[static_cast<std::size_t>(idx)];
                int32_t moveCost = grid.movementCost(grid.toIndex(chosen));
                unit.position = chosen;
                unit.movementRemaining -= moveCost;
            }
        }
    }
}

} // namespace aoc::sim

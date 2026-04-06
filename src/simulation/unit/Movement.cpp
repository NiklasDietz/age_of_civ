/**
 * @file Movement.cpp
 * @brief Unit movement implementation with stacking rules and zone of control.
 */

#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/map/Pathfinding.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::sim {

// ============================================================================
// Stacking helpers
// ============================================================================

/**
 * @brief Check whether a unit may legally occupy a tile given stacking rules.
 *
 * Only one military unit and one civilian/settler/scout unit of the same
 * player may occupy the same tile simultaneously.
 */
static bool canOccupyTile(const aoc::ecs::World& world,
                           hex::AxialCoord tile,
                           PlayerId owner,
                           bool isMilitaryUnit) {
    const aoc::ecs::ComponentPool<UnitComponent>* pool = world.getPool<UnitComponent>();
    if (pool == nullptr) {
        return true;
    }

    for (uint32_t i = 0; i < pool->size(); ++i) {
        const UnitComponent& other = pool->data()[i];
        if (other.owner != owner) {
            continue;
        }
        if (!(other.position == tile)) {
            continue;
        }
        const UnitTypeDef& otherDef = unitTypeDef(other.typeId);
        const bool otherIsMilitary = isMilitary(otherDef.unitClass);
        if (otherIsMilitary == isMilitaryUnit) {
            return false;  // Same classification already present
        }
    }
    return true;
}

// ============================================================================
// Zone of control helpers
// ============================================================================

/**
 * @brief Check if a tile is in an enemy military unit's zone of control.
 *
 * A tile is in enemy ZoC if any of its 6 hex neighbors contains an enemy
 * military unit belonging to a different player.
 */
static bool isInEnemyZoneOfControl(const aoc::ecs::World& world,
                                    hex::AxialCoord tile,
                                    PlayerId movingPlayer) {
    const aoc::ecs::ComponentPool<UnitComponent>* pool = world.getPool<UnitComponent>();
    if (pool == nullptr) {
        return false;
    }

    const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(tile);

    for (uint32_t i = 0; i < pool->size(); ++i) {
        const UnitComponent& other = pool->data()[i];
        if (other.owner == movingPlayer) {
            continue;
        }
        const UnitTypeDef& otherDef = unitTypeDef(other.typeId);
        if (!isMilitary(otherDef.unitClass)) {
            continue;
        }
        for (const hex::AxialCoord& nbr : nbrs) {
            if (other.position == nbr) {
                return true;
            }
        }
    }
    return false;
}

// ============================================================================
// Movement
// ============================================================================

bool moveUnitAlongPath(aoc::ecs::World& world, EntityId unitEntity,
                        const aoc::map::HexGrid& grid) {
    UnitComponent& unit = world.getComponent<UnitComponent>(unitEntity);

    if (unit.pendingPath.empty()) {
        return false;
    }

    const UnitTypeDef& movingDef = unitTypeDef(unit.typeId);
    const bool unitIsMilitary = isMilitary(movingDef.unitClass);

    bool moved = false;
    bool prevInZoC = isInEnemyZoneOfControl(world, unit.position, unit.owner);

    while (!unit.pendingPath.empty() && unit.movementRemaining > 0) {
        hex::AxialCoord nextTile = unit.pendingPath.front();

        if (!grid.isValid(nextTile)) {
            unit.pendingPath.clear();
            break;
        }

        // Determine movement cost based on unit type / state
        int32_t tileIndex = grid.toIndex(nextTile);
        int32_t cost = 0;
        if (isNaval(movingDef.unitClass)) {
            cost = grid.navalMovementCost(tileIndex);
        } else if (unit.state == UnitState::Embarked) {
            // Embarked land units can only traverse coast tiles at cost 2
            aoc::map::TerrainType terrain = grid.terrain(tileIndex);
            if (terrain == aoc::map::TerrainType::Coast) {
                cost = 2;
            }
        } else {
            cost = grid.movementCost(tileIndex);
        }

        if (cost == 0) {
            // Path blocked, clear it
            unit.pendingPath.clear();
            break;
        }

        if (unit.movementRemaining < cost) {
            // Not enough movement this turn; continue next turn
            break;
        }

        // Stacking check: can we occupy the target tile?
        if (!canOccupyTile(world, nextTile, unit.owner, unitIsMilitary)) {
            unit.pendingPath.clear();
            break;
        }

        // Move the unit -- set up animation from old position
        hex::AxialCoord oldPosition = unit.position;
        unit.position = nextTile;
        unit.movementRemaining -= cost;
        unit.pendingPath.erase(unit.pendingPath.begin());
        moved = true;

        // Animation: record transition for smooth interpolation
        unit.isAnimating = true;
        unit.animProgress = 0.0f;
        unit.animFrom = oldPosition;
        unit.animTo = nextTile;

        // City capture: if a military unit steps onto an enemy city, capture it
        if (unitIsMilitary) {
            aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
            if (cityPool != nullptr) {
                for (uint32_t ci = 0; ci < cityPool->size(); ++ci) {
                    CityComponent& city = cityPool->data()[ci];
                    if (city.location == nextTile && city.owner != unit.owner) {
                        const PlayerId previousOwner = city.owner;
                        city.owner = unit.owner;
                        if (city.population > 1) {
                            --city.population;
                        }

                        // Clear production queue
                        EntityId cityEntity = cityPool->entities()[ci];
                        ProductionQueueComponent* queue =
                            world.tryGetComponent<ProductionQueueComponent>(cityEntity);
                        if (queue != nullptr) {
                            queue->queue.clear();
                        }

                        // Update tile ownership for worked tiles
                        for (const hex::AxialCoord& workedTile : city.workedTiles) {
                            if (grid.isValid(workedTile)) {
                                const int32_t wtIndex = grid.toIndex(workedTile);
                                if (grid.owner(wtIndex) == previousOwner) {
                                    const_cast<aoc::map::HexGrid&>(grid).setOwner(wtIndex, unit.owner);
                                }
                            }
                        }

                        unit.movementRemaining = 0;
                        LOG_INFO("City %s captured by player %u (was player %u)",
                                 city.name.c_str(),
                                 static_cast<unsigned>(unit.owner),
                                 static_cast<unsigned>(previousOwner));
                        break;
                    }
                }
            }
        }

        // Zone of control: if previous AND current tile are in enemy ZoC,
        // the unit must stop immediately.
        bool currentInZoC = isInEnemyZoneOfControl(world, nextTile, unit.owner);
        if (prevInZoC && currentInZoC) {
            unit.movementRemaining = 0;
            break;
        }
        prevInZoC = currentInZoC;
    }

    // Update state
    if (unit.pendingPath.empty()) {
        unit.state = UnitState::Idle;
    } else {
        unit.state = UnitState::Moving;
    }

    return moved;
}

bool orderUnitMove(aoc::ecs::World& world, EntityId unitEntity,
                    hex::AxialCoord goal, const aoc::map::HexGrid& grid) {
    UnitComponent& unit = world.getComponent<UnitComponent>(unitEntity);

    if (unit.position == goal) {
        unit.pendingPath.clear();
        return true;
    }

    const UnitTypeDef& def = unitTypeDef(unit.typeId);
    const bool navalPath = isNaval(def.unitClass);

    std::optional<aoc::map::PathResult> pathResult = aoc::map::findPath(
        grid, unit.position, goal, 0, nullptr, INVALID_PLAYER, navalPath);

    if (!pathResult.has_value()) {
        return false;
    }

    // Store path excluding the current position (first element)
    unit.pendingPath.clear();
    if (pathResult->path.size() > 1) {
        unit.pendingPath.assign(
            pathResult->path.begin() + 1,
            pathResult->path.end());
    }
    unit.state = UnitState::Moving;

    return true;
}

void refreshMovement(aoc::ecs::World& world, PlayerId player) {
    aoc::ecs::ComponentPool<UnitComponent>* pool = world.getPool<UnitComponent>();
    if (pool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < pool->size(); ++i) {
        UnitComponent& unit = pool->data()[i];
        if (unit.owner != player) {
            continue;
        }
        const UnitTypeDef& def = unitTypeDef(unit.typeId);
        unit.movementRemaining = def.movementPoints;
    }
}

void executeMovement(aoc::ecs::World& world, PlayerId player,
                      const aoc::map::HexGrid& grid) {
    aoc::ecs::ComponentPool<UnitComponent>* pool = world.getPool<UnitComponent>();
    if (pool == nullptr) {
        return;
    }

    // Collect entity IDs first (iteration may be invalidated by modification)
    std::vector<EntityId> unitEntities;
    for (uint32_t i = 0; i < pool->size(); ++i) {
        if (pool->data()[i].owner == player && !pool->data()[i].pendingPath.empty()) {
            unitEntities.push_back(pool->entities()[i]);
        }
    }

    for (EntityId entity : unitEntities) {
        moveUnitAlongPath(world, entity, grid);
    }
}

} // namespace aoc::sim

/**
 * @file Movement.cpp
 * @brief Unit movement implementation.
 */

#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/map/Pathfinding.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/ecs/World.hpp"

namespace aoc::sim {

bool moveUnitAlongPath(aoc::ecs::World& world, EntityId unitEntity,
                        const aoc::map::HexGrid& grid) {
    UnitComponent& unit = world.getComponent<UnitComponent>(unitEntity);

    if (unit.pendingPath.empty()) {
        return false;
    }

    bool moved = false;

    while (!unit.pendingPath.empty() && unit.movementRemaining > 0) {
        hex::AxialCoord nextTile = unit.pendingPath.front();

        if (!grid.isValid(nextTile)) {
            unit.pendingPath.clear();
            break;
        }

        int32_t cost = grid.movementCost(grid.toIndex(nextTile));
        if (cost == 0) {
            // Path blocked, clear it
            unit.pendingPath.clear();
            break;
        }

        if (unit.movementRemaining < cost) {
            // Not enough movement this turn; continue next turn
            break;
        }

        // Move the unit
        unit.position = nextTile;
        unit.movementRemaining -= cost;
        unit.pendingPath.erase(unit.pendingPath.begin());
        moved = true;
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

    std::optional<aoc::map::PathResult> pathResult = aoc::map::findPath(
        grid, unit.position, goal);

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

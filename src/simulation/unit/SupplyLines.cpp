/**
 * @file SupplyLines.cpp
 * @brief Military supply line computation and attrition.
 */

#include "aoc/simulation/unit/SupplyLines.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <queue>
#include <unordered_set>

namespace aoc::sim {

void computeSupplyLines(aoc::ecs::World& world,
                        const aoc::map::HexGrid& grid,
                        PlayerId player) {
    // Collect all supply sources (cities and forts owned by player)
    std::vector<int32_t> supplySources;

    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t c = 0; c < cityPool->size(); ++c) {
            if (cityPool->data()[c].owner == player) {
                int32_t idx = grid.toIndex(cityPool->data()[c].location);
                supplySources.push_back(idx);
            }
        }
    }

    // Also include forts in owned territory
    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        if (grid.owner(i) == player
            && grid.improvement(i) == aoc::map::ImprovementType::Fort) {
            supplySources.push_back(i);
        }
    }

    if (supplySources.empty()) {
        // No supply sources: all units unsupplied
        aoc::ecs::ComponentPool<UnitSupplyComponent>* supplyPool =
            world.getPool<UnitSupplyComponent>();
        if (supplyPool != nullptr) {
            for (uint32_t i = 0; i < supplyPool->size(); ++i) {
                supplyPool->data()[i].isSupplied = false;
            }
        }
        return;
    }

    // BFS from all supply sources, following roads/railways, to compute
    // supply distance to every reachable tile
    std::unordered_map<int32_t, int32_t> supplyDistance;
    std::queue<int32_t> frontier;

    for (int32_t src : supplySources) {
        supplyDistance[src] = 0;
        frontier.push(src);
    }

    while (!frontier.empty()) {
        int32_t current = frontier.front();
        frontier.pop();
        int32_t currentDist = supplyDistance[current];

        // Max range depends on infrastructure at current tile
        int32_t maxRange = supplyRange(grid.infrastructureTier(current));
        if (currentDist >= maxRange) {
            continue;
        }

        hex::AxialCoord currentAxial = grid.toAxial(current);
        std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(currentAxial);
        for (const hex::AxialCoord& nbr : nbrs) {
            if (!grid.isValid(nbr)) { continue; }
            int32_t nbrIdx = grid.toIndex(nbr);

            // Can only supply through passable land
            if (aoc::map::isWater(grid.terrain(nbrIdx))
                || aoc::map::isImpassable(grid.terrain(nbrIdx))) {
                continue;
            }

            int32_t newDist = currentDist + 1;
            std::unordered_map<int32_t, int32_t>::iterator it = supplyDistance.find(nbrIdx);
            if (it == supplyDistance.end() || newDist < it->second) {
                supplyDistance[nbrIdx] = newDist;
                frontier.push(nbrIdx);
            }
        }
    }

    // Update each military unit's supply status
    aoc::ecs::ComponentPool<UnitComponent>* unitPool =
        world.getPool<UnitComponent>();
    if (unitPool == nullptr) { return; }

    for (uint32_t u = 0; u < unitPool->size(); ++u) {
        UnitComponent& unit = unitPool->data()[u];
        if (unit.owner != player) { continue; }
        if (!isMilitary(unitTypeDef(unit.typeId).unitClass)) { continue; }

        EntityId unitEntity = unitPool->entities()[u];
        int32_t unitTileIdx = grid.toIndex(unit.position);

        // Get or create supply component
        UnitSupplyComponent* supply =
            world.tryGetComponent<UnitSupplyComponent>(unitEntity);
        if (supply == nullptr) {
            UnitSupplyComponent newSupply{};
            world.addComponent<UnitSupplyComponent>(unitEntity, std::move(newSupply));
            supply = world.tryGetComponent<UnitSupplyComponent>(unitEntity);
        }
        if (supply == nullptr) { continue; }

        std::unordered_map<int32_t, int32_t>::iterator it = supplyDistance.find(unitTileIdx);
        if (it != supplyDistance.end()) {
            supply->isSupplied = true;
            supply->distanceFromSupply = it->second;
            supply->maxSupplyRange = supplyRange(grid.infrastructureTier(unitTileIdx));
        } else {
            supply->isSupplied = false;
            supply->distanceFromSupply = 999;
        }
    }
}

void applySupplyAttrition(aoc::ecs::World& world, PlayerId player) {
    aoc::ecs::ComponentPool<UnitComponent>* unitPool =
        world.getPool<UnitComponent>();
    aoc::ecs::ComponentPool<UnitSupplyComponent>* supplyPool =
        world.getPool<UnitSupplyComponent>();
    if (unitPool == nullptr || supplyPool == nullptr) { return; }

    for (uint32_t i = 0; i < supplyPool->size(); ++i) {
        const UnitSupplyComponent& supply = supplyPool->data()[i];
        if (supply.isSupplied) { continue; }

        EntityId entity = supplyPool->entities()[i];
        UnitComponent* unit = world.tryGetComponent<UnitComponent>(entity);
        if (unit == nullptr || unit->owner != player) { continue; }

        // Attrition damage
        unit->hitPoints -= UNSUPPLIED_ATTRITION_HP;
        if (unit->hitPoints <= 0) {
            world.destroyEntity(entity);
            LOG_INFO("Unit destroyed by supply attrition");
        }
    }
}

} // namespace aoc::sim

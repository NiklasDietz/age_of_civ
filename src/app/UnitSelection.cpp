/**
 * @file UnitSelection.cpp
 * @brief Multi-unit selection, group commands, and rally point system.
 */

#include "aoc/app/UnitSelection.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Pathfinding.hpp"
#include "aoc/ecs/World.hpp"

#include <algorithm>

namespace aoc::sim {

// ============================================================================
// Selection management
// ============================================================================

void UnitSelection::clear() {
    this->m_selected.clear();
}

void UnitSelection::selectUnit(EntityId unitEntity) {
    this->m_selected.clear();
    this->m_selected.push_back(unitEntity);
}

void UnitSelection::addUnit(EntityId unitEntity) {
    if (!this->isSelected(unitEntity)) {
        this->m_selected.push_back(unitEntity);
    }
}

void UnitSelection::removeUnit(EntityId unitEntity) {
    auto it = std::find(this->m_selected.begin(), this->m_selected.end(), unitEntity);
    if (it != this->m_selected.end()) {
        this->m_selected.erase(it);
    }
}

void UnitSelection::toggleUnit(EntityId unitEntity) {
    if (this->isSelected(unitEntity)) {
        this->removeUnit(unitEntity);
    } else {
        this->addUnit(unitEntity);
    }
}

void UnitSelection::selectInRegion(const aoc::ecs::World& world,
                                    const aoc::map::HexGrid& /*grid*/,
                                    PlayerId player,
                                    hex::AxialCoord corner1,
                                    hex::AxialCoord corner2) {
    this->m_selected.clear();

    // Compute bounding box in axial coordinates
    int32_t minQ = std::min(corner1.q, corner2.q);
    int32_t maxQ = std::max(corner1.q, corner2.q);
    int32_t minR = std::min(corner1.r, corner2.r);
    int32_t maxR = std::max(corner1.r, corner2.r);

    const aoc::ecs::ComponentPool<UnitComponent>* unitPool =
        world.getPool<UnitComponent>();
    if (unitPool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < unitPool->size(); ++i) {
        const UnitComponent& unit = unitPool->data()[i];
        if (unit.owner != player) {
            continue;
        }
        if (unit.position.q >= minQ && unit.position.q <= maxQ
            && unit.position.r >= minR && unit.position.r <= maxR) {
            this->m_selected.push_back(unitPool->entities()[i]);
        }
    }
}

bool UnitSelection::isSelected(EntityId unitEntity) const {
    return std::find(this->m_selected.begin(), this->m_selected.end(), unitEntity)
           != this->m_selected.end();
}

// ============================================================================
// Group commands
// ============================================================================

void UnitSelection::moveAllTo(aoc::ecs::World& world,
                               const aoc::map::HexGrid& grid,
                               hex::AxialCoord target) {
    for (EntityId unitEntity : this->m_selected) {
        UnitComponent* unit = world.tryGetComponent<UnitComponent>(unitEntity);
        if (unit == nullptr) {
            continue;
        }
        // Pathfind from unit position to target
        auto pathResult = aoc::map::findPath(grid, unit->position, target);
        if (pathResult.has_value() && !pathResult->path.empty()) {
            unit->pendingPath = std::move(pathResult->path);
        }
    }
}

void UnitSelection::fortifyAll(aoc::ecs::World& world) {
    for (EntityId unitEntity : this->m_selected) {
        UnitComponent* unit = world.tryGetComponent<UnitComponent>(unitEntity);
        if (unit != nullptr && isMilitary(unitTypeDef(unit->typeId).unitClass)) {
            unit->state = UnitState::Fortified;
            unit->pendingPath.clear();
        }
    }
}

void UnitSelection::autoExploreAll(aoc::ecs::World& world) {
    for (EntityId unitEntity : this->m_selected) {
        UnitComponent* unit = world.tryGetComponent<UnitComponent>(unitEntity);
        if (unit != nullptr) {
            unit->autoExplore = true;
        }
    }
}

// ============================================================================
// Control groups
// ============================================================================

void UnitSelection::saveControlGroup(int32_t groupIndex) {
    if (groupIndex < 0 || groupIndex >= MAX_CONTROL_GROUPS) {
        return;
    }
    this->m_controlGroups[static_cast<std::size_t>(groupIndex)] = this->m_selected;
}

void UnitSelection::loadControlGroup(int32_t groupIndex) {
    if (groupIndex < 0 || groupIndex >= MAX_CONTROL_GROUPS) {
        return;
    }
    this->m_selected = this->m_controlGroups[static_cast<std::size_t>(groupIndex)];
}

// ============================================================================
// Rally points
// ============================================================================

void setRallyPoint(aoc::ecs::World& world, EntityId cityEntity, hex::AxialCoord target) {
    CityRallyPointComponent* rally =
        world.tryGetComponent<CityRallyPointComponent>(cityEntity);
    if (rally == nullptr) {
        CityRallyPointComponent newRally{};
        newRally.rallyPoint = target;
        newRally.hasRallyPoint = true;
        world.addComponent<CityRallyPointComponent>(cityEntity, std::move(newRally));
    } else {
        rally->rallyPoint = target;
        rally->hasRallyPoint = true;
    }
}

void clearRallyPoint(aoc::ecs::World& world, EntityId cityEntity) {
    CityRallyPointComponent* rally =
        world.tryGetComponent<CityRallyPointComponent>(cityEntity);
    if (rally != nullptr) {
        rally->hasRallyPoint = false;
    }
}

void processRallyPoints(aoc::ecs::World& world, const aoc::map::HexGrid& grid) {
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    aoc::ecs::ComponentPool<UnitComponent>* unitPool = world.getPool<UnitComponent>();
    if (unitPool == nullptr) {
        return;
    }

    for (uint32_t c = 0; c < cityPool->size(); ++c) {
        const CityComponent& city = cityPool->data()[c];
        EntityId cityEntity = cityPool->entities()[c];

        const CityRallyPointComponent* rally =
            world.tryGetComponent<CityRallyPointComponent>(cityEntity);
        if (rally == nullptr || !rally->hasRallyPoint) {
            continue;
        }

        // Find units at this city's location that don't have a path yet
        for (uint32_t u = 0; u < unitPool->size(); ++u) {
            UnitComponent& unit = unitPool->data()[u];
            if (unit.owner != city.owner) {
                continue;
            }
            if (unit.position != city.location) {
                continue;
            }
            if (!unit.pendingPath.empty()) {
                continue;  // Already has a path
            }

            // Pathfind to rally point
            auto pathResult = aoc::map::findPath(
                grid, unit.position, rally->rallyPoint);
            if (pathResult.has_value() && !pathResult->path.empty()) {
                unit.pendingPath = std::move(pathResult->path);
            }
        }
    }
}

} // namespace aoc::sim

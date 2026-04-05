/**
 * @file Naval.cpp
 * @brief Naval unit embarkation, disembarkation, and water traversal logic.
 */

#include "aoc/simulation/unit/Naval.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <cassert>

namespace aoc::sim {

bool tryEmbark(aoc::ecs::World& world,
               EntityId unitEntity,
               hex::AxialCoord coastTile,
               const aoc::map::HexGrid& grid) {
    if (!world.isAlive(unitEntity)) {
        return false;
    }

    UnitComponent& unit = world.getComponent<UnitComponent>(unitEntity);
    const UnitTypeDef& def = unitTypeDef(unit.typeId);

    // Only land units can embark (not naval, not already embarked)
    if (isNaval(def.unitClass)) {
        return false;
    }
    if (unit.state == UnitState::Embarked) {
        return false;
    }

    // Target must be a coast tile
    if (!grid.isValid(coastTile)) {
        return false;
    }
    int32_t targetIndex = grid.toIndex(coastTile);
    aoc::map::TerrainType terrain = grid.terrain(targetIndex);
    if (terrain != aoc::map::TerrainType::Coast) {
        return false;
    }

    // Must be adjacent
    int32_t dist = hex::distance(unit.position, coastTile);
    if (dist != 1) {
        return false;
    }

    // Embark: move to coast tile, set state, consume all movement
    unit.position = coastTile;
    unit.state = UnitState::Embarked;
    unit.movementRemaining = 0;

    LOG_INFO("Unit embarked at (%d,%d)", coastTile.q, coastTile.r);
    return true;
}

bool tryDisembark(aoc::ecs::World& world,
                  EntityId unitEntity,
                  hex::AxialCoord landTile,
                  const aoc::map::HexGrid& grid) {
    if (!world.isAlive(unitEntity)) {
        return false;
    }

    UnitComponent& unit = world.getComponent<UnitComponent>(unitEntity);

    // Must be embarked
    if (unit.state != UnitState::Embarked) {
        return false;
    }

    // Target must be passable land
    if (!grid.isValid(landTile)) {
        return false;
    }
    int32_t targetIndex = grid.toIndex(landTile);
    aoc::map::TerrainType terrain = grid.terrain(targetIndex);
    if (aoc::map::isWater(terrain) || aoc::map::isImpassable(terrain)) {
        return false;
    }

    // Must be adjacent
    int32_t dist = hex::distance(unit.position, landTile);
    if (dist != 1) {
        return false;
    }

    // Disembark: move to land tile, set state to Idle, consume all movement
    unit.position = landTile;
    unit.state = UnitState::Idle;
    unit.movementRemaining = 0;

    LOG_INFO("Unit disembarked at (%d,%d)", landTile.q, landTile.r);
    return true;
}

bool canTraverseWater(UnitTypeId typeId) {
    const UnitTypeDef& def = unitTypeDef(typeId);
    return isNaval(def.unitClass);
}

} // namespace aoc::sim

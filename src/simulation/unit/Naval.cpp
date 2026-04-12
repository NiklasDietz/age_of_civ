/**
 * @file Naval.cpp
 * @brief Naval unit embarkation, disembarkation, and water traversal logic.
 */

#include "aoc/simulation/unit/Naval.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

#include "aoc/game/Unit.hpp"

namespace aoc::sim {

bool tryEmbark(aoc::game::Unit& unit,
               hex::AxialCoord coastTile,
               const aoc::map::HexGrid& grid) {
    const UnitTypeDef& def = unit.typeDef();

    // Only land units can embark (not naval, not already embarked)
    if (isNaval(def.unitClass)) {
        return false;
    }
    if (unit.state() == UnitState::Embarked) {
        return false;
    }

    if (!grid.isValid(coastTile)) {
        return false;
    }
    const int32_t targetIndex = grid.toIndex(coastTile);
    if (grid.terrain(targetIndex) != aoc::map::TerrainType::Coast) {
        return false;
    }
    if (hex::distance(unit.position(), coastTile) != 1) {
        return false;
    }

    unit.setPosition(coastTile);
    unit.setState(UnitState::Embarked);
    unit.setMovementRemaining(0);

    LOG_INFO("Unit embarked at (%d,%d)", coastTile.q, coastTile.r);
    return true;
}

bool tryDisembark(aoc::game::Unit& unit,
                  hex::AxialCoord landTile,
                  const aoc::map::HexGrid& grid) {
    if (unit.state() != UnitState::Embarked) {
        return false;
    }

    if (!grid.isValid(landTile)) {
        return false;
    }
    const int32_t targetIndex = grid.toIndex(landTile);
    const aoc::map::TerrainType terrain = grid.terrain(targetIndex);
    if (aoc::map::isWater(terrain) || aoc::map::isImpassable(terrain)) {
        return false;
    }
    if (hex::distance(unit.position(), landTile) != 1) {
        return false;
    }

    unit.setPosition(landTile);
    unit.setState(UnitState::Idle);
    unit.setMovementRemaining(0);

    LOG_INFO("Unit disembarked at (%d,%d)", landTile.q, landTile.r);
    return true;
}

bool canTraverseWater(UnitTypeId typeId) {
    return isNaval(unitTypeDef(typeId).unitClass);
}

} // namespace aoc::sim

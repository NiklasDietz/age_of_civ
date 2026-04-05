#pragma once

/**
 * @file UnitComponent.hpp
 * @brief ECS component for game units (military, settlers, scouts).
 */

#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>
#include <vector>

namespace aoc::sim {

/// ECS component attached to unit entities.
struct UnitComponent {
    PlayerId        owner;
    UnitTypeId      typeId;
    hex::AxialCoord position;
    int32_t         hitPoints;
    int32_t         movementRemaining;
    UnitState       state = UnitState::Idle;
    int8_t          chargesRemaining = -1;   ///< -1 = unlimited, >= 0 = remaining build charges
    int8_t          cargoCapacity = 0;       ///< Number of land units this naval unit can carry (0 = none).
    std::vector<EntityId> cargo;             ///< Land units currently embarked on this naval unit.

    /// Path the unit is currently following (empty if stationary).
    std::vector<hex::AxialCoord> pendingPath;

    /// Create a unit with full HP and movement from its type definition.
    static UnitComponent create(PlayerId owner, UnitTypeId typeId, hex::AxialCoord position) {
        const UnitTypeDef& def = unitTypeDef(typeId);
        UnitComponent unit{};
        unit.owner             = owner;
        unit.typeId            = typeId;
        unit.position          = position;
        unit.hitPoints         = def.maxHitPoints;
        unit.movementRemaining = def.movementPoints;
        unit.state             = UnitState::Idle;
        // Builders start with 3 charges
        if (def.unitClass == UnitClass::Civilian) {
            unit.chargesRemaining = 3;
        }
        // Naval cargo capacity
        if (def.unitClass == UnitClass::Naval) {
            if (typeId.value == 6) {        // Galley
                unit.cargoCapacity = 1;
            } else if (typeId.value == 7) { // Caravel
                unit.cargoCapacity = 2;
            }
            // Battleship (id 8) has 0 cargo capacity
        }
        return unit;
    }
};

} // namespace aoc::sim

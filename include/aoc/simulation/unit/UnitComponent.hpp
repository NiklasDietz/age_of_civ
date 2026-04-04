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
        return unit;
    }
};

} // namespace aoc::sim

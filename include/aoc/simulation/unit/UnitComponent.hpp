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

    uint8_t spreadingReligion = 255;  ///< Which religion this religious unit spreads (255 = N/A)
    int8_t  spreadCharges = -1;       ///< Number of times this unit can spread (-1 = N/A)

    bool autoExplore = false;   ///< When true, unit auto-moves toward unexplored tiles each turn.
    bool autoImprove = false;   ///< When true, civilian unit auto-builds improvements each turn.

    /// Path the unit is currently following (empty if stationary).
    std::vector<hex::AxialCoord> pendingPath;

    // -- Transient animation fields (not saved) --
    bool            isAnimating = false;
    float           animProgress = 0.0f;
    hex::AxialCoord animFrom;
    hex::AxialCoord animTo;

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
        // Religious units start with spread charges
        if (def.unitClass == UnitClass::Religious) {
            if (typeId.value == 19) {        // Missionary
                unit.spreadCharges = 3;
            } else if (typeId.value == 20) { // Apostle
                unit.spreadCharges = 4;
            } else if (typeId.value == 21) { // Inquisitor
                unit.spreadCharges = 2;
            }
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

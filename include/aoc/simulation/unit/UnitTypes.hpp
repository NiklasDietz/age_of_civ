#pragma once

/**
 * @file UnitTypes.hpp
 * @brief Unit type definitions and their base stats.
 */

#include "aoc/core/Types.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace aoc::sim {

/// Unit class determines movement and combat rules.
enum class UnitClass : uint8_t {
    Melee,
    Ranged,
    Cavalry,
    Settler,     ///< Can found cities
    Scout,

    Count
};

/// Unit state machine.
enum class UnitState : uint8_t {
    Idle,
    Moving,
    Fortified,
    Sleeping,
    Embarked,
};

/// Static definition of a unit type (loaded from data eventually).
struct UnitTypeDef {
    UnitTypeId      id;
    std::string_view name;
    UnitClass       unitClass;
    int32_t         maxHitPoints;
    int32_t         combatStrength;
    int32_t         rangedStrength;   ///< 0 for melee units
    int32_t         range;            ///< 0 for melee units
    int32_t         movementPoints;
    int32_t         productionCost;
};

/// Hard-coded starter unit types. Will be data-driven later.
inline constexpr std::array<UnitTypeDef, 5> UNIT_TYPE_DEFS = {{
    {UnitTypeId{0}, "Warrior",  UnitClass::Melee,    100, 20, 0, 0, 2, 40},
    {UnitTypeId{1}, "Slinger",  UnitClass::Ranged,    80, 10, 15, 1, 2, 35},
    {UnitTypeId{2}, "Scout",    UnitClass::Scout,      80, 10, 0, 0, 3, 30},
    {UnitTypeId{3}, "Settler",  UnitClass::Settler,    80,  0, 0, 0, 2, 80},
    {UnitTypeId{4}, "Horseman", UnitClass::Cavalry,   100, 28, 0, 0, 4, 80},
}};

[[nodiscard]] inline constexpr const UnitTypeDef& unitTypeDef(UnitTypeId id) {
    return UNIT_TYPE_DEFS[id.value];
}

} // namespace aoc::sim

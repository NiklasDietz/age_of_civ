#pragma once

/// @file UnitUpgrade.hpp
/// @brief Unit upgrade paths: promote units to stronger types via technology.

#include "aoc/core/Types.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

#include <cstdint>
#include <vector>

namespace aoc::game {
class GameState;
}

namespace aoc::sim {

/// Defines a single upgrade path from one unit type to another.
struct UnitUpgradeDef {
    UnitTypeId from;
    UnitTypeId to;
    TechId     requiredTech;
};

/// Get available upgrades for a unit type.
[[nodiscard]] std::vector<UnitUpgradeDef> getAvailableUpgrades(UnitTypeId currentType);

/// Compute gold cost to upgrade a unit.
[[nodiscard]] int32_t upgradeCost(UnitTypeId from, UnitTypeId to);

/// Perform the upgrade: changes unit type, adjusts HP, costs gold.
/// Returns true if successful.
[[nodiscard]] bool upgradeUnit(aoc::game::GameState& gameState, EntityId unitEntity,
                                UnitTypeId newType, PlayerId player);

} // namespace aoc::sim

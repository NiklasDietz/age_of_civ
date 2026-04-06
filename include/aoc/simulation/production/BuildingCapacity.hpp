#pragma once

/**
 * @file BuildingCapacity.hpp
 * @brief Building throughput limits and upgrade system.
 *
 * Each building has a maximum number of recipe batches it can process per turn.
 * Buildings can be upgraded (Lv1 -> Lv2 -> Lv3) to increase capacity.
 *
 * Default capacities by building type:
 *   Basic buildings (Forge, Workshop):   Lv1=3, Lv2=5, Lv3=8
 *   Mid-tier (Factory, Electronics):     Lv1=2, Lv2=4, Lv3=6
 *   High-tier (Semiconductor Fab, etc):  Lv1=1, Lv2=2, Lv3=3
 *
 * Upgrading costs production hammers (like building a new building).
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"

#include <array>
#include <cstdint>
#include <unordered_map>

namespace aoc::sim {

// ============================================================================
// Building capacity tiers
// ============================================================================

/// Maximum building level.
constexpr int32_t MAX_BUILDING_LEVEL = 3;

/// Capacity per level for each building tier class.
enum class BuildingTierClass : uint8_t {
    Basic   = 0,  ///< Forge, Workshop, Textile Mill, Food Plant, Mint
    Mid     = 1,  ///< Factory, Electronics Plant, Refinery, Precision Workshop
    High    = 2,  ///< Semiconductor Fab, Industrial Complex, Research Lab

    Count
};

/// Recipes per turn at each level, indexed by [tierClass][level-1].
inline constexpr int32_t CAPACITY_TABLE[3][3] = {
    {3, 5, 8},  // Basic
    {2, 4, 6},  // Mid
    {1, 2, 3},  // High
};

/// Production cost to upgrade from level N to N+1, by tier class.
inline constexpr int32_t UPGRADE_COST_TABLE[3][2] = {
    { 60, 120},  // Basic: Lv1->2, Lv2->3
    {100, 200},  // Mid
    {160, 320},  // High
};

/// Get the tier class for a building.
[[nodiscard]] constexpr BuildingTierClass buildingTierClass(BuildingId id) {
    switch (id.value) {
        case 0:  // Forge
        case 1:  // Workshop
        case 8:  // Textile Mill
        case 9:  // Food Proc. Plant
        case 24: // Mint
            return BuildingTierClass::Basic;

        case 2:  // Refinery
        case 3:  // Factory
        case 4:  // Electronics Plant
        case 10: // Precision Workshop
        case 13: // Telecom Hub
            return BuildingTierClass::Mid;

        case 5:  // Industrial Complex
        case 11: // Semiconductor Fab
        case 12: // Research Lab
        case 14: // Airport
            return BuildingTierClass::High;

        default:
            return BuildingTierClass::Basic;
    }
}

// ============================================================================
// Per-city building levels (ECS component)
// ============================================================================

struct CityBuildingLevelsComponent {
    /// Map: buildingId.value -> current level (1-3). Missing = level 1.
    std::unordered_map<uint16_t, int32_t> levels;

    [[nodiscard]] int32_t getLevel(BuildingId id) const {
        auto it = this->levels.find(id.value);
        return (it != this->levels.end()) ? it->second : 1;
    }

    void setLevel(BuildingId id, int32_t level) {
        this->levels[id.value] = std::min(level, MAX_BUILDING_LEVEL);
    }

    /// Get the throughput capacity for a building in this city.
    [[nodiscard]] int32_t capacity(BuildingId id) const {
        int32_t level = this->getLevel(id);
        BuildingTierClass tier = buildingTierClass(id);
        return CAPACITY_TABLE[static_cast<uint8_t>(tier)][level - 1];
    }

    /// Get the production cost to upgrade a building to the next level.
    /// Returns 0 if already max level.
    [[nodiscard]] int32_t upgradeCost(BuildingId id) const {
        int32_t level = this->getLevel(id);
        if (level >= MAX_BUILDING_LEVEL) {
            return 0;
        }
        BuildingTierClass tier = buildingTierClass(id);
        return UPGRADE_COST_TABLE[static_cast<uint8_t>(tier)][level - 1];
    }

    /// Upgrade a building to the next level.
    [[nodiscard]] ErrorCode upgrade(BuildingId id) {
        int32_t level = this->getLevel(id);
        if (level >= MAX_BUILDING_LEVEL) {
            return ErrorCode::InvalidArgument;
        }
        this->setLevel(id, level + 1);
        return ErrorCode::Ok;
    }
};

} // namespace aoc::sim

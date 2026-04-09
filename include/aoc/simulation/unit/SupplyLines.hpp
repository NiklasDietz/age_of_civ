#pragma once

/**
 * @file SupplyLines.hpp
 * @brief Military supply line system: units need supply to fight effectively.
 *
 * Supply range: units far from owned territory or supply depots fight at
 * reduced strength and lose HP each turn from attrition.
 *
 * Supply sources:
 *   - Cities (owned by the unit's player)
 *   - Fort improvements in owned territory
 *   - Supply depot (new building in Encampment district)
 *
 * Supply reaches units via roads/railways. Units without supply:
 *   - -25% combat strength
 *   - -10 HP per turn (attrition)
 *   - Cannot heal
 *
 * Cutting supply: destroying a road/railway behind enemy lines starves
 * their forward units. This makes infrastructure a military target.
 *
 * Supply range (max tiles from nearest supply source via connected road):
 *   - No road: 3 tiles
 *   - Road: 8 tiles
 *   - Railway: 15 tiles
 *   - Highway: 20 tiles
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>

namespace aoc::ecs { class World; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

// ============================================================================
// Supply state per unit
// ============================================================================

struct UnitSupplyComponent {
    bool    isSupplied = true;        ///< Whether the unit has supply this turn
    int32_t distanceFromSupply = 0;   ///< Tiles to nearest supply source
    int32_t maxSupplyRange = 3;       ///< Max range based on best infrastructure
};

/// Combat strength modifier when unsupplied.
constexpr float UNSUPPLIED_COMBAT_PENALTY = 0.75f;

/// HP lost per turn when unsupplied (attrition).
constexpr int32_t UNSUPPLIED_ATTRITION_HP = 10;

/// Supply range by infrastructure tier.
[[nodiscard]] constexpr int32_t supplyRange(int32_t infraTier) {
    switch (infraTier) {
        case 0: return 3;   // No road
        case 1: return 8;   // Road
        case 2: return 15;  // Railway
        case 3: return 20;  // Highway
        default: return 3;
    }
}

// ============================================================================
// Supply computation
// ============================================================================

/**
 * @brief Compute supply status for all units of a player.
 *
 * For each military unit, traces back along roads/railways to the nearest
 * city, fort, or supply depot. If the unit is out of range, it's unsupplied.
 *
 * @param world   ECS world.
 * @param grid    Hex grid (for road/railway checks).
 * @param player  Player whose units to check.
 */
void computeSupplyLines(aoc::ecs::World& world,
                        const aoc::map::HexGrid& grid,
                        PlayerId player);

/**
 * @brief Apply attrition damage to unsupplied units.
 *
 * Called once per turn. Unsupplied units lose HP and cannot heal.
 *
 * @param world  ECS world.
 * @param player Player whose units to process.
 */
void applySupplyAttrition(aoc::ecs::World& world, PlayerId player);

} // namespace aoc::sim

#pragma once

/**
 * @file RiverGameplay.hpp
 * @brief River and elevation gameplay effects.
 *
 * Rivers affect:
 *   - Movement: crossing a river edge costs +1 MP
 *   - Combat: defending across a river gives +25% defense bonus
 *   - Trade: cities connected by river get 50% transport cost reduction
 *   - Yields: tiles adjacent to rivers get +1 food (irrigation)
 *   - Flooding: floodplain tiles may flood, giving +2 food but risk of crop loss
 *   - Fresh water: cities on rivers get +3 housing capacity
 *
 * Elevation affects:
 *   - Combat: units on higher elevation get +10% combat bonus per elevation level
 *   - Food: high elevation (>=2) tiles get -1 food (thin air/cold)
 *   - Defense: cities on hills/high elevation get +25% defense
 *   - Visibility: higher elevation = further sight range
 *
 * Watersheds:
 *   - Cities downstream of polluted cities receive health penalties
 *   - Dam improvement blocks flooding + enables hydroelectric
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::map {
class HexGrid;
}

namespace aoc::ecs { class World; }

namespace aoc::map {

// ============================================================================
// River crossing (movement + combat)
// ============================================================================

/**
 * @brief Check if moving from tile A to tile B crosses a river.
 *
 * @param grid  Hex grid.
 * @param fromIndex  Source tile index.
 * @param toIndex    Destination tile index.
 * @return true if a river edge lies between the two tiles.
 */
[[nodiscard]] bool crossesRiver(const HexGrid& grid, int32_t fromIndex, int32_t toIndex);

/// Extra movement cost for crossing a river (added to base terrain cost).
constexpr int32_t RIVER_CROSSING_COST = 1;

/// Combat defense bonus when enemy must cross a river to attack (multiplier).
constexpr float RIVER_DEFENSE_BONUS = 1.25f;

// ============================================================================
// Elevation combat modifiers
// ============================================================================

/**
 * @brief Compute elevation-based combat modifier.
 *
 * Attacker on higher ground: +10% per elevation level difference.
 * Defender on higher ground: +10% per elevation level difference (works both ways).
 *
 * @param grid         Hex grid.
 * @param attackerTile Attacker's tile index.
 * @param defenderTile Defender's tile index.
 * @return Multiplier for the attacker (>1.0 = advantage, <1.0 = disadvantage).
 */
[[nodiscard]] float elevationCombatModifier(const HexGrid& grid,
                                            int32_t attackerTile,
                                            int32_t defenderTile);

// ============================================================================
// River yield bonuses
// ============================================================================

/**
 * @brief Compute the food bonus from river adjacency for a tile.
 *
 * Tiles with at least one river edge get +1 food (natural irrigation).
 *
 * @param grid      Hex grid.
 * @param tileIndex Tile to check.
 * @return Food yield bonus (0 or 1).
 */
[[nodiscard]] int32_t riverFoodBonus(const HexGrid& grid, int32_t tileIndex);

/**
 * @brief Housing bonus for a city on a river.
 *
 * Cities whose center tile has river edges get +3 housing.
 *
 * @param grid      Hex grid.
 * @param tileIndex City center tile.
 * @return Housing bonus (0 or 3).
 */
[[nodiscard]] int32_t riverHousingBonus(const HexGrid& grid, int32_t tileIndex);

// ============================================================================
// Flooding
// ============================================================================

/**
 * @brief Process seasonal flooding for floodplain tiles.
 *
 * Each turn, floodplain tiles adjacent to rivers have a chance to flood:
 *   - Flooded tile: +2 food this turn (rich silt deposits)
 *   - 5% chance of crop destruction: improvements on the tile are damaged
 *   - Dam improvement on any upstream tile prevents flooding for downstream tiles
 *
 * @param world      ECS world (for city data, notifications).
 * @param grid       Hex grid.
 * @param turnNumber Current turn (for deterministic flooding pattern).
 */
void processFlooding(aoc::ecs::World& world, HexGrid& grid, int32_t turnNumber);

// ============================================================================
// Trade corridor (river-based transport cost reduction)
// ============================================================================

/**
 * @brief Check if two tiles are connected by a continuous river path.
 *
 * Traces river edges from source to destination. If a continuous river
 * path exists, trade transport cost is halved.
 *
 * @param grid       Hex grid.
 * @param fromIndex  Source tile.
 * @param toIndex    Destination tile.
 * @return true if a river corridor connects the two tiles.
 */
[[nodiscard]] bool hasRiverCorridor(const HexGrid& grid, int32_t fromIndex, int32_t toIndex);

/**
 * @brief Compute transport cost modifier for trade between two tiles.
 *
 * Returns a multiplier on the base transport cost:
 *   - Road: 0.5x
 *   - River corridor: 0.5x
 *   - Both: 0.25x
 *   - Neither: 1.0x
 *
 * @param grid       Hex grid.
 * @param fromIndex  Source tile.
 * @param toIndex    Destination tile.
 * @return Transport cost multiplier.
 */
[[nodiscard]] float transportCostModifier(const HexGrid& grid,
                                          int32_t fromIndex, int32_t toIndex);

// ============================================================================
// Watershed pollution
// ============================================================================

/**
 * @brief Check if a city is downstream of a polluted city via river.
 *
 * Traces river flow downstream from polluted cities. Cities on the same
 * river system below a polluted city get a health penalty.
 *
 * @param world  ECS world (for city pollution data).
 * @param grid   Hex grid.
 * @param cityTileIndex  Tile of the city to check.
 * @return Health penalty (0 = no upstream pollution, 1-3 based on severity).
 */
[[nodiscard]] int32_t watershedPollutionPenalty(const aoc::ecs::World& world,
                                                const HexGrid& grid,
                                                int32_t cityTileIndex);

} // namespace aoc::map

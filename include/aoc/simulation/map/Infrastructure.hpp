#pragma once

/**
 * @file Infrastructure.hpp
 * @brief Infrastructure tier system: roads, railways, highways.
 *
 * Infrastructure tiers affect movement, trade capacity, and maintenance:
 *
 *   Tier 0 (None):    1.0x movement, 1x trade capacity, 0 maintenance
 *   Tier 1 (Road):    1.0 MP cost, 2x trade capacity, 0 maintenance
 *   Tier 2 (Railway): 0.5 MP effective, 5x trade capacity, 1 gold/tile/turn
 *   Tier 3 (Highway): 0.33 MP effective, 8x trade capacity, 2 gold/tile/turn
 *
 * Construction requirements:
 *   Road:    Builder unit, 2 charges
 *   Railway: Builder unit, Steel + Coal in stockpile, requires Industrialization tech
 *   Highway: Builder unit, Plastics + Steel in stockpile, requires Computers tech
 *
 * Railways are the backbone of the 1st Industrial Revolution.
 * Highways enable the 3rd Industrial Revolution's logistics.
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexGrid.hpp"

#include <cstdint>

namespace aoc::game { class GameState; }

namespace aoc::sim {

// ============================================================================
// Infrastructure properties
// ============================================================================

/// Trade capacity multiplier per infrastructure tier.
[[nodiscard]] constexpr float infraTradeCapacityMultiplier(int32_t tier) {
    switch (tier) {
        case 0: return 1.0f;
        case 1: return 2.0f;   // Road
        case 2: return 5.0f;   // Railway
        case 3: return 8.0f;   // Highway
        default: return 1.0f;
    }
}

/// Movement cost multiplier per infrastructure tier (applied to base terrain cost).
/// Lower = faster movement.
[[nodiscard]] constexpr float infraMovementMultiplier(int32_t tier) {
    switch (tier) {
        case 0: return 1.0f;
        case 1: return 1.0f;   // Road: terrain cost = 1 (handled in movementCost)
        case 2: return 0.5f;   // Railway: half cost
        case 3: return 0.34f;  // Highway: third cost
        default: return 1.0f;
    }
}

/// Per-tile maintenance cost (gold per turn) for infrastructure.
[[nodiscard]] constexpr int32_t infraMaintenanceCost(int32_t tier) {
    switch (tier) {
        case 0: return 0;
        case 1: return 0;   // Roads are free
        case 2: return 1;   // Railway: 1 gold/tile/turn
        case 3: return 2;   // Highway: 2 gold/tile/turn
        default: return 0;
    }
}

/// Fuel consumption per turn for railways (Coal needed per 10 rail tiles).
constexpr int32_t RAILWAY_COAL_PER_10_TILES = 1;

// ============================================================================
// Infrastructure functions
// ============================================================================

/**
 * @brief Compute total infrastructure maintenance cost for a player.
 *
 * Sums the per-tile maintenance for all railway and highway tiles
 * owned by the player.
 *
 * @param grid    Hex grid.
 * @param player  Player to compute for.
 * @return Total gold maintenance cost per turn.
 */
[[nodiscard]] int32_t computeInfraMaintenanceCost(const aoc::map::HexGrid& grid,
                                                   PlayerId player);

/**
 * @brief Compute total railway fuel consumption for a player.
 *
 * @param grid    Hex grid.
 * @param player  Player to compute for.
 * @return Coal units consumed per turn (1 per 10 railway tiles).
 */
[[nodiscard]] int32_t computeRailwayFuelCost(const aoc::map::HexGrid& grid,
                                              PlayerId player);

/**
 * @brief Get the effective trade capacity between two tiles.
 *
 * Uses the minimum infrastructure tier along the path between tiles.
 * For simplicity, uses the tier at both endpoints (bottleneck principle).
 *
 * @param grid       Hex grid.
 * @param fromIndex  Source tile.
 * @param toIndex    Destination tile.
 * @return Trade capacity multiplier.
 */
[[nodiscard]] float tradeRouteCapacity(const aoc::map::HexGrid& grid,
                                       int32_t fromIndex, int32_t toIndex);

} // namespace aoc::sim

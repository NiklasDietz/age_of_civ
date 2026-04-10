#pragma once

/**
 * @file TradeRouteSystem.hpp
 * @brief Physical trade routes with Trader units carrying real goods.
 *
 * Trade routes work like Civ 6 but with actual goods movement:
 *
 * 1. Player produces a Trader unit in a city
 * 2. Trader is assigned to a destination city (own or foreign)
 * 3. Trader physically walks the path between the two cities
 * 4. Outbound: carries surplus goods from origin city
 * 5. At destination: unloads goods, picks up destination's surplus
 * 6. Return trip: carries destination goods back to origin
 * 7. Repeat until the trade route expires or the Trader is killed
 *
 * Benefits:
 *   - Both cities get goods they don't produce locally
 *   - Gold income from market price differential
 *   - Small science/culture bonus (ideas spread along trade routes)
 *   - Roads are auto-built along the path after repeated trips
 *
 * Vulnerabilities:
 *   - Trader can be pillaged by enemy military units
 *   - Pillaging captures the cargo (goods + gold)
 *   - War automatically cancels routes with enemy civs
 *   - Barbarians may ambush traders
 *
 * Speed:
 *   - Base: 2 tiles/turn
 *   - On road: 4 tiles/turn
 *   - On railway: 6 tiles/turn
 *   - Caravan (upgraded Trader): +1 tile/turn over base
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <vector>

namespace aoc::ecs { class World; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

/// Cargo being carried by a Trader unit.
struct TradeCargo {
    uint16_t goodId = 0;
    int32_t  amount = 0;
};

/// State of an active trade route (attached to the Trader entity).
struct TraderComponent {
    PlayerId     owner = INVALID_PLAYER;
    EntityId     originCity = NULL_ENTITY;
    EntityId     destCity = NULL_ENTITY;
    PlayerId     destOwner = INVALID_PLAYER;  ///< Owner of destination city

    /// Goods currently being carried.
    std::vector<TradeCargo> cargo;

    /// Planned path from current position to next destination.
    std::vector<aoc::hex::AxialCoord> path;
    int32_t pathIndex = 0;  ///< Current position along path

    /// Whether the Trader is heading to destination (outbound) or returning.
    bool isReturning = false;

    /// Total round trips completed (route gets better over time).
    int32_t completedTrips = 0;

    /// Turns this route has been active.
    int32_t turnsActive = 0;

    /// Maximum round trips before the Trader expires (like Builder charges).
    int32_t maxTrips = 10;

    /// Gold earned this turn from trade.
    CurrencyAmount goldEarnedThisTurn = 0;

    /// Cumulative science/culture spread bonus.
    float scienceSpread = 0.0f;
    float cultureSpread = 0.0f;

    /// Movement speed (tiles per turn).
    [[nodiscard]] int32_t movementSpeed(bool onRoad, bool onRailway) const {
        int32_t base = 2;
        if (onRailway) { return base + 4; }
        if (onRoad)    { return base + 2; }
        return base;
    }
};

// ============================================================================
// Trade route operations
// ============================================================================

/**
 * @brief Establish a new trade route from a Trader unit to a destination city.
 *
 * Computes the path, loads surplus goods from the origin city's stockpile,
 * and starts the Trader moving toward the destination.
 *
 * @param world   ECS world.
 * @param grid    Hex grid for pathfinding.
 * @param traderEntity  The Trader unit entity.
 * @param destCity  Destination city entity.
 * @return Ok if route established.
 */
[[nodiscard]] ErrorCode establishTradeRoute(aoc::ecs::World& world,
                                             aoc::map::HexGrid& grid,
                                             EntityId traderEntity,
                                             EntityId destCity);

/**
 * @brief Process all active trade routes for one turn.
 *
 * For each Trader unit:
 * 1. Move along path (speed depends on road/railway)
 * 2. If arrived at destination: unload cargo, load return cargo, earn gold
 * 3. If arrived back at origin: unload return cargo, load new outbound cargo
 * 4. Apply science/culture spread bonuses
 * 5. Check if max trips reached (expire the Trader)
 *
 * @param world  ECS world.
 * @param grid   Hex grid.
 */
void processTradeRoutes(aoc::ecs::World& world, aoc::map::HexGrid& grid);

/**
 * @brief Pillage a Trader unit (called when enemy attacks it).
 *
 * The attacker captures the cargo. The Trader is destroyed.
 * A diplomatic penalty is applied if the Trader belonged to a non-enemy civ.
 *
 * @param world       ECS world.
 * @param traderEntity  The Trader being pillaged.
 * @param pillager      The player doing the pillaging.
 * @return Gold value of captured cargo.
 */
CurrencyAmount pillageTrader(aoc::ecs::World& world,
                              EntityId traderEntity,
                              PlayerId pillager);

/**
 * @brief Count active trade routes for a player.
 */
[[nodiscard]] int32_t countActiveTradeRoutes(const aoc::ecs::World& world,
                                              PlayerId player);

} // namespace aoc::sim

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
#include "aoc/simulation/monetary/MonetarySystem.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace aoc::game { class GameState; class Unit; class City; }
namespace aoc::map { class HexGrid; }
namespace aoc::sim { class Market; class DiplomacyManager; }

namespace aoc::sim {

/// Cargo being carried by a Trader unit.
struct TradeCargo {
    uint16_t goodId = 0;
    int32_t  amount = 0;
};

/// How a trade route travels between cities.
enum class TradeRouteType : uint8_t {
    Land,   ///< Walks overland, uses roads/railways, slowest but cheapest
    Sea,    ///< Sails between coastal cities (both need Harbor), +50% capacity
    Air,    ///< Flies between cities with Airports, fastest, +25% gold, late-game
};

/// State of an active trade route (attached to the Trader entity).
struct TraderComponent {
    PlayerId     owner = INVALID_PLAYER;
    aoc::hex::AxialCoord originCityLocation{};
    aoc::hex::AxialCoord destCityLocation{};
    PlayerId     destOwner = INVALID_PLAYER;  ///< Owner of destination city

    /// How this route travels.
    TradeRouteType routeType = TradeRouteType::Land;

    /// Goods currently being carried.
    std::vector<TradeCargo> cargo;

    /// WP-O: goods reserved at the next-pickup city's exportBuffer for this
    /// trader. Set when a leg starts toward a city; consumed on arrival;
    /// released back to that city's stockpile on trader death pre-pickup.
    /// Keeps the seller's stockpile freed during travel.
    std::vector<TradeCargo> pendingPickupCargo;

    /// City whose exportBuffer holds `pendingPickupCargo`. Equals the leg's
    /// target (origin or destination depending on isReturning).
    aoc::hex::AxialCoord pickupCityLocation{};

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
    int32_t maxTrips = -1;  ///< -1 = permanent (route persists until trader killed)

    /// Gold earned this turn from trade.
    CurrencyAmount goldEarnedThisTurn = 0;

    /// Physical gold/coin riding with the trader between sell at destination
    /// and return home. Only populated under CommodityMoney (metal coins).
    /// Paper/fiat/digital settle electronically -- this field stays zero.
    /// Stolen in full on pillage; credited to the owner's treasury on arrival.
    CurrencyAmount carriedGold = 0;

    /// Toll paid this turn to territory owners along the route.
    CurrencyAmount tollPaidThisTurn = 0;

    /// Cumulative science/culture spread bonus.
    float scienceSpread = 0.0f;
    float cultureSpread = 0.0f;

    /// WP-K3: cargo by route type. Land 3 (wagons / pack animals — smallest
    /// payload, modeled per user request), Sea 6 (bulk shipping — largest),
    /// Air 4 (mid — faster but limited fuselage).
    [[nodiscard]] int32_t maxCargoSlots() const {
        switch (this->routeType) {
            case TradeRouteType::Sea: return 6;
            case TradeRouteType::Air: return 4;
            case TradeRouteType::Land:
            default: return 3;
        }
    }

    /// Cargo slots actually available for goods after the currency medium
    /// takes its cut. Metal coins (CommodityMoney) chew into the bay; paper
    /// and electronic money are effectively free. Always leaves >= 1 slot.
    [[nodiscard]] int32_t effectiveCargoSlots(MonetarySystemType system) const {
        const int32_t raw    = this->maxCargoSlots();
        const int32_t weight = moneyWeightSlots(system);
        return std::max(1, raw - weight);
    }

    /// Movement speed (tiles per turn). Air is fastest, sea is medium, land
    /// depends on roads/rails. WP-C3: pipelines double the throughput of any
    /// land trader whose current tile has `hasPipeline` — models bulk oil /
    /// gas / fuel pumping rather than caravan hauling.
    [[nodiscard]] int32_t movementSpeed(bool onRoad, bool onRailway,
                                        bool onPipeline = false) const {
        switch (this->routeType) {
            case TradeRouteType::Air:
                return 8;
            case TradeRouteType::Sea:
                return 5;
            case TradeRouteType::Land:
            default: {
                int32_t base = 2;
                int32_t speed = base;
                if (onRailway)      { speed = base + 4; }
                else if (onRoad)    { speed = base + 2; }
                if (onPipeline)     { speed *= 2; }
                return speed;
            }
        }
    }

    /// Gold multiplier for route type. Air and Sea give bonuses.
    [[nodiscard]] float goldMultiplier() const {
        switch (this->routeType) {
            case TradeRouteType::Air: return 1.25f;  // +25% gold (premium express delivery)
            case TradeRouteType::Sea: return 1.50f;  // +50% gold (bulk shipping)
            default: return 1.0f;
        }
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
[[nodiscard]] ErrorCode establishTradeRoute(aoc::game::GameState& gameState,
                                             aoc::map::HexGrid& grid,
                                             const Market& market,
                                             const DiplomacyManager* diplomacy,
                                             aoc::game::Unit& traderUnit,
                                             aoc::game::City& destCity);

/**
 * @brief Process all active trade routes for one turn.
 *
 * Uses market prices for gold calculation and demand-driven cargo selection.
 * Collects tolls when traders traverse foreign territory (soft border system).
 */
void processTradeRoutes(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                         const Market& market,
                         DiplomacyManager* diplomacy);

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
CurrencyAmount pillageTrader(aoc::game::GameState& gameState,
                              EntityId traderEntity,
                              PlayerId pillager);

/**
 * @brief Count active trade routes for a player.
 */
[[nodiscard]] int32_t countActiveTradeRoutes(const aoc::game::GameState& gameState,
                                              PlayerId player);

/// Preview information for a potential trade route (no side effects).
struct TradeRouteEstimate {
    int32_t        distanceTiles  = 0;   ///< Path length in tiles.
    int32_t        roundTripTurns = 0;   ///< Estimated turns for one round trip.
    CurrencyAmount estimatedGoldPerTrip = 0;  ///< Rough gold income per round trip.
    TradeRouteType routeType = TradeRouteType::Land;
};

/**
 * @brief Estimate trade route income without establishing the route.
 *
 * Computes distance, route type, and a rough gold estimate based on
 * market price differentials between origin and destination stockpiles.
 * Used by the UI to show previews before the player confirms.
 */
[[nodiscard]] TradeRouteEstimate estimateTradeRouteIncome(
    const aoc::game::GameState& gameState,
    const aoc::map::HexGrid& grid,
    const Market& market,
    const aoc::game::Unit& traderUnit,
    const aoc::game::City& destCity);

} // namespace aoc::sim

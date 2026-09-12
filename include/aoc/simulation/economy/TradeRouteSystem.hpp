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

namespace aoc::game {
class GameState;
class Unit;
class City;
class Player;
} // namespace aoc::game
namespace aoc::map {
class HexGrid;
}
namespace aoc::sim {
class Market;
class DiplomacyManager;
} // namespace aoc::sim

namespace aoc::sim {

/// Cargo being carried by a Trader unit.
struct TradeCargo {
    uint16_t goodId = 0;
    int32_t amount  = 0;
};

/// How a trade route travels between cities.
enum class TradeRouteType : uint8_t {
    Land, ///< Walks overland, uses roads/railways. Wagon = worst capacity;
          ///< rail-majority path = train tier (large capacity).
    Sea,  ///< Sails between coastal cities (both need Harbor). Largest
          ///< capacity (8 slots). Profit comes from volume, not bonuses.
    Air,  ///< Flies between cities with Airports. Lowest capacity but
          ///< fastest (8 tiles/turn). Profit comes from trip frequency.
};

/// State of an active trade route (attached to the Trader entity).
struct TraderComponent {
    PlayerId owner = INVALID_PLAYER;
    aoc::hex::AxialCoord originCityLocation{};
    aoc::hex::AxialCoord destCityLocation{};
    PlayerId destOwner = INVALID_PLAYER; ///< Owner of destination city

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
    int32_t pathIndex = 0; ///< Current position along path

    /// Whether the Trader is heading to destination (outbound) or returning.
    bool isReturning = false;

    /// Total round trips completed (route gets better over time).
    int32_t completedTrips = 0;

    /// Turns this route has been active.
    int32_t turnsActive = 0;

    /// Maximum round trips before the Trader expires (like Builder charges).
    int32_t maxTrips = -1; ///< -1 = permanent (route persists until trader killed)

    /// The treasury's share of coin this trader landed at home this turn.
    CurrencyAmount goldEarnedThisTurn = 0;
    /// The whole purse it landed this turn from a foreign destination (M3:
    /// coin brought home). Per-turn scratch, not saved.
    CurrencyAmount coinLandedThisTurn = 0;

    /// The purse: coin the buyer's people paid, riding home with the trader.
    /// Pays tolls on the way, is looted whole on pillage, and lands at home
    /// as the treasury's customs share plus the merchants' proceeds
    /// (MoneyFlow.hpp receiveTradeCoin). Counted as money in the world.
    CurrencyAmount carriedGold = 0;

    /// What `carriedGold` is made of (v34): 0 = specie, 1 = notes. Decides
    /// which private pool it lands in on arrival once settlement is conserved;
    /// until then it is written and read only by the save file.
    uint8_t carriedMedium = 0;

    /// Toll paid this turn to territory owners along the route.
    CurrencyAmount tollPaidThisTurn = 0;

    /// WP-R: fuel pre-filled at establish + topped up at each round-trip
    /// reversal. Drained per tile travelled. 0 for wagon (Land pre-rail).
    /// goodId 0 = no fuel needed.
    uint16_t fuelGoodId = 0;
    int32_t fuelOnBoard = 0;
    float fuelPerTile   = 0.0f;
    /// Consecutive turns the trader has stalled with empty fuel. After
    /// 20 idle turns the route is auto-abandoned.
    int32_t idleTurnsNoFuel = 0;

    /// Cumulative science/culture spread bonus.
    float scienceSpread = 0.0f;
    float cultureSpread = 0.0f;

    /// WP-K3 v2: cargo by route type, with land split by infrastructure.
    ///   Sea: 8 slots (bulk shipping — vastly highest, cheapest per-unit).
    ///   Land + railway majority on path: 6 slots (train haulage).
    ///   Land wagon/truck (no rail majority): 2 slots (worst, slow).
    ///   Air: 3 slots (lower than train, but speed compensates).
    /// `onRail` flag is computed once at load time from path coverage.
    [[nodiscard]] int32_t maxCargoSlots(bool onRail = false) const {
        switch (this->routeType) {
        case TradeRouteType::Sea:
            return 8;
        case TradeRouteType::Air:
            return 3;
        case TradeRouteType::Land:
        default:
            return onRail ? 6 : 2;
        }
    }

    /// Cargo slots actually available for goods after the currency medium
    /// takes its cut. Metal coins (CommodityMoney) chew into the bay; paper
    /// and electronic money are effectively free. Always leaves >= 1 slot.
    [[nodiscard]] int32_t effectiveCargoSlots(MonetarySystemType system,
                                              bool onRail = false) const {
        const int32_t raw    = this->maxCargoSlots(onRail);
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
            int32_t base  = 2;
            int32_t speed = base;
            if (onRailway) {
                speed = base + 4;
            } else if (onRoad) {
                speed = base + 2;
            }
            if (onPipeline) {
                speed *= 2;
            }
            return speed;
        }
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
                                            aoc::map::HexGrid& grid, const Market& market,
                                            const DiplomacyManager* diplomacy,
                                            aoc::game::Unit& traderUnit, aoc::game::City& destCity);

/// Civ-wide trade slot pool (monetary tier + Markets/Banks/Stock Exchanges
/// + Trading Posts + Merchant GP slots + great-people bonuses).
[[nodiscard]] int32_t computeTotalTradeSlots(const aoc::game::Player& player,
                                             const aoc::map::HexGrid& grid);

/**
 * @brief Process all active trade routes for one turn.
 *
 * Uses market prices for gold calculation and demand-driven cargo selection.
 * Collects tolls when traders traverse foreign territory (soft border system).
 */
void processTradeRoutes(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                        const Market& market, DiplomacyManager* diplomacy);

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
CurrencyAmount pillageTrader(aoc::game::GameState& gameState, EntityId traderEntity,
                             PlayerId pillager);

/**
 * @brief Loot a Trader's cargo without destroying it.
 *
 * The same transfer as `pillageTrader`, minus the removal: cargo goes to the
 * pillager's first city, carried coin to their treasury, the seller's pickup
 * reservation is released, and an auto-renew request is queued if the route had
 * one. The caller is left owning the unit's fate.
 *
 * This exists for the combat kill path, which removes its dead through one
 * deferred pass at the end of `resolveCombat` -- letting the loot step remove
 * the trader too would free it twice. Until 2026-09-07 combat looted only the
 * domestic Courier, so killing a laden international Trader silently voided its
 * cargo and its carried gold.
 *
 * @return Gold value of the captured cargo, coin included.
 */
CurrencyAmount lootTraderCargo(aoc::game::GameState& gameState, aoc::game::Unit& traderUnit,
                               PlayerId pillager);

/**
 * @brief End trade routes that ran to or from a city which just changed hands.
 *
 * A route's `destOwner` is a snapshot taken when the route was established,
 * while the arrival path reads the destination city's LIVE owner. Nothing
 * reconciled the two, and no route-cancellation existed anywhere in the repo,
 * so after a conquest the losing side's trader kept walking to the same tile
 * and unloaded its cargo into the conqueror's stockpile -- paying the enemy,
 * at the at-war rate of 20 percent, for the privilege.
 *
 * A trader still outbound turns around and carries its cargo home; one whose
 * own origin city was the one taken has nowhere to return to and is removed.
 * Traders belonging to the new owner are left alone: their route is now
 * internal, which is legitimate.
 *
 * Called from `GameState::transferCity`, the single chokepoint every ownership
 * change goes through -- conquest, loyalty revolt, secession and liberation.
 *
 * @param at        The city that changed hands.
 * @param newOwner  Who holds it now.
 * @return Number of routes ended.
 */
int32_t cancelRoutesToCity(aoc::game::GameState& gameState, aoc::hex::AxialCoord at,
                           PlayerId newOwner);

/**
 * @brief Count active trade routes for a player.
 */
[[nodiscard]] int32_t countActiveTradeRoutes(const aoc::game::GameState& gameState,
                                             PlayerId player);

/**
 * @brief WP-S2: drive Logistics units' supply cycle for one turn.
 *
 * For each Logistics unit owned by any player:
 *   - Idle: scan owner's encampments; pick one with food < 50 or fuel < 50.
 *           Set as target. Walk to nearest home city to load.
 *   - LoadingAtCity: drain stockpile into onboard cargo, set state EnRouteToDepot.
 *   - EnRouteToDepot: walk toward target encampment.
 *   - UnloadingAtDepot: dump cargo into encampment buffer, set state EnRouteToCity.
 *   - EnRouteToCity: walk back to home city, return to idle.
 */
void processLogisticsUnits(aoc::game::GameState& gameState, aoc::map::HexGrid& grid);

/// Local prices (plan B5, 3.1): no state, derived from what a city needs and
/// holds, so a route's worth is the spread between two cities.
inline constexpr float LOCAL_PRICE_ELASTICITY = 0.6f;
inline constexpr float LOCAL_PRICE_MIN        = 0.5f; ///< of the market price
inline constexpr float LOCAL_PRICE_MAX        = 2.5f;
inline constexpr float DESTINATION_SALE_CAP   = 1.40f;

/// What a city of `population` consumes of `good` each turn, the population
/// rule computePlayerNeeds sums: wheat, clothing, consumer goods, processed
/// food and advanced consumer goods; zero for everything else.
[[nodiscard]] int32_t cityConsumptionNeed(uint16_t goodId, int32_t population);

/// The price `good` fetches in `city`: the market price scaled by how short
/// the city is of it, ((need + 2) / (have + 2)) ^ elasticity, clamped to
/// [0.5, 2.5] of the market. Need is the city's consumption, have its stock.
[[nodiscard]] int32_t localPrice(const Market& market, uint16_t goodId, const aoc::game::City& city);

/// What a destination's commerce adds to a sale there: Commercial Hub 0.10,
/// Market 0.05, Bank 0.10, Stock Exchange 0.15, and a Harbor 0.10 for Sea
/// routes; capped at 1.40.
[[nodiscard]] float destinationSaleMultiplier(const aoc::game::City& city, TradeRouteType routeType);

/// Preview information for a potential trade route (no side effects).
struct TradeRouteEstimate {
    int32_t distanceTiles               = 0; ///< Path length in tiles.
    int32_t roundTripTurns              = 0; ///< Estimated turns for one round trip.
    CurrencyAmount estimatedGoldPerTrip = 0; ///< Rough gold income per round trip.
    TradeRouteType routeType            = TradeRouteType::Land;
};

/**
 * @brief Estimate trade route income without establishing the route.
 *
 * Computes distance, route type, and a gold estimate from the spread
 * between the origin's local prices and the destination's (times its sale
 * multiplier) over the goods a trader would carry. Used by the UI to show
 * previews before the player confirms, and by the AI's route choice.
 */
[[nodiscard]] TradeRouteEstimate estimateTradeRouteIncome(const aoc::game::GameState& gameState,
                                                          const aoc::map::HexGrid& grid,
                                                          const Market& market,
                                                          const aoc::game::Unit& traderUnit,
                                                          const aoc::game::City& destCity);

} // namespace aoc::sim

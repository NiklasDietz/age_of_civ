#pragma once

/**
 * @file NavalTrade.hpp
 * @brief Naval trade, merchant ships, and river navigation economics.
 *
 * Ships are the economic backbone of trade. One merchant vessel carries
 * as much cargo as 10 road wagons or 2 railway cars. Coastal and river
 * trade is cheaper and higher-capacity than any land route.
 *
 * === Navigable Rivers ===
 *
 * A river is navigable if it has sufficient flow (determined by the number
 * of upstream tiles feeding into it). Rivers with 3+ upstream tiles are
 * navigable -- ships can travel on them like coastal water.
 *
 * This means:
 *   - Small headwater streams: not navigable (foot-crossable only)
 *   - Medium rivers: navigable by small ships (Galley, Merchant Barge)
 *   - Large river mouths: navigable by all ships
 *
 * River navigation enables inland cities to participate in naval trade,
 * making river cities far more valuable than land-locked cities.
 *
 * === Merchant Ships ===
 *
 * Merchant Barge:  Small, river-capable. Cargo capacity 5. Cheap.
 * Merchant Cog:    Medium, coastal+river. Cargo capacity 10. Medieval.
 * Trade Galleon:   Large, ocean-capable. Cargo capacity 20. Renaissance.
 * Cargo Steamer:   Industrial. Cargo capacity 40. Requires Coal.
 * Container Ship:  Modern. Cargo capacity 80. Requires Oil.
 *
 * Merchant ships move along trade routes automatically. Each turn they
 * deliver cargo proportional to their capacity. Multiple ships on one
 * route stack capacity.
 *
 * === Trade Route Capacity ===
 *
 * Each trade route has a cargo capacity per turn determined by:
 *   - Land segment: infrastructure tier capacity (road 2, rail 5, highway 8)
 *   - Sea/river segment: sum of merchant ship capacities on the route
 *   - Bottleneck: route capacity = min(land segment, sea segment)
 *
 * This means building railways to your port AND having merchant ships
 * on the sea route is optimal. Either alone is a bottleneck.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"

#include <cstdint>

namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

// ============================================================================
// Navigable river determination
// ============================================================================

/// Minimum upstream tile count for a river to be navigable.
constexpr int32_t NAVIGABLE_RIVER_THRESHOLD = 3;

/**
 * @brief Check if a tile's river is navigable by ships.
 *
 * A river is navigable if the tile has river edges AND the river has
 * sufficient upstream flow (3+ river tiles feeding into this point).
 *
 * @param grid       Hex grid.
 * @param tileIndex  Tile to check.
 * @return true if ships can travel on this tile's river.
 */
[[nodiscard]] bool isRiverNavigable(const aoc::map::HexGrid& grid, int32_t tileIndex);

/**
 * @brief Check if a naval unit can enter a land tile via navigable river.
 *
 * Naval units normally can only enter water tiles. This function extends
 * that to include land tiles with navigable rivers.
 *
 * @param grid       Hex grid.
 * @param tileIndex  Target tile.
 * @return true if a naval unit can be on this tile.
 */
[[nodiscard]] bool canNavalUnitEnter(const aoc::map::HexGrid& grid, int32_t tileIndex);

// ============================================================================
// Merchant ship definitions
// ============================================================================

enum class MerchantShipType : uint8_t {
    Barge         = 0,  ///< River-only, early game
    Cog           = 1,  ///< Coastal + river, medieval
    Galleon       = 2,  ///< Ocean-capable, renaissance
    Steamer       = 3,  ///< Industrial, coal-powered
    ContainerShip = 4,  ///< Modern, high capacity

    Count
};

struct MerchantShipDef {
    MerchantShipType type;
    UnitTypeId       unitTypeId;      ///< Corresponding unit type
    int32_t          cargoCapacity;   ///< Goods per turn
    bool             canTraverseOcean; ///< Can cross deep water
    bool             canTraverseRiver; ///< Can go up navigable rivers
    uint16_t         fuelGoodId;      ///< Fuel consumed (0xFFFF = none)
    int32_t          fuelPerTurn;
    int32_t          productionCost;
};

inline constexpr std::array<MerchantShipDef, 5> MERCHANT_SHIP_DEFS = {{
    {MerchantShipType::Barge,         UnitTypeId{27}, 5,   false, true,  0xFFFF, 0, 40},
    {MerchantShipType::Cog,           UnitTypeId{28}, 10,  false, true,  0xFFFF, 0, 80},
    {MerchantShipType::Galleon,       UnitTypeId{29}, 20,  true,  true,  0xFFFF, 0, 160},
    {MerchantShipType::Steamer,       UnitTypeId{30}, 40,  true,  true,  2 /*COAL*/, 1, 250},
    {MerchantShipType::ContainerShip, UnitTypeId{31}, 80,  true,  false, 3 /*OIL*/,  2, 400},
}};

// ============================================================================
// Trade route capacity computation
// ============================================================================

/**
 * @brief Compute the cargo capacity of a trade route.
 *
 * Considers both land infrastructure and assigned merchant ships.
 *
 * @param world       ECS world.
 * @param grid        Hex grid.
 * @param routeEntity Entity of the trade route.
 * @return Maximum cargo units per turn for this route.
 */
[[nodiscard]] int32_t computeTradeRouteCapacity(const aoc::game::GameState& gameState,
                                                const aoc::map::HexGrid& grid,
                                                EntityId routeEntity);

/**
 * @brief Process merchant ship fuel consumption.
 *
 * Steamers consume coal, container ships consume oil each turn.
 * Ships without fuel become immobile (don't contribute to trade).
 *
 * @param world  ECS world.
 */
void processMerchantShipFuel(aoc::game::GameState& gameState);

// ============================================================================
// Naval trade bonuses
// ============================================================================

/// Cargo capacity multiplier for sea routes vs land routes.
/// One ship carries as much as 10 road wagons.
constexpr float SEA_TRADE_MULTIPLIER = 10.0f;

/// Cargo capacity multiplier for river routes.
/// One barge carries as much as 5 road wagons.
constexpr float RIVER_TRADE_MULTIPLIER = 5.0f;

/// Cost reduction for sea trade vs land trade (transport is cheaper by sea).
constexpr float SEA_TRANSPORT_COST_MULTIPLIER = 0.30f;

/// Cost reduction for river trade.
constexpr float RIVER_TRANSPORT_COST_MULTIPLIER = 0.40f;

} // namespace aoc::sim

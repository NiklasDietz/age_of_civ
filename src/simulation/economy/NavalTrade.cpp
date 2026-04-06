/**
 * @file NavalTrade.cpp
 * @brief Naval trade, river navigation, and merchant ship economics.
 */

#include "aoc/simulation/economy/NavalTrade.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <queue>
#include <unordered_set>

namespace aoc::sim {

// ============================================================================
// Navigable river detection
// ============================================================================

bool isRiverNavigable(const aoc::map::HexGrid& grid, int32_t tileIndex) {
    // A river is navigable if this tile has river edges AND enough upstream
    // river tiles feed into it. We count river-edge neighbors recursively
    // upstream (higher elevation) to determine flow volume.

    if (grid.riverEdges(tileIndex) == 0) {
        return false;
    }

    // Count connected river tiles upstream (BFS, limited depth)
    int32_t upstreamCount = 0;
    std::unordered_set<int32_t> visited;
    std::queue<int32_t> frontier;
    frontier.push(tileIndex);
    visited.insert(tileIndex);

    int8_t tileElev = grid.elevation(tileIndex);

    while (!frontier.empty() && upstreamCount < NAVIGABLE_RIVER_THRESHOLD + 1) {
        int32_t current = frontier.front();
        frontier.pop();

        hex::AxialCoord currentAxial = grid.toAxial(current);
        std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(currentAxial);

        for (int dir = 0; dir < 6; ++dir) {
            if (!grid.hasRiverOnEdge(current, dir)) {
                continue;
            }
            hex::AxialCoord nbrAxial = nbrs[static_cast<std::size_t>(dir)];
            if (!grid.isValid(nbrAxial)) {
                continue;
            }
            int32_t nbrIndex = grid.toIndex(nbrAxial);
            if (visited.count(nbrIndex) > 0) {
                continue;
            }

            // Only count upstream (higher or equal elevation)
            if (grid.elevation(nbrIndex) >= tileElev && grid.riverEdges(nbrIndex) != 0) {
                ++upstreamCount;
                visited.insert(nbrIndex);
                frontier.push(nbrIndex);
            }
        }
    }

    return upstreamCount >= NAVIGABLE_RIVER_THRESHOLD;
}

bool canNavalUnitEnter(const aoc::map::HexGrid& grid, int32_t tileIndex) {
    aoc::map::TerrainType terrain = grid.terrain(tileIndex);

    // Water tiles are always accessible to naval units (except ocean for some)
    if (terrain == aoc::map::TerrainType::Coast || terrain == aoc::map::TerrainType::Ocean) {
        return true;
    }

    // Land tiles with navigable rivers can be entered
    if (!aoc::map::isWater(terrain) && !aoc::map::isImpassable(terrain)) {
        return isRiverNavigable(grid, tileIndex);
    }

    return false;
}

// ============================================================================
// Trade route capacity
// ============================================================================

int32_t computeTradeRouteCapacity(const aoc::ecs::World& world,
                                   const aoc::map::HexGrid& grid,
                                   EntityId routeEntity) {
    const TradeRouteComponent* route =
        world.tryGetComponent<TradeRouteComponent>(routeEntity);
    if (route == nullptr || route->path.empty()) {
        return 1;  // Minimum capacity
    }

    // Determine if the route has sea/river segments
    int32_t landSegments = 0;
    int32_t seaSegments = 0;
    int32_t riverSegments = 0;

    for (const hex::AxialCoord& coord : route->path) {
        if (!grid.isValid(coord)) {
            continue;
        }
        int32_t idx = grid.toIndex(coord);
        aoc::map::TerrainType terrain = grid.terrain(idx);

        if (aoc::map::isWater(terrain)) {
            ++seaSegments;
        } else if (isRiverNavigable(grid, idx)) {
            ++riverSegments;
        } else {
            ++landSegments;
        }
    }

    // Land capacity based on infrastructure (minimum tier along path)
    int32_t landCapacity = 2;  // Base road capacity
    if (landSegments > 0) {
        int32_t minTier = 3;
        for (const hex::AxialCoord& coord : route->path) {
            if (!grid.isValid(coord)) {
                continue;
            }
            int32_t idx = grid.toIndex(coord);
            if (!aoc::map::isWater(grid.terrain(idx)) && !isRiverNavigable(grid, idx)) {
                int32_t tier = grid.infrastructureTier(idx);
                if (tier < minTier) {
                    minTier = tier;
                }
            }
        }
        // Capacity: road=2, railway=5, highway=8
        constexpr int32_t LAND_CAPACITY[] = {1, 2, 5, 8};
        landCapacity = LAND_CAPACITY[std::min(minTier, 3)];
    }

    // Sea/river capacity based on assigned merchant ships
    int32_t navalCapacity = 0;
    const aoc::ecs::ComponentPool<UnitComponent>* unitPool =
        world.getPool<UnitComponent>();
    if (unitPool != nullptr) {
        for (uint32_t i = 0; i < unitPool->size(); ++i) {
            const UnitComponent& unit = unitPool->data()[i];
            if (unit.owner != route->sourcePlayer) {
                continue;
            }
            // Check if this unit is a merchant ship on or near this route
            for (const MerchantShipDef& shipDef : MERCHANT_SHIP_DEFS) {
                if (unit.typeId == shipDef.unitTypeId) {
                    // Check if the ship's position is on the route path
                    for (const hex::AxialCoord& pathTile : route->path) {
                        if (unit.position == pathTile) {
                            navalCapacity += shipDef.cargoCapacity;
                            break;
                        }
                    }
                    break;
                }
            }
        }
    }

    // If route has sea/river segments but no ships, very limited capacity
    if ((seaSegments > 0 || riverSegments > 0) && navalCapacity == 0) {
        navalCapacity = 1;  // Canoe-level trade
    }

    // Route capacity = bottleneck of land and sea segments
    if (landSegments == 0) {
        return std::max(1, navalCapacity);  // Pure sea/river route
    }
    if (seaSegments == 0 && riverSegments == 0) {
        return std::max(1, landCapacity);  // Pure land route
    }
    // Mixed route: bottleneck
    return std::max(1, std::min(landCapacity, navalCapacity));
}

// ============================================================================
// Merchant ship fuel
// ============================================================================

void processMerchantShipFuel(aoc::ecs::World& world) {
    aoc::ecs::ComponentPool<UnitComponent>* unitPool =
        world.getPool<UnitComponent>();
    if (unitPool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < unitPool->size(); ++i) {
        UnitComponent& unit = unitPool->data()[i];

        // Check if this unit is a fuel-consuming merchant ship
        for (const MerchantShipDef& shipDef : MERCHANT_SHIP_DEFS) {
            if (unit.typeId != shipDef.unitTypeId) {
                continue;
            }
            if (shipDef.fuelGoodId == 0xFFFF || shipDef.fuelPerTurn <= 0) {
                break;  // No fuel needed
            }

            // Find a city owned by this player to consume fuel from
            const aoc::ecs::ComponentPool<CityComponent>* cityPool =
                world.getPool<CityComponent>();
            if (cityPool == nullptr) {
                break;
            }

            bool fuelConsumed = false;
            for (uint32_t c = 0; c < cityPool->size(); ++c) {
                if (cityPool->data()[c].owner != unit.owner) {
                    continue;
                }
                EntityId cityEntity = cityPool->entities()[c];
                CityStockpileComponent* stockpile =
                    world.tryGetComponent<CityStockpileComponent>(cityEntity);
                if (stockpile != nullptr
                    && stockpile->getAmount(shipDef.fuelGoodId) >= shipDef.fuelPerTurn) {
                    [[maybe_unused]] bool ok =
                        stockpile->consumeGoods(shipDef.fuelGoodId, shipDef.fuelPerTurn);
                    fuelConsumed = true;
                    break;
                }
            }

            if (!fuelConsumed) {
                // No fuel: ship can't move this turn
                unit.movementRemaining = 0;
            }
            break;
        }
    }
}

} // namespace aoc::sim

/**
 * @file NavalTrade.cpp
 * @brief Naval trade, river navigation, and merchant ship economics.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/economy/NavalTrade.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"

#include <algorithm>
#include <queue>
#include <unordered_set>

namespace aoc::sim {

// ============================================================================
// Navigable river detection
// ============================================================================

bool isRiverNavigable(const aoc::map::HexGrid& grid, int32_t tileIndex) {
    if (grid.riverEdges(tileIndex) == 0) {
        return false;
    }

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
            if (!grid.hasRiverOnEdge(current, dir)) { continue; }
            hex::AxialCoord nbrAxial = nbrs[static_cast<std::size_t>(dir)];
            if (!grid.isValid(nbrAxial)) { continue; }
            int32_t nbrIndex = grid.toIndex(nbrAxial);
            if (visited.count(nbrIndex) > 0) { continue; }

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

    if (terrain == aoc::map::TerrainType::Coast || terrain == aoc::map::TerrainType::Ocean) {
        return true;
    }

    if (!aoc::map::isWater(terrain) && !aoc::map::isImpassable(terrain)) {
        return isRiverNavigable(grid, tileIndex);
    }

    return false;
}

// ============================================================================
// Trade route capacity
// ============================================================================

int32_t computeTradeRouteCapacity(const aoc::game::GameState& gameState,
                                   const aoc::map::HexGrid& grid,
                                   const TradeRouteComponent& routeRef) {
    const TradeRouteComponent* route = &routeRef;
    if (route->path.empty()) {
        return 1;
    }

    int32_t landSegments  = 0;
    int32_t seaSegments   = 0;
    int32_t riverSegments = 0;

    for (const hex::AxialCoord& coord : route->path) {
        if (!grid.isValid(coord)) { continue; }
        int32_t idx               = grid.toIndex(coord);
        aoc::map::TerrainType terrain = grid.terrain(idx);

        if (aoc::map::isWater(terrain)) {
            ++seaSegments;
        } else if (isRiverNavigable(grid, idx)) {
            ++riverSegments;
        } else {
            ++landSegments;
        }
    }

    int32_t landCapacity = 2;
    if (landSegments > 0) {
        int32_t minTier = 3;
        for (const hex::AxialCoord& coord : route->path) {
            if (!grid.isValid(coord)) { continue; }
            int32_t idx = grid.toIndex(coord);
            if (!aoc::map::isWater(grid.terrain(idx)) && !isRiverNavigable(grid, idx)) {
                int32_t tier = grid.infrastructureTier(idx);
                if (tier < minTier) { minTier = tier; }
            }
        }
        // Capacity by road tier: none=1, road=2, railway=5, highway=8
        constexpr int32_t LAND_CAPACITY[] = {1, 2, 5, 8};
        landCapacity = LAND_CAPACITY[std::min(minTier, 3)];
    }

    int32_t navalCapacity = 0;
    const aoc::game::Player* sourcePlayer = gameState.player(route->sourcePlayer);
    if (sourcePlayer != nullptr) {
        for (const std::unique_ptr<aoc::game::Unit>& unitPtr : sourcePlayer->units()) {
            if (unitPtr == nullptr) { continue; }
            for (const MerchantShipDef& shipDef : MERCHANT_SHIP_DEFS) {
                if (unitPtr->typeId() != shipDef.unitTypeId) { continue; }
                for (const hex::AxialCoord& pathTile : route->path) {
                    if (unitPtr->position() == pathTile) {
                        navalCapacity += shipDef.cargoCapacity;
                        break;
                    }
                }
                break;
            }
        }
    }

    if ((seaSegments > 0 || riverSegments > 0) && navalCapacity == 0) {
        navalCapacity = 1;  // Canoe-level trade without assigned ships
    }

    if (landSegments == 0) {
        return std::max(1, navalCapacity);
    }
    if (seaSegments == 0 && riverSegments == 0) {
        return std::max(1, landCapacity);
    }
    return std::max(1, std::min(landCapacity, navalCapacity));
}

// ============================================================================
// Merchant ship fuel
// ============================================================================

void processMerchantShipFuel(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        for (const std::unique_ptr<aoc::game::Unit>& unitPtr : playerPtr->units()) {
            if (unitPtr == nullptr) { continue; }

            for (const MerchantShipDef& shipDef : MERCHANT_SHIP_DEFS) {
                if (unitPtr->typeId() != shipDef.unitTypeId) { continue; }
                if (shipDef.fuelGoodId == 0xFFFF || shipDef.fuelPerTurn <= 0) { break; }

                bool fuelConsumed = false;
                for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
                    if (cityPtr == nullptr) { continue; }
                    CityStockpileComponent& stockpile = cityPtr->stockpile();
                    if (stockpile.getAmount(shipDef.fuelGoodId) >= shipDef.fuelPerTurn) {
                        [[maybe_unused]] bool ok =
                            stockpile.consumeGoods(shipDef.fuelGoodId, shipDef.fuelPerTurn);
                        fuelConsumed = true;
                        break;
                    }
                }

                if (!fuelConsumed) {
                    unitPtr->setMovementRemaining(0);
                }
                break;
            }
        }
    }
}

} // namespace aoc::sim

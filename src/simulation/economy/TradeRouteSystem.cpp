/**
 * @file TradeRouteSystem.cpp
 * @brief Physical trade routes with Trader units carrying real goods.
 */

#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Pathfinding.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

namespace {

/// Select surplus goods from a city stockpile (goods with amount > threshold).
/// Returns up to maxGoods types of cargo.
void selectSurplusGoods(const CityStockpileComponent& stockpile,
                         std::vector<TradeCargo>& outCargo,
                         int32_t maxGoods,
                         int32_t surplusThreshold) {
    outCargo.clear();
    int32_t count = 0;

    for (const std::pair<const uint16_t, int32_t>& entry : stockpile.goods) {
        if (count >= maxGoods) { break; }
        if (entry.second <= surplusThreshold) { continue; }

        // Transfer half of the surplus
        int32_t surplus = entry.second - surplusThreshold;
        int32_t transfer = std::max(1, surplus / 2);

        TradeCargo cargo;
        cargo.goodId = entry.first;
        cargo.amount = transfer;
        outCargo.push_back(cargo);
        ++count;
    }
}

} // anonymous namespace

ErrorCode establishTradeRoute(aoc::ecs::World& world,
                               aoc::map::HexGrid& grid,
                               EntityId traderEntity,
                               EntityId destCity) {
    UnitComponent* unit = world.tryGetComponent<UnitComponent>(traderEntity);
    if (unit == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    const UnitTypeDef& unitDef = unitTypeDef(unit->typeId);
    if (unitDef.unitClass != UnitClass::Trader) {
        return ErrorCode::InvalidArgument;
    }

    const CityComponent* dest = world.tryGetComponent<CityComponent>(destCity);
    if (dest == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    // Find the origin city (closest owned city to the Trader)
    EntityId originCity = NULL_ENTITY;
    int32_t bestDist = 9999;
    aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            if (cityPool->data()[i].owner != unit->owner) { continue; }
            EntityId cityEnt = cityPool->entities()[i];
            int32_t dist = aoc::hex::distance(unit->position, cityPool->data()[i].location);
            if (dist < bestDist) {
                bestDist = dist;
                originCity = cityEnt;
            }
        }
    }
    if (!originCity.isValid()) {
        return ErrorCode::InvalidArgument;
    }

    // Create TraderComponent
    TraderComponent trader;
    trader.owner = unit->owner;
    trader.originCity = originCity;
    trader.destCity = destCity;
    trader.destOwner = dest->owner;
    trader.isReturning = false;
    trader.completedTrips = 0;
    trader.turnsActive = 0;
    trader.maxTrips = 10;

    // Compute path to destination
    const CityComponent* origin = world.tryGetComponent<CityComponent>(originCity);
    if (origin != nullptr) {
        // Simple straight-line path (real pathfinding would use A*)
        aoc::hex::AxialCoord from = origin->location;
        aoc::hex::AxialCoord to = dest->location;
        int32_t dist = aoc::hex::distance(from, to);
        trader.path.clear();
        for (int32_t step = 0; step <= dist; ++step) {
            float t = (dist > 0) ? static_cast<float>(step) / static_cast<float>(dist) : 0.0f;
            int32_t q = static_cast<int32_t>(std::round(
                static_cast<float>(from.q) * (1.0f - t) + static_cast<float>(to.q) * t));
            int32_t r = static_cast<int32_t>(std::round(
                static_cast<float>(from.r) * (1.0f - t) + static_cast<float>(to.r) * t));
            trader.path.push_back(aoc::hex::AxialCoord{q, r});
        }
        trader.pathIndex = 0;
    }

    // Load surplus goods from origin city
    CityStockpileComponent* originStock =
        world.tryGetComponent<CityStockpileComponent>(originCity);
    if (originStock != nullptr) {
        selectSurplusGoods(*originStock, trader.cargo, 4, 3);
        // Remove loaded goods from city stockpile
        for (const TradeCargo& c : trader.cargo) {
            originStock->consumeGoods(c.goodId, c.amount);
        }
    }

    world.addComponent<TraderComponent>(traderEntity, std::move(trader));

    LOG_INFO("Trade route established: player %u, %d goods loaded",
             static_cast<unsigned>(unit->owner),
             static_cast<int>(world.getComponent<TraderComponent>(traderEntity).cargo.size()));

    return ErrorCode::Ok;
}

void processTradeRoutes(aoc::ecs::World& world, aoc::map::HexGrid& grid) {
    aoc::ecs::ComponentPool<TraderComponent>* traderPool =
        world.getPool<TraderComponent>();
    aoc::ecs::ComponentPool<UnitComponent>* unitPool =
        world.getPool<UnitComponent>();

    if (traderPool == nullptr || unitPool == nullptr) {
        return;
    }

    // Collect trader entities first (avoid iterator invalidation from entity destruction)
    std::vector<EntityId> traderEntities;
    traderEntities.reserve(traderPool->size());
    for (uint32_t i = 0; i < traderPool->size(); ++i) {
        traderEntities.push_back(traderPool->entities()[i]);
    }

    for (EntityId traderEntity : traderEntities) {
        if (!world.isAlive(traderEntity)) { continue; }

        TraderComponent* trader = world.tryGetComponent<TraderComponent>(traderEntity);
        UnitComponent* unit = world.tryGetComponent<UnitComponent>(traderEntity);
        if (trader == nullptr || unit == nullptr) { continue; }

        ++trader->turnsActive;

        // Determine movement speed based on terrain under the Trader
        int32_t tileIdx = grid.isValid(unit->position) ? grid.toIndex(unit->position) : -1;
        bool onRoad = (tileIdx >= 0) && grid.hasRoad(tileIdx);
        bool onRailway = (tileIdx >= 0)
            && (grid.improvement(tileIdx) == aoc::map::ImprovementType::Railway
                || grid.improvement(tileIdx) == aoc::map::ImprovementType::Highway);
        int32_t speed = trader->movementSpeed(onRoad, onRailway);

        // Move along path
        for (int32_t step = 0; step < speed; ++step) {
            if (trader->pathIndex >= static_cast<int32_t>(trader->path.size()) - 1) {
                break;  // Arrived
            }
            ++trader->pathIndex;
            unit->position = trader->path[static_cast<std::size_t>(trader->pathIndex)];
        }

        // Check if arrived at destination
        bool arrived = (trader->pathIndex >= static_cast<int32_t>(trader->path.size()) - 1);
        if (!arrived) {
            continue;
        }

        // Arrived at either destination or origin
        EntityId targetCity = trader->isReturning ? trader->originCity : trader->destCity;

        CityStockpileComponent* targetStock =
            world.tryGetComponent<CityStockpileComponent>(targetCity);
        const CityComponent* targetCityComp =
            world.tryGetComponent<CityComponent>(targetCity);

        if (targetStock != nullptr) {
            // Unload cargo
            CurrencyAmount goldEarned = 0;
            for (const TradeCargo& c : trader->cargo) {
                targetStock->addGoods(c.goodId, c.amount);
                // Gold from price differential (simplified: 20% of market value)
                goldEarned += static_cast<CurrencyAmount>(c.amount) * 2;
            }
            trader->goldEarnedThisTurn = goldEarned;

            // Load return cargo (surplus from this city)
            selectSurplusGoods(*targetStock, trader->cargo, 4, 3);
            for (const TradeCargo& c : trader->cargo) {
                targetStock->consumeGoods(c.goodId, c.amount);
            }
        }

        // Science/culture spread: trade spreads ideas
        trader->scienceSpread += 0.5f;
        trader->cultureSpread += 0.3f;

        if (trader->isReturning) {
            // Completed a full round trip
            ++trader->completedTrips;

            // Auto-build road along the trade route after 3 trips
            if (trader->completedTrips == 3) {
                for (const aoc::hex::AxialCoord& tile : trader->path) {
                    if (grid.isValid(tile)) {
                        int32_t idx = grid.toIndex(tile);
                        if (grid.improvement(idx) == aoc::map::ImprovementType::None
                            && !aoc::map::isWater(grid.terrain(idx))
                            && !aoc::map::isImpassable(grid.terrain(idx))) {
                            grid.setImprovement(idx, aoc::map::ImprovementType::Road);
                        }
                    }
                }
                LOG_INFO("Trade route auto-built road (player %u, trip %d)",
                         static_cast<unsigned>(trader->owner), trader->completedTrips);
            }

            // Check if max trips reached
            if (trader->completedTrips >= trader->maxTrips) {
                LOG_INFO("Trade route expired after %d trips (player %u)",
                         trader->completedTrips, static_cast<unsigned>(trader->owner));
                world.destroyEntity(traderEntity);
                continue;
            }
        }

        // Reverse direction: set up path for the next leg
        trader->isReturning = !trader->isReturning;
        // Reverse the path
        std::vector<aoc::hex::AxialCoord> reversedPath(trader->path.rbegin(), trader->path.rend());
        trader->path = std::move(reversedPath);
        trader->pathIndex = 0;
    }
}

CurrencyAmount pillageTrader(aoc::ecs::World& world,
                              EntityId traderEntity,
                              PlayerId pillager) {
    TraderComponent* trader = world.tryGetComponent<TraderComponent>(traderEntity);
    if (trader == nullptr) {
        return 0;
    }

    // Calculate cargo value
    CurrencyAmount totalValue = 0;
    for (const TradeCargo& c : trader->cargo) {
        totalValue += static_cast<CurrencyAmount>(c.amount) * 3;  // Loot value
    }

    // Transfer loot to pillager's first city
    aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            if (cityPool->data()[i].owner != pillager) { continue; }
            CityStockpileComponent* stock =
                world.tryGetComponent<CityStockpileComponent>(cityPool->entities()[i]);
            if (stock != nullptr) {
                for (const TradeCargo& c : trader->cargo) {
                    stock->addGoods(c.goodId, c.amount);
                }
            }
            break;
        }
    }

    LOG_INFO("Trader pillaged! Player %u captured %lld gold worth of goods from player %u",
             static_cast<unsigned>(pillager),
             static_cast<long long>(totalValue),
             static_cast<unsigned>(trader->owner));

    world.destroyEntity(traderEntity);
    return totalValue;
}

int32_t countActiveTradeRoutes(const aoc::ecs::World& world, PlayerId player) {
    const aoc::ecs::ComponentPool<TraderComponent>* traderPool =
        world.getPool<TraderComponent>();
    if (traderPool == nullptr) {
        return 0;
    }

    int32_t count = 0;
    for (uint32_t i = 0; i < traderPool->size(); ++i) {
        if (traderPool->data()[i].owner == player) {
            ++count;
        }
    }
    return count;
}

} // namespace aoc::sim

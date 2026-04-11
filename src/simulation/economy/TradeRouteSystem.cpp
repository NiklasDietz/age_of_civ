/**
 * @file TradeRouteSystem.cpp
 * @brief Physical trade routes with Trader units carrying real goods.
 */

#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
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

/// Select goods for trade, prioritizing what the destination needs most.
/// Score: surplus * max(1, destDeficit) * marketPrice.
/// This ensures traders carry high-value goods the destination actually wants.
void selectTradeGoods(const CityStockpileComponent& originStock,
                       const CityStockpileComponent* destStock,
                       const Market& market,
                       std::vector<TradeCargo>& outCargo,
                       int32_t maxGoods) {
    outCargo.clear();

    struct ScoredGood {
        uint16_t goodId;
        int32_t  surplus;
        float    score;
    };
    std::vector<ScoredGood> candidates;
    candidates.reserve(originStock.goods.size());

    for (const std::pair<const uint16_t, int32_t>& entry : originStock.goods) {
        if (entry.second <= 1) { continue; }  // Keep at least 1 in reserve

        int32_t surplus = entry.second - 1;
        int32_t destDeficit = 1;  // Base score even if no dest info
        if (destStock != nullptr) {
            int32_t destAmount = destStock->getAmount(entry.first);
            // Deficit: how much the dest would benefit from this good
            // Higher score if dest has none of this good
            destDeficit = std::max(1, 5 - destAmount);
        }

        int32_t price = market.marketData(entry.first).currentPrice;
        if (price <= 0) { price = 1; }

        float score = static_cast<float>(surplus)
                    * static_cast<float>(destDeficit)
                    * static_cast<float>(price);

        candidates.push_back({entry.first, surplus, score});
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const ScoredGood& a, const ScoredGood& b) { return a.score > b.score; });

    int32_t count = 0;
    for (const ScoredGood& c : candidates) {
        if (count >= maxGoods) { break; }

        int32_t transfer = std::max(1, c.surplus / 2);

        TradeCargo cargo;
        cargo.goodId = c.goodId;
        cargo.amount = transfer;
        outCargo.push_back(cargo);
        ++count;
    }
}

/// Evaluate whether the destination player would accept a trade route from the proposer.
/// AI decision based on: gold need, resource benefit, relations, war/embargo status.
bool evaluateTradeConsent(const aoc::ecs::World& world, const Market& market,
                           const DiplomacyManager* diplomacy,
                           PlayerId proposer, PlayerId target) {
    // Block trade during war or embargo
    if (diplomacy != nullptr) {
        if (diplomacy->isAtWar(proposer, target)) {
            return false;
        }
        if (diplomacy->hasEmbargo(proposer, target)) {
            return false;
        }
    }

    // Compute benefit score for the target player
    float score = 0.0f;

    // Gold benefit: target always gains some gold from trade
    score += 20.0f;

    // Resource benefit: does the proposer have goods we need?
    const PlayerEconomyComponent* targetEcon = nullptr;
    const PlayerEconomyComponent* proposerEcon = nullptr;
    const aoc::ecs::ComponentPool<PlayerEconomyComponent>* econPool =
        world.getPool<PlayerEconomyComponent>();
    if (econPool != nullptr) {
        for (uint32_t i = 0; i < econPool->size(); ++i) {
            if (econPool->data()[i].owner == target) { targetEcon = &econPool->data()[i]; }
            if (econPool->data()[i].owner == proposer) { proposerEcon = &econPool->data()[i]; }
        }
    }

    if (targetEcon != nullptr && proposerEcon != nullptr) {
        for (const std::pair<const uint16_t, int32_t>& need : targetEcon->totalNeeds) {
            std::unordered_map<uint16_t, int32_t>::const_iterator supIt =
                proposerEcon->totalSupply.find(need.first);
            if (supIt != proposerEcon->totalSupply.end() && supIt->second > 1) {
                int32_t price = market.marketData(need.first).currentPrice;
                score += static_cast<float>(std::min(supIt->second, need.second))
                       * static_cast<float>(std::max(1, price)) * 0.1f;
            }
        }
    }

    // Treasury desperation: accept more readily if poor
    if (targetEcon != nullptr && targetEcon->treasury < 200) {
        score += 30.0f;
    }

    // Relation bonus: friendly players get benefit of the doubt
    if (diplomacy != nullptr) {
        const PairwiseRelation& rel = diplomacy->relation(proposer, target);
        score += static_cast<float>(rel.baseScore) * 0.5f;
    }

    return score > 0.0f;
}

} // anonymous namespace

ErrorCode establishTradeRoute(aoc::ecs::World& world,
                               aoc::map::HexGrid& grid,
                               const Market& market,
                               const DiplomacyManager* diplomacy,
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

    // Trade consent: foreign trade requires destination player's acceptance.
    // The AI evaluates whether the trade benefits them based on:
    //   - Gold income from the route
    //   - Resources the partner could bring that we need
    //   - Diplomatic relation (friendly players get a bonus)
    //   - War/embargo blocks trade entirely
    if (dest->owner != unit->owner) {
        if (!evaluateTradeConsent(world, market, diplomacy, unit->owner, dest->owner)) {
            LOG_INFO("Trade route rejected: player %u -> player %u (no benefit / hostile)",
                     static_cast<unsigned>(unit->owner),
                     static_cast<unsigned>(dest->owner));
            return ErrorCode::InvalidArgument;
        }
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

    // Determine route type based on city infrastructure
    const CityComponent* origin = world.tryGetComponent<CityComponent>(originCity);
    const CityDistrictsComponent* originDistricts =
        world.tryGetComponent<CityDistrictsComponent>(originCity);
    const CityDistrictsComponent* destDistricts =
        world.tryGetComponent<CityDistrictsComponent>(destCity);

    bool originHasAirport = (originDistricts != nullptr
        && originDistricts->hasBuilding(BuildingId{14}));  // Airport
    bool destHasAirport = (destDistricts != nullptr
        && destDistricts->hasBuilding(BuildingId{14}));
    bool originHasHarbor = (originDistricts != nullptr
        && originDistricts->hasDistrict(DistrictType::Harbor));
    bool destHasHarbor = (destDistricts != nullptr
        && destDistricts->hasDistrict(DistrictType::Harbor));

    if (originHasAirport && destHasAirport) {
        trader.routeType = TradeRouteType::Air;
    } else if (originHasHarbor && destHasHarbor) {
        trader.routeType = TradeRouteType::Sea;
    } else {
        trader.routeType = TradeRouteType::Land;
    }

    // Compute path based on route type
    if (origin != nullptr) {
        aoc::hex::AxialCoord from = origin->location;
        aoc::hex::AxialCoord to = dest->location;

        if (trader.routeType == TradeRouteType::Air) {
            // Air routes: direct line (planes don't need paths through terrain)
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
        } else {
            // Land and Sea routes: use A* pathfinding
            std::optional<aoc::map::PathResult> pathResult = aoc::map::findPath(
                grid, from, to, 0, nullptr, INVALID_PLAYER);
            if (pathResult.has_value()) {
                trader.path = pathResult->path;
            } else {
                // Pathfinding failed: fall back to straight line
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
            }
        }
        trader.pathIndex = 0;
    }

    const char* routeNames[] = {"Land", "Sea", "Air"};
    LOG_INFO("Trade route type: %s (player %u -> player %u)",
             routeNames[static_cast<int>(trader.routeType)],
             static_cast<unsigned>(unit->owner),
             static_cast<unsigned>(dest->owner));

    // Load goods prioritized by what destination needs (demand-driven)
    CityStockpileComponent* originStock =
        world.tryGetComponent<CityStockpileComponent>(originCity);
    const CityStockpileComponent* destStock =
        world.tryGetComponent<CityStockpileComponent>(destCity);
    if (originStock != nullptr) {
        selectTradeGoods(*originStock, destStock, market, trader.cargo,
                         trader.maxCargoSlots());
        for (const TradeCargo& c : trader.cargo) {
            [[maybe_unused]] bool ok = originStock->consumeGoods(c.goodId, c.amount);
        }
    }

    world.addComponent<TraderComponent>(traderEntity, std::move(trader));

    LOG_INFO("Trade route established: player %u, %d goods loaded",
             static_cast<unsigned>(unit->owner),
             static_cast<int>(world.getComponent<TraderComponent>(traderEntity).cargo.size()));

    return ErrorCode::Ok;
}

void processTradeRoutes(aoc::ecs::World& world, aoc::map::HexGrid& grid,
                         const Market& market) {
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

        if (targetStock != nullptr) {
            // Unload cargo and earn gold based on market prices
            CurrencyAmount goldEarned = 0;
            for (const TradeCargo& c : trader->cargo) {
                targetStock->addGoods(c.goodId, c.amount);
                // Gold = 20% of market value per unit traded
                int32_t price = market.marketData(c.goodId).currentPrice;
                if (price <= 0) { price = 1; }
                goldEarned += static_cast<CurrencyAmount>(c.amount)
                            * static_cast<CurrencyAmount>(price) / 5;
            }

            // Route type gold multiplier (Sea +50%, Air +25%, Land 1.0x)
            goldEarned = static_cast<CurrencyAmount>(
                static_cast<float>(goldEarned) * trader->goldMultiplier());

            trader->goldEarnedThisTurn = goldEarned;

            // Load return cargo: demand-driven, capacity varies by route type
            const CityStockpileComponent* returnDestStock =
                world.tryGetComponent<CityStockpileComponent>(
                    trader->isReturning ? trader->destCity : trader->originCity);
            selectTradeGoods(*targetStock, returnDestStock, market, trader->cargo,
                             trader->maxCargoSlots());
            for (const TradeCargo& c : trader->cargo) {
                [[maybe_unused]] bool ok = targetStock->consumeGoods(c.goodId, c.amount);
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

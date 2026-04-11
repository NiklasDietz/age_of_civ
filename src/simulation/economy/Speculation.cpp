/**
 * @file Speculation.cpp
 * @brief Market speculation, commodity hoarding, and gold rush events.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/economy/Speculation.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

ErrorCode hoardCommodity(aoc::game::GameState& gameState,
                         const Market& market,
                         PlayerId player,
                         uint16_t goodId, int32_t amount) {
    aoc::ecs::World& world = gameState.legacyWorld();
    if (amount <= 0 || goodId >= market.goodsCount()) {
        return ErrorCode::InvalidArgument;
    }

    // Find the player's cities and pull goods from stockpiles
    aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    aoc::ecs::ComponentPool<CityStockpileComponent>* stockpilePool =
        world.getPool<CityStockpileComponent>();
    if (cityPool == nullptr || stockpilePool == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    int32_t remaining = amount;
    for (uint32_t i = 0; i < cityPool->size() && remaining > 0; ++i) {
        if (cityPool->data()[i].owner != player) {
            continue;
        }
        EntityId cityEntity = cityPool->entities()[i];
        CityStockpileComponent* stockpile =
            world.tryGetComponent<CityStockpileComponent>(cityEntity);
        if (stockpile == nullptr) {
            continue;
        }
        int32_t available = stockpile->getAmount(goodId);
        int32_t take = std::min(available, remaining);
        if (take > 0) {
            stockpile->consumeGoods(goodId, take);
            remaining -= take;
        }
    }

    int32_t actuallyHoarded = amount - remaining;
    if (actuallyHoarded <= 0) {
        return ErrorCode::InsufficientResources;
    }

    // Add to hoard component
    aoc::ecs::ComponentPool<CommodityHoardComponent>* hoardPool =
        world.getPool<CommodityHoardComponent>();
    if (hoardPool == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    for (uint32_t i = 0; i < hoardPool->size(); ++i) {
        if (hoardPool->data()[i].owner == player) {
            CommodityHoardComponent& hoard = hoardPool->data()[i];
            bool found = false;
            for (CommodityHoardComponent::HoardPosition& pos : hoard.positions) {
                if (pos.goodId == goodId) {
                    // Update average purchase price
                    int32_t currentPrice = market.price(goodId);
                    int32_t totalCost = pos.purchasePrice * pos.amount
                                      + currentPrice * actuallyHoarded;
                    pos.amount += actuallyHoarded;
                    pos.purchasePrice = (pos.amount > 0) ? totalCost / pos.amount : 0;
                    found = true;
                    break;
                }
            }
            if (!found) {
                CommodityHoardComponent::HoardPosition newPos;
                newPos.goodId = goodId;
                newPos.amount = actuallyHoarded;
                newPos.purchasePrice = market.price(goodId);
                hoard.positions.push_back(newPos);
            }
            break;
        }
    }

    LOG_INFO("Player %u hoarded %d units of good %u",
             static_cast<unsigned>(player), actuallyHoarded, static_cast<unsigned>(goodId));
    return ErrorCode::Ok;
}

ErrorCode releaseCommodity(aoc::game::GameState& gameState,
                           const Market& /*market*/,
                           PlayerId player,
                           uint16_t goodId, int32_t amount) {
    aoc::ecs::World& world = gameState.legacyWorld();
    aoc::ecs::ComponentPool<CommodityHoardComponent>* hoardPool =
        world.getPool<CommodityHoardComponent>();
    if (hoardPool == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    int32_t released = 0;
    for (uint32_t i = 0; i < hoardPool->size(); ++i) {
        if (hoardPool->data()[i].owner != player) {
            continue;
        }
        CommodityHoardComponent& hoard = hoardPool->data()[i];
        for (std::vector<CommodityHoardComponent::HoardPosition>::iterator it = hoard.positions.begin(); it != hoard.positions.end(); ++it) {
            if (it->goodId == goodId) {
                released = (amount <= 0) ? it->amount : std::min(amount, it->amount);
                it->amount -= released;
                if (it->amount <= 0) {
                    hoard.positions.erase(it);
                }
                break;
            }
        }
        break;
    }

    if (released <= 0) {
        return ErrorCode::InvalidArgument;
    }

    // Put goods back into the player's first city stockpile
    aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            if (cityPool->data()[i].owner == player) {
                EntityId cityEntity = cityPool->entities()[i];
                CityStockpileComponent* stockpile =
                    world.tryGetComponent<CityStockpileComponent>(cityEntity);
                if (stockpile != nullptr) {
                    stockpile->addGoods(goodId, released);
                }
                break;
            }
        }
    }

    LOG_INFO("Player %u released %d units of good %u from hoard",
             static_cast<unsigned>(player), released, static_cast<unsigned>(goodId));
    return ErrorCode::Ok;
}

float marketShareOfGood(const aoc::game::GameState& gameState,
                        const Market& /*market*/,
                        PlayerId player, uint16_t goodId) {
    aoc::ecs::World& world = gameState.legacyWorld();
    // Sum total supply of this good across all players
    int32_t totalSupply = 0;
    int32_t playerSupply = 0;

    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    const aoc::ecs::ComponentPool<CityStockpileComponent>* stockpilePool =
        world.getPool<CityStockpileComponent>();
    if (cityPool == nullptr || stockpilePool == nullptr) {
        return 0.0f;
    }

    for (uint32_t i = 0; i < stockpilePool->size(); ++i) {
        EntityId cityEntity = stockpilePool->entities()[i];
        const CityComponent* city = world.tryGetComponent<CityComponent>(cityEntity);
        if (city == nullptr) {
            continue;
        }
        int32_t amount = stockpilePool->data()[i].getAmount(goodId);
        if (amount > 0) {
            totalSupply += amount;
            if (city->owner == player) {
                playerSupply += amount;
            }
        }
    }

    // Add hoarded supply
    const aoc::ecs::ComponentPool<CommodityHoardComponent>* hoardPool =
        world.getPool<CommodityHoardComponent>();
    if (hoardPool != nullptr) {
        for (uint32_t i = 0; i < hoardPool->size(); ++i) {
            int32_t hoarded = hoardPool->data()[i].hoarded(goodId);
            totalSupply += hoarded;
            if (hoardPool->data()[i].owner == player) {
                playerSupply += hoarded;
            }
        }
    }

    if (totalSupply <= 0) {
        return 0.0f;
    }
    return static_cast<float>(playerSupply) / static_cast<float>(totalSupply);
}

void triggerGoldRushInflation(aoc::game::GameState& gameState, int32_t goldAmount) {
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool == nullptr) {
        return;
    }

    // All gold-standard players experience inflation from increased gold supply.
    // Inflation bump = goldAmount * 0.5% (small per unit, but big gold strikes matter).
    float inflationBump = static_cast<float>(goldAmount) * 0.005f;

    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        MonetaryStateComponent& state = monetaryPool->data()[i];
        if (state.system == MonetarySystemType::GoldStandard
            || state.system == MonetarySystemType::CommodityMoney) {
            state.inflationRate += inflationBump;
            LOG_INFO("Player %u: gold rush inflation +%.1f%%",
                     static_cast<unsigned>(state.owner), inflationBump * 100.0f);
        }
    }
}

void processSpeculation(aoc::game::GameState& gameState, Market& market) {
    const aoc::ecs::ComponentPool<CommodityHoardComponent>* hoardPool =
        world.getPool<CommodityHoardComponent>();
    if (hoardPool == nullptr) {
        return;
    }

    // Hoarded goods reduce effective supply on the market.
    // For each hoarded good, report negative supply (supply reduction).
    for (uint32_t i = 0; i < hoardPool->size(); ++i) {
        const CommodityHoardComponent& hoard = hoardPool->data()[i];
        for (const CommodityHoardComponent::HoardPosition& pos : hoard.positions) {
            if (pos.amount > 0) {
                // Report hoarded amount as demand (reduces net supply, drives price up)
                market.reportDemand(pos.goodId, pos.amount);
            }
        }
    }
}

} // namespace aoc::sim

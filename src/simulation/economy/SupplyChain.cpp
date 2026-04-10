/**
 * @file SupplyChain.cpp
 * @brief Supply chain dependency tracking and disruption.
 */

#include "aoc/simulation/economy/SupplyChain.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void updateSupplyChainHealth(aoc::ecs::World& world, PlayerId player) {
    aoc::ecs::ComponentPool<PlayerSupplyChainComponent>* chainPool =
        world.getPool<PlayerSupplyChainComponent>();
    if (chainPool == nullptr) {
        return;
    }

    PlayerSupplyChainComponent* chain = nullptr;
    for (uint32_t i = 0; i < chainPool->size(); ++i) {
        if (chainPool->data()[i].owner == player) {
            chain = &chainPool->data()[i];
            break;
        }
    }
    if (chain == nullptr) {
        return;
    }

    // Sum stockpiles of each critical good across all player's cities
    aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    aoc::ecs::ComponentPool<CityStockpileComponent>* stockpilePool =
        world.getPool<CityStockpileComponent>();

    if (cityPool == nullptr || stockpilePool == nullptr) {
        return;
    }

    std::array<int32_t, CRITICAL_GOOD_COUNT> totalStockpile = {};
    for (uint32_t i = 0; i < stockpilePool->size(); ++i) {
        EntityId cityEntity = stockpilePool->entities()[i];
        const CityComponent* city = world.tryGetComponent<CityComponent>(cityEntity);
        if (city == nullptr || city->owner != player) {
            continue;
        }

        const CityStockpileComponent& stockpile = stockpilePool->data()[i];
        for (int32_t g = 0; g < CRITICAL_GOOD_COUNT; ++g) {
            totalStockpile[static_cast<std::size_t>(g)] +=
                stockpile.getAmount(CRITICAL_GOODS[g]);
        }
    }

    // Update health for each critical good
    for (int32_t g = 0; g < CRITICAL_GOOD_COUNT; ++g) {
        std::size_t idx = static_cast<std::size_t>(g);
        int32_t stock = totalStockpile[idx];

        // Threshold: need at least 3 units to be "healthy"
        constexpr int32_t HEALTHY_THRESHOLD = 3;

        if (stock >= HEALTHY_THRESHOLD) {
            // Well supplied: health recovers toward 1.0
            chain->supplyHealth[idx] = chain->supplyHealth[idx] * 0.70f + 1.0f * 0.30f;
            chain->stockpileBuffer[idx] = std::min(10, stock / 2);
        } else if (stock > 0) {
            // Low supply: health decays slowly
            chain->supplyHealth[idx] = chain->supplyHealth[idx] * 0.90f + 0.50f * 0.10f;
            chain->stockpileBuffer[idx] = stock;
        } else {
            // Zero supply: use buffer, then health drops
            if (chain->stockpileBuffer[idx] > 0) {
                --chain->stockpileBuffer[idx];
                chain->supplyHealth[idx] *= 0.95f;  // Slow decay while buffer lasts
            } else {
                // No buffer: health drops fast
                chain->supplyHealth[idx] *= 0.80f;  // 20% drop per turn
            }
        }

        chain->supplyHealth[idx] = std::clamp(chain->supplyHealth[idx], 0.0f, 1.0f);
    }
}

void processSupplyChains(aoc::ecs::World& world) {
    aoc::ecs::ComponentPool<PlayerSupplyChainComponent>* chainPool =
        world.getPool<PlayerSupplyChainComponent>();
    if (chainPool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < chainPool->size(); ++i) {
        updateSupplyChainHealth(world, chainPool->data()[i].owner);

        // Log critically low supply chains
        const PlayerSupplyChainComponent& chain = chainPool->data()[i];
        float prodMult = chain.productionMultiplier();
        if (prodMult < 0.80f) {
            LOG_INFO("Player %u supply chain crisis: production at %.0f%%",
                     static_cast<unsigned>(chain.owner),
                     static_cast<double>(prodMult) * 100.0);
        }
    }
}

} // namespace aoc::sim

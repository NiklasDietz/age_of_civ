/**
 * @file BlackMarket.cpp
 * @brief Black market smuggling past embargoes.
 */

#include "aoc/simulation/economy/BlackMarket.hpp"
#include "aoc/simulation/economy/Sanctions.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void processBlackMarketTrade(aoc::ecs::World& world) {
    aoc::ecs::ComponentPool<PlayerBlackMarketComponent>* bmPool =
        world.getPool<PlayerBlackMarketComponent>();
    aoc::ecs::ComponentPool<GlobalSanctionTracker>* sanctionPool =
        world.getPool<GlobalSanctionTracker>();
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();

    if (bmPool == nullptr || monetaryPool == nullptr) {
        return;
    }

    // Get the global sanctions tracker
    const GlobalSanctionTracker* sanctions = nullptr;
    if (sanctionPool != nullptr && sanctionPool->size() > 0) {
        sanctions = &sanctionPool->data()[0];
    }

    // For each player under sanctions, some goods leak through
    for (uint32_t i = 0; i < bmPool->size(); ++i) {
        PlayerBlackMarketComponent& bm = bmPool->data()[i];
        bm.smugglingIncome = 0;
        bm.smugglingCost = 0;

        // Check if this player is under any embargo
        bool isEmbargoed = false;
        if (sanctions != nullptr) {
            for (const SanctionEntry& entry : sanctions->activeSanctions) {
                if (entry.target == bm.owner
                    && entry.type == SanctionType::TradeEmbargo) {
                    isEmbargoed = true;
                    break;
                }
            }
        }

        if (!isEmbargoed) {
            continue;
        }

        // Smuggling provides a fraction of needed strategic goods at premium
        float leakRate = bm.smugglingLeakRate();
        float priceMult = bm.blackMarketPriceMultiplier();

        // Find player's most needed strategic goods (lowest stockpile)
        MonetaryStateComponent* state = nullptr;
        for (uint32_t m = 0; m < monetaryPool->size(); ++m) {
            if (monetaryPool->data()[m].owner == bm.owner) {
                state = &monetaryPool->data()[m];
                break;
            }
        }
        if (state == nullptr) { continue; }

        // Smuggle in 1-3 units of critical goods per turn (expensive)
        constexpr uint16_t CRITICAL[] = {
            goods::IRON_ORE, goods::COAL, goods::OIL, goods::COPPER_ORE
        };
        for (uint16_t goodId : CRITICAL) {
            // Cost: base price * 2.5x * (1-leak) probability filter
            int32_t basePrice = goodDef(goodId).basePrice;
            CurrencyAmount cost = static_cast<CurrencyAmount>(
                static_cast<float>(basePrice) * priceMult * leakRate);

            if (cost > 0 && state->treasury >= cost) {
                state->treasury -= cost;
                bm.smugglingCost += cost;

                // Add smuggled goods to a random city stockpile
                aoc::ecs::ComponentPool<CityComponent>* cityPool =
                    world.getPool<CityComponent>();
                aoc::ecs::ComponentPool<CityStockpileComponent>* stockPool =
                    world.getPool<CityStockpileComponent>();
                if (cityPool != nullptr && stockPool != nullptr) {
                    for (uint32_t c = 0; c < cityPool->size(); ++c) {
                        if (cityPool->data()[c].owner != bm.owner) { continue; }
                        EntityId cityEntity = cityPool->entities()[c];
                        CityStockpileComponent* stockpile =
                            world.tryGetComponent<CityStockpileComponent>(cityEntity);
                        if (stockpile != nullptr) {
                            int32_t amount = static_cast<int32_t>(leakRate * 2.0f);
                            amount = std::max(1, amount);
                            stockpile->addGoods(goodId, amount);
                            break;
                        }
                    }
                }
            }
        }

        if (bm.smugglingCost > 0) {
            LOG_INFO("Black market: player %u smuggled goods for %lld gold (leak rate: %.0f%%)",
                     static_cast<unsigned>(bm.owner),
                     static_cast<long long>(bm.smugglingCost),
                     static_cast<double>(leakRate) * 100.0);
        }
    }
}

void updateBorderControl(PlayerBlackMarketComponent& component,
                          bool hasWalls, bool hasTelecom,
                          float govSpendingRatio) {
    float efficiency = 0.10f;  // Base minimum

    if (hasWalls)   { efficiency += 0.15f; }   // Physical border control
    if (hasTelecom) { efficiency += 0.20f; }   // Electronic surveillance

    // Government spending on security: more spending = better control
    efficiency += std::clamp(govSpendingRatio * 0.5f, 0.0f, 0.25f);

    component.borderControlEfficiency = std::clamp(efficiency, 0.10f, 0.80f);
}

} // namespace aoc::sim

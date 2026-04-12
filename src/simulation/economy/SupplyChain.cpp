/**
 * @file SupplyChain.cpp
 * @brief Supply chain dependency tracking and disruption.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/economy/SupplyChain.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void updateSupplyChainHealth(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return;
    }

    PlayerSupplyChainComponent& chain = playerObj->supplyChain();

    std::array<int32_t, CRITICAL_GOOD_COUNT> totalStockpile = {};
    for (const std::unique_ptr<aoc::game::City>& cityPtr : playerObj->cities()) {
        if (cityPtr == nullptr) { continue; }
        const CityStockpileComponent& stockpile = cityPtr->stockpile();
        for (int32_t g = 0; g < CRITICAL_GOOD_COUNT; ++g) {
            totalStockpile[static_cast<std::size_t>(g)] +=
                stockpile.getAmount(CRITICAL_GOODS[g]);
        }
    }

    for (int32_t g = 0; g < CRITICAL_GOOD_COUNT; ++g) {
        std::size_t idx  = static_cast<std::size_t>(g);
        int32_t    stock = totalStockpile[idx];

        constexpr int32_t HEALTHY_THRESHOLD = 3;

        if (stock >= HEALTHY_THRESHOLD) {
            // Healthy supply: push health toward 1.0 with a strong pull.
            // Base bonus of 0.10f added each tick ensures new civs start healthy
            // even before they've accumulated any strategic resource stockpile.
            chain.supplyHealth[idx] = std::min(1.0f,
                chain.supplyHealth[idx] * 0.70f + 1.0f * 0.30f + 0.10f);
            chain.stockpileBuffer[idx] = std::min(10, stock / 2);
        } else if (stock > 0) {
            chain.supplyHealth[idx]    = chain.supplyHealth[idx] * 0.90f + 0.50f * 0.10f;
            chain.stockpileBuffer[idx] = stock;
        } else {
            if (chain.stockpileBuffer[idx] > 0) {
                --chain.stockpileBuffer[idx];
                chain.supplyHealth[idx] *= 0.95f;
            } else {
                chain.supplyHealth[idx] *= 0.80f;
            }
        }

        chain.supplyHealth[idx] = std::clamp(chain.supplyHealth[idx], 0.0f, 1.0f);
    }
}

void processSupplyChains(aoc::game::GameState& gameState) {
    // Crisis threshold: only report when production drops below 70% (not 80% or 100%).
    // This eliminates spam for civs that are slightly below full health but not in crisis.
    constexpr float CRISIS_LOG_THRESHOLD = 0.70f;

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        updateSupplyChainHealth(gameState, playerPtr->id());

        PlayerSupplyChainComponent& chain = playerPtr->supplyChain();
        const float prodMult = chain.productionMultiplier();
        const bool inCrisisNow = (prodMult < CRISIS_LOG_THRESHOLD);

        // Log only on the turn the crisis begins, not every subsequent turn
        if (inCrisisNow && !chain.wasInCrisisLastTurn) {
            LOG_WARN("Player %u [SupplyChain.cpp:processSupplyChains] supply chain crisis "
                     "started: production at %.0f%%",
                     static_cast<unsigned>(playerPtr->id()),
                     static_cast<double>(prodMult) * 100.0);
        }

        chain.wasInCrisisLastTurn = inCrisisNow;
    }
}

} // namespace aoc::sim

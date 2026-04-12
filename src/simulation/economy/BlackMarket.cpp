/**
 * @file BlackMarket.cpp
 * @brief Black market smuggling past embargoes.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/economy/BlackMarket.hpp"
#include "aoc/simulation/economy/Sanctions.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void processBlackMarketTrade(aoc::game::GameState& gameState) {
    const GlobalSanctionTracker& sanctions = gameState.sanctions();

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        PlayerBlackMarketComponent& bm = playerPtr->blackMarket();
        bm.smugglingIncome = 0;
        bm.smugglingCost   = 0;

        bool isEmbargoed = false;
        for (const SanctionEntry& entry : sanctions.activeSanctions) {
            if (entry.target == playerPtr->id()
                && entry.type == SanctionType::TradeEmbargo) {
                isEmbargoed = true;
                break;
            }
        }

        if (!isEmbargoed) { continue; }

        float leakRate  = bm.smugglingLeakRate();
        float priceMult = bm.blackMarketPriceMultiplier();

        MonetaryStateComponent& state = playerPtr->monetary();

        constexpr uint16_t CRITICAL[] = {
            goods::IRON_ORE, goods::COAL, goods::OIL, goods::COPPER_ORE
        };

        for (uint16_t goodId : CRITICAL) {
            int32_t        basePrice = goodDef(goodId).basePrice;
            CurrencyAmount cost      = static_cast<CurrencyAmount>(
                static_cast<float>(basePrice) * priceMult * leakRate);

            if (cost > 0 && state.treasury >= cost) {
                state.treasury    -= cost;
                bm.smugglingCost  += cost;

                // Deliver smuggled goods to the player's first city
                for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
                    if (cityPtr == nullptr) { continue; }
                    int32_t amount = static_cast<int32_t>(leakRate * 2.0f);
                    amount = std::max(1, amount);
                    cityPtr->stockpile().addGoods(goodId, amount);
                    break;
                }
            }
        }

        if (bm.smugglingCost > 0) {
            LOG_INFO("Black market: player %u smuggled goods for %lld gold (leak rate: %.0f%%)",
                     static_cast<unsigned>(playerPtr->id()),
                     static_cast<long long>(bm.smugglingCost),
                     static_cast<double>(leakRate) * 100.0);
        }
    }
}

void updateBorderControl(PlayerBlackMarketComponent& component,
                          bool hasWalls, bool hasTelecom,
                          float govSpendingRatio) {
    float efficiency = 0.10f;

    if (hasWalls)   { efficiency += 0.15f; }
    if (hasTelecom) { efficiency += 0.20f; }

    efficiency += std::clamp(govSpendingRatio * 0.5f, 0.0f, 0.25f);

    component.borderControlEfficiency = std::clamp(efficiency, 0.10f, 0.80f);
}

} // namespace aoc::sim

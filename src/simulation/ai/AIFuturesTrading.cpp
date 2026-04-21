/**
 * @file AIFuturesTrading.cpp
 * @brief Gene-driven AI commodity futures trading.
 */

#include "aoc/simulation/ai/AIFuturesTrading.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/economy/EconomicDepth.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"

#include <algorithm>
#include <memory>

namespace aoc::sim {

namespace {

constexpr float   kLowThreshold       = 0.85f;
constexpr float   kHighThreshold      = 1.20f;
constexpr int32_t kMinTreasury        = 200;
constexpr int32_t kMaxOpenContracts   = 4;
constexpr int32_t kMaxBuyAmount       = 20;
constexpr int32_t kMaxSellAmount      = 10;
constexpr float   kBuyBudgetFraction  = 0.05f;
constexpr float   kSellBudgetFraction = 0.03f;

} // namespace

void processAIFuturesTrading(aoc::game::GameState& gameState, Market& market) {
    const uint16_t goodsCount = market.goodsCount();
    if (goodsCount == 0) { return; }

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        if (playerPtr->id() == BARBARIAN_PLAYER) { continue; }
        if (playerPtr->victoryTracker().isEliminated) { continue; }

        const LeaderBehavior& beh =
            leaderPersonality(playerPtr->civId()).behavior;
        const float appetite = beh.speculationAppetite;
        const float risk     = beh.riskTolerance;

        if (appetite < 0.5f && risk < 1.0f) { continue; }

        const CurrencyAmount treasury = playerPtr->monetary().treasury;
        if (treasury < kMinTreasury) { continue; }
        if (static_cast<int32_t>(playerPtr->futures().contracts.size())
            >= kMaxOpenContracts) {
            continue;
        }

        int32_t bestGoodLow   = -1;
        float   bestLowRatio  = 1.0f;
        int32_t bestGoodHigh  = -1;
        float   bestHighRatio = 1.0f;
        for (uint16_t g = 0; g < goodsCount; ++g) {
            const Market::GoodMarketData& data = market.marketData(g);
            if (data.basePrice <= 0) { continue; }
            const float ratio = static_cast<float>(data.currentPrice)
                              / static_cast<float>(data.basePrice);
            if (ratio < bestLowRatio) {
                bestLowRatio = ratio;
                bestGoodLow  = static_cast<int32_t>(g);
            }
            if (ratio > bestHighRatio) {
                bestHighRatio = ratio;
                bestGoodHigh  = static_cast<int32_t>(g);
            }
        }

        bool opened = false;

        if (bestGoodLow >= 0 && bestLowRatio < kLowThreshold && appetite > 0.5f) {
            const int32_t price =
                market.price(static_cast<uint16_t>(bestGoodLow));
            if (price > 0) {
                const int32_t budget = static_cast<int32_t>(
                    static_cast<float>(treasury) * kBuyBudgetFraction * appetite);
                const int32_t amount = std::clamp(
                    budget / std::max(1, price), 1, kMaxBuyAmount);
                const CurrencyAmount cost =
                    static_cast<CurrencyAmount>(amount) * price;
                if (amount >= 1 && cost <= treasury) {
                    (void)buyFuture(gameState, market, playerPtr->id(),
                                    static_cast<uint16_t>(bestGoodLow), amount);
                    opened = true;
                }
            }
        }

        if (!opened && bestGoodHigh >= 0 && bestHighRatio > kHighThreshold
            && risk > 1.0f) {
            const int32_t price =
                market.price(static_cast<uint16_t>(bestGoodHigh));
            if (price > 0) {
                const int32_t budget = static_cast<int32_t>(
                    static_cast<float>(treasury) * kSellBudgetFraction * risk);
                const int32_t amount = std::clamp(
                    budget / std::max(1, price), 1, kMaxSellAmount);
                if (amount >= 1) {
                    (void)sellFuture(gameState, market, playerPtr->id(),
                                     static_cast<uint16_t>(bestGoodHigh), amount);
                }
            }
        }
    }
}

} // namespace aoc::sim

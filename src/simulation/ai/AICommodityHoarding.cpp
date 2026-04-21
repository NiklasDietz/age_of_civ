/**
 * @file AICommodityHoarding.cpp
 * @brief Gene-driven AI commodity hoarding and release.
 */

#include "aoc/simulation/ai/AICommodityHoarding.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/economy/Speculation.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>

namespace aoc::sim {

namespace {

constexpr float   kHoardBuyRatio        = 0.90f;
constexpr float   kProfitTakeMultiplier = 1.15f;
constexpr int32_t kMinStockpileSurplus  = 40;
constexpr int32_t kMaxHoardPositions    = 3;
constexpr int32_t kMaxHoardAmount       = 80;
constexpr float   kHoardFraction        = 0.25f;
constexpr float   kAppetiteThreshold    = 0.8f;

int32_t totalStockpile(const aoc::game::Player& player, uint16_t goodId) {
    int32_t total = 0;
    for (const std::unique_ptr<aoc::game::City>& cityPtr : player.cities()) {
        if (cityPtr == nullptr) { continue; }
        total += cityPtr->stockpile().getAmount(goodId);
    }
    return total;
}

} // namespace

void processAICommodityHoarding(aoc::game::GameState& gameState, Market& market) {
    const uint16_t goodsCount = market.goodsCount();
    if (goodsCount == 0) { return; }

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        if (playerPtr->id() == BARBARIAN_PLAYER) { continue; }
        if (playerPtr->isHuman()) { continue; }
        if (playerPtr->victoryTracker().isEliminated) { continue; }

        const LeaderBehavior& beh =
            leaderPersonality(playerPtr->civId()).behavior;
        const float appetite = beh.speculationAppetite;
        if (appetite < kAppetiteThreshold) { continue; }

        // Step 1: take profit on any position whose current price has risen
        // >= kProfitTakeMultiplier × average purchase price. releaseCommodity()
        // invalidates iterators on erase, so we do one release per turn.
        CommodityHoardComponent* hoard = nullptr;
        for (CommodityHoardComponent& h : gameState.commodityHoards()) {
            if (h.owner == playerPtr->id()) { hoard = &h; break; }
        }
        if (hoard != nullptr) {
            for (const CommodityHoardComponent::HoardPosition& pos : hoard->positions) {
                const int32_t curPrice = market.price(pos.goodId);
                const int32_t target   = static_cast<int32_t>(
                    static_cast<float>(pos.purchasePrice) * kProfitTakeMultiplier);
                if (pos.purchasePrice > 0 && curPrice >= target) {
                    (void)releaseCommodity(gameState, market,
                                           playerPtr->id(), pos.goodId, 0);
                    break;
                }
            }
        }

        // Refresh: releaseCommodity may have erased a position.
        int32_t openPositions = (hoard != nullptr)
            ? static_cast<int32_t>(hoard->positions.size()) : 0;
        if (openPositions >= kMaxHoardPositions) { continue; }

        // Step 2: scan for the cheapest good we can hoard against surplus.
        int32_t bestGood    = -1;
        float   bestRatio   = kHoardBuyRatio;
        int32_t bestSurplus = 0;
        for (uint16_t g = 0; g < goodsCount; ++g) {
            const Market::GoodMarketData& data = market.marketData(g);
            if (data.basePrice <= 0) { continue; }
            const float ratio = static_cast<float>(data.currentPrice)
                              / static_cast<float>(data.basePrice);
            if (ratio >= bestRatio) { continue; }
            const int32_t surplus = totalStockpile(*playerPtr, g);
            if (surplus < kMinStockpileSurplus) { continue; }
            bestRatio   = ratio;
            bestGood    = static_cast<int32_t>(g);
            bestSurplus = surplus;
        }
        if (bestGood < 0) { continue; }

        const int32_t amount = std::clamp(
            static_cast<int32_t>(static_cast<float>(bestSurplus) * kHoardFraction * appetite),
            1, kMaxHoardAmount);
        if (amount < 1) { continue; }
        (void)hoardCommodity(gameState, market, playerPtr->id(),
                             static_cast<uint16_t>(bestGood), amount);
    }
}

} // namespace aoc::sim

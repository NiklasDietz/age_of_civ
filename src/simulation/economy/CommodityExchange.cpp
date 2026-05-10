/**
 * @file CommodityExchange.cpp
 * @brief Bilateral commodity barter between players and AI-driven matching.
 */

#include "aoc/simulation/economy/CommodityExchange.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

namespace aoc::sim {

namespace {

int32_t totalStockpile(const aoc::game::Player& player, uint16_t goodId) {
    int32_t total = 0;
    for (const std::unique_ptr<aoc::game::City>& cityPtr : player.cities()) {
        if (cityPtr == nullptr) { continue; }
        total += cityPtr->stockpile().getAmount(goodId);
    }
    return total;
}

void consumeAcrossCities(aoc::game::Player& player, uint16_t goodId, int32_t amount) {
    int32_t remaining = amount;
    for (const std::unique_ptr<aoc::game::City>& cityPtr : player.cities()) {
        if (cityPtr == nullptr || remaining <= 0) { continue; }
        CityStockpileComponent& stockpile = cityPtr->stockpile();
        int32_t avail = stockpile.getAmount(goodId);
        int32_t take  = std::min(avail, remaining);
        if (take > 0) {
            [[maybe_unused]] bool ok = stockpile.consumeGoods(goodId, take);
            remaining -= take;
        }
    }
}

void depositFirstCity(aoc::game::Player& player, uint16_t goodId, int32_t amount) {
    for (const std::unique_ptr<aoc::game::City>& cityPtr : player.cities()) {
        if (cityPtr == nullptr) { continue; }
        cityPtr->stockpile().addGoods(goodId, amount);
        return;
    }
}

} // namespace

ErrorCode executeCommodityTrade(aoc::game::GameState& gameState,
                                PlayerId from, PlayerId to,
                                uint16_t offerGood, int32_t offerAmount,
                                uint16_t requestGood, int32_t requestAmount) {
    if (offerAmount <= 0 || requestAmount <= 0 || offerGood == requestGood) {
        return ErrorCode::InvalidArgument;
    }
    aoc::game::Player* fromPtr = gameState.player(from);
    aoc::game::Player* toPtr   = gameState.player(to);
    if (fromPtr == nullptr || toPtr == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    if (totalStockpile(*fromPtr, offerGood)   < offerAmount)   { return ErrorCode::InsufficientResources; }
    if (totalStockpile(*toPtr,   requestGood) < requestAmount) { return ErrorCode::InsufficientResources; }

    consumeAcrossCities(*fromPtr, offerGood,   offerAmount);
    depositFirstCity   (*toPtr,   offerGood,   offerAmount);

    consumeAcrossCities(*toPtr,   requestGood, requestAmount);
    depositFirstCity   (*fromPtr, requestGood, requestAmount);

    LOG_INFO("Commodity trade: P%u -> P%u (%d x good %u  <-> %d x good %u)",
             static_cast<unsigned>(from), static_cast<unsigned>(to),
             offerAmount,   static_cast<unsigned>(offerGood),
             requestAmount, static_cast<unsigned>(requestGood));
    return ErrorCode::Ok;
}

void processAICommodityExchange(aoc::game::GameState& gameState,
                                 Market& market,
                                 DiplomacyManager* diplomacy) {
    if (diplomacy == nullptr) { return; }
    const uint16_t goodsCount = market.goodsCount();
    if (goodsCount == 0) { return; }

    constexpr int32_t kSurplusThreshold  = 60;
    constexpr int32_t kShortageThreshold = 15;
    constexpr int32_t kMaxTradeAmount    = 40;

    struct Snapshot {
        PlayerId             id            = INVALID_PLAYER;
        float                economicFocus = 1.0f;
        std::vector<int32_t> stockpile;
    };

    std::vector<Snapshot> snapshots;
    snapshots.reserve(gameState.players().size());
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        if (playerPtr->id() == BARBARIAN_PLAYER) { continue; }
        if (playerPtr->isHuman()) { continue; }
        if (playerPtr->victoryTracker().isEliminated) { continue; }
        Snapshot snap;
        snap.id            = playerPtr->id();
        snap.economicFocus = leaderPersonality(playerPtr->civId()).behavior.economicFocus;
        snap.stockpile.assign(goodsCount, 0);
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }
            for (uint16_t g = 0; g < goodsCount; ++g) {
                snap.stockpile[g] += cityPtr->stockpile().getAmount(g);
            }
        }
        snapshots.push_back(std::move(snap));
    }

    // Audit Warning hot-path fix: precompute each snapshot's surplus
    // good list once, then the inner search per (from, to) pair walks
    // `from`'s surplus goods (typically << goodsCount) instead of
    // iterating every good twice. A snapshot is rarely surplus on more
    // than a handful of goods, so this collapses an O(snapshots² × goods²)
    // worst case to O(snapshots × goods + snapshots² × |surplus|).
    std::vector<std::vector<uint16_t>> surplusGoods(snapshots.size());
    for (std::size_t i = 0; i < snapshots.size(); ++i) {
        for (uint16_t g = 0; g < goodsCount; ++g) {
            if (snapshots[i].stockpile[g] >= kSurplusThreshold) {
                surplusGoods[i].push_back(g);
            }
        }
    }

    for (std::size_t fromIdx = 0; fromIdx < snapshots.size(); ++fromIdx) {
        Snapshot& fromSnap = snapshots[fromIdx];
        if (fromSnap.economicFocus < 1.0f) { continue; }
        for (std::size_t toIdx = 0; toIdx < snapshots.size(); ++toIdx) {
            if (toIdx == fromIdx) { continue; }
            Snapshot& toSnap = snapshots[toIdx];
            const PairwiseRelation& rel = diplomacy->relation(fromSnap.id, toSnap.id);
            if (!rel.hasMet || rel.isAtWar) { continue; }
            if (rel.totalScore() < 0)       { continue; }

            // offerGood: a good `from` has surplus on AND `to` is shortage on.
            // Walk `from`'s precomputed surplus list (small) instead of
            // re-scanning all `goodsCount` goods.
            int32_t offerGood = -1;
            for (uint16_t g : surplusGoods[fromIdx]) {
                if (rel.isGoodEmbargoed(g)) { continue; }
                if (toSnap.stockpile[g] > kShortageThreshold) { continue; }
                offerGood = static_cast<int32_t>(g);
                break;
            }
            if (offerGood < 0) { continue; }

            // requestGood: symmetric. Walk `to`'s surplus list.
            int32_t requestGood = -1;
            for (uint16_t g : surplusGoods[toIdx]) {
                if (static_cast<int32_t>(g) == offerGood) { continue; }
                if (rel.isGoodEmbargoed(g)) { continue; }
                if (fromSnap.stockpile[g] > kShortageThreshold) { continue; }
                requestGood = static_cast<int32_t>(g);
                break;
            }
            if (requestGood < 0) { continue; }

            const int32_t offerPrice   = std::max(1, market.price(static_cast<uint16_t>(offerGood)));
            const int32_t requestPrice = std::max(1, market.price(static_cast<uint16_t>(requestGood)));

            int32_t offerAmount = std::clamp(
                fromSnap.stockpile[offerGood] / 4, 1, kMaxTradeAmount);
            int32_t requestAmount = std::clamp(
                (offerAmount * offerPrice) / requestPrice, 1, kMaxTradeAmount);

            if (requestAmount > toSnap.stockpile[requestGood] / 2) {
                requestAmount = toSnap.stockpile[requestGood] / 2;
            }
            if (requestAmount < 1 || offerAmount < 1) { continue; }

            const ErrorCode rc = executeCommodityTrade(gameState,
                fromSnap.id, toSnap.id,
                static_cast<uint16_t>(offerGood),   offerAmount,
                static_cast<uint16_t>(requestGood), requestAmount);
            if (rc == ErrorCode::Ok) {
                fromSnap.stockpile[offerGood]    -= offerAmount;
                toSnap.stockpile[offerGood]      += offerAmount;
                toSnap.stockpile[requestGood]    -= requestAmount;
                fromSnap.stockpile[requestGood]  += requestAmount;
                break;  // one trade per initiator per turn
            }
        }
    }
}

} // namespace aoc::sim

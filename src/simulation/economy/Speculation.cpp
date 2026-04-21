/**
 * @file Speculation.cpp
 * @brief Market speculation, commodity hoarding, and gold rush events.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/economy/Speculation.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aoc::sim {

ErrorCode hoardCommodity(aoc::game::GameState& gameState,
                         const Market& market,
                         PlayerId player,
                         uint16_t goodId, int32_t amount) {
    if (amount <= 0 || goodId >= market.goodsCount()) {
        return ErrorCode::InvalidArgument;
    }

    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    // Pull goods from the player's city stockpiles
    int32_t remaining = amount;
    for (const std::unique_ptr<aoc::game::City>& cityPtr : playerObj->cities()) {
        if (cityPtr == nullptr || remaining <= 0) { continue; }
        CityStockpileComponent& stockpile = cityPtr->stockpile();
        int32_t available = stockpile.getAmount(goodId);
        int32_t take      = std::min(available, remaining);
        if (take > 0) {
            [[maybe_unused]] bool ok = stockpile.consumeGoods(goodId, take);
            remaining -= take;
        }
    }

    int32_t actuallyHoarded = amount - remaining;
    if (actuallyHoarded <= 0) {
        return ErrorCode::InsufficientResources;
    }

    CommodityHoardComponent* hoardPtr = nullptr;
    for (CommodityHoardComponent& h : gameState.commodityHoards()) {
        if (h.owner == player) { hoardPtr = &h; break; }
    }
    if (hoardPtr == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    bool found = false;
    for (CommodityHoardComponent::HoardPosition& pos : hoardPtr->positions) {
        if (pos.goodId == goodId) {
            int32_t currentPrice = market.price(goodId);
            // Cost-basis weighted average. Use float division so the
            // average doesn't truncate toward zero and drift low across
            // repeated hoards (which would understate hoard value and
            // inflate apparent profit on release).
            int64_t totalCost = static_cast<int64_t>(pos.purchasePrice) * pos.amount
                              + static_cast<int64_t>(currentPrice) * actuallyHoarded;
            pos.amount       += actuallyHoarded;
            pos.purchasePrice = (pos.amount > 0)
                ? static_cast<int32_t>(std::llround(
                      static_cast<double>(totalCost)
                      / static_cast<double>(pos.amount)))
                : 0;
            found = true;
            break;
        }
    }
    if (!found) {
        CommodityHoardComponent::HoardPosition newPos;
        newPos.goodId        = goodId;
        newPos.amount        = actuallyHoarded;
        newPos.purchasePrice = market.price(goodId);
        hoardPtr->positions.push_back(newPos);
    }

    LOG_INFO("Player %u hoarded %d units of good %u",
             static_cast<unsigned>(player), actuallyHoarded, static_cast<unsigned>(goodId));
    return ErrorCode::Ok;
}

ErrorCode releaseCommodity(aoc::game::GameState& gameState,
                           const Market& /*market*/,
                           PlayerId player,
                           uint16_t goodId, int32_t amount) {
    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    int32_t released = 0;
    for (CommodityHoardComponent& h : gameState.commodityHoards()) {
        if (h.owner != player) { continue; }
        for (std::vector<CommodityHoardComponent::HoardPosition>::iterator it =
                 h.positions.begin(); it != h.positions.end(); ++it) {
            if (it->goodId == goodId) {
                released    = (amount <= 0) ? it->amount : std::min(amount, it->amount);
                it->amount -= released;
                if (it->amount <= 0) {
                    h.positions.erase(it);
                }
                break;
            }
        }
        break;
    }

    if (released <= 0) {
        return ErrorCode::InvalidArgument;
    }

    // Return goods to the player's first city stockpile
    for (const std::unique_ptr<aoc::game::City>& cityPtr : playerObj->cities()) {
        if (cityPtr == nullptr) { continue; }
        cityPtr->stockpile().addGoods(goodId, released);
        break;
    }

    LOG_INFO("Player %u released %d units of good %u from hoard",
             static_cast<unsigned>(player), released, static_cast<unsigned>(goodId));
    return ErrorCode::Ok;
}

float marketShareOfGood(const aoc::game::GameState& gameState,
                        const Market& /*market*/,
                        PlayerId player, uint16_t goodId) {
    int32_t totalSupply  = 0;
    int32_t playerSupply = 0;

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }
            int32_t amount = cityPtr->stockpile().getAmount(goodId);
            if (amount > 0) {
                totalSupply += amount;
                if (playerPtr->id() == player) {
                    playerSupply += amount;
                }
            }
        }
    }

    // Add hoarded supply from the per-player hoard components
    for (const CommodityHoardComponent& h : gameState.commodityHoards()) {
        int32_t hoarded = h.hoarded(goodId);
        totalSupply += hoarded;
        if (h.owner == player) {
            playerSupply += hoarded;
        }
    }

    if (totalSupply <= 0) {
        return 0.0f;
    }
    return static_cast<float>(playerSupply) / static_cast<float>(totalSupply);
}

void triggerGoldRushInflation(aoc::game::GameState& gameState, int32_t goldAmount) {
    const float inflationBump = static_cast<float>(goldAmount) * 0.005f;

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        MonetaryStateComponent& state = playerPtr->monetary();
        if (state.system == MonetarySystemType::GoldStandard
            || state.system == MonetarySystemType::CommodityMoney) {
            state.inflationRate = std::clamp(state.inflationRate + inflationBump, -0.20f, 0.50f);
            LOG_INFO("Player %u: gold rush inflation +%.1f%%",
                     static_cast<unsigned>(playerPtr->id()),
                     static_cast<double>(inflationBump) * 100.0);
        }
    }
}

void processSpeculation(aoc::game::GameState& gameState, Market& market) {
    for (const CommodityHoardComponent& h : gameState.commodityHoards()) {
        for (const CommodityHoardComponent::HoardPosition& pos : h.positions) {
            if (pos.amount > 0) {
                market.reportDemand(pos.goodId, pos.amount);
            }
        }
    }
}

} // namespace aoc::sim

/**
 * @file Market.cpp
 * @brief Supply/demand market price discovery.
 */

#include "aoc/simulation/economy/Market.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>

namespace aoc::sim {

// Storage assumption: Market::GoodMarketData lookups index by uint16_t
// `goodId`, so the `goods::GOOD_COUNT` upper bound must remain
// addressable in 16 bits. If GOOD_COUNT ever crosses 65535, the public
// API (and savefile schema) needs to widen before this assert can drop.
static_assert(goods::GOOD_COUNT <= UINT16_MAX,
              "Market goodId is uint16_t; widen API + saves before exceeding UINT16_MAX goods");

void Market::initialize() {
    uint16_t count = goodCount();
    this->m_goods.resize(count);

    for (uint16_t i = 0; i < count; ++i) {
        const GoodDef& def = goodDef(i);
        GoodMarketData& data = this->m_goods[i];
        data.basePrice    = def.basePrice;
        data.currentPrice = def.basePrice;
        data.totalSupply  = 0;
        data.totalDemand  = 0;

        for (int32_t& h : data.priceHistory) {
            h = def.basePrice;
        }
    }
}

void Market::reportSupply(uint16_t goodId, int32_t amount) {
    if (goodId < this->m_goods.size()) {
        this->m_goods[goodId].totalSupply += amount;
    }
}

void Market::reportDemand(uint16_t goodId, int32_t amount) {
    if (goodId < this->m_goods.size()) {
        this->m_goods[goodId].totalDemand += amount;
    }
}

void Market::updatePrices() {
    // size_t goodIndex matches m_goods.size()'s type and avoids the
    // signed/unsigned comparison + 16-bit wrap that the prior uint16_t
    // counter would have introduced once GOOD_COUNT approached 65535.
    std::size_t goodIndex = 0;
    for (GoodMarketData& data : this->m_goods) {
        // Avoid division by zero: if no supply or demand, price drifts toward base
        float supply = static_cast<float>(std::max(data.totalSupply, 1));
        float demand = static_cast<float>(std::max(data.totalDemand, 1));

        // Use per-good elasticity if available, otherwise fall back to global
        float goodElasticity = this->elasticity;
        if (goodIndex < static_cast<std::size_t>(goodCount())) {
            goodElasticity = goodDef(static_cast<uint16_t>(goodIndex)).priceElasticity;
        }

        // Price = basePrice * (demand/supply)^elasticity
        float ratio = demand / supply;
        float priceFactor = std::pow(ratio, goodElasticity);

        float newPrice = static_cast<float>(data.basePrice) * priceFactor;

        // Clamp to bounds
        float minPrice = static_cast<float>(data.basePrice) * this->minPriceRatio;
        float maxPrice = static_cast<float>(data.basePrice) * this->maxPriceRatio;
        newPrice = std::clamp(newPrice, minPrice, maxPrice);

        // Smooth price changes (80% new, 20% old) to avoid violent oscillation
        float smoothed = static_cast<float>(data.currentPrice) * 0.2f + newPrice * 0.8f;
        data.currentPrice = std::max(1, static_cast<int32_t>(std::round(smoothed)));

        // Record history
        data.priceHistory[data.historyIndex % GoodMarketData::HISTORY_SIZE] = data.currentPrice;
        data.historyIndex = (data.historyIndex + 1) % GoodMarketData::HISTORY_SIZE;

        // Reset accumulators for next turn
        data.totalSupply = 0;
        data.totalDemand = 0;

        ++goodIndex;
    }
}

int32_t Market::price(uint16_t goodId) const {
    if (goodId >= this->m_goods.size()) {
        return 0;
    }
    return this->m_goods[goodId].currentPrice;
}

const Market::GoodMarketData& Market::marketData(uint16_t goodId) const {
    // Debug-build precondition: callers must pass a valid goodId. Release
    // builds fall back to a static empty entry so a malformed save / mod
    // load can't dereference past m_goods.end() (audit Critical: prior
    // code returned m_goods[goodId] unchecked).
    assert(goodId < this->m_goods.size() && "Market::marketData: goodId out of range");
    if (goodId >= this->m_goods.size()) {
        static const GoodMarketData kEmpty{};
        return kEmpty;
    }
    return this->m_goods[goodId];
}

void Market::setPrice(uint16_t goodId, int32_t price) {
    if (goodId < this->m_goods.size()) {
        this->m_goods[goodId].currentPrice = price;
    }
}

} // namespace aoc::sim

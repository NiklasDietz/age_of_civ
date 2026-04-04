/**
 * @file Market.cpp
 * @brief Supply/demand market price discovery.
 */

#include "aoc/simulation/economy/Market.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

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
    for (GoodMarketData& data : this->m_goods) {
        // Avoid division by zero: if no supply or demand, price drifts toward base
        float supply = static_cast<float>(std::max(data.totalSupply, 1));
        float demand = static_cast<float>(std::max(data.totalDemand, 1));

        // Price = basePrice * (demand/supply)^elasticity
        float ratio = demand / supply;
        float priceFactor = std::pow(ratio, this->elasticity);

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
    }
}

int32_t Market::price(uint16_t goodId) const {
    if (goodId >= this->m_goods.size()) {
        return 0;
    }
    return this->m_goods[goodId].currentPrice;
}

const Market::GoodMarketData& Market::marketData(uint16_t goodId) const {
    return this->m_goods[goodId];
}

} // namespace aoc::sim

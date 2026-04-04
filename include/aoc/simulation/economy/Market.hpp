#pragma once

/**
 * @file Market.hpp
 * @brief Global market with supply/demand-driven price discovery.
 *
 * Each turn, cities report their surplus (supply) and deficits (demand).
 * The market clears by adjusting prices: scarce goods become expensive,
 * abundant goods become cheap. This creates natural trade incentives.
 *
 * Price formula: price = basePrice * (demand / supply) ^ elasticity
 * Clamped to [minPrice, maxPrice] per good to prevent runaway prices.
 */

#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <cstdint>
#include <vector>

namespace aoc::sim {

class Market {
public:
    struct GoodMarketData {
        int32_t        totalSupply  = 0;
        int32_t        totalDemand  = 0;
        int32_t        currentPrice = 0;   ///< Current market price
        int32_t        basePrice    = 0;   ///< Equilibrium price (from GoodDef)

        /// Rolling price history for trend display (most recent last).
        static constexpr int32_t HISTORY_SIZE = 20;
        int32_t priceHistory[HISTORY_SIZE] = {};
        int32_t historyIndex = 0;
    };

    /**
     * @brief Initialize the market with base prices from good definitions.
     */
    void initialize();

    /**
     * @brief Report supply of a good (from city production).
     */
    void reportSupply(uint16_t goodId, int32_t amount);

    /**
     * @brief Report demand for a good (from city consumption / build orders).
     */
    void reportDemand(uint16_t goodId, int32_t amount);

    /**
     * @brief Update all prices based on accumulated supply/demand.
     *
     * Call once per turn after all cities have reported.
     * Resets supply/demand accumulators for the next turn.
     */
    void updatePrices();

    /**
     * @brief Get the current market price for a good.
     */
    [[nodiscard]] int32_t price(uint16_t goodId) const;

    /**
     * @brief Get full market data for a good (for UI display).
     */
    [[nodiscard]] const GoodMarketData& marketData(uint16_t goodId) const;

    /// Price elasticity: higher = more volatile prices.
    float elasticity = 0.5f;

    /// Price floor: goods never go below 20% of base price.
    float minPriceRatio = 0.2f;

    /// Price ceiling: goods never exceed 500% of base price.
    float maxPriceRatio = 5.0f;

private:
    std::vector<GoodMarketData> m_goods;
};

} // namespace aoc::sim

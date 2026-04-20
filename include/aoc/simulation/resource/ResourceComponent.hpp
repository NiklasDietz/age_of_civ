#pragma once

/**
 * @file ResourceComponent.hpp
 * @brief ECS components for tile resources and city stockpiles.
 */

#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>
#include <unordered_map>

namespace aoc::sim {

/// Attached to tiles that contain a harvestable resource.
struct TileResourceComponent {
    uint16_t       goodId;       ///< Which good this tile produces
    int32_t        baseYield;    ///< Base units per turn (before modifiers)
    int32_t        currentYield; ///< After tech/improvement modifiers
};

/// Attached to city entities. Tracks the city's inventory of all goods.
struct CityStockpileComponent {
    /// goodId -> amount in stockpile. Positive = surplus, negative should not occur.
    std::unordered_map<uint16_t, int32_t> goods;

    [[nodiscard]] int32_t getAmount(uint16_t goodId) const {
        std::unordered_map<uint16_t, int32_t>::const_iterator it = this->goods.find(goodId);
        return (it != this->goods.end()) ? it->second : 0;
    }

    void addGoods(uint16_t goodId, int32_t amount) {
        this->goods[goodId] += amount;
    }

    /// Consume goods. Returns true if sufficient amount was available.
    [[nodiscard]] bool consumeGoods(uint16_t goodId, int32_t amount) {
        int32_t current = this->getAmount(goodId);
        if (current < amount) {
            return false;
        }
        this->goods[goodId] = current - amount;
        return true;
    }
};

/// Attached to player entities. Tracks the player's treasury.
struct PlayerEconomyComponent {
    PlayerId       owner = INVALID_PLAYER;
    CurrencyAmount treasury = 100;          ///< Gold / currency on hand
    CurrencyAmount incomePerTurn = 0;       ///< Net income last turn (for display)

    /// Player-wide resource totals (aggregated from all cities each turn).
    std::unordered_map<uint16_t, int32_t> totalSupply;
    std::unordered_map<uint16_t, int32_t> totalDemand;

    /// Per-good: resources the player needs but lacks (computed from recipe
    /// inputs, building fuel, unit requirements, missing luxuries).
    /// Positive = deficit that trade should fill. Updated each turn.
    std::unordered_map<uint16_t, int32_t> totalNeeds;

    /// Number of unique luxury types the player has across all cities.
    int32_t uniqueLuxuryCount = 0;
};

} // namespace aoc::sim

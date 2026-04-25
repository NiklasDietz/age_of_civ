#pragma once

/**
 * @file ResourceComponent.hpp
 * @brief ECS components for tile resources and city stockpiles.
 */

#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/core/Types.hpp"

#include <algorithm>
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

    /// WP-O export buffer: goods committed for trade but not yet picked
    /// up by a trader. Frees stockpile space (incentivizes trading).
    /// Trader load step pulls from this map first; if empty, falls back
    /// to `goods`. If buffer goods sit too long with no pickup, they
    /// trickle back to `goods` (capped) — the surplus that can't fit
    /// is lost (modeled as warehouse spoilage).
    std::unordered_map<uint16_t, int32_t> exportBuffer;
    /// Per-good idle counter: turns since the buffer entry last gained
    /// or shipped a unit. Drives the stale-return mechanic.
    std::unordered_map<uint16_t, int32_t> exportBufferIdleTurns;

    [[nodiscard]] int32_t getAmount(uint16_t goodId) const {
        std::unordered_map<uint16_t, int32_t>::const_iterator it = this->goods.find(goodId);
        return (it != this->goods.end()) ? it->second : 0;
    }

    [[nodiscard]] int32_t getBufferAmount(uint16_t goodId) const {
        auto it = this->exportBuffer.find(goodId);
        return (it != this->exportBuffer.end()) ? it->second : 0;
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

    /// Move N units of `goodId` from `goods` into `exportBuffer`. Frees
    /// stockpile slot space without losing the goods. Returns the actual
    /// quantity moved (capped at current stockpile).
    int32_t commitToExport(uint16_t goodId, int32_t amount) {
        const int32_t current = this->getAmount(goodId);
        const int32_t moved = std::min(amount, current);
        if (moved <= 0) { return 0; }
        this->goods[goodId] = current - moved;
        this->exportBuffer[goodId] += moved;
        this->exportBufferIdleTurns[goodId] = 0;
        return moved;
    }

    /// Pull up to `amount` from buffer (priority) then stockpile. Returns
    /// units actually taken. Used by trader load.
    int32_t pullForExport(uint16_t goodId, int32_t amount) {
        if (amount <= 0) { return 0; }
        int32_t taken = 0;
        auto bufIt = this->exportBuffer.find(goodId);
        if (bufIt != this->exportBuffer.end() && bufIt->second > 0) {
            const int32_t fromBuf = std::min(amount, bufIt->second);
            bufIt->second -= fromBuf;
            taken += fromBuf;
            this->exportBufferIdleTurns[goodId] = 0;
        }
        if (taken < amount) {
            const int32_t remaining = amount - taken;
            const int32_t stock = this->getAmount(goodId);
            const int32_t fromStock = std::min(remaining, stock);
            if (fromStock > 0) {
                this->goods[goodId] = stock - fromStock;
                taken += fromStock;
            }
        }
        return taken;
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

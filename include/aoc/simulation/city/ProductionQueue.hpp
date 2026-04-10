#pragma once

/**
 * @file ProductionQueue.hpp
 * @brief City production queue with multiple concurrent production slots.
 *
 * Base: 1 production slot per city.
 * Bonuses that add slots:
 *   - Industrial district: +1
 *   - Factory building: +1
 *   - Industrial Revolution achieved: +1
 *   - Fiat Money economy: +1
 *   - Maximum: 5 concurrent slots
 *
 * Each slot runs at reduced efficiency when multiple items are active:
 *   1 item:  100% speed
 *   2 items: 60% each (120% total throughput)
 *   3 items: 45% each (135% total)
 *   4 items: 35% each (140% total)
 *   5 items: 30% each (150% total, diminishing returns)
 *
 * The queue also supports templates: repeating build order patterns
 * that automatically re-queue when completed.
 */

#include "aoc/core/Types.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace aoc::sim {

enum class ProductionItemType : uint8_t {
    Unit,
    Building,
    District,
    Wonder,
};

struct ProductionQueueItem {
    ProductionItemType type;
    uint16_t           itemId;      ///< UnitTypeId, BuildingId, or DistrictType (cast)
    std::string        name;
    float              totalCost;   ///< Total production needed
    float              progress;    ///< Production accumulated so far
};

/// A template entry for repeating build orders.
struct ProductionTemplateEntry {
    ProductionItemType type;
    uint16_t           itemId;
    std::string        name;
    float              baseCost;   ///< Base cost (before GamePace multiplier)
};

/// ECS component attached to city entities.
struct ProductionQueueComponent {
    /// Active production items (up to maxSlots can be worked simultaneously).
    std::vector<ProductionQueueItem> queue;

    /// Maximum concurrent production slots for this city (1-5).
    int32_t maxSlots = 1;

    /// Repeating template: when queue empties, refill from this template.
    std::vector<ProductionTemplateEntry> productionTemplate;

    /// Whether the template is active (auto-refill).
    bool templateActive = false;

    [[nodiscard]] bool isEmpty() const { return this->queue.empty(); }

    [[nodiscard]] const ProductionQueueItem* currentItem() const {
        return this->queue.empty() ? nullptr : &this->queue.front();
    }

    /// Number of items currently being worked on (up to maxSlots).
    [[nodiscard]] int32_t activeItemCount() const {
        return std::min(static_cast<int32_t>(this->queue.size()), this->maxSlots);
    }

    /// Production efficiency per item when multiple slots are active.
    /// Splitting workforce has diminishing returns.
    [[nodiscard]] float perItemEfficiency() const {
        int32_t active = this->activeItemCount();
        if (active <= 1) { return 1.0f; }
        switch (active) {
            case 2:  return 0.60f;
            case 3:  return 0.45f;
            case 4:  return 0.35f;
            default: return 0.30f;
        }
    }

    /**
     * @brief Add production progress to all active items simultaneously.
     *
     * Distributes production across active slots with efficiency penalty.
     * Returns a bitmask of which slots completed (bit 0 = first item, etc.).
     * Caller should handle completion for each flagged slot.
     */
    uint32_t addProgressMultiSlot(float totalProduction) {
        if (this->queue.empty()) {
            return 0;
        }

        int32_t active = this->activeItemCount();
        float perItem = totalProduction * this->perItemEfficiency();
        uint32_t completedMask = 0;

        for (int32_t i = 0; i < active; ++i) {
            this->queue[static_cast<std::size_t>(i)].progress += perItem;
            if (this->queue[static_cast<std::size_t>(i)].progress
                >= this->queue[static_cast<std::size_t>(i)].totalCost) {
                completedMask |= (1u << static_cast<uint32_t>(i));
            }
        }
        return completedMask;
    }

    /// Legacy single-slot interface (for backward compatibility).
    bool addProgress(float amount) {
        if (this->queue.empty()) {
            return false;
        }
        this->queue.front().progress += amount;
        return (this->queue.front().progress >= this->queue.front().totalCost);
    }

    /// Remove the completed front item.
    void popCompleted() {
        if (!this->queue.empty()) {
            this->queue.erase(this->queue.begin());
        }
    }

    /// Remove a completed item at a specific index (for multi-slot).
    void popCompletedAt(int32_t index) {
        if (index >= 0 && index < static_cast<int32_t>(this->queue.size())) {
            this->queue.erase(this->queue.begin() + index);
        }
    }
};

/// Compute max production slots for a city based on buildings, tech, and population.
/// Population provides labor (1 slot per 5 pop), buildings provide infrastructure cap.
/// Actual slots = min(labor, infrastructure), max 5.
[[nodiscard]] int32_t computeMaxProductionSlots(bool hasIndustrialDistrict,
                                                 bool hasFactory,
                                                 bool hasIndustrialRevolution,
                                                 bool hasFiatMoney,
                                                 int32_t population);

} // namespace aoc::sim

#pragma once

/**
 * @file ProductionQueue.hpp
 * @brief City production queue ECS component.
 */

#include "aoc/core/Types.hpp"

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

/// ECS component attached to city entities.
struct ProductionQueueComponent {
    std::vector<ProductionQueueItem> queue;

    [[nodiscard]] bool isEmpty() const { return this->queue.empty(); }

    [[nodiscard]] const ProductionQueueItem* currentItem() const {
        return this->queue.empty() ? nullptr : &this->queue.front();
    }

    /// Add production progress. Returns true if the front item completed.
    bool addProgress(float amount) {
        if (this->queue.empty()) {
            return false;
        }
        this->queue.front().progress += amount;
        if (this->queue.front().progress >= this->queue.front().totalCost) {
            return true;  // Caller should handle completion and pop
        }
        return false;
    }

    /// Remove the completed front item.
    void popCompleted() {
        if (!this->queue.empty()) {
            this->queue.erase(this->queue.begin());
        }
    }
};

} // namespace aoc::sim

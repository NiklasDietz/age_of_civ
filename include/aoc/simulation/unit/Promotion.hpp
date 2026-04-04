#pragma once

/**
 * @file Promotion.hpp
 * @brief Unit experience, leveling, and promotion tree.
 */

#include "aoc/core/Types.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace aoc::sim {

struct PromotionDef {
    PromotionId      id;
    std::string_view name;
    int32_t          combatStrengthBonus;
    int32_t          movementBonus;
    int32_t          healingBonus;        ///< Extra HP healed per turn when fortified
    float            terrainDefenseBonus; ///< Additional terrain defense multiplier
};

inline constexpr std::array<PromotionDef, 6> PROMOTION_DEFS = {{
    {PromotionId{0}, "Battlecry",       3, 0, 0, 0.0f},
    {PromotionId{1}, "Tortoise",        0, 0, 0, 0.15f},
    {PromotionId{2}, "Commando",        0, 1, 0, 0.0f},
    {PromotionId{3}, "Medic",           0, 0, 10, 0.0f},
    {PromotionId{4}, "Blitz",           2, 1, 0, 0.0f},
    {PromotionId{5}, "Elite",           5, 0, 5, 0.1f},
}};

/// XP thresholds for each promotion level.
inline constexpr std::array<int32_t, 6> XP_THRESHOLDS = {{15, 30, 60, 100, 150, 225}};

/// ECS component for unit experience and promotions.
struct UnitExperienceComponent {
    int32_t experience = 0;
    int32_t level      = 0;   ///< Number of promotions earned
    std::vector<PromotionId> promotions;

    void addExperience(int32_t xp) {
        this->experience += xp;
    }

    /// Check if the unit has enough XP for the next promotion.
    [[nodiscard]] bool canPromote() const {
        if (this->level >= static_cast<int32_t>(XP_THRESHOLDS.size())) {
            return false;
        }
        return this->experience >= XP_THRESHOLDS[static_cast<std::size_t>(this->level)];
    }

    /// Apply a promotion. Consumes the XP threshold.
    void applyPromotion(PromotionId promo) {
        this->promotions.push_back(promo);
        ++this->level;
    }

    /// Sum combat strength bonus from all promotions.
    [[nodiscard]] int32_t totalCombatBonus() const {
        int32_t total = 0;
        for (PromotionId pid : this->promotions) {
            total += PROMOTION_DEFS[pid.value].combatStrengthBonus;
        }
        return total;
    }

    [[nodiscard]] int32_t totalMovementBonus() const {
        int32_t total = 0;
        for (PromotionId pid : this->promotions) {
            total += PROMOTION_DEFS[pid.value].movementBonus;
        }
        return total;
    }
};

} // namespace aoc::sim

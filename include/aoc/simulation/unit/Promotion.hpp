#pragma once

/**
 * @file Promotion.hpp
 * @brief Unit experience, leveling, and promotion tree.
 */

#include "aoc/core/Types.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

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

    /// Apply a promotion. Consumes the XP threshold so subsequent levels require
    /// fresh XP instead of cascading from a pre-accumulated pool.
    void applyPromotion(PromotionId promo) {
        const auto thresholdIdx = static_cast<std::size_t>(this->level);
        if (thresholdIdx < XP_THRESHOLDS.size()) {
            this->experience -= XP_THRESHOLDS[thresholdIdx];
            if (this->experience < 0) { this->experience = 0; }
        }
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

/// Get available promotions for a unit (ones it hasn't already taken).
[[nodiscard]] inline std::vector<PromotionId> availablePromotions(
    const UnitExperienceComponent& xp) {
    std::vector<PromotionId> available;
    for (std::size_t i = 0; i < PROMOTION_DEFS.size(); ++i) {
        const PromotionId pid = PROMOTION_DEFS[i].id;
        bool alreadyHas = false;
        for (const PromotionId& existing : xp.promotions) {
            if (existing == pid) { alreadyHas = true; break; }
        }
        if (!alreadyHas) {
            available.push_back(pid);
        }
    }
    return available;
}

/// AI auto-selects the best promotion for a unit based on its class.
/// Melee: prefer Battlecry > Blitz > Elite.
/// Ranged: prefer Tortoise > Elite > Medic.
/// Cavalry: prefer Commando > Blitz > Battlecry.
[[nodiscard]] inline PromotionId aiSelectPromotion(
    const UnitExperienceComponent& xp, UnitClass unitClass) {
    const std::vector<PromotionId> available = availablePromotions(xp);
    if (available.empty()) { return PromotionId{0}; }

    // Preference order by unit class
    std::array<uint8_t, 6> preference{};
    switch (unitClass) {
        case UnitClass::Melee:
        case UnitClass::AntiCavalry:
            preference = {0, 4, 5, 1, 3, 2};  // Battlecry, Blitz, Elite...
            break;
        case UnitClass::Ranged:
        case UnitClass::Artillery:
            preference = {1, 5, 3, 0, 4, 2};  // Tortoise, Elite, Medic...
            break;
        case UnitClass::Cavalry:
        case UnitClass::Armor:
            preference = {2, 4, 0, 5, 1, 3};  // Commando, Blitz, Battlecry...
            break;
        default:
            preference = {5, 0, 4, 1, 3, 2};  // Elite first for other types
            break;
    }

    for (uint8_t prefId : preference) {
        const PromotionId pid{prefId};
        for (const PromotionId& avail : available) {
            if (avail == pid) { return pid; }
        }
    }
    return available.front();
}

} // namespace aoc::sim

// Forward-declared game types for the promotion processor (implemented in .cpp).
namespace aoc::game { class Player; }

namespace aoc::sim {

/**
 * @brief Process promotions for all units of a player.
 *
 * For AI players: auto-select and apply the best promotion.
 * For human players: skip (UI prompts for choice).
 */
void processUnitPromotions(aoc::game::Player& player, bool isHuman);

} // namespace aoc::sim

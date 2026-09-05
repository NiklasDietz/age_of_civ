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
    /// Unit classes that may take it: one bit per UnitClass, 0xFFFF = any.
    uint16_t         classMask = 0xFFFF;
    /// Promotion the unit must already hold; invalid = none (a root of the tree).
    PromotionId      prerequisite{};
};

[[nodiscard]] constexpr uint16_t promotionClassBit(UnitClass c) {
    return static_cast<uint16_t>(1u << static_cast<uint8_t>(c));
}
inline constexpr uint16_t PROMO_ANY_CLASS    = 0xFFFF;
inline constexpr uint16_t PROMO_MELEE_LINE   = promotionClassBit(UnitClass::Melee)
                                             | promotionClassBit(UnitClass::AntiCavalry);
inline constexpr uint16_t PROMO_RANGED_LINE  = promotionClassBit(UnitClass::Ranged)
                                             | promotionClassBit(UnitClass::Artillery);
inline constexpr uint16_t PROMO_MOUNTED_LINE = promotionClassBit(UnitClass::Cavalry)
                                             | promotionClassBit(UnitClass::Armor);

/// Append-only: ids are stored in saves. Rows 0-5 are the roots every class may
/// take; rows 6-11 (2026-09-05) are the second tier, each behind one root and,
/// for the class-bound ones, limited to a line.
inline constexpr std::array<PromotionDef, 12> PROMOTION_DEFS = {{
    {PromotionId{0},  "Battlecry",       3, 0, 0, 0.0f},
    {PromotionId{1},  "Tortoise",        0, 0, 0, 0.15f},
    {PromotionId{2},  "Commando",        0, 1, 0, 0.0f},
    {PromotionId{3},  "Medic",           0, 0, 10, 0.0f},
    {PromotionId{4},  "Blitz",           2, 1, 0, 0.0f},
    {PromotionId{5},  "Elite",           5, 0, 5, 0.1f},
    {PromotionId{6},  "Zweihander",      5, 0, 0, 0.0f,  PROMO_MELEE_LINE,   PromotionId{0}},
    {PromotionId{7},  "Camouflage",      0, 0, 0, 0.20f, PROMO_RANGED_LINE,  PromotionId{1}},
    {PromotionId{8},  "Depredation",     2, 1, 0, 0.0f,  PROMO_MOUNTED_LINE, PromotionId{2}},
    {PromotionId{9},  "Survivalism",     0, 0, 10, 0.05f, PROMO_ANY_CLASS,   PromotionId{3}},
    {PromotionId{10}, "Ambush",          4, 0, 0, 0.0f,  PROMO_MOUNTED_LINE, PromotionId{4}},
    {PromotionId{11}, "Legendary",       4, 0, 5, 0.10f, PROMO_ANY_CLASS,    PromotionId{5}},
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

[[nodiscard]] inline bool hasPromotion(const UnitExperienceComponent& xp, PromotionId pid) {
    for (const PromotionId& existing : xp.promotions) {
        if (existing == pid) { return true; }
    }
    return false;
}

/// Promotions `unitClass` may take next: not held yet, open to the class, and
/// with the prerequisite already held.
[[nodiscard]] inline std::vector<PromotionId> availablePromotions(
    const UnitExperienceComponent& xp, UnitClass unitClass) {
    std::vector<PromotionId> available;
    const uint16_t classBit = promotionClassBit(unitClass);
    for (std::size_t i = 0; i < PROMOTION_DEFS.size(); ++i) {
        const PromotionDef& def = PROMOTION_DEFS[i];
        if (hasPromotion(xp, def.id)) { continue; }
        if ((def.classMask & classBit) == 0) { continue; }
        if (def.prerequisite.isValid() && !hasPromotion(xp, def.prerequisite)) { continue; }
        available.push_back(def.id);
    }
    return available;
}

/// What a class values in a promotion: weights per bonus field. Deterministic,
/// no RNG: ties go to the lower id, so append-only rows never reorder picks.
struct PromotionWeights {
    float combat;
    float movement;
    float healing;
    float terrainDefense;
};

[[nodiscard]] constexpr PromotionWeights promotionWeightsFor(UnitClass unitClass) {
    switch (unitClass) {
        case UnitClass::Melee:
        case UnitClass::AntiCavalry: return {3.0f, 1.0f, 0.5f, 10.0f};
        case UnitClass::Ranged:
        case UnitClass::Artillery:   return {2.0f, 1.0f, 0.5f, 25.0f};
        case UnitClass::Cavalry:
        case UnitClass::Armor:       return {2.0f, 4.0f, 0.5f, 5.0f};
        default:                     return {2.0f, 1.0f, 1.0f, 10.0f};
    }
}

[[nodiscard]] constexpr float scorePromotion(const PromotionDef& def, UnitClass unitClass) {
    const PromotionWeights w = promotionWeightsFor(unitClass);
    return w.combat * static_cast<float>(def.combatStrengthBonus)
         + w.movement * static_cast<float>(def.movementBonus)
         + w.healing * static_cast<float>(def.healingBonus)
         + w.terrainDefense * def.terrainDefenseBonus;
}

/// The best available promotion for the class by scorePromotion; PromotionId{0}
/// only when nothing is available (callers check canPromote first).
[[nodiscard]] inline PromotionId aiSelectPromotion(
    const UnitExperienceComponent& xp, UnitClass unitClass) {
    const std::vector<PromotionId> available = availablePromotions(xp, unitClass);
    if (available.empty()) { return PromotionId{0}; }
    PromotionId best = available.front();
    float bestScore  = -1.0f;
    for (const PromotionId pid : available) {
        const float score = scorePromotion(PROMOTION_DEFS[pid.value], unitClass);
        if (score > bestScore) {
            bestScore = score;
            best      = pid;
        }
    }
    return best;
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

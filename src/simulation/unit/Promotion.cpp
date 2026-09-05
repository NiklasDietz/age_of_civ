/**
 * @file Promotion.cpp
 * @brief Unit promotion processing: the scored pick for every seat. The human
 *        was skipped for a choice prompt that never existed, so human units never
 *        promoted until 2026-09-05; a choice UI can replace the auto-pick later.
 */

#include "aoc/simulation/unit/Promotion.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::sim {

void processUnitPromotions(aoc::game::Player& player, bool isHuman) {
    for (const std::unique_ptr<aoc::game::Unit>& unitPtr : player.units()) {
        UnitExperienceComponent& xp = unitPtr->experience();
        if (!xp.canPromote()) { continue; }
        const UnitClass unitClass = unitPtr->typeDef().unitClass;
        if (availablePromotions(xp, unitClass).empty()) { continue; }   // tree exhausted
        const PromotionId chosen = aiSelectPromotion(xp, unitClass);
        xp.applyPromotion(chosen);
        LOG_INFO("%s P%u promoted %.*s: chose %.*s (level %d)", isHuman ? "Human" : "AI",
                 static_cast<unsigned>(player.id()),
                 static_cast<int>(unitPtr->typeDef().name.size()),
                 unitPtr->typeDef().name.data(),
                 static_cast<int>(PROMOTION_DEFS[chosen.value].name.size()),
                 PROMOTION_DEFS[chosen.value].name.data(),
                 xp.level);
    }
}

} // namespace aoc::sim

/**
 * @file Promotion.cpp
 * @brief Unit promotion processing — auto-select for AI, pending for human.
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

        if (isHuman) {
            // Human: UI will prompt for choice. Skip auto-promotion.
            continue;
        }

        // AI: auto-select best promotion for this unit class
        const PromotionId chosen = aiSelectPromotion(xp, unitPtr->typeDef().unitClass);
        xp.applyPromotion(chosen);
        LOG_INFO("AI P%u promoted %.*s: chose %.*s (level %d)",
                 static_cast<unsigned>(player.id()),
                 static_cast<int>(unitPtr->typeDef().name.size()),
                 unitPtr->typeDef().name.data(),
                 static_cast<int>(PROMOTION_DEFS[chosen.value].name.size()),
                 PROMOTION_DEFS[chosen.value].name.data(),
                 xp.level);
    }
}

} // namespace aoc::sim

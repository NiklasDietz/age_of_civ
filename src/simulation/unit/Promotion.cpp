/**
 * @file Promotion.cpp
 * @brief Unit promotion processing: the scored pick for every seat. The human
 *        was skipped for a choice prompt that never existed, so human units never
 *        promoted until 2026-09-05; a choice UI can replace the auto-pick later.
 */

#include "aoc/simulation/unit/Promotion.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::sim {

void processUnitPromotions(aoc::game::Player& player, bool isHuman) {
    if (isHuman) {
        return;   // the human picks through requestPromotion (unit panel, route, MCP)
    }
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

ErrorCode requestPromotion(aoc::game::GameState& gameState, PlayerId player, hex::AxialCoord at,
                           PromotionId promotion) {
    aoc::game::Player* owner = gameState.player(player);
    if (owner == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    aoc::game::Unit* unit = owner->unitAt(at);
    if (unit == nullptr || !promotion.isValid() || promotion.value >= PROMOTION_DEFS.size()) {
        return ErrorCode::InvalidArgument;
    }
    UnitExperienceComponent& xp = unit->experience();
    if (!xp.canPromote()) {
        return ErrorCode::InvalidState;
    }
    bool offered = false;
    for (PromotionId option : availablePromotions(xp, unit->typeDef().unitClass)) {
        if (option == promotion) { offered = true; }
    }
    if (!offered) {
        return ErrorCode::InvalidUnitAction;
    }
    xp.applyPromotion(promotion);
    unit->setMovementRemaining(0);
    LOG_INFO("Player %u promoted %.*s at (%d,%d): %.*s (level %d)", static_cast<unsigned>(player),
             static_cast<int>(unit->typeDef().name.size()), unit->typeDef().name.data(), at.q, at.r,
             static_cast<int>(PROMOTION_DEFS[promotion.value].name.size()),
             PROMOTION_DEFS[promotion.value].name.data(), xp.level);
    return ErrorCode::Ok;
}

int32_t unitsAwaitingPromotion(const aoc::game::Player& player) {
    int32_t n = 0;
    for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
        if (unit->experience().canPromote()
            && !availablePromotions(unit->experience(), unit->typeDef().unitClass).empty()) {
            ++n;
        }
    }
    return n;
}

} // namespace aoc::sim

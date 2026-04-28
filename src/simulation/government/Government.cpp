/**
 * @file Government.cpp
 * @brief Government modifier computation, anarchy, unique actions, corruption.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

GovernmentModifiers computeGovernmentModifiers(
    const PlayerGovernmentComponent& gov) {
    // During anarchy: no bonuses at all
    if (gov.isInAnarchy()) {
        return {};
    }

    // Start with government inherent bonuses
    const GovernmentDef& gdef = governmentDef(gov.government);
    GovernmentModifiers result = gdef.inherentBonuses;

    // Combine active policy card modifiers. Limit iteration to slots
    // actually granted by the current government tier — early govs have
    // fewer slots, so policy cards in higher slots are inert until the
    // player advances to a government with enough slots.
    const uint8_t availableSlots = std::min<uint8_t>(MAX_POLICY_SLOTS,
        static_cast<uint8_t>(gdef.militarySlots + gdef.economicSlots
                              + gdef.diplomaticSlots + gdef.wildcardSlots));
    for (uint8_t slot = 0; slot < availableSlots; ++slot) {
        const int8_t policyId = gov.activePolicies[slot];
        if (policyId == EMPTY_POLICY_SLOT || policyId < 0) {
            continue;
        }
        if (static_cast<uint8_t>(policyId) >= POLICY_CARD_COUNT) {
            continue;
        }

        const PolicyCardDef& pdef = policyCardDef(static_cast<uint8_t>(policyId));
        const GovernmentModifiers& pm = pdef.modifiers;

        // Multipliers: multiply together
        result.productionMultiplier *= pm.productionMultiplier;
        result.goldMultiplier       *= pm.goldMultiplier;
        result.scienceMultiplier    *= pm.scienceMultiplier;
        result.cultureMultiplier    *= pm.cultureMultiplier;
        result.faithMultiplier      *= pm.faithMultiplier;
        result.growthMultiplier     *= pm.growthMultiplier;

        // Flat bonuses: add
        result.combatStrengthBonus      += pm.combatStrengthBonus;
        result.tradeRouteBonus          += pm.tradeRouteBonus;
        result.unitMaintenanceReduction += pm.unitMaintenanceReduction;
        result.productionPerCity        += pm.productionPerCity;
        result.extraTradeRoutes         += pm.extraTradeRoutes;
        result.diplomaticInfluence      += pm.diplomaticInfluence;
        result.corruptionReduction      += pm.corruptionReduction;
        result.loyaltyBonus             += pm.loyaltyBonus;
        result.espionageDefense         += pm.espionageDefense;
        result.tariffEfficiency         += pm.tariffEfficiency;
        result.warWearinessReduction    += pm.warWearinessReduction;
    }

    // Apply active unique action effects
    if (gov.activeAction != GovernmentAction::None && gov.actionTurnsRemaining > 0) {
        switch (gov.activeAction) {
            case GovernmentAction::FiveYearPlan:
                result.productionMultiplier *= 1.30f;
                break;
            case GovernmentAction::RoyalDecree:
                result.goldMultiplier *= 1.15f;
                break;
            case GovernmentAction::HolyWar:
                result.combatStrengthBonus += 4.0f;
                result.faithMultiplier *= 1.20f;
                break;
            case GovernmentAction::Referendum:
                result.loyaltyBonus += 20.0f;
                break;
            case GovernmentAction::TradeFleet:
                result.extraTradeRoutes += 3;
                break;
            default:
                break;
        }
    }

    return result;
}

GovernmentModifiers computeGovernmentModifiers(
    const aoc::game::GameState& gameState, PlayerId player) {
    const aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) {
        return {};
    }
    return computeGovernmentModifiers(gsPlayer->government());
}

ErrorCode executeGovernmentAction(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    PlayerGovernmentComponent& gov = gsPlayer->government();

    GovernmentAction action = governmentUniqueAction(gov.government);
    if (action == GovernmentAction::None) {
        return ErrorCode::InvalidArgument;
    }
    if (gov.activeAction != GovernmentAction::None) {
        return ErrorCode::InvalidArgument;  // Already have an active action
    }
    if (gov.isInAnarchy()) {
        return ErrorCode::InvalidArgument;  // Can't use during anarchy
    }

    gov.activeAction = action;

    switch (action) {
        case GovernmentAction::Referendum:
            gov.actionTurnsRemaining = 5;
            LOG_INFO("Player %u activated Referendum (+20 loyalty for 5 turns)",
                     static_cast<unsigned>(player));
            break;

        case GovernmentAction::FiveYearPlan:
            gov.actionTurnsRemaining = 10;
            LOG_INFO("Player %u activated Five Year Plan (+30%% production for 10 turns)",
                     static_cast<unsigned>(player));
            break;

        case GovernmentAction::Mobilization: {
            gov.actionTurnsRemaining = 1;  // Instant effect
            // Spawn 3 military units at the capital
            for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
                if (city->isOriginalCapital()) {
                    for (int32_t u = 0; u < 3; ++u) {
                        gsPlayer->addUnit(UnitTypeId{0}, city->location());
                    }
                    break;
                }
            }
            LOG_INFO("Player %u activated Mobilization (3 instant military units)",
                     static_cast<unsigned>(player));
            break;
        }

        case GovernmentAction::RoyalDecree:
            gov.actionTurnsRemaining = 10;
            LOG_INFO("Player %u activated Royal Decree (+15%% gold for 10 turns)",
                     static_cast<unsigned>(player));
            break;

        case GovernmentAction::HolyWar:
            gov.actionTurnsRemaining = 10;
            LOG_INFO("Player %u activated Holy War (+4 combat, +20%% faith for 10 turns)",
                     static_cast<unsigned>(player));
            break;

        case GovernmentAction::TradeFleet:
            gov.actionTurnsRemaining = 10;
            LOG_INFO("Player %u activated Trade Fleet (+3 trade routes for 10 turns)",
                     static_cast<unsigned>(player));
            break;

        default:
            break;
    }

    return ErrorCode::Ok;
}

void processGovernment(aoc::game::Player& player) {
    PlayerGovernmentComponent& gov = player.government();

    // Tick anarchy
    if (gov.anarchyTurnsRemaining > 0) {
        --gov.anarchyTurnsRemaining;
        if (gov.anarchyTurnsRemaining == 0) {
            LOG_INFO("Player %u: anarchy ended, %.*s government established",
                     static_cast<unsigned>(player.id()),
                     static_cast<int>(governmentDef(gov.government).name.size()),
                     governmentDef(gov.government).name.data());
        }
    }

    // Tick active action
    if (gov.actionTurnsRemaining > 0) {
        --gov.actionTurnsRemaining;
        if (gov.actionTurnsRemaining <= 0) {
            gov.activeAction = GovernmentAction::None;
        }
    }
}

void equipBestPolicies(PlayerGovernmentComponent& gov) {
    if (gov.isInAnarchy()) { return; }

    const GovernmentDef& gdef = governmentDef(gov.government);

    struct SlotInfo {
        uint8_t        slotIndex;
        PolicySlotType slotType;
    };
    std::array<SlotInfo, MAX_POLICY_SLOTS> slots{};
    uint8_t slotCount = 0;
    uint8_t idx = 0;
    for (uint8_t s = 0; s < gdef.militarySlots   && idx < MAX_POLICY_SLOTS; ++s, ++idx) {
        slots[slotCount++] = {idx, PolicySlotType::Military};
    }
    for (uint8_t s = 0; s < gdef.economicSlots   && idx < MAX_POLICY_SLOTS; ++s, ++idx) {
        slots[slotCount++] = {idx, PolicySlotType::Economic};
    }
    for (uint8_t s = 0; s < gdef.diplomaticSlots && idx < MAX_POLICY_SLOTS; ++s, ++idx) {
        slots[slotCount++] = {idx, PolicySlotType::Diplomatic};
    }
    for (uint8_t s = 0; s < gdef.wildcardSlots   && idx < MAX_POLICY_SLOTS; ++s, ++idx) {
        slots[slotCount++] = {idx, PolicySlotType::Wildcard};
    }

    std::array<bool, POLICY_CARD_COUNT> alreadyEquipped{};
    for (uint8_t s = 0; s < MAX_POLICY_SLOTS; ++s) {
        const int8_t pid = gov.activePolicies[s];
        if (pid != EMPTY_POLICY_SLOT && pid >= 0
            && static_cast<uint8_t>(pid) < POLICY_CARD_COUNT) {
            alreadyEquipped[static_cast<std::size_t>(pid)] = true;
        }
    }

    for (uint8_t i = 0; i < slotCount; ++i) {
        const SlotInfo& slot = slots[i];
        if (gov.activePolicies[slot.slotIndex] != EMPTY_POLICY_SLOT) { continue; }

        int8_t bestPolicy = EMPTY_POLICY_SLOT;
        float  bestValue  = -1.0f;

        for (uint8_t p = 0; p < POLICY_CARD_COUNT; ++p) {
            if (!gov.isPolicyUnlocked(p)) { continue; }
            if (alreadyEquipped[p])       { continue; }

            const PolicyCardDef& pdef = policyCardDef(p);
            if (pdef.slotType != slot.slotType
                && slot.slotType != PolicySlotType::Wildcard) {
                continue;
            }

            const float value = pdef.modifiers.productionMultiplier
                              + pdef.modifiers.goldMultiplier
                              + pdef.modifiers.scienceMultiplier
                              + pdef.modifiers.cultureMultiplier
                              + pdef.modifiers.faithMultiplier
                              + pdef.modifiers.combatStrengthBonus * 0.1f
                              + pdef.modifiers.tradeRouteBonus     * 0.1f
                              + pdef.modifiers.productionPerCity   * 0.2f
                              + pdef.modifiers.loyaltyBonus        * 0.05f;
            if (value > bestValue) {
                bestValue  = value;
                bestPolicy = static_cast<int8_t>(p);
            }
        }

        if (bestPolicy != EMPTY_POLICY_SLOT) {
            gov.activePolicies[slot.slotIndex] = bestPolicy;
            alreadyEquipped[static_cast<std::size_t>(bestPolicy)] = true;
        }
    }
}

} // namespace aoc::sim

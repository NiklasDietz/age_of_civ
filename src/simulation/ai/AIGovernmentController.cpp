/**
 * @file AIGovernmentController.cpp
 * @brief AI government / civic policy turn-step. Extracted from
 *        AIController.cpp on 2026-05-04.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/ai/AIController.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim::ai {

void AIController::manageGovernment(aoc::game::GameState& gameState) {
    aoc::game::Player* gsPlayer = gameState.player(this->m_player);
    if (gsPlayer == nullptr) { return; }

    aoc::sim::PlayerGovernmentComponent& gov = gsPlayer->government();
    const LeaderBehavior& bh = leaderPersonality(gsPlayer->civId()).behavior;

    // Default: keep the highest-id unlocked gov (unlock order = tech progression).
    GovernmentType bestGov = gov.government;
    for (uint8_t g = 0; g < GOVERNMENT_COUNT; ++g) {
        const GovernmentType gt = static_cast<GovernmentType>(g);
        if (gov.isGovernmentUnlocked(gt)) {
            bestGov = gt;
        }
    }

    // Post-industrial ideological pick overrides the linear default when
    // available. High ideologicalFervor = strong commitment; low fervor keeps
    // the pragmatic (default) choice.
    const bool demUnlocked = gov.isGovernmentUnlocked(GovernmentType::Democracy);
    const bool comUnlocked = gov.isGovernmentUnlocked(GovernmentType::Communism);
    const bool fasUnlocked = gov.isGovernmentUnlocked(GovernmentType::Fascism);
    if ((demUnlocked || comUnlocked || fasUnlocked) && bh.ideologicalFervor > 0.8f) {
        // Each ideology has one conditional bonus matched to its character so
        // the final tally doesn't collapse onto one option. Without these,
        // evolved populations (most trustworthiness < 0.6) all pick Communism.
        const float fasScore = bh.militaryAggression + bh.ideologicalFervor
                             + (bh.nukeWillingness > 0.7f ? 0.5f : 0.0f);
        const float demScore = bh.economicFocus + bh.ideologicalFervor
                             + (bh.trustworthiness > 0.7f ? 0.5f : 0.0f);
        const float comScore = bh.expansionism + bh.ideologicalFervor
                             + (bh.trustworthiness < 0.6f ? 0.5f : 0.0f);
        float best = -1.0f;
        if (fasUnlocked && fasScore > best) { best = fasScore; bestGov = GovernmentType::Fascism; }
        if (demUnlocked && demScore > best) { best = demScore; bestGov = GovernmentType::Democracy; }
        if (comUnlocked && comScore > best) { best = comScore; bestGov = GovernmentType::Communism; }
    }
    if (bestGov != gov.government) {
        gov.government = bestGov;
        for (uint8_t s = 0; s < MAX_POLICY_SLOTS; ++s) {
            gov.activePolicies[s] = EMPTY_POLICY_SLOT;
        }
        LOG_INFO("AI %u Adopted government: %.*s",
                 static_cast<unsigned>(this->m_player),
                 static_cast<int>(governmentDef(bestGov).name.size()),
                 governmentDef(bestGov).name.data());
    }

    const GovernmentDef& gdef = governmentDef(gov.government);

    struct SlotInfo {
        uint8_t slotIndex;
        PolicySlotType slotType;
    };
    std::vector<SlotInfo> slots;
    uint8_t slotIdx = 0;
    for (uint8_t s = 0; s < gdef.militarySlots  && slotIdx < MAX_POLICY_SLOTS; ++s, ++slotIdx) {
        slots.push_back({slotIdx, PolicySlotType::Military});
    }
    for (uint8_t s = 0; s < gdef.economicSlots  && slotIdx < MAX_POLICY_SLOTS; ++s, ++slotIdx) {
        slots.push_back({slotIdx, PolicySlotType::Economic});
    }
    for (uint8_t s = 0; s < gdef.diplomaticSlots && slotIdx < MAX_POLICY_SLOTS; ++s, ++slotIdx) {
        slots.push_back({slotIdx, PolicySlotType::Diplomatic});
    }
    for (uint8_t s = 0; s < gdef.wildcardSlots  && slotIdx < MAX_POLICY_SLOTS; ++s, ++slotIdx) {
        slots.push_back({slotIdx, PolicySlotType::Wildcard});
    }

    std::array<bool, POLICY_CARD_COUNT> assigned{};

    for (const SlotInfo& slot : slots) {
        int8_t bestPolicy = EMPTY_POLICY_SLOT;
        float bestValue = -1.0f;

        for (uint8_t p = 0; p < POLICY_CARD_COUNT; ++p) {
            if (!gov.isPolicyUnlocked(p) || assigned[static_cast<std::size_t>(p)]) {
                continue;
            }
            const PolicyCardDef& pdef = policyCardDef(p);
            if (pdef.slotType != slot.slotType &&
                slot.slotType != PolicySlotType::Wildcard) {
                continue;
            }
            float value = pdef.modifiers.productionMultiplier +
                          pdef.modifiers.goldMultiplier +
                          pdef.modifiers.scienceMultiplier +
                          pdef.modifiers.cultureMultiplier +
                          pdef.modifiers.combatStrengthBonus * 0.1f;
            if (value > bestValue) {
                bestValue = value;
                bestPolicy = static_cast<int8_t>(p);
            }
        }

        gov.activePolicies[slot.slotIndex] = bestPolicy;
        if (bestPolicy != EMPTY_POLICY_SLOT) {
            assigned[static_cast<std::size_t>(bestPolicy)] = true;
        }
    }
}

// ============================================================================
// Trade route management
// ============================================================================

} // namespace aoc::sim::ai

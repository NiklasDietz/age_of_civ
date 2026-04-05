/**
 * @file Government.cpp
 * @brief Government modifier computation and policy slot logic.
 */

#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::sim {

GovernmentModifiers computeGovernmentModifiers(
    const aoc::ecs::World& world, PlayerId player) {

    const aoc::ecs::ComponentPool<PlayerGovernmentComponent>* govPool =
        world.getPool<PlayerGovernmentComponent>();
    if (govPool == nullptr) {
        return {};
    }

    // Find this player's government component
    for (uint32_t i = 0; i < govPool->size(); ++i) {
        const PlayerGovernmentComponent& gov = govPool->data()[i];
        if (gov.owner != player) {
            continue;
        }

        // Start with government inherent bonuses
        const GovernmentDef& gdef = governmentDef(gov.government);
        GovernmentModifiers result = gdef.inherentBonuses;

        // Combine active policy card modifiers
        for (uint8_t slot = 0; slot < MAX_POLICY_SLOTS; ++slot) {
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

            // Flat bonuses: add
            result.combatStrengthBonus += pm.combatStrengthBonus;
        }

        return result;
    }

    // No government component found; return defaults
    return {};
}

} // namespace aoc::sim

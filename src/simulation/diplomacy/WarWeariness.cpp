/**
 * @file WarWeariness.cpp
 * @brief War weariness accumulation, decay, and effect computation.
 */

#include "aoc/simulation/diplomacy/WarWeariness.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void processWarWeariness(aoc::ecs::World& world, PlayerId player,
                         const DiplomacyManager& diplomacy) {
    aoc::ecs::ComponentPool<PlayerWarWearinessComponent>* pool =
        world.getPool<PlayerWarWearinessComponent>();
    if (pool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < pool->size(); ++i) {
        PlayerWarWearinessComponent& ww = pool->data()[i];
        if (ww.owner != player) {
            continue;
        }

        bool atWarWithAnyone = false;
        const uint8_t playerCount = diplomacy.playerCount();

        for (uint8_t other = 0; other < playerCount; ++other) {
            if (other == player) {
                continue;
            }

            if (diplomacy.isAtWar(player, other)) {
                atWarWithAnyone = true;
                ww.turnsAtWar[other] += 1;

                // Combat losses approximated by turns at war for simplicity.
                // Each turn at war: weariness += 1 + (combatLosses * 0.5).
                // We approximate combatLosses as 0 for the base tick; the
                // actual combat loss reporting would need hooks from Combat.cpp.
                // For now use a flat per-war increase.
                constexpr float BASE_WEARINESS_PER_TURN = 1.0f;
                ww.weariness += BASE_WEARINESS_PER_TURN;
            } else {
                // Remove from turnsAtWar if peace restored
                ww.turnsAtWar.erase(other);
            }
        }

        // Peace decay
        if (!atWarWithAnyone) {
            constexpr float PEACE_DECAY = 2.0f;
            ww.weariness -= PEACE_DECAY;
        }

        ww.weariness = std::clamp(ww.weariness, 0.0f, 100.0f);

        if (ww.weariness > 0.1f) {
            LOG_INFO("Player %u war weariness: %.1f",
                     static_cast<unsigned>(player),
                     static_cast<double>(ww.weariness));
        }
        return;  // Found the component for this player
    }
}

float warWearinessHappinessPenalty(float weariness) {
    if (weariness < 20.0f) {
        return 0.0f;
    }
    if (weariness < 40.0f) {
        return -1.0f;
    }
    if (weariness < 60.0f) {
        return -2.0f;
    }
    if (weariness < 80.0f) {
        return -3.0f;
    }
    return -5.0f;
}

float warWearinessProductionModifier(float weariness) {
    if (weariness < 60.0f) {
        return 1.0f;
    }
    if (weariness < 80.0f) {
        return 0.9f;   // -10%
    }
    return 0.8f;       // -20%
}

float warWearinessCombatModifier(float weariness) {
    if (weariness < 80.0f) {
        return 1.0f;
    }
    return 0.9f;        // -10%
}

} // namespace aoc::sim

/**
 * @file EraScore.cpp
 * @brief Golden/Dark age logic: score accumulation, era transitions, effects.
 */

#include "aoc/simulation/tech/EraScore.hpp"
#include "aoc/simulation/tech/EraProgression.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void addEraScore(aoc::ecs::World& world, PlayerId player,
                 int32_t points, const std::string& reason) {
    aoc::ecs::ComponentPool<PlayerEraScoreComponent>* pool =
        world.getPool<PlayerEraScoreComponent>();
    if (pool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < pool->size(); ++i) {
        PlayerEraScoreComponent& esc = pool->data()[i];
        if (esc.owner != player) {
            continue;
        }

        esc.eraScore += points;
        LOG_INFO("Player %u era score +%d (%s) => %d",
                 static_cast<unsigned>(player), points,
                 reason.c_str(), esc.eraScore);
        return;
    }
}

void checkEraTransition(aoc::ecs::World& world, PlayerId player) {
    aoc::ecs::ComponentPool<PlayerEraScoreComponent>* pool =
        world.getPool<PlayerEraScoreComponent>();
    if (pool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < pool->size(); ++i) {
        PlayerEraScoreComponent& esc = pool->data()[i];
        if (esc.owner != player) {
            continue;
        }

        constexpr int32_t AGE_DURATION = 10;

        if (esc.eraScore >= esc.goldenAgeThreshold) {
            esc.currentAgeType = AgeType::Golden;
            esc.turnsRemaining = AGE_DURATION;
            LOG_INFO("Player %u enters GOLDEN AGE (score %d >= %d)",
                     static_cast<unsigned>(player),
                     esc.eraScore, esc.goldenAgeThreshold);
        } else if (esc.eraScore < esc.darkAgeThreshold) {
            esc.currentAgeType = AgeType::Dark;
            esc.turnsRemaining = AGE_DURATION;
            LOG_INFO("Player %u enters DARK AGE (score %d < %d)",
                     static_cast<unsigned>(player),
                     esc.eraScore, esc.darkAgeThreshold);
        } else {
            esc.currentAgeType = AgeType::Normal;
            esc.turnsRemaining = 0;
            LOG_INFO("Player %u enters Normal age (score %d)",
                     static_cast<unsigned>(player), esc.eraScore);
        }

        // Reset score for the new era and raise thresholds
        esc.eraScore = 0;
        esc.goldenAgeThreshold += 5;
        esc.darkAgeThreshold += 2;
        return;
    }
}

void processAgeEffects(aoc::ecs::World& world, PlayerId player) {
    aoc::ecs::ComponentPool<PlayerEraScoreComponent>* pool =
        world.getPool<PlayerEraScoreComponent>();
    if (pool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < pool->size(); ++i) {
        PlayerEraScoreComponent& esc = pool->data()[i];
        if (esc.owner != player) {
            continue;
        }

        if (esc.turnsRemaining > 0) {
            --esc.turnsRemaining;
            if (esc.turnsRemaining == 0) {
                LOG_INFO("Player %u age bonus/penalty expired",
                         static_cast<unsigned>(player));
                esc.currentAgeType = AgeType::Normal;
            }
        }
        return;
    }
}

} // namespace aoc::sim

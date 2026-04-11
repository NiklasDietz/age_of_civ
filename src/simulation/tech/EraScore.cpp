/**
 * @file EraScore.cpp
 * @brief Golden/Dark age logic: score accumulation, era transitions, effects.
 *
 * Migrated from ECS to GameState object model.
 */

#include "aoc/simulation/tech/EraScore.hpp"
#include "aoc/simulation/tech/EraProgression.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void addEraScore(aoc::game::Player& player, int32_t points, const std::string& reason) {
    PlayerEraScoreComponent& esc = player.eraScore();
    esc.eraScore += points;
    LOG_INFO("Player %u era score +%d (%s) => %d",
             static_cast<unsigned>(player.id()), points,
             reason.c_str(), esc.eraScore);
}

void checkEraTransition(aoc::game::Player& player) {
    PlayerEraScoreComponent& esc = player.eraScore();

    constexpr int32_t AGE_DURATION = 10;

    if (esc.eraScore >= esc.goldenAgeThreshold) {
        esc.currentAgeType = AgeType::Golden;
        esc.turnsRemaining = AGE_DURATION;
        LOG_INFO("Player %u enters GOLDEN AGE (score %d >= %d)",
                 static_cast<unsigned>(player.id()),
                 esc.eraScore, esc.goldenAgeThreshold);
    } else if (esc.eraScore < esc.darkAgeThreshold) {
        esc.currentAgeType = AgeType::Dark;
        esc.turnsRemaining = AGE_DURATION;
        LOG_INFO("Player %u enters DARK AGE (score %d < %d)",
                 static_cast<unsigned>(player.id()),
                 esc.eraScore, esc.darkAgeThreshold);
    } else {
        esc.currentAgeType = AgeType::Normal;
        esc.turnsRemaining = 0;
    }

    esc.eraScore = 0;
    esc.goldenAgeThreshold += 5;
    esc.darkAgeThreshold += 2;
}

void processAgeEffects(aoc::game::Player& player) {
    PlayerEraScoreComponent& esc = player.eraScore();

    if (esc.turnsRemaining > 0) {
        --esc.turnsRemaining;
        if (esc.turnsRemaining == 0) {
            LOG_INFO("Player %u age bonus/penalty expired",
                     static_cast<unsigned>(player.id()));
            esc.currentAgeType = AgeType::Normal;
        }
    }
}

} // namespace aoc::sim

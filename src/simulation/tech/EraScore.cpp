/**
 * @file EraScore.cpp
 * @brief Golden/Dark age logic: score accumulation, era transitions, effects.
 *
 * Migrated from ECS to GameState object model.
 */

#include "aoc/simulation/tech/EraScore.hpp"
#include "aoc/simulation/tech/EraProgression.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <memory>

namespace aoc::sim {

// A7: Taj Mahal (WonderId 16) — "+1 era score per Golden Age". On Golden Age
// entry, seed the next accumulation period with a positive head-start so
// subsequent Golden Ages chain more reliably for the owner.
static bool playerOwnsTajMahal(const aoc::game::Player& player) {
    for (const std::unique_ptr<aoc::game::City>& c : player.cities()) {
        if (c->wonders().hasWonder(static_cast<aoc::sim::WonderId>(16))) {
            return true;
        }
    }
    return false;
}

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

        // A7 Taj Mahal: owner banks +3 era score for the next period so
        // Golden Ages tend to chain. Applied AFTER the score-reset below.
        if (playerOwnsTajMahal(player)) {
            esc.eraScore = 0;
            esc.goldenAgeThreshold += 5;
            esc.darkAgeThreshold += 2;
            esc.eraScore += 3;
            LOG_INFO("Player %u Taj Mahal: +3 era score carry-over",
                     static_cast<unsigned>(player.id()));
            return;
        }
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

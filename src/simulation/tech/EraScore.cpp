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

// A2 (2026-09-03): these thresholds used to ratchet unconditionally at the tail
// of every checkEraTransition call (+5 golden / +2 dark, every 10 turns) with no
// cap. Since era score resets to 0 each period, a player who fell behind could
// never catch up -- the bar outran any achievable score and every subsequent
// period was a Dark Age, which drove loyalty down (CityLoyalty applies -25% own
// / +25% foreign pressure in a Dark Age) until every civ was eliminated by
// revolution. Derive the bar from the player's era instead: it grows with real
// progress and is bounded by ERA_COUNT.
static constexpr int32_t GOLDEN_AGE_BASE    = 20;
static constexpr int32_t GOLDEN_AGE_PER_ERA = 5;
static constexpr int32_t DARK_AGE_BASE      = 5;
static constexpr int32_t DARK_AGE_PER_ERA   = 2;

static void refreshAgeThresholds(aoc::game::Player& player) {
    PlayerEraScoreComponent& esc = player.eraScore();
    const int32_t era =
        std::min<int32_t>(player.era().currentEra.value, ERA_COUNT - 1);
    esc.goldenAgeThreshold = GOLDEN_AGE_BASE + era * GOLDEN_AGE_PER_ERA;
    esc.darkAgeThreshold   = DARK_AGE_BASE + era * DARK_AGE_PER_ERA;
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

    // A7 Taj Mahal: owner banks +3 era score into the next period so Golden
    // Ages tend to chain. Seeded after the score reset below.
    int32_t carryOver = 0;

    if (esc.eraScore >= esc.goldenAgeThreshold) {
        esc.currentAgeType = AgeType::Golden;
        esc.turnsRemaining = AGE_DURATION;
        LOG_INFO("Player %u enters GOLDEN AGE (score %d >= %d)",
                 static_cast<unsigned>(player.id()),
                 esc.eraScore, esc.goldenAgeThreshold);

        if (playerOwnsTajMahal(player)) {
            carryOver = 3;
            LOG_INFO("Player %u Taj Mahal: +3 era score carry-over",
                     static_cast<unsigned>(player.id()));
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

    esc.eraScore = carryOver;
    refreshAgeThresholds(player);
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

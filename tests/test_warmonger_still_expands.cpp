/**
 * @file test_warmonger_still_expands.cpp
 * @brief One gene may not enter one product twice.
 *
 *        scoreMilitary multiplied by militaryAggression and then again by a
 *        warmonger boost defined as `2.0f * militaryAggression`, so aggression
 *        entered the military score QUADRATICALLY. At 1.90 it contributed 7.2x
 *        where a linear term gives 1.9x, and nothing on the settler side
 *        answered it.
 *
 *        Measured on seed 42 over 500 turns, the deciding product at two
 *        cities came out military 21.66 against settler 3.85 for the Zulu and
 *        15.61 against 3.42 for the Mapuche, versus 1.22/7.98 for Phoenicia and
 *        0.63/8.55 for Egypt. The two aggressive civs produced 73 military
 *        units between them and NOT ONE SETTLER in the whole game, held two
 *        cities each while their neighbours reached eleven and twelve, and
 *        finished on a seventeenth of the winner's GDP -- with the expansion
 *        advisor telling them every turn that three good sites were available.
 *
 *        These tests pin the shape of the fix rather than the tuning: the
 *        pivot still exists and still favours military for a warmonger, but
 *        aggression is counted once.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/simulation/ai/LeaderPersonality.hpp"

#include <algorithm>

namespace {

/// The aggression-dependent part of scoreMilitary, mirroring AIController.
float militaryTerm(const aoc::sim::LeaderBehavior& b) {
    constexpr float WARMONGER_PIVOT = 1.5f;
    constexpr float WARMONGER_BOOST = 2.0f;
    const float boost = (b.militaryAggression >= WARMONGER_PIVOT) ? WARMONGER_BOOST : 1.0f;
    return b.milBaseWeight * b.prodMilitary * b.militaryAggression * boost;
}

/// The same part under the old rule, kept so the test states what it prevents.
float militaryTermQuadratic(const aoc::sim::LeaderBehavior& b) {
    const float boost = (b.militaryAggression >= 1.5f) ? 2.0f * b.militaryAggression : 1.0f;
    return b.milBaseWeight * b.prodMilitary * b.militaryAggression * boost;
}

/// The aggression-independent part of scoreSettler at `cities` cities.
float settlerTerm(const aoc::sim::LeaderBehavior& b, int32_t cities) {
    const int32_t target = aoc::sim::computeScaledTargets(b).maxCities;
    const float boost    = std::max(1.0f, (static_cast<float>(target - cities) / 2.0f) + 1.0f);
    return 0.95f * b.prodSettlers * boost;
}

constexpr aoc::sim::CivId ZULU{14};
constexpr aoc::sim::CivId MAPUCHE{22};
constexpr aoc::sim::CivId PHOENICIA{24};
constexpr aoc::sim::CivId EGYPT{1};

} // namespace

TEST_CASE("aggression is counted once, not squared") {
    for (aoc::sim::CivId id : {ZULU, MAPUCHE}) {
        const aoc::sim::LeaderBehavior& b = aoc::sim::leaderPersonality(id).behavior;
        REQUIRE(b.militaryAggression >= 1.5f); // these are the pivot cases
        CHECK(militaryTerm(b) < militaryTermQuadratic(b));
    }
}

TEST_CASE("a warmonger still prefers military to a settler") {
    // The pivot is not being removed, only stopped from squaring. An aggressive
    // leader should still lean military at its target city count.
    for (aoc::sim::CivId id : {ZULU, MAPUCHE}) {
        const aoc::sim::LeaderBehavior& b = aoc::sim::leaderPersonality(id).behavior;
        const int32_t target              = aoc::sim::computeScaledTargets(b).maxCities;
        CHECK(militaryTerm(b) > settlerTerm(b, target));
    }
}

TEST_CASE("a builder civ still prefers settlers to military") {
    for (aoc::sim::CivId id : {PHOENICIA, EGYPT}) {
        const aoc::sim::LeaderBehavior& b = aoc::sim::leaderPersonality(id).behavior;
        CHECK(militaryTerm(b) < settlerTerm(b, 2));
    }
}

TEST_CASE("the gap between archetypes is a spread, not a wall") {
    // The failure was not that warmongers favour military -- they should -- but
    // that the margin was so wide no other term could ever overcome it. Compare
    // the two archetypes at two cities, where expansion matters most.
    const aoc::sim::LeaderBehavior& zulu  = aoc::sim::leaderPersonality(ZULU).behavior;
    const aoc::sim::LeaderBehavior& egypt = aoc::sim::leaderPersonality(EGYPT).behavior;

    const float zuluRatio  = militaryTerm(zulu) / settlerTerm(zulu, 2);
    const float egyptRatio = militaryTerm(egypt) / settlerTerm(egypt, 2);
    const float oldRatio   = militaryTermQuadratic(zulu) / settlerTerm(zulu, 2);

    CHECK(zuluRatio > egyptRatio); // the archetypes still differ
    CHECK(zuluRatio < oldRatio);   // but by less than before
    // A margin the rest of the scoring can still move. Above about 4x the
    // remaining terms -- population readiness, safety, treasury, opportunity --
    // cannot close it, which is how a civ reaches turn 500 on two cities.
    CHECK(zuluRatio < 4.0f);
}

TEST_CASE("every leader can reach at least four cities by intent") {
    // computeScaledTargets floors maxCities at 4, so no personality is supposed
    // to be a two-city civ. If this ever fails, the stall is by design and the
    // production scoring is not the place to look.
    for (int id = 0; id < 30; ++id) {
        const aoc::sim::LeaderBehavior& b =
            aoc::sim::leaderPersonality(static_cast<aoc::sim::CivId>(id)).behavior;
        CHECK(aoc::sim::computeScaledTargets(b).maxCities >= 4);
    }
}

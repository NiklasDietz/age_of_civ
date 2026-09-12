/**
 * @file test_goody_huts.cpp
 * @brief Goody hut rewards do what they say. The Ancient Map was a log line
 *        with no fog-of-war call behind it -- a ten-in-a-hundred roll that gave
 *        literally nothing -- and Oral Tradition, the culture reward, paid 80
 *        gold instead of any culture at all.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/core/Random.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/simulation/map/GoodyHuts.hpp"

using aoc::PlayerId;
using aoc::sim::GOODY_MAP_REVEAL_RADIUS;
using aoc::sim::GoodyHutReward;

namespace {

constexpr aoc::hex::AxialCoord HUT{8, 6};

/// The first seed whose weighted roll yields `want`, or -1.
///
/// Found on a throwaway world so the search does not itself apply a string of
/// other rewards to the world under test -- a Gold hut on the way would leave
/// gold behind and make a "this reward paid no gold" assertion meaningless.
[[nodiscard]] int32_t seedFor(GoodyHutReward want, int32_t attempts = 400) {
    for (int32_t seed = 0; seed < attempts; ++seed) {
        aoc::test::World scratch = aoc::test::makeWorld(1);
        aoc::sim::GoodyHutState huts;
        huts.hutLocations.push_back(HUT);
        aoc::Random rng{static_cast<uint32_t>(seed) + 1u};
        const GoodyHutReward got = aoc::sim::checkAndClaimGoodyHut(
            huts, scratch.gameState, *scratch.gameState.player(PlayerId{0}), HUT, rng,
            scratch.grid, nullptr);
        if (got == want) {
            return seed;
        }
    }
    return -1;
}

/// Apply exactly the reward `want` to `w`, once. Returns false if no seed rolls it.
bool claimOnce(aoc::test::World& w, GoodyHutReward want, aoc::map::FogOfWar* fog) {
    const int32_t seed = seedFor(want);
    if (seed < 0) {
        return false;
    }
    aoc::sim::GoodyHutState huts;
    huts.hutLocations.push_back(HUT);
    aoc::Random rng{static_cast<uint32_t>(seed) + 1u};
    return aoc::sim::checkAndClaimGoodyHut(huts, w.gameState, *w.gameState.player(PlayerId{0}),
                                           HUT, rng, w.grid, fog) == want;
}

} // namespace

TEST_CASE("standing on no hut claims nothing") {
    aoc::test::World w = aoc::test::makeWorld(1);
    aoc::sim::GoodyHutState huts;
    aoc::Random rng{1u};
    const GoodyHutReward got = aoc::sim::checkAndClaimGoodyHut(
        huts, w.gameState, *w.gameState.player(PlayerId{0}), HUT, rng, w.grid, nullptr);
    CHECK(got == GoodyHutReward::Count);
}

TEST_CASE("a claimed hut is consumed") {
    aoc::test::World w = aoc::test::makeWorld(1);
    aoc::sim::GoodyHutState huts;
    huts.hutLocations.push_back(HUT);
    REQUIRE(huts.hasHut(HUT));

    aoc::Random rng{1u};
    [[maybe_unused]] const GoodyHutReward got = aoc::sim::checkAndClaimGoodyHut(
        huts, w.gameState, *w.gameState.player(PlayerId{0}), HUT, rng, w.grid, nullptr);
    CHECK_FALSE(huts.hasHut(HUT));
}

TEST_CASE("the Ancient Map actually reveals tiles") {
    aoc::test::World w = aoc::test::makeWorld(1);
    aoc::map::FogOfWar fog;
    fog.initialize(w.grid.tileCount(), 1);

    // Nothing is revealed to begin with.
    const int32_t hutIdx = w.grid.toIndex(HUT);
    REQUIRE(fog.visibility(PlayerId{0}, hutIdx) == aoc::map::TileVisibility::Unseen);

    REQUIRE(claimOnce(w, GoodyHutReward::MapReveal, &fog));

    // The hut's own tile and its surroundings are now at least revealed.
    CHECK(fog.visibility(PlayerId{0}, hutIdx) != aoc::map::TileVisibility::Unseen);
    int32_t revealed = 0;
    std::vector<aoc::hex::AxialCoord> area;
    aoc::hex::spiral(HUT, GOODY_MAP_REVEAL_RADIUS, std::back_inserter(area));
    for (const aoc::hex::AxialCoord& tile : area) {
        if (!w.grid.isValid(tile)) {
            continue;
        }
        if (fog.visibility(PlayerId{0}, w.grid.toIndex(tile)) != aoc::map::TileVisibility::Unseen) {
            ++revealed;
        }
    }
    CHECK(revealed > 1);
}

TEST_CASE("the Ancient Map is harmless without a fog layer") {
    // Headless runs pass null: there is no fog, so there is nothing to reveal
    // and nothing to crash on.
    aoc::test::World w = aoc::test::makeWorld(1);
    CHECK(claimOnce(w, GoodyHutReward::MapReveal, nullptr));
}

TEST_CASE("Oral Tradition grants culture to a civic in progress, not gold") {
    aoc::test::World w        = aoc::test::makeWorld(1);
    aoc::game::Player& player = *w.gameState.player(PlayerId{0});
    player.setTreasury(0, aoc::sim::MoneyFlow::external());
    // Culture only lands somewhere if something is being researched.
    player.civics().currentResearch = aoc::CivicId{0};
    const float cultureBefore       = player.civics().researchProgress;

    REQUIRE(claimOnce(w, GoodyHutReward::Culture, nullptr));

    // Either progress rose, or the civic completed outright and reset it --
    // both mean the culture landed. Asserting only on progress would fail on
    // a cheap civic that the reward finishes in one go.
    const bool progressed = player.civics().researchProgress > cultureBefore;
    const bool completed  = !player.civics().currentResearch.isValid();
    CHECK((progressed || completed));
    // And it was not quietly turned back into gold.
    CHECK(player.treasury() == 0);
}

TEST_CASE("Oral Tradition falls back to gold when no civic is in progress") {
    // advanceCivicResearch drops culture when nothing is being researched. A
    // reward that can silently amount to nothing is exactly what this item was
    // about, so the empty case must still pay.
    aoc::test::World w        = aoc::test::makeWorld(1);
    aoc::game::Player& player = *w.gameState.player(PlayerId{0});
    player.setTreasury(0, aoc::sim::MoneyFlow::external());
    REQUIRE_FALSE(player.civics().currentResearch.isValid());

    REQUIRE(claimOnce(w, GoodyHutReward::Culture, nullptr));
    CHECK(player.treasury() > 0);
}

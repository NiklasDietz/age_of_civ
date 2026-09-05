/**
 * @file test_research_cost.cpp
 * @brief One effective research cost for the sim and every reader: base cost
 *        scaled by pace and tree depth, the number advanceResearch completes
 *        against and the number the HUD fraction divides by. Until 2026-09-05
 *        the HUD, tech screen, eurekas, Great Scientists, spies and goody huts
 *        all read the unscaled base.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/turn/GameLength.hpp"

using aoc::PlayerId;
using aoc::TechId;

TEST_CASE("effective cost is the base scaled by pace and by completed techs") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::sim::PlayerTechComponent& tech = w.gameState.player(PlayerId{0})->tech();
    const float pace = aoc::sim::GamePace::instance().costMultiplier;
    const TechId mining{0};
    const float base = static_cast<float>(aoc::sim::techDef(mining).researchCost);

    CHECK(aoc::sim::effectiveResearchCost(tech, mining) == doctest::Approx(base * pace));
    for (uint16_t t = 1; t <= 10; ++t) {
        tech.completedTechs[t] = true;
    }
    CHECK(aoc::sim::effectiveResearchCost(tech, mining) == doctest::Approx(base * pace * 1.5f));
    CHECK(aoc::sim::effectiveResearchCost(tech, TechId{}) == 0.0f);
}

TEST_CASE("advanceResearch completes exactly at the effective cost") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::sim::PlayerTechComponent& tech = w.gameState.player(PlayerId{0})->tech();
    const TechId mining{0};
    for (uint16_t t = 1; t <= 4; ++t) {
        tech.completedTechs[t] = true; // depth multiplier 1.2
    }
    tech.currentResearch  = mining;
    tech.researchProgress = 0.0f;
    const float cost = aoc::sim::effectiveResearchCost(tech, mining);
    REQUIRE(cost > 0.0f);

    CHECK_FALSE(aoc::sim::advanceResearch(tech, cost - 0.5f));
    CHECK_FALSE(tech.hasResearched(mining));
    CHECK(aoc::sim::researchFraction(tech) == doctest::Approx((cost - 0.5f) / cost));
    CHECK(aoc::sim::advanceResearch(tech, 0.5f));
    CHECK(tech.hasResearched(mining));
}

TEST_CASE("the HUD fraction is progress over the effective cost, clamped") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::sim::PlayerTechComponent& tech = w.gameState.player(PlayerId{0})->tech();
    CHECK(aoc::sim::researchFraction(tech) == 0.0f); // nothing researched yet
    const TechId mining{0};
    tech.currentResearch  = mining;
    tech.researchProgress = aoc::sim::effectiveResearchCost(tech, mining) * 0.25f;
    CHECK(aoc::sim::researchFraction(tech) == doctest::Approx(0.25f));
    tech.researchProgress = aoc::sim::effectiveResearchCost(tech, mining) * 3.0f;
    CHECK(aoc::sim::researchFraction(tech) == 1.0f);
}

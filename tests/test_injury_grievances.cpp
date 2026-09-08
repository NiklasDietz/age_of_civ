/**
 * @file test_injury_grievances.cpp
 * @brief "Combined stress" needs an injury, not a disagreement.
 *
 *        The combined-stress revolt gate is documented as war-weariness AND
 *        grievance count both being high. But IdeologicalDifference is accrued
 *        every turn for every pair of civs holding different late-game
 *        ideologies, and addGrievance REFRESHES an existing entry's timer
 *        rather than appending -- so those never expire while the governments
 *        differ. A plain count therefore sat at >= 2 permanently from mid-game,
 *        and the gate had quietly collapsed to war-weariness alone.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/simulation/diplomacy/Grievance.hpp"

using aoc::PlayerId;
using aoc::sim::GrievanceType;

TEST_CASE("disliking a government is not an injury") {
    aoc::sim::PlayerGrievanceComponent g;
    g.addGrievance(GrievanceType::IdeologicalDifference, PlayerId{1});
    g.addGrievance(GrievanceType::IdeologicalDifference, PlayerId{2});
    g.addGrievance(GrievanceType::IdeologicalDifference, PlayerId{3});

    // Three grievances by the raw count -- enough to satisfy a ">= 2" gate on
    // its own, from nothing anyone did.
    CHECK(g.grievances.size() == 3);
    CHECK(g.injuryGrievanceCount() == 0);
}

TEST_CASE("things a rival actually did are counted") {
    aoc::sim::PlayerGrievanceComponent g;
    g.addGrievance(GrievanceType::ConqueredCity, PlayerId{1});
    g.addGrievance(GrievanceType::BrokePromise, PlayerId{1});
    CHECK(g.injuryGrievanceCount() == 2);

    // And ideology on top does not inflate the figure.
    g.addGrievance(GrievanceType::IdeologicalDifference, PlayerId{2});
    CHECK(g.grievances.size() == 3);
    CHECK(g.injuryGrievanceCount() == 2);
}

TEST_CASE("an ideological grievance never expires while it keeps being accrued") {
    // This is why excluding it matters: ticking cannot clear it.
    aoc::sim::PlayerGrievanceComponent g;
    g.addGrievance(GrievanceType::IdeologicalDifference, PlayerId{1});
    for (int32_t turn = 0; turn < 100; ++turn) {
        g.tickGrievances();
        g.addGrievance(GrievanceType::IdeologicalDifference, PlayerId{1});
    }
    CHECK(g.grievances.size() == 1);
    CHECK(g.injuryGrievanceCount() == 0);
}

TEST_CASE("an injury does expire on its own") {
    // The contrast: a real grievance ages out, so a civ that was wronged long
    // ago is no longer counted as stressed.
    aoc::sim::PlayerGrievanceComponent g;
    g.addGrievance(GrievanceType::DMZViolation, PlayerId{1});
    REQUIRE(g.injuryGrievanceCount() == 1);
    for (int32_t turn = 0; turn < 200; ++turn) {
        g.tickGrievances();
    }
    CHECK(g.injuryGrievanceCount() == 0);
}

/**
 * @file test_robot_maintenance.cpp
 * @brief Robot workers are charged upkeep once per turn, not twice. Until
 *        2026-09-07 `updateCityAutomation` (production/Automation.cpp) and an
 *        identical inline copy in `EconomySimulation.cpp` both ran each turn,
 *        so `turnsSinceLastMaintenance` advanced twice and a robot was consumed
 *        every ROBOT_MAINTENANCE_INTERVAL / 2 turns.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/production/Automation.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"

using aoc::PlayerId;
using aoc::sim::ROBOT_MAINTENANCE_INTERVAL;
using aoc::sim::ROBOT_WORKERS_GOOD;
using aoc::sim::updateCityAutomation;

namespace {

aoc::game::City& cityWithRobots(aoc::test::World& w, int32_t robots) {
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::game::City& city = *w.gameState.player(PlayerId{0})->cities()[0];
    city.stockpile().addGoods(ROBOT_WORKERS_GOOD, robots);
    return city;
}

} // namespace

TEST_CASE("one turn advances the maintenance counter by exactly one") {
    aoc::test::World w    = aoc::test::makeWorld(1);
    aoc::game::City& city = cityWithRobots(w, 10);

    CHECK(city.automation().turnsSinceLastMaintenance == 0);
    updateCityAutomation(city);
    CHECK(city.automation().turnsSinceLastMaintenance == 1);
    updateCityAutomation(city);
    CHECK(city.automation().turnsSinceLastMaintenance == 2);
}

TEST_CASE("a robot is consumed once per ROBOT_MAINTENANCE_INTERVAL turns") {
    aoc::test::World w         = aoc::test::makeWorld(1);
    constexpr int32_t STARTING = 10;
    aoc::game::City& city      = cityWithRobots(w, STARTING);

    // Just short of the interval: nothing has been consumed yet.
    for (int32_t t = 0; t < ROBOT_MAINTENANCE_INTERVAL - 1; ++t) {
        updateCityAutomation(city);
    }
    CHECK(city.stockpile().getAmount(ROBOT_WORKERS_GOOD) == STARTING);

    // The interval turn charges exactly one robot.
    updateCityAutomation(city);
    CHECK(city.stockpile().getAmount(ROBOT_WORKERS_GOOD) == STARTING - 1);
    CHECK(city.automation().turnsSinceLastMaintenance == 0);
}

TEST_CASE("upkeep over many turns costs one robot per interval, not two") {
    aoc::test::World w          = aoc::test::makeWorld(1);
    constexpr int32_t STARTING  = 20;
    constexpr int32_t INTERVALS = 3;
    aoc::game::City& city       = cityWithRobots(w, STARTING);

    for (int32_t t = 0; t < ROBOT_MAINTENANCE_INTERVAL * INTERVALS; ++t) {
        updateCityAutomation(city);
    }

    // Exactly INTERVALS robots consumed. The double-tick would have taken 2x.
    CHECK(city.stockpile().getAmount(ROBOT_WORKERS_GOOD) == STARTING - INTERVALS);
}

TEST_CASE("a city with no robots reports none and is charged nothing") {
    aoc::test::World w    = aoc::test::makeWorld(1);
    aoc::game::City& city = cityWithRobots(w, 0);

    for (int32_t t = 0; t < ROBOT_MAINTENANCE_INTERVAL * 2; ++t) {
        updateCityAutomation(city);
    }
    CHECK(city.automation().robotWorkers == 0);
    CHECK(city.stockpile().getAmount(ROBOT_WORKERS_GOOD) == 0);
}

/**
 * @file test_natural_disasters.cpp
 * @brief Disaster records feed the Climate screen and notify the struck owner.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/simulation/climate/NaturalDisasters.hpp"
#include "aoc/simulation/event/GameNotifications.hpp"

#include <vector>

using aoc::PlayerId;
using aoc::sim::DisasterType;

TEST_CASE("a recorded disaster lands in the history and notifies the owner; the history is capped") {
    aoc::test::World w = aoc::test::makeWorld(2);
    static_cast<void>(aoc::sim::event::drainNotifications(PlayerId{0}));
    static_cast<void>(aoc::sim::event::drainNotifications(PlayerId{1}));

    aoc::sim::recordDisaster(w.gameState, DisasterType::Earthquake, 12, {4, 5}, 2, PlayerId{0});
    REQUIRE(w.gameState.disasterHistory().size() == 1);
    CHECK(w.gameState.disasterHistory().front().type == DisasterType::Earthquake);
    CHECK(w.gameState.disasterHistory().front().turn == 12);
    const std::vector<aoc::sim::event::GameNotification> mine = aoc::sim::event::drainNotifications(PlayerId{0});
    REQUIRE(mine.size() == 1);
    CHECK(mine.front().title == "Earthquake");
    CHECK(mine.front().body == "Earthquake at (4,5), severity 2");
    CHECK(mine.front().category == aoc::sim::event::NotificationCategory::Disaster);
    CHECK(aoc::sim::event::drainNotifications(PlayerId{1}).empty());

    // Unclaimed land: recorded, nobody notified.
    aoc::sim::recordDisaster(w.gameState, DisasterType::Wildfire, 13, {9, 9}, 1, aoc::INVALID_PLAYER);
    CHECK(w.gameState.disasterHistory().size() == 2);
    CHECK(aoc::sim::event::drainNotifications(PlayerId{0}).empty());

    for (int32_t t = 0; t < 40; ++t) {
        aoc::sim::recordDisaster(w.gameState, DisasterType::Drought, 20 + t, {1, 1}, 1, aoc::INVALID_PLAYER);
    }
    CHECK(w.gameState.disasterHistory().size() == aoc::sim::MAX_DISASTER_HISTORY);
    CHECK(w.gameState.disasterHistory().back().turn == 59);   // newest kept
    CHECK(w.gameState.disasterHistory().front().turn == 28);  // oldest dropped
}

TEST_CASE("a calm flat world at baseline temperature has no disasters") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    const int32_t count = aoc::sim::processNaturalDisasters(w.gameState, w.grid, 10, 0.0f);
    CHECK(count == 0);
    CHECK(w.gameState.disasterHistory().empty());
}

TEST_CASE("every disaster type has a name") {
    CHECK(aoc::sim::disasterTypeName(DisasterType::VolcanicEruption) == "Volcanic Eruption");
    CHECK(aoc::sim::disasterTypeName(DisasterType::Hurricane) == "Hurricane");
    CHECK(aoc::sim::disasterTypeName(DisasterType::None) == "None");
}

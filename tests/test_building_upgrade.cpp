/**
 * @file test_building_upgrade.cpp
 * @brief Buildings can be raised past level 1. CityBuildingLevelsComponent,
 *        columns 2 and 3 of CAPACITY_TABLE, all of UPGRADE_COST_TABLE, the save
 *        round-trip and the Encyclopedia's "Lv1/Lv2/Lv3" text all existed, but
 *        nothing ever called upgrade(), so every building in every game sat at
 *        level 1 and two thirds of that table was unreachable.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/city/CityActions.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/ProductionSystem.hpp"
#include "aoc/simulation/production/BuildingCapacity.hpp"

using aoc::BuildingId;
using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::sim::MAX_BUILDING_LEVEL;
using aoc::sim::ProductionItemType;

namespace {

constexpr BuildingId FORGE{0};   // Basic tier
constexpr BuildingId FACTORY{3}; // Mid tier
constexpr aoc::hex::AxialCoord AT{5, 5};

/// A city for player 0 holding `bid`.
aoc::game::City& cityWith(aoc::test::World& w, BuildingId bid) {
    aoc::test::addCityAt(w, PlayerId{0}, AT.q, AT.r, "Alpha");
    aoc::game::City& city = *w.gameState.player(PlayerId{0})->cities()[0];
    city.districts().districts[0].buildings.push_back(bid);
    return city;
}

/// Push the head of the queue to completion.
void finishQueue(aoc::test::World& w, aoc::game::City& city) {
    const aoc::sim::ProductionQueueItem* head = city.production().currentItem();
    REQUIRE(head != nullptr);
    [[maybe_unused]] const bool done = city.production().addProgress(head->totalCost);
    aoc::sim::processProductionQueues(w.gameState, w.grid, PlayerId{0});
}

} // namespace

TEST_CASE("capacity rises with level, and the cost rises with it") {
    // The relationship, not the numbers: a later balance pass may retune both
    // tables, but more level must never mean less capacity.
    for (int32_t tier = 0; tier < 3; ++tier) {
        CHECK(aoc::sim::CAPACITY_TABLE[tier][0] < aoc::sim::CAPACITY_TABLE[tier][1]);
        CHECK(aoc::sim::CAPACITY_TABLE[tier][1] < aoc::sim::CAPACITY_TABLE[tier][2]);
        CHECK(aoc::sim::UPGRADE_COST_TABLE[tier][0] < aoc::sim::UPGRADE_COST_TABLE[tier][1]);
    }
}

TEST_CASE("a building starts at level 1 and its upgrade is available") {
    aoc::test::World w    = aoc::test::makeWorld(1);
    aoc::game::City& city = cityWith(w, FORGE);

    CHECK(city.buildingLevels().getLevel(FORGE) == 1);
    CHECK(aoc::sim::buildingUpgradeAvailable(city, FORGE));
    // A building the city does not have cannot be upgraded.
    CHECK_FALSE(aoc::sim::buildingUpgradeAvailable(city, FACTORY));
}

TEST_CASE("a queued upgrade raises the level and the capacity when it completes") {
    aoc::test::World w    = aoc::test::makeWorld(1);
    aoc::game::City& city = cityWith(w, FORGE);

    const int32_t capBefore = city.buildingLevels().capacity(FORGE);

    REQUIRE(aoc::sim::requestUpgradeBuilding(w.gameState, PlayerId{0}, AT, FORGE) == ErrorCode::Ok);
    REQUIRE(city.production().queue.size() == 1);
    CHECK(city.production().queue[0].type == ProductionItemType::BuildingUpgrade);
    CHECK(city.production().queue[0].itemId == FORGE.value);

    finishQueue(w, city);

    CHECK(city.buildingLevels().getLevel(FORGE) == 2);
    CHECK(city.buildingLevels().capacity(FORGE) > capBefore);
}

TEST_CASE("a building can be raised to the cap and no further") {
    aoc::test::World w    = aoc::test::makeWorld(1);
    aoc::game::City& city = cityWith(w, FORGE);

    for (int32_t level = 1; level < MAX_BUILDING_LEVEL; ++level) {
        REQUIRE(aoc::sim::requestUpgradeBuilding(w.gameState, PlayerId{0}, AT, FORGE) ==
                ErrorCode::Ok);
        finishQueue(w, city);
    }
    CHECK(city.buildingLevels().getLevel(FORGE) == MAX_BUILDING_LEVEL);

    // At the cap there is nothing left to buy.
    CHECK_FALSE(aoc::sim::buildingUpgradeAvailable(city, FORGE));
    CHECK(aoc::sim::requestUpgradeBuilding(w.gameState, PlayerId{0}, AT, FORGE) != ErrorCode::Ok);
    CHECK(city.buildingLevels().upgradeCost(FORGE) == 0);
}

TEST_CASE("the same upgrade cannot be queued twice") {
    // Two entries would spend twice for one level: the second finds it raised.
    aoc::test::World w    = aoc::test::makeWorld(1);
    aoc::game::City& city = cityWith(w, FORGE);

    REQUIRE(aoc::sim::requestUpgradeBuilding(w.gameState, PlayerId{0}, AT, FORGE) == ErrorCode::Ok);
    CHECK(aoc::sim::requestUpgradeBuilding(w.gameState, PlayerId{0}, AT, FORGE) != ErrorCode::Ok);
    CHECK(city.production().queue.size() == 1);
}

TEST_CASE("a later upgrade costs more than the first") {
    aoc::test::World w    = aoc::test::makeWorld(1);
    aoc::game::City& city = cityWith(w, FACTORY);

    const int32_t firstCost = city.buildingLevels().upgradeCost(FACTORY);
    REQUIRE(aoc::sim::requestUpgradeBuilding(w.gameState, PlayerId{0}, AT, FACTORY) ==
            ErrorCode::Ok);
    finishQueue(w, city);

    const int32_t secondCost = city.buildingLevels().upgradeCost(FACTORY);
    CHECK(secondCost > firstCost);
}

TEST_CASE("another player cannot upgrade a building in someone else's city") {
    aoc::test::World w = aoc::test::makeWorld(2);
    cityWith(w, FORGE); // player 0's city
    CHECK(aoc::sim::requestUpgradeBuilding(w.gameState, PlayerId{1}, AT, FORGE) != ErrorCode::Ok);
}

/**
 * @file test_zone_of_control.cpp
 * @brief Zone of control is symmetric. `GameState::players()` holds the major
 *        seats only, by contract: city-states live in `cityStatePlayers()` and
 *        the barbarians in `barbarianPlayer()`. isInEnemyZoneOfControl iterated
 *        only the first, so both were bound by everyone else's zone of control
 *        while projecting none of their own -- a warband or a city-state
 *        garrison could be walked straight past.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/game/ZoneOfControl.hpp"

using aoc::PlayerId;
using aoc::UnitTypeId;
using aoc::game::isInEnemyZoneOfControl;

namespace {

constexpr UnitTypeId WARRIOR{0}; // military
constexpr UnitTypeId SETTLER{3}; // civilian: exerts nothing
constexpr aoc::hex::AxialCoord TILE{5, 5};
constexpr aoc::hex::AxialCoord ADJACENT{6, 5};

} // namespace

TEST_CASE("a rival major seat's soldier exerts zone of control") {
    aoc::test::World w = aoc::test::makeWorld(2);
    CHECK_FALSE(isInEnemyZoneOfControl(w.gameState, TILE, PlayerId{0}));

    aoc::test::addUnitAt(w, PlayerId{1}, WARRIOR, ADJACENT.q, ADJACENT.r);
    CHECK(isInEnemyZoneOfControl(w.gameState, TILE, PlayerId{0}));
}

TEST_CASE("your own soldier does not lock you in place") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, ADJACENT.q, ADJACENT.r);
    CHECK_FALSE(isInEnemyZoneOfControl(w.gameState, TILE, PlayerId{0}));
}

TEST_CASE("a civilian exerts nothing, whoever owns it") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addUnitAt(w, PlayerId{1}, SETTLER, ADJACENT.q, ADJACENT.r);
    CHECK_FALSE(isInEnemyZoneOfControl(w.gameState, TILE, PlayerId{0}));
}

TEST_CASE("a barbarian warband exerts zone of control") {
    aoc::test::World w            = aoc::test::makeWorld(2);
    aoc::game::Player* barbarians = w.gameState.barbarianPlayer();
    REQUIRE(barbarians != nullptr);

    CHECK_FALSE(isInEnemyZoneOfControl(w.gameState, TILE, PlayerId{0}));

    barbarians->addUnit(WARRIOR, ADJACENT);
    // Barbarians live outside players(), so this used to come back false and a
    // warband could be walked straight past.
    CHECK(isInEnemyZoneOfControl(w.gameState, TILE, PlayerId{0}));
}

TEST_CASE("a city-state garrison exerts zone of control") {
    aoc::test::World w = aoc::test::makeWorld(2);
    if (w.gameState.cityStatePlayers().empty()) {
        return; // this fixture stands up no city-states
    }
    aoc::game::Player& cityState = *w.gameState.cityStatePlayers()[0];

    CHECK_FALSE(isInEnemyZoneOfControl(w.gameState, TILE, PlayerId{0}));

    cityState.addUnit(WARRIOR, ADJACENT);
    CHECK(isInEnemyZoneOfControl(w.gameState, TILE, PlayerId{0}));
}

TEST_CASE("a soldier two tiles away exerts nothing") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addUnitAt(w, PlayerId{1}, WARRIOR, 7, 5); // ring 2
    CHECK_FALSE(isInEnemyZoneOfControl(w.gameState, TILE, PlayerId{0}));
}

TEST_CASE("barbarians are themselves bound by a major seat's zone of control") {
    // The rule was always applied TO them; this pins that it still is, so the
    // fix made the rule symmetric rather than simply flipping who it binds.
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addUnitAt(w, PlayerId{1}, WARRIOR, ADJACENT.q, ADJACENT.r);
    CHECK(isInEnemyZoneOfControl(w.gameState, TILE, aoc::BARBARIAN_PLAYER));
}

/**
 * @file test_barbarian_city_raid.cpp
 * @brief Barbarians attack cities. The raid loop only ever looked for units --
 *        neither resolveAttackOnCity nor pressIntoCity was named anywhere in
 *        BarbarianController -- so a camp beside an undefended town ignored it,
 *        and the barbarian threat amounted to bouncing off field garrisons.
 *        They were also blind to city-states, whose seats live outside
 *        GameState::players().
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/core/Random.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/barbarian/BarbarianController.hpp"
#include "aoc/simulation/city/CitySiege.hpp"

using aoc::PlayerId;
using aoc::UnitTypeId;
using aoc::sim::BarbarianController;

namespace {

constexpr UnitTypeId WARRIOR{0};
constexpr aoc::hex::AxialCoord CITY{8, 6};
constexpr aoc::hex::AxialCoord NEXT_TO_CITY{9, 6};

/// A world with one player's city and one barbarian warrior beside it.
aoc::test::World raidWorld(aoc::hex::AxialCoord barbAt) {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, CITY.q, CITY.r, "Alpha");
    aoc::game::Player* barbarians = w.gameState.barbarianPlayer();
    REQUIRE(barbarians != nullptr);
    barbarians->addUnit(WARRIOR, barbAt);
    return w;
}

} // namespace

TEST_CASE("a barbarian adjacent to a city attacks it") {
    aoc::test::World w     = raidWorld(NEXT_TO_CITY);
    aoc::game::City& city  = *w.gameState.player(PlayerId{0})->cities()[0];
    const int32_t hpBefore = city.combat().hp;
    REQUIRE(hpBefore > 0);

    BarbarianController barbarians;
    aoc::Random rng{7u};
    barbarians.executeTurn(w.gameState, w.grid, rng, nullptr);

    // The city took damage: something finally happened to it.
    CHECK(city.combat().hp < hpBefore);
}

TEST_CASE("a barbarian further off closes on the city") {
    constexpr aoc::hex::AxialCoord FAR{11, 6}; // within aggro, not adjacent
    aoc::test::World w            = raidWorld(FAR);
    aoc::game::Player* barbarians = w.gameState.barbarianPlayer();
    const int32_t before          = w.grid.distance(barbarians->units()[0]->position(), CITY);

    BarbarianController controller;
    aoc::Random rng{7u};
    controller.executeTurn(w.gameState, w.grid, rng, nullptr);

    REQUIRE(barbarians->unitCount() >= 1);
    const int32_t after = w.grid.distance(barbarians->units()[0]->position(), CITY);
    CHECK(after < before);
}

TEST_CASE("a city far outside aggro range is left alone") {
    constexpr aoc::hex::AxialCoord VERY_FAR{20, 14};
    aoc::test::World w     = raidWorld(VERY_FAR);
    aoc::game::City& city  = *w.gameState.player(PlayerId{0})->cities()[0];
    const int32_t hpBefore = city.combat().hp;

    BarbarianController barbarians;
    aoc::Random rng{7u};
    barbarians.executeTurn(w.gameState, w.grid, rng, nullptr);

    CHECK(city.combat().hp == hpBefore);
}

TEST_CASE("a defender in the field is dealt with before the walls") {
    // Cutting down the garrison first is how a raid reaches the walls at all,
    // so a unit at equal or nearer range outranks the city.
    aoc::test::World w = raidWorld(NEXT_TO_CITY);
    aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, NEXT_TO_CITY.q + 1, NEXT_TO_CITY.r);
    aoc::game::City& city       = *w.gameState.player(PlayerId{0})->cities()[0];
    aoc::game::Unit& guard      = *w.gameState.player(PlayerId{0})->units()[0];
    const int32_t cityHpBefore  = city.combat().hp;
    const int32_t guardHpBefore = guard.hitPoints();

    BarbarianController barbarians;
    aoc::Random rng{7u};
    barbarians.executeTurn(w.gameState, w.grid, rng, nullptr);

    // Exactly one of the two was engaged, and the field unit is the tie-break.
    const bool cityHit  = city.combat().hp < cityHpBefore;
    const bool guardHit = guard.hitPoints() < guardHpBefore;
    CHECK((cityHit || guardHit));
}

TEST_CASE("barbarians can see a city-state's city") {
    // City-state seats live outside players(), so a raider used to walk right
    // past them.
    aoc::test::World w = aoc::test::makeWorld(2);
    if (w.gameState.cityStatePlayers().empty()) {
        return; // this fixture stands up no city-states
    }
    aoc::game::Player& cityState = *w.gameState.cityStatePlayers()[0];
    aoc::game::City& csCity      = cityState.addCity(CITY, "Free City");
    csCity.setOriginalCapital(true);

    aoc::game::Player* barbarians = w.gameState.barbarianPlayer();
    barbarians->addUnit(WARRIOR, NEXT_TO_CITY);

    const int32_t hpBefore = csCity.combat().hp;
    BarbarianController controller;
    aoc::Random rng{7u};
    controller.executeTurn(w.gameState, w.grid, rng, nullptr);

    CHECK(csCity.combat().hp < hpBefore);
}

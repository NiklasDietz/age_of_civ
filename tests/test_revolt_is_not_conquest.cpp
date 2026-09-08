/**
 * @file test_revolt_is_not_conquest.cpp
 * @brief A ten-turn revolt is a loan, not a death sentence.
 *
 *        The combined-stress revolt hands a city to INVALID_PLAYER for ten
 *        turns and gives it back -- "city reverts automatically", per its own
 *        comment. Two bugs turned that into permanent elimination:
 *
 *        civStressed is a CIV-level flag tested per city, with no per-civ
 *        limit, so every unhappy city flipped on the SAME turn. Measured on
 *        seed 42: player 1 lost eleven cities in one turn.
 *
 *        And the conquest check counted those cities as lost, so the civ was
 *        declared "ELIMINATED: conquest" on the spot -- while the only city
 *        actually captured in that entire 500-turn run was taken by
 *        barbarians. One civ died with a city still standing.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/victory/VictoryCondition.hpp"

using aoc::PlayerId;

namespace {

/// Put `city` into the ten-turn free-city revolt state, exactly as
/// computeCityLoyalty's combined-stress branch does.
void revolt(aoc::test::World& w, aoc::game::City& city, PlayerId owner) {
    city.loyalty().revoltFreeCityTurns = 10;
    city.loyalty().revoltOriginalOwner = owner;
    w.gameState.transferCity(city.location(), aoc::INVALID_PLAYER);
}

} // namespace

TEST_CASE("a civ whose capital is merely in revolt is not eliminated") {
    aoc::test::World w        = aoc::test::makeWorld(2);
    aoc::game::Player& p      = *w.gameState.player(PlayerId{0});
    aoc::game::City& capital  = aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Capital");
    aoc::test::addCityAt(w, PlayerId{1}, 15, 9, "Rival");
    REQUIRE(capital.isOriginalCapital());

    // Establish the civ as alive first, so hasEverFoundedCity is set.
    aoc::sim::checkCollapseConditions(w.gameState, 50);
    REQUIRE_FALSE(p.victoryTracker().isEliminated);

    revolt(w, capital, PlayerId{0});
    REQUIRE(capital.owner() != PlayerId{0}); // genuinely not owned right now

    aoc::sim::checkCollapseConditions(w.gameState, 51);
    CHECK_FALSE(p.victoryTracker().isEliminated);
}

TEST_CASE("a capital taken by a rival still eliminates") {
    // The rule this replaces must keep working: losing the capital for real,
    // with nothing left, is elimination.
    aoc::test::World w       = aoc::test::makeWorld(2);
    aoc::game::Player& p     = *w.gameState.player(PlayerId{0});
    aoc::game::City& capital = aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Capital");
    aoc::test::addCityAt(w, PlayerId{1}, 15, 9, "Rival");

    aoc::sim::checkCollapseConditions(w.gameState, 50);
    REQUIRE_FALSE(p.victoryTracker().isEliminated);

    // Captured, not revolted: no revolt bookkeeping, a real new owner.
    w.gameState.transferCity(capital.location(), PlayerId{1});
    aoc::sim::checkCollapseConditions(w.gameState, 51);
    CHECK(p.victoryTracker().isEliminated);
}

TEST_CASE("at most one city revolts per civ per turn") {
    // civStressed is civ-level, so without a limit an entire empire flips at
    // once. Drive the real pass with a civ that is stressed and miserable
    // everywhere, and count what it loses in a single turn.
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});
    for (int32_t i = 0; i < 5; ++i) {
        aoc::test::addCityAt(w, PlayerId{0}, 4 + i * 2, 5, "City" + std::to_string(i));
    }
    aoc::test::addCityAt(w, PlayerId{1}, 19, 11, "Rival");

    // The combined-revolt gate: high weariness, two grievances, unhappy cities.
    p.warWeariness().weariness = 50.0f;
    p.grievances().addGrievance(aoc::sim::GrievanceType::ConqueredCity, PlayerId{1});
    p.grievances().addGrievance(aoc::sim::GrievanceType::BrokePromise, PlayerId{1});
    for (const std::unique_ptr<aoc::game::City>& c : p.cities()) {
        c->happiness().happiness = -5.0f;
    }

    const int32_t before = p.ownedCityCount();
    REQUIRE(before == 5);
    aoc::sim::computeCityLoyalty(w.gameState, w.grid, PlayerId{0});

    int32_t revolting = 0;
    for (const std::unique_ptr<aoc::game::City>& c : p.cities()) {
        if (c->loyalty().revoltFreeCityTurns > 0) { ++revolting; }
    }
    CHECK(revolting <= 1);
}

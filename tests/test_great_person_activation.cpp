/**
 * @file test_great_person_activation.cpp
 * @brief `requestGreatPersonActivation` is the one validated way to use a Great
 *        Person (screen, unit panel, right-click, debug route). Until 2026-09-04 the
 *        only path was a right-click on the tile where the person appeared, and the
 *        underlying `activateGreatPerson` never checked the unit was a Great Person.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/simulation/greatpeople/GreatPeople.hpp"

using aoc::PlayerId;
using aoc::sim::GreatPersonType;

namespace {

constexpr aoc::UnitTypeId WARRIOR{0};
constexpr aoc::UnitTypeId GREAT_PERSON{102};

/// Recruit one General for player 0; it spawns on the capital tile (5,5).
aoc::game::Unit* recruitGeneral(aoc::test::World& w) {
    aoc::game::Player& p = *w.gameState.players()[0];
    p.greatPeople().points[static_cast<uint8_t>(GreatPersonType::General)] =
        p.greatPeople().threshold(GreatPersonType::General) + 1.0f;
    aoc::sim::checkGreatPeopleRecruitment(w.gameState, PlayerId{0});
    for (const std::unique_ptr<aoc::game::Unit>& u : p.units()) {
        if (u->typeId() == GREAT_PERSON) { return u.get(); }
    }
    return nullptr;
}

} // namespace

TEST_CASE("rejects an unknown player, an empty tile, and a unit that is not a Great Person") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, 7, 7);

    CHECK(aoc::sim::requestGreatPersonActivation(w.gameState, w.grid, PlayerId{9}, {5, 5}) ==
          aoc::ErrorCode::InvalidArgument);
    CHECK(aoc::sim::requestGreatPersonActivation(w.gameState, w.grid, PlayerId{0}, {3, 3}) ==
          aoc::ErrorCode::InvalidUnitAction);
    CHECK(aoc::sim::requestGreatPersonActivation(w.gameState, w.grid, PlayerId{0}, {7, 7}) ==
          aoc::ErrorCode::InvalidUnitAction);   // a Warrior, defId 0 would have run the Scientist arm
    CHECK(w.gameState.players()[0]->unitCount() == 1);
}

TEST_CASE("a rival cannot activate someone else's Great Person") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    REQUIRE(recruitGeneral(w) != nullptr);
    CHECK(aoc::sim::requestGreatPersonActivation(w.gameState, w.grid, PlayerId{1}, {5, 5}) ==
          aoc::ErrorCode::InvalidUnitAction);
    CHECK(w.gameState.players()[0]->unitCount() == 1);
}

TEST_CASE("a General heals nearby units where it stands and is consumed") {
    aoc::test::World w = aoc::test::makeWorld(1);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    aoc::game::Unit& wounded = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, 6, 5);
    wounded.setHitPoints(40);
    aoc::game::Unit* general = recruitGeneral(w);
    REQUIRE(general != nullptr);
    // Simulate the person having walked: the recorded spawn position is stale.
    general->greatPerson().position = {0, 0};

    CHECK(aoc::sim::requestGreatPersonActivation(w.gameState, w.grid, PlayerId{0}, {5, 5}) ==
          aoc::ErrorCode::Ok);
    CHECK(wounded.hitPoints() == wounded.typeDef().maxHitPoints);
    CHECK(w.gameState.players()[0]->unitCount() == 1);   // the General is gone, the Warrior stays
    CHECK(aoc::sim::requestGreatPersonActivation(w.gameState, w.grid, PlayerId{0}, {5, 5}) ==
          aoc::ErrorCode::InvalidUnitAction);
}

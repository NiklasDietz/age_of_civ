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
#include "aoc/simulation/religion/Religion.hpp"

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

TEST_CASE("a Great General lends five strength to land units within two hexes") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    aoc::game::Unit* general = recruitGeneral(w);
    REQUIRE(general != nullptr);
    REQUIRE(general->position() == aoc::hex::AxialCoord{5, 5});

    aoc::game::Unit& near = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, 7, 5); // 2 away
    aoc::game::Unit& far  = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, 8, 5); // 3 away
    CHECK(aoc::sim::greatPersonAuraBonus(w.gameState, w.grid, near)
          == doctest::Approx(aoc::sim::GP_AURA_STRENGTH));
    CHECK(aoc::sim::greatPersonAuraBonus(w.gameState, w.grid, far) == doctest::Approx(0.0f));

    // Someone else's units feel nothing.
    aoc::game::Unit& enemy = aoc::test::addUnitAt(w, PlayerId{1}, WARRIOR, 6, 5);
    CHECK(aoc::sim::greatPersonAuraBonus(w.gameState, w.grid, enemy) == doctest::Approx(0.0f));

    // Two generals side by side are still worth one.
    aoc::game::Unit* second = recruitGeneral(w);
    if (second != nullptr) {
        second->setPosition({6, 5});
        CHECK(aoc::sim::greatPersonAuraBonus(w.gameState, w.grid, near)
              == doctest::Approx(aoc::sim::GP_AURA_STRENGTH));
    }
}

TEST_CASE("a General does nothing for ships, and an Admiral nothing for foot soldiers") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    REQUIRE(recruitGeneral(w) != nullptr);
    constexpr aoc::UnitTypeId GALLEY{6};
    aoc::game::Unit& ship = aoc::test::addUnitAt(w, PlayerId{0}, GALLEY, 6, 5);
    CHECK(aoc::sim::greatPersonAuraBonus(w.gameState, w.grid, ship) == doctest::Approx(0.0f));

    aoc::game::Player& p = *w.gameState.players()[0];
    p.greatPeople().points[static_cast<uint8_t>(GreatPersonType::Admiral)] =
        p.greatPeople().threshold(GreatPersonType::Admiral) + 1.0f;
    aoc::sim::checkGreatPeopleRecruitment(w.gameState, PlayerId{0});
    CHECK(aoc::sim::greatPersonAuraBonus(w.gameState, w.grid, ship)
          == doctest::Approx(aoc::sim::GP_AURA_STRENGTH));

    aoc::game::Unit& foot = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, 4, 5);
    CHECK(aoc::sim::greatPersonAuraBonus(w.gameState, w.grid, foot)
          == doctest::Approx(aoc::sim::GP_AURA_STRENGTH)); // the General is still there
}

TEST_CASE("retiring a great person pays gold and era score and takes it off the map") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    REQUIRE(recruitGeneral(w) != nullptr);
    aoc::game::Player& p = *w.gameState.players()[0];
    const int64_t goldBefore = p.monetary().treasury;
    const int32_t scoreBefore = p.victoryTracker().eraVictoryPoints;

    CHECK(aoc::sim::requestRetireGreatPerson(w.gameState, PlayerId{0}, {5, 5})
          == aoc::ErrorCode::Ok);
    CHECK(p.monetary().treasury == goldBefore + aoc::sim::GP_RETIRE_GOLD);
    CHECK(p.victoryTracker().eraVictoryPoints == scoreBefore + aoc::sim::GP_RETIRE_ERA_SCORE);
    CHECK(p.unitCount() == 0);

    // Nothing left to retire, and a plain Warrior is not a great person.
    CHECK(aoc::sim::requestRetireGreatPerson(w.gameState, PlayerId{0}, {5, 5})
          == aoc::ErrorCode::InvalidArgument);
    aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, 7, 7);
    CHECK(aoc::sim::requestRetireGreatPerson(w.gameState, PlayerId{0}, {7, 7})
          == aoc::ErrorCode::InvalidArgument);
}

namespace {

/// Recruit one person of `type` for player 0 and return its unit.
aoc::game::Unit* recruitOf(aoc::test::World& w, GreatPersonType type) {
    aoc::game::Player& p = *w.gameState.players()[0];
    p.greatPeople().points[static_cast<uint8_t>(type)] = p.greatPeople().threshold(type) + 1.0f;
    aoc::sim::checkGreatPeopleRecruitment(w.gameState, PlayerId{0});
    for (const std::unique_ptr<aoc::game::Unit>& u : p.units()) {
        if (u->typeId() == GREAT_PERSON && !u->greatPerson().isActivated) { return u.get(); }
    }
    return nullptr;
}

} // namespace

TEST_CASE("a Prophet founds a religion for a civ that has none") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    aoc::game::Player& p = *w.gameState.players()[0];
    REQUIRE(p.faith().foundedReligion == aoc::sim::NO_RELIGION);
    REQUIRE_FALSE(p.faith().hasPantheon);

    REQUIRE(recruitOf(w, GreatPersonType::Prophet) != nullptr);
    CHECK(aoc::sim::requestGreatPersonActivation(w.gameState, w.grid, PlayerId{0}, {5, 5})
          == aoc::ErrorCode::Ok);
    CHECK(p.faith().hasPantheon);
    CHECK(p.faith().pantheonBelief != 255);                  // a belief was really chosen
    CHECK(p.faith().foundedReligion != aoc::sim::NO_RELIGION); // the religion exists
    CHECK(p.unitCount() == 0);                                // the prophet is spent
}

TEST_CASE("a second Prophet cannot found a second religion, so it leaves its faith") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    aoc::game::Player& p = *w.gameState.players()[0];

    REQUIRE(recruitOf(w, GreatPersonType::Prophet) != nullptr);
    REQUIRE(aoc::sim::requestGreatPersonActivation(w.gameState, w.grid, PlayerId{0}, {5, 5})
            == aoc::ErrorCode::Ok);
    const aoc::sim::ReligionId first = p.faith().foundedReligion;
    REQUIRE(first != aoc::sim::NO_RELIGION);
    const float faithAfterFirst = p.faith().faith;

    REQUIRE(recruitOf(w, GreatPersonType::Prophet) != nullptr);
    CHECK(aoc::sim::requestGreatPersonActivation(w.gameState, w.grid, PlayerId{0}, {5, 5})
          == aoc::ErrorCode::Ok);
    CHECK(p.faith().foundedReligion == first);              // still the same one
    CHECK(p.faith().faith == doctest::Approx(faithAfterFirst + aoc::sim::PROPHET_FAITH));
}

TEST_CASE("a Writer and a Musician push the civics along when no slot is free") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home"); // no Theatre, so no slot
    aoc::game::Player& p = *w.gameState.players()[0];
    p.civics().initialize();

    const float before = p.civics().researchProgress;
    REQUIRE(recruitOf(w, GreatPersonType::Writer) != nullptr);
    CHECK(aoc::sim::requestGreatPersonActivation(w.gameState, w.grid, PlayerId{0}, {5, 5})
          == aoc::ErrorCode::Ok);
    const float afterWriter = p.civics().researchProgress;
    CHECK(afterWriter > before);

    REQUIRE(recruitOf(w, GreatPersonType::Musician) != nullptr);
    CHECK(aoc::sim::requestGreatPersonActivation(w.gameState, w.grid, PlayerId{0}, {5, 5})
          == aoc::ErrorCode::Ok);
    CHECK(p.civics().researchProgress > afterWriter);
}

TEST_CASE("the roster now covers nine types and every one of them can be recruited") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    CHECK(static_cast<int>(GreatPersonType::Count) == 9);
    for (uint8_t t = 0; t < static_cast<uint8_t>(GreatPersonType::Count); ++t) {
        aoc::game::Unit* person = recruitOf(w, static_cast<GreatPersonType>(t));
        CHECK(person != nullptr); // every type has a def behind it
        if (person != nullptr) {
            person->greatPerson().isActivated = true; // park it, recruit the next
        }
    }
}

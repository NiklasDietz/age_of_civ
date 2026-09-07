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
#include "aoc/simulation/wonder/Wonder.hpp"

#include <algorithm>
#include <array>
#include <vector>
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


aoc::game::Unit* recruitOf(aoc::test::World& w, GreatPersonType type) {
    aoc::game::Player& p = *w.gameState.players()[0];
    p.greatPeople().points[static_cast<uint8_t>(type)] = p.greatPeople().threshold(type) + 1.0f;
    aoc::sim::checkGreatPeopleRecruitment(w.gameState, PlayerId{0});
    for (const std::unique_ptr<aoc::game::Unit>& u : p.units()) {
        if (u->typeId() == GREAT_PERSON && !u->greatPerson().isActivated) { return u.get(); }
    }
    return nullptr;
}

/// Recruit people of `type` until one with `wanted` turns up, removing the
/// others outright. Parking them by flag is not enough: great people all spawn
/// on the capital tile, so a parked one still occupies the tile the activation
/// request looks at.
aoc::game::Unit* recruitWithEffect(aoc::test::World& w, GreatPersonType type,
                                   aoc::sim::GreatPersonEffect wanted) {
    aoc::game::Player& p = *w.gameState.players()[0];
    for (int32_t attempt = 0; attempt < 6; ++attempt) {
        aoc::game::Unit* candidate = recruitOf(w, type);
        if (candidate == nullptr) { return nullptr; }
        if (aoc::sim::allGreatPersonDefs()[candidate->greatPerson().defId].effect == wanted) {
            return candidate;
        }
        p.removeUnit(candidate);
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
    // Sun Tzu trains rather than heals, so ask for one that still heals.
    aoc::game::Unit* general =
        recruitWithEffect(w, GreatPersonType::General, aoc::sim::GreatPersonEffect::TypeDefault);
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

    aoc::game::Unit* second = recruitOf(w, GreatPersonType::Prophet);
    REQUIRE(second != nullptr);
    // Each prophet carries its own amount of faith, so read the one we got.
    const float secondFaith =
        aoc::sim::allGreatPersonDefs()[second->greatPerson().defId].faith;
    CHECK(aoc::sim::requestGreatPersonActivation(w.gameState, w.grid, PlayerId{0}, {5, 5})
          == aoc::ErrorCode::Ok);
    CHECK(p.faith().foundedReligion == first);              // still the same one
    CHECK(p.faith().faith == doctest::Approx(faithAfterFirst + secondFaith));
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

TEST_CASE("two great people of the same type do not do the same thing") {
    // Every person of a type used to run the same hard-coded numbers, so Marco
    // Polo and Mansa Musa both handed over exactly 200 gold and the ability
    // text promising otherwise was decoration.
    const std::array<aoc::sim::GreatPersonDef, aoc::sim::GREAT_PERSON_COUNT>& defs =
        aoc::sim::allGreatPersonDefs();

    const auto spread = [&defs](GreatPersonType type, auto pick) {
        std::vector<double> seen;
        for (const aoc::sim::GreatPersonDef& d : defs) {
            if (d.type == type) { seen.push_back(static_cast<double>(pick(d))); }
        }
        REQUIRE(seen.size() >= 2u);
        const auto lo = std::min_element(seen.begin(), seen.end());
        const auto hi = std::max_element(seen.begin(), seen.end());
        return *hi - *lo;
    };

    CHECK(spread(GreatPersonType::Merchant, [](const auto& d) { return d.gold; }) > 0.0);
    CHECK(spread(GreatPersonType::Engineer, [](const auto& d) { return d.production; }) > 0.0);
    CHECK(spread(GreatPersonType::Prophet, [](const auto& d) { return d.faith; }) > 0.0);
    CHECK(spread(GreatPersonType::Scientist,
                 [](const auto& d) { return d.researchFraction; }) > 0.0);
}

TEST_CASE("a merchant hands over its own gold, not a fixed amount") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    aoc::game::Player& p = *w.gameState.players()[0];

    int64_t previousGain = -1;
    bool sawDifferentGain = false;
    for (int32_t n = 0; n < 3; ++n) {
        aoc::game::Unit* merchant = recruitOf(w, GreatPersonType::Merchant);
        if (merchant == nullptr) { break; }
        const int64_t expected =
            aoc::sim::allGreatPersonDefs()[merchant->greatPerson().defId].gold;
        const int64_t before = p.economy().treasury;
        REQUIRE(aoc::sim::requestGreatPersonActivation(w.gameState, w.grid, PlayerId{0}, {5, 5})
                == aoc::ErrorCode::Ok);
        const int64_t gained = p.economy().treasury - before;
        CHECK(gained == expected);            // its own amount, from its own row
        if (previousGain >= 0 && gained != previousGain) { sawDifferentGain = true; }
        previousGain = gained;
    }
    CHECK(sawDifferentGain); // and the amounts really do differ between people
}

TEST_CASE("a wonder draws its own kind of great person, and only that kind") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::City& home = aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    aoc::game::Player& p = *w.gameState.players()[0];

    // Oxford University is a place of learning, not a shipyard.
    constexpr aoc::sim::WonderId OXFORD{19};
    REQUIRE(aoc::sim::greatPersonForWonder(OXFORD) == GreatPersonType::Scientist);
    home.wonders().wonders.push_back(OXFORD);

    const auto pointsFor = [&p](GreatPersonType t) {
        return p.greatPeople().points[static_cast<std::size_t>(t)];
    };
    const float sciBefore = pointsFor(GreatPersonType::Scientist);
    const float admBefore = pointsFor(GreatPersonType::Admiral);

    aoc::sim::accumulateGreatPeoplePoints(w.gameState, PlayerId{0});

    CHECK(pointsFor(GreatPersonType::Scientist)
          == doctest::Approx(sciBefore + aoc::sim::WONDER_GREAT_PERSON_POINTS));
    CHECK(pointsFor(GreatPersonType::Admiral) == doctest::Approx(admBefore));
}

TEST_CASE("the wonder roster draws more than one kind of great person") {
    // A mapping that sent every wonder to the same type would pass the case
    // above and still be useless.
    std::vector<GreatPersonType> drawn;
    for (uint8_t id = 0; id < aoc::sim::WONDER_COUNT; ++id) {
        drawn.push_back(aoc::sim::greatPersonForWonder(static_cast<aoc::sim::WonderId>(id)));
    }
    std::sort(drawn.begin(), drawn.end());
    drawn.erase(std::unique(drawn.begin(), drawn.end()), drawn.end());
    CHECK(drawn.size() >= 6u);
}

TEST_CASE("a person with a unique effect does not take its type's default path") {
    SUBCASE("Sun Tzu trains the troops instead of healing them") {
        aoc::test::World w = aoc::test::makeWorld(2);
        aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
        aoc::game::Unit& soldier = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, 6, 5);
        soldier.setHitPoints(40); // wounded: the General's default would heal this

        aoc::game::Player& p = *w.gameState.players()[0];
        aoc::game::Unit* general = recruitWithEffect(
            w, GreatPersonType::General, aoc::sim::GreatPersonEffect::TrainTroops);
        REQUIRE(general != nullptr);
        const int32_t xpBefore = soldier.experience().experience;

        REQUIRE(aoc::sim::requestGreatPersonActivation(
                    w.gameState, w.grid, PlayerId{0}, general->position()) == aoc::ErrorCode::Ok);

        CHECK(soldier.experience().experience > xpBefore); // trained
        CHECK(soldier.hitPoints() == 40);                  // and pointedly not healed
        static_cast<void>(p);
    }

    SUBCASE("Mansa Musa brings faith where other merchants bring gold") {
        aoc::test::World w = aoc::test::makeWorld(2);
        aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
        aoc::game::Player& p = *w.gameState.players()[0];

        aoc::game::Unit* pilgrim = recruitWithEffect(
            w, GreatPersonType::Merchant, aoc::sim::GreatPersonEffect::Pilgrimage);
        REQUIRE(pilgrim != nullptr);
        const int64_t goldBefore  = p.economy().treasury;
        const float   faithBefore = p.faith().faith;

        REQUIRE(aoc::sim::requestGreatPersonActivation(
                    w.gameState, w.grid, PlayerId{0}, pilgrim->position()) == aoc::ErrorCode::Ok);

        CHECK(p.faith().faith > faithBefore);          // faith arrived
        CHECK(p.economy().treasury == goldBefore);     // and no gold did
    }
}

/**
 * @file test_religion.cpp
 * @brief Religious units carry charges and a religion, founding runs through one
 *        shared path with exclusive beliefs and seeded pressure, the AI founds and
 *        picks beliefs, and an auto-spreading missionary converts a city. Until
 *        2026-09-05 spreadCharges was never assigned, so no religious unit could act.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/automation/Automation.hpp"
#include "aoc/simulation/religion/Religion.hpp"

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::UnitTypeId;

TEST_CASE("religious units are created with charges, their owner's religion and AI auto-spread") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::Player& human = *w.gameState.player(PlayerId{0});
    aoc::game::Player& ai    = *w.gameState.player(PlayerId{1});
    human.faith().foundedReligion = 0;

    const aoc::game::Unit& missionary = aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{19}, 3, 3);
    const aoc::game::Unit& apostle    = aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{20}, 4, 3);
    const aoc::game::Unit& inquisitor = aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{21}, 5, 3);
    const aoc::game::Unit& aiUnit     = aoc::test::addUnitAt(w, PlayerId{1}, UnitTypeId{19}, 9, 9);
    const aoc::game::Unit& warrior    = aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{0}, 6, 3);

    CHECK(missionary.spreadCharges == 3);
    CHECK(apostle.spreadCharges == 4);
    CHECK(inquisitor.spreadCharges == 2);
    CHECK(missionary.spreadingReligion == 0);
    CHECK_FALSE(missionary.autoSpreadReligion);
    CHECK(aiUnit.spreadingReligion == aoc::sim::NO_RELIGION); // the AI has no religion yet
    CHECK(aiUnit.autoSpreadReligion);
    CHECK(warrior.spreadCharges == -1);
    static_cast<void>(ai);
}

TEST_CASE("founding runs through one path: exclusive beliefs and seeded pressure") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::City& home  = aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    aoc::game::Player& p0  = *w.gameState.player(PlayerId{0});
    aoc::game::Player& p1  = *w.gameState.player(PlayerId{1});
    aoc::test::addCityAt(w, PlayerId{1}, 12, 9, "Rival");

    CHECK_FALSE(aoc::sim::foundPantheonFor(w.gameState, PlayerId{0})); // no faith yet
    p0.faith().faith = 30.0f;
    CHECK(aoc::sim::foundPantheonFor(w.gameState, PlayerId{0}));
    CHECK(p0.faith().hasPantheon);
    CHECK(p0.faith().pantheonBelief == 4);
    CHECK(p0.faith().faith == doctest::Approx(5.0f));
    CHECK_FALSE(aoc::sim::foundPantheonFor(w.gameState, PlayerId{0})); // only once

    CHECK(aoc::sim::foundReligionFor(w.gameState, PlayerId{0}) == aoc::sim::NO_RELIGION); // 5 faith
    p0.faith().faith = 60.0f;
    const aoc::sim::ReligionId first = aoc::sim::foundReligionFor(w.gameState, PlayerId{0});
    REQUIRE(first != aoc::sim::NO_RELIGION);
    const aoc::sim::ReligionDef& def = w.gameState.religionTracker().religions[first];
    CHECK(def.founder == PlayerId{0});
    // Beliefs are scored against the leader's priorities, not taken in table
    // order, so assert the CONTRACT -- right type, really free -- rather than
    // the indices a first-come picker happened to produce.
    const auto isOfType = [](uint8_t belief, aoc::sim::BeliefType type) {
        return belief < aoc::sim::BELIEF_COUNT && aoc::sim::allBeliefs()[belief].type == type;
    };
    CHECK(isOfType(def.founderBelief, aoc::sim::BeliefType::Founder));
    CHECK(isOfType(def.followerBelief, aoc::sim::BeliefType::Follower));
    CHECK(isOfType(def.worshipBelief, aoc::sim::BeliefType::Worship));
    CHECK(isOfType(def.enhancerBelief, aoc::sim::BeliefType::Enhancer));
    CHECK(p0.faith().foundedReligion == first);
    CHECK(p0.faith().faith == doctest::Approx(10.0f));
    CHECK(home.religion().pressure[first] == doctest::Approx(5.0f));

    // The second founder gets the next free belief of every type.
    p1.faith().faith = 100.0f;
    CHECK(aoc::sim::foundPantheonFor(w.gameState, PlayerId{1}));
    CHECK(p1.faith().pantheonBelief == 5);
    const aoc::sim::ReligionId second = aoc::sim::foundReligionFor(w.gameState, PlayerId{1});
    REQUIRE(second != aoc::sim::NO_RELIGION);
    CHECK(second != first);
    const aoc::sim::ReligionDef& def2 = w.gameState.religionTracker().religions[second];
    CHECK(isOfType(def2.founderBelief, aoc::sim::BeliefType::Founder));
    CHECK(isOfType(def2.followerBelief, aoc::sim::BeliefType::Follower));
    CHECK(isOfType(def2.worshipBelief, aoc::sim::BeliefType::Worship));
    CHECK(isOfType(def2.enhancerBelief, aoc::sim::BeliefType::Enhancer));
    // Exclusivity is the point of this case: no belief serves two religions.
    CHECK(def2.founderBelief != def.founderBelief);
    CHECK(def2.followerBelief != def.followerBelief);
    CHECK(def2.worshipBelief != def.worshipBelief);
    CHECK(def2.enhancerBelief != def.enhancerBelief);
}

TEST_CASE("the AI founds a pantheon and a religion with beliefs once it has the faith") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{1}, 12, 9, "Rival");
    aoc::game::Player& ai = *w.gameState.player(PlayerId{1});
    ai.faith().faith = 100.0f;
    aoc::sim::processAIReligionFounding(w.gameState);
    CHECK(ai.faith().hasPantheon);
    REQUIRE(ai.faith().foundedReligion != aoc::sim::NO_RELIGION);
    const aoc::sim::ReligionDef& def =
        w.gameState.religionTracker().religions[ai.faith().foundedReligion];
    CHECK(def.founderBelief != 255);
    CHECK(def.followerBelief != 255);
    CHECK(def.worshipBelief != 255);
    CHECK(def.enhancerBelief != 255);
    // The human is left alone: the screen founds for it.
    CHECK_FALSE(w.gameState.player(PlayerId{0})->faith().hasPantheon);
}

TEST_CASE("an auto-spreading missionary converts the city it stands in and spends a charge") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::City& target = aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Target");
    aoc::game::Player& ai   = *w.gameState.player(PlayerId{1});
    ai.faith().foundedReligion = 0;
    static_cast<void>(w.gameState.religionTracker().foundReligion("Faith", PlayerId{1}));
    aoc::game::Unit& missionary = aoc::test::addUnitAt(w, PlayerId{1}, UnitTypeId{19}, 5, 5);
    REQUIRE(missionary.autoSpreadReligion);
    REQUIRE(missionary.spreadingReligion == 0);

    aoc::sim::processAutoSpreadReligion(w.gameState, w.grid, nullptr, PlayerId{1});

    CHECK(target.religion().pressure[0] == doctest::Approx(100.0f));
    CHECK(target.religion().dominantReligion() == 0);
    REQUIRE(ai.unitCount() == 1);
    CHECK(ai.units()[0]->spreadCharges == 2);
}

TEST_CASE("the human founds with chosen beliefs; a taken or wrong-typed belief is refused") {
    aoc::test::World w = aoc::test::makeWorld(3);
    aoc::game::Player& p0 = *w.gameState.player(PlayerId{0});
    aoc::game::Player& p1 = *w.gameState.player(PlayerId{1});
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    p0.faith().faith = 500.0f;
    p1.faith().faith = 500.0f;

    CHECK(aoc::sim::requestFoundPantheon(w.gameState, PlayerId{0}, 0) == ErrorCode::InvalidArgument); // founder type
    CHECK(aoc::sim::requestFoundPantheon(w.gameState, PlayerId{0}, 6) == ErrorCode::Ok);
    CHECK(p0.faith().pantheonBelief == 6);
    CHECK(aoc::sim::requestFoundPantheon(w.gameState, PlayerId{1}, 6) == ErrorCode::InvalidArgument); // taken
    CHECK(aoc::sim::requestFoundPantheon(w.gameState, PlayerId{1}, 5) == ErrorCode::Ok);
    CHECK(aoc::sim::requestFoundPantheon(w.gameState, PlayerId{1}, 4) == ErrorCode::InvalidState);   // has one
    CHECK_FALSE(aoc::sim::beliefIsFree(w.gameState, 6, aoc::sim::BeliefType::Follower));
    CHECK(aoc::sim::beliefIsFree(w.gameState, 7, aoc::sim::BeliefType::Follower));

    aoc::sim::ReligionId id = aoc::sim::NO_RELIGION;
    CHECK(aoc::sim::requestFoundReligion(w.gameState, PlayerId{0}, 4, 9, 14, &id) == ErrorCode::InvalidArgument); // 4 is a follower belief
    CHECK(aoc::sim::requestFoundReligion(w.gameState, PlayerId{0}, 2, 9, 14, &id) == ErrorCode::Ok);
    REQUIRE(id != aoc::sim::NO_RELIGION);
    const aoc::sim::ReligionDef& def = w.gameState.religionTracker().religions[id];
    CHECK(def.founderBelief == 2);
    CHECK(def.followerBelief == 6);
    CHECK(def.worshipBelief == 9);
    CHECK(def.enhancerBelief == 14);
    CHECK(aoc::sim::requestFoundReligion(w.gameState, PlayerId{1}, 2, 10, 15, nullptr) == ErrorCode::InvalidArgument); // founder 2 taken
    CHECK(aoc::sim::requestFoundReligion(w.gameState, PlayerId{1}, 3, 10, 15, nullptr) == ErrorCode::Ok);
}

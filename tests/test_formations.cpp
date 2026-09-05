/**
 * @file test_formations.cpp
 * @brief `requestMergeUnits` is the one validated way to form a Corps / Army (Fleet /
 *        Armada for ships): the civic gates, the reach and type rules, the consumed
 *        source, and the strength multipliers. Until 2026-09-05 the primitives had no
 *        caller, so no formation was ever formed.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/unit/CombatExtensions.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

using aoc::CivicId;
using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::UnitTypeId;
using aoc::hex::AxialCoord;
using aoc::sim::FormationLevel;
using aoc::sim::UnitClass;

namespace {

constexpr PlayerId P0{0};
constexpr UnitTypeId WARRIOR{0};
constexpr UnitTypeId ARCHER{36};
constexpr UnitTypeId BUILDER{5};
constexpr UnitTypeId GALLEY{6};

void grantCivic(aoc::game::Player& player, CivicId civic) {
    aoc::sim::PlayerCivicComponent& c = player.civics();
    if (c.completedCivics.size() <= civic.value) {
        c.completedCivics.resize(civic.value + 1, false);
    }
    c.completedCivics[civic.value] = true;
}

ErrorCode merge(aoc::test::World& w, AxialCoord at, AxialCoord sourceAt) {
    return aoc::sim::requestMergeUnits(w.gameState, P0, at, sourceAt);
}

} // namespace

TEST_CASE(
    "the request rejects unknown seats, missing or mismatched units, bad reach and civilians") {
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(P0);
    grantCivic(p, aoc::sim::FORMATION_CORPS_CIVIC);
    aoc::test::addUnitAt(w, P0, WARRIOR, 5, 5);
    aoc::test::addUnitAt(w, P0, WARRIOR, 7, 5); // two tiles away
    aoc::test::addUnitAt(w, P0, ARCHER, 6, 5);  // adjacent but another type
    aoc::test::addUnitAt(w, P0, BUILDER, 5, 6); // adjacent civilian twin-less
    aoc::test::addUnitAt(w, P0, BUILDER, 4, 6);

    CHECK(aoc::sim::requestMergeUnits(w.gameState, PlayerId{9}, {5, 5}, {6, 5}) ==
          ErrorCode::InvalidArgument);
    CHECK(merge(w, {5, 5}, {5, 5}) == ErrorCode::InvalidArgument);   // itself
    CHECK(merge(w, {5, 5}, {9, 9}) == ErrorCode::InvalidArgument);   // nobody there
    CHECK(merge(w, {5, 5}, {6, 5}) == ErrorCode::InvalidArgument);   // an Archer into a Warrior
    CHECK(merge(w, {5, 5}, {7, 5}) == ErrorCode::InvalidArgument);   // not adjacent
    CHECK(merge(w, {5, 6}, {4, 6}) == ErrorCode::InvalidUnitAction); // Builders are not military
    CHECK(p.unitCount() == 5);                                       // nothing consumed
}

TEST_CASE(
    "Corps needs Nationalism, Army needs Mobilization; the source is consumed and HP is kept") {
    aoc::test::World w    = aoc::test::makeWorld(2);
    aoc::game::Player& p  = *w.gameState.player(P0);
    aoc::game::Unit& lead = aoc::test::addUnitAt(w, P0, WARRIOR, 5, 5);
    aoc::game::Unit& two  = aoc::test::addUnitAt(w, P0, WARRIOR, 6, 5);
    aoc::test::addUnitAt(w, P0, WARRIOR, 5, 6);
    aoc::test::addUnitAt(w, P0, WARRIOR, 4, 5);
    lead.setHitPoints(40);
    two.setHitPoints(90);

    CHECK(merge(w, {5, 5}, {6, 5}) == ErrorCode::InvalidState); // no Nationalism yet
    grantCivic(p, aoc::sim::FORMATION_CORPS_CIVIC);
    REQUIRE(merge(w, {5, 5}, {6, 5}) == ErrorCode::Ok);
    CHECK(lead.formationLevel() == FormationLevel::Corps);
    CHECK(lead.hitPoints() == 90);      // the better HP survives
    CHECK(p.unitAt({6, 5}) == nullptr); // consumed
    CHECK(p.unitCount() == 3);
    CHECK(aoc::sim::formationLabel(UnitClass::Melee, lead.formationLevel()) == "Corps");

    CHECK(merge(w, {5, 5}, {5, 6}) == ErrorCode::InvalidState); // no Mobilization yet
    grantCivic(p, aoc::sim::FORMATION_ARMY_CIVIC);
    REQUIRE(merge(w, {5, 5}, {5, 6}) == ErrorCode::Ok);
    CHECK(lead.formationLevel() == FormationLevel::Army);
    CHECK(aoc::sim::formationLabel(UnitClass::Melee, lead.formationLevel()) == "Army");
    CHECK(merge(w, {5, 5}, {4, 5}) == ErrorCode::InvalidUnitAction); // already an Army
    CHECK(p.unitCount() == 2);

    // A Corps cannot be the source: only Single units merge into others.
    aoc::game::Unit& other = aoc::test::addUnitAt(w, P0, WARRIOR, 10, 5);
    aoc::game::Unit& corps = aoc::test::addUnitAt(w, P0, WARRIOR, 11, 5);
    corps.setFormationLevel(FormationLevel::Corps);
    CHECK(merge(w, {10, 5}, {11, 5}) == ErrorCode::InvalidUnitAction);
    CHECK(other.formationLevel() == FormationLevel::Single);
}

TEST_CASE("ships form a Fleet and an Armada with the same gates and labels") {
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(P0);
    grantCivic(p, aoc::sim::FORMATION_CORPS_CIVIC);
    grantCivic(p, aoc::sim::FORMATION_ARMY_CIVIC);
    aoc::game::Unit& flagship = aoc::test::addUnitAt(w, P0, GALLEY, 8, 8);
    aoc::test::addUnitAt(w, P0, GALLEY, 9, 8);
    aoc::test::addUnitAt(w, P0, GALLEY, 8, 9);

    REQUIRE(merge(w, {8, 8}, {9, 8}) == ErrorCode::Ok);
    CHECK(aoc::sim::formationLabel(UnitClass::Naval, flagship.formationLevel()) == "Fleet");
    REQUIRE(merge(w, {8, 8}, {8, 9}) == ErrorCode::Ok);
    CHECK(aoc::sim::formationLabel(UnitClass::Naval, flagship.formationLevel()) == "Armada");
    CHECK(aoc::sim::formationLabel(UnitClass::Naval, FormationLevel::Single).empty());
}

TEST_CASE("formations multiply combat strength sub-linearly") {
    CHECK(aoc::sim::formationStrengthMultiplier(FormationLevel::Single) == doctest::Approx(1.00f));
    CHECK(aoc::sim::formationStrengthMultiplier(FormationLevel::Corps) == doctest::Approx(1.15f));
    CHECK(aoc::sim::formationStrengthMultiplier(FormationLevel::Army) == doctest::Approx(1.25f));
}

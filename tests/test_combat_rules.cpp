/**
 * @file test_combat_rules.cpp
 * @brief Combat rules fixed 2026-09-05 (Civ VI plan Phase 1.8): flanking counts
 *        military allies only and never the attacker, barbarians teach nothing
 *        past the first promotion, an unsupplied unit fights weaker, a melee
 *        attack on an embarked unit is refused without costing the turn, and a
 *        nuclear strike leaves fallout.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/core/Random.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/unit/AttackRequest.hpp"
#include "aoc/simulation/unit/Combat.hpp"
#include "aoc/simulation/unit/CombatExtensions.hpp"
#include "aoc/simulation/unit/SupplyLines.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::UnitTypeId;
using aoc::hex::AxialCoord;

TEST_CASE("flanking counts military allies only, never the attacker") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::Unit& attacker = aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{0}, 5, 5);
    aoc::game::Unit& defender = aoc::test::addUnitAt(w, PlayerId{1}, UnitTypeId{0}, 6, 5);
    const AxialCoord at = defender.position();
    CHECK(aoc::sim::countAdjacentFriendlies(w.gameState, at, PlayerId{0}, &attacker) == 0);
    aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{5}, 7, 5); // Builder: a civilian
    CHECK(aoc::sim::countAdjacentFriendlies(w.gameState, at, PlayerId{0}, &attacker) == 0);
    aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{0}, 6, 4); // a second Warrior
    CHECK(aoc::sim::countAdjacentFriendlies(w.gameState, at, PlayerId{0}, &attacker) == 1);
}

TEST_CASE("barbarians teach nothing past the first promotion") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::Player* barbarians = w.gameState.barbarianPlayer();
    REQUIRE(barbarians != nullptr);
    aoc::Random rng(7);

    aoc::game::Unit& rookie = aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{0}, 5, 5);
    barbarians->addUnit(UnitTypeId{0}, AxialCoord{6, 5});
    const aoc::sim::CombatResult first = aoc::sim::resolveMeleeCombat(
        w.gameState, rng, w.grid, rookie, *barbarians->unitAt(AxialCoord{6, 5}));
    CHECK(first.attackerXpGained == 5);

    aoc::game::Unit& veteran = aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{0}, 12, 12);
    veteran.experience().level = 1;
    barbarians->addUnit(UnitTypeId{0}, AxialCoord{13, 12});
    const aoc::sim::CombatResult second = aoc::sim::resolveMeleeCombat(
        w.gameState, rng, w.grid, veteran, *barbarians->unitAt(AxialCoord{13, 12}));
    CHECK(second.attackerXpGained == 0);
}

TEST_CASE("an unsupplied attacker deals less damage than a supplied one") {
    int32_t suppliedDamage = 0;
    int32_t unsuppliedDamage = 0;
    for (int pass = 0; pass < 2; ++pass) {
        aoc::test::World w = aoc::test::makeWorld(2);
        aoc::Random rng(11);
        aoc::game::Unit& attacker = aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{0}, 5, 5);
        aoc::game::Unit& defender = aoc::test::addUnitAt(w, PlayerId{1}, UnitTypeId{0}, 6, 5);
        attacker.supply().isSupplied = (pass == 0);
        const aoc::sim::CombatResult r =
            aoc::sim::resolveMeleeCombat(w.gameState, rng, w.grid, attacker, defender);
        (pass == 0 ? suppliedDamage : unsuppliedDamage) = r.defenderDamage;
    }
    CHECK(suppliedDamage > 0);
    CHECK(unsuppliedDamage < suppliedDamage);
}

TEST_CASE("a melee attack on an embarked unit is refused and costs no movement") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::Random rng(1);
    aoc::game::Unit& attacker = aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{0}, 5, 5);
    const AxialCoord sea{6, 5};
    w.grid.setTerrain(w.grid.toIndex(sea), aoc::map::TerrainType::Coast);
    aoc::game::Unit& victim = aoc::test::addUnitAt(w, PlayerId{1}, UnitTypeId{0}, sea.q, sea.r);
    victim.setState(aoc::sim::UnitState::Embarked);
    attacker.refreshMovement();
    const int32_t before = attacker.movementRemaining();
    CHECK(aoc::sim::requestAttack(w.gameState, rng, w.grid, PlayerId{0}, AxialCoord{5, 5}, sea)
          == ErrorCode::InvalidUnitAction);
    CHECK(attacker.movementRemaining() == before);
    CHECK(victim.hitPoints() == victim.typeDef().maxHitPoints);
}

TEST_CASE("a nuclear strike leaves fallout on the blast tiles") {
    aoc::test::World w = aoc::test::makeWorld(2);
    const AxialCoord target{8, 8};
    CHECK(aoc::sim::launchNuclearStrike(w.gameState, w.grid, PlayerId{0}, target,
                                        aoc::sim::NukeType::NuclearDevice) == ErrorCode::Ok);
    CHECK(w.grid.hasFallout(w.grid.toIndex(target)));
    for (const AxialCoord& nbr : aoc::hex::neighbors(target)) {
        if (w.grid.isValid(nbr)) {
            CHECK(w.grid.hasFallout(w.grid.toIndex(nbr)));
        }
    }
}

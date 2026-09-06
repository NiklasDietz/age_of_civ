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

namespace {

constexpr aoc::UnitTypeId BOMBER{51};
constexpr aoc::UnitTypeId WARRIOR_UNIT{0};

/// Player 0 with every nuclear gate open: tech, Manhattan Project, Uranium and
/// a bomber to carry the thing.
struct NukeReady {
    aoc::test::World world = aoc::test::makeWorld(2);
    aoc::game::City* city  = nullptr;

    NukeReady() {
        this->city = &aoc::test::addCityAt(this->world, PlayerId{0}, 4, 4, "Alamogordo");
        aoc::test::addCityAt(this->world, PlayerId{1}, 12, 9, "Target");
        aoc::game::Player& p = *this->world.gameState.players()[0];
        p.tech().initialize();
        p.tech().completedTechs[aoc::sim::NUKE_TECH.value] = true;
        this->city->wonders().wonders.push_back(
            static_cast<aoc::sim::WonderId>(aoc::sim::NUKE_WONDER));
        this->city->stockpile().addGoods(aoc::sim::NUKE_GOOD, 5);
        aoc::test::addUnitAt(this->world, PlayerId{0}, BOMBER, 5, 5);
    }
};

} // namespace

TEST_CASE("a nuclear strike needs the tech, the project, the uranium and a carrier") {
    const AxialCoord target{12, 9};

    SUBCASE("every gate open") {
        NukeReady n;
        CHECK(aoc::sim::nuclearStrikeBlocker(n.world.gameState, n.world.grid, PlayerId{0}, target)
              == ErrorCode::Ok);
    }
    SUBCASE("without Nuclear Fission") {
        NukeReady n;
        n.world.gameState.players()[0]->tech().completedTechs[aoc::sim::NUKE_TECH.value] = false;
        CHECK(aoc::sim::nuclearStrikeBlocker(n.world.gameState, n.world.grid, PlayerId{0}, target)
              == ErrorCode::InvalidState);
    }
    SUBCASE("without the Manhattan Project") {
        NukeReady n;
        n.city->wonders().wonders.clear();
        CHECK(aoc::sim::nuclearStrikeBlocker(n.world.gameState, n.world.grid, PlayerId{0}, target)
              == ErrorCode::InvalidState);
    }
    SUBCASE("without uranium") {
        NukeReady n;
        CHECK(n.city->stockpile().consumeGoods(aoc::sim::NUKE_GOOD, 5));
        CHECK(aoc::sim::nuclearStrikeBlocker(n.world.gameState, n.world.grid, PlayerId{0}, target)
              == ErrorCode::InsufficientResources);
    }
    SUBCASE("without a unit built to carry a warhead") {
        NukeReady n;
        aoc::game::Player& p = *n.world.gameState.players()[0];
        p.removeUnit(p.unitAt({5, 5}));
        aoc::test::addUnitAt(n.world, PlayerId{0}, WARRIOR_UNIT, 5, 5); // a warrior is not a bomber
        CHECK(aoc::sim::nuclearStrikeBlocker(n.world.gameState, n.world.grid, PlayerId{0}, target)
              == ErrorCode::InvalidUnitAction);
    }
}

TEST_CASE("only a bomber, a missile cruiser or a missile sub carries a warhead") {
    CHECK(aoc::sim::canDeliverNuke(aoc::UnitTypeId{51}));  // Bomber
    CHECK(aoc::sim::canDeliverNuke(aoc::UnitTypeId{52}));  // Stealth Bomber
    CHECK(aoc::sim::canDeliverNuke(aoc::UnitTypeId{58}));  // Missile Cruiser
    CHECK(aoc::sim::canDeliverNuke(aoc::UnitTypeId{60}));  // Nuclear Sub
    CHECK_FALSE(aoc::sim::canDeliverNuke(aoc::UnitTypeId{0}));   // Warrior
    CHECK_FALSE(aoc::sim::canDeliverNuke(aoc::UnitTypeId{6}));   // Galley
}

TEST_CASE("a strike spends one uranium and a blocked strike spends nothing") {
    NukeReady n;
    const AxialCoord target{12, 9};
    const int32_t before = n.city->stockpile().getAmount(aoc::sim::NUKE_GOOD);

    CHECK(aoc::sim::requestNuclearStrike(n.world.gameState, n.world.grid, PlayerId{0}, target,
                                         aoc::sim::NukeType::NuclearDevice) == ErrorCode::Ok);
    CHECK(n.city->stockpile().getAmount(aoc::sim::NUKE_GOOD)
          == before - aoc::sim::NUKE_URANIUM_COST);
    CHECK(n.world.grid.hasFallout(n.world.grid.toIndex(target)));

    // A rejected strike leaves the stockpile alone.
    const int32_t afterFirst = n.city->stockpile().getAmount(aoc::sim::NUKE_GOOD);
    n.city->wonders().wonders.clear();
    CHECK(aoc::sim::requestNuclearStrike(n.world.gameState, n.world.grid, PlayerId{0}, target,
                                         aoc::sim::NukeType::NuclearDevice) != ErrorCode::Ok);
    CHECK(n.city->stockpile().getAmount(aoc::sim::NUKE_GOOD) == afterFirst);
}

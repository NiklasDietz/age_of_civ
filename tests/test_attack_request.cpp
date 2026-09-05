/**
 * @file test_attack_request.cpp
 * @brief `requestAttack` is the one validated attack order (right-click, debug
 *        route, MCP tool): reach, movement and sorties are checked, melee and
 *        ranged units use the combat formulas, aircraft fly bombing runs and can
 *        be intercepted by patrolling fighters. Until 2026-09-05 the human could
 *        not attack in-game and the debug route attacked at any distance.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/core/Random.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/unit/AttackRequest.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/unit/CombatExtensions.hpp"

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::UnitTypeId;
using aoc::hex::AxialCoord;

namespace {

constexpr PlayerId P0{0};
constexpr PlayerId P1{1};
constexpr UnitTypeId WARRIOR{0};  // melee 20, 100 hp, 2 mp
constexpr UnitTypeId BUILDER{5};  // civilian
constexpr UnitTypeId ARCHER{36};  // ranged 25, range 2, 80 hp
constexpr UnitTypeId FIGHTER{18}; // air, patrols
constexpr UnitTypeId BOMBER{51};  // air, ranged strength 80, 80 hp

struct Arena {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::Random rng{5u};

    aoc::game::Unit& add(PlayerId owner, UnitTypeId type, int32_t q, int32_t r) {
        aoc::game::Unit& u = aoc::test::addUnitAt(this->w, owner, type, q, r);
        u.refreshMovement();
        return u;
    }
    ErrorCode attack(PlayerId player, AxialCoord from, AxialCoord to) {
        return aoc::sim::requestAttack(this->w.gameState, this->rng, this->w.grid, player, from,
                                       to);
    }
};

} // namespace

TEST_CASE("enemyUnitAt finds another seat's unit, never the viewer's own") {
    Arena a;
    a.add(P0, WARRIOR, 5, 5);
    a.add(P1, WARRIOR, 6, 5);
    CHECK(aoc::sim::enemyUnitAt(a.w.gameState, P0, {6, 5}) != nullptr);
    CHECK(aoc::sim::enemyUnitAt(a.w.gameState, P0, {5, 5}) == nullptr);
    CHECK(aoc::sim::enemyUnitAt(a.w.gameState, P1, {5, 5}) != nullptr);
    CHECK(aoc::sim::enemyUnitAt(a.w.gameState, P0, {9, 9}) == nullptr);
}

TEST_CASE("requestAttack rejects unknown seats, empty tiles, civilians, own units and bad reach") {
    Arena a;
    aoc::game::Unit& warrior = a.add(P0, WARRIOR, 5, 5);
    a.add(P0, BUILDER, 5, 6);
    a.add(P1, WARRIOR, 6, 5); // adjacent enemy
    a.add(P1, WARRIOR, 8, 5); // enemy at distance 3

    CHECK(a.attack(PlayerId{9}, {5, 5}, {6, 5}) == ErrorCode::InvalidArgument);
    CHECK(a.attack(P0, {4, 4}, {6, 5}) == ErrorCode::InvalidArgument);   // nobody there
    CHECK(a.attack(P0, {5, 5}, {-1, -1}) == ErrorCode::InvalidArgument); // off the map
    CHECK(a.attack(P0, {5, 6}, {6, 5}) == ErrorCode::InvalidUnitAction); // a Builder
    CHECK(a.attack(P0, {5, 5}, {7, 7}) == ErrorCode::InvalidArgument);   // no enemy there
    CHECK(a.attack(P0, {5, 5}, {5, 6}) == ErrorCode::InvalidArgument);   // own unit there
    CHECK(a.attack(P0, {5, 5}, {8, 5}) == ErrorCode::InvalidUnitAction); // melee at range 3
    warrior.setMovementRemaining(0);
    CHECK(a.attack(P0, {5, 5}, {6, 5}) == ErrorCode::InvalidUnitAction); // no movement left
    CHECK(a.w.gameState.player(P1)->unitAt({6, 5})->hitPoints() == 100); // nothing happened
}

TEST_CASE("a melee attack on an adjacent enemy resolves combat and spends the movement") {
    Arena a;
    a.add(P0, WARRIOR, 5, 5);
    a.add(P1, WARRIOR, 6, 5);

    CHECK(a.attack(P0, {5, 5}, {6, 5}) == ErrorCode::Ok);
    const aoc::game::Unit* defender = a.w.gameState.player(P1)->unitAt({6, 5});
    const aoc::game::Unit* attacker = a.w.gameState.player(P0)->unitAt({5, 5});
    const bool someoneHurt          = (defender == nullptr || defender->hitPoints() < 100) ||
                                      (attacker == nullptr || attacker->hitPoints() < 100);
    CHECK(someoneHurt);
    if (attacker != nullptr) {
        CHECK(attacker->movementRemaining() == 0);
        CHECK(a.attack(P0, {5, 5}, {6, 5}) == ErrorCode::InvalidUnitAction); // one attack per turn
    }
}

TEST_CASE("a ranged attack needs the target within range, takes no retaliation, spends movement") {
    Arena a;
    aoc::game::Unit& archer = a.add(P0, ARCHER, 5, 5);
    a.add(P1, WARRIOR, 7, 5); // distance 2 = range
    a.add(P1, WARRIOR, 8, 5); // distance 3

    CHECK(a.attack(P0, {5, 5}, {8, 5}) == ErrorCode::InvalidUnitAction);
    CHECK(archer.movementRemaining() == 2);
    CHECK(a.attack(P0, {5, 5}, {7, 5}) == ErrorCode::Ok);
    CHECK(a.w.gameState.player(P1)->unitAt({7, 5})->hitPoints() < 100);
    CHECK(archer.hitPoints() == 80);
    CHECK(archer.movementRemaining() == 0);
}

TEST_CASE("an aircraft flies a bombing run within its operational range while it has a sortie") {
    Arena a;
    aoc::game::Unit& bomber = a.add(P0, BOMBER, 5, 5);
    a.add(P1, WARRIOR, 10, 5); // distance 5 <= range 8
    a.add(P1, WARRIOR, 14, 5); // distance 9

    bomber.airUnit().sortiesRemaining = 0;
    CHECK(a.attack(P0, {5, 5}, {10, 5}) == ErrorCode::InvalidUnitAction); // grounded
    bomber.airUnit().sortiesRemaining = 1;
    CHECK(a.attack(P0, {5, 5}, {14, 5}) == ErrorCode::InvalidUnitAction); // out of range
    CHECK(a.attack(P0, {5, 5}, {5, 5}) == ErrorCode::InvalidUnitAction);  // its own tile
    CHECK(bomber.airUnit().sortiesRemaining == 1);

    CHECK(a.attack(P0, {5, 5}, {10, 5}) == ErrorCode::Ok);
    CHECK(a.w.gameState.player(P1)->unitAt({10, 5})->hitPoints() == 20); // 100 - ranged 80
    CHECK(bomber.airUnit().sortiesRemaining == 0);
    CHECK(bomber.hitPoints() == 80);       // nobody intercepted
    CHECK(bomber.movementRemaining() > 0); // sorties, not movement, gate aircraft
}

TEST_CASE("a patrolling enemy fighter in range intercepts the bomber and spends its sortie") {
    Arena a;
    aoc::game::Unit& bomber  = a.add(P0, BOMBER, 5, 5);
    aoc::game::Unit& fighter = a.add(P1, FIGHTER, 8, 5); // 3 tiles from the bomber
    a.add(P1, WARRIOR, 10, 5);

    fighter.airUnit().isIntercepting   = false; // not on patrol: no interception
    fighter.airUnit().sortiesRemaining = 1;
    CHECK(a.attack(P0, {5, 5}, {10, 5}) == ErrorCode::Ok);
    CHECK(bomber.hitPoints() == 80);
    CHECK(fighter.airUnit().sortiesRemaining == 1);

    bomber.airUnit().sortiesRemaining = 1;
    fighter.airUnit().isIntercepting  = true; // what resetAirSorties sets each turn
    CHECK(a.attack(P0, {5, 5}, {10, 5}) == ErrorCode::Ok);
    CHECK(bomber.hitPoints() == 38); // intercepted: 25 / 2 from the fighter, 30 for the abort
    CHECK(fighter.airUnit().sortiesRemaining == 0);
    // The interception aborts the run (Civ VI): the target keeps its 20 hp and
    // the bomber's sortie is spent. Until 2026-09-05 the bomb still landed.
    REQUIRE(a.w.gameState.player(P1)->unitAt({10, 5}) != nullptr);
    CHECK(a.w.gameState.player(P1)->unitAt({10, 5})->hitPoints() == 20);
    CHECK(bomber.airUnit().sortiesRemaining == 0);
}

TEST_CASE("with diplomacy, attacking a major at peace is refused until war is declared") {
    Arena a;
    aoc::sim::DiplomacyManager diplomacy;
    diplomacy.initialize(2);
    aoc::game::Unit& attacker = a.add(P0, WARRIOR, 5, 5);
    a.add(P1, WARRIOR, 6, 5);
    CHECK(aoc::sim::requestAttack(a.w.gameState, a.rng, a.w.grid, P0, {5, 5}, {6, 5}, &diplomacy)
          == ErrorCode::InvalidState);
    CHECK(attacker.movementRemaining() == attacker.typeDef().movementPoints);   // nothing spent
    diplomacy.declareWar(P0, P1);
    CHECK(aoc::sim::requestAttack(a.w.gameState, a.rng, a.w.grid, P0, {5, 5}, {6, 5}, &diplomacy)
          == ErrorCode::Ok);
}

TEST_CASE("a civilian is captured, not killed: it changes owner and the attacker steps onto it") {
    Arena a;
    a.add(P0, WARRIOR, 5, 5);
    aoc::game::Unit& builder = a.add(P1, BUILDER, 6, 5);
    builder.setChargesRemaining(2);
    CHECK(a.attack(P0, {5, 5}, {6, 5}) == ErrorCode::Ok);
    CHECK(a.w.gameState.player(P1)->unitAt({6, 5}) == nullptr);
    const aoc::game::Unit* captured = nullptr;
    for (const std::unique_ptr<aoc::game::Unit>& u : a.w.gameState.player(P0)->units()) {
        if (u->typeId() == BUILDER) { captured = u.get(); }
    }
    REQUIRE(captured != nullptr);
    CHECK(captured->position() == AxialCoord{6, 5});
    CHECK(captured->chargesRemaining() == 2);
    const aoc::game::Unit* warrior = nullptr;
    for (const std::unique_ptr<aoc::game::Unit>& u : a.w.gameState.player(P0)->units()) {
        if (u->typeId() == WARRIOR) { warrior = u.get(); }
    }
    REQUIRE(warrior != nullptr);
    CHECK(warrior->position() == AxialCoord{6, 5});
    CHECK(warrior->movementRemaining() == 0);
}

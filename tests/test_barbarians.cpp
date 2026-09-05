/**
 * @file test_barbarians.cpp
 * @brief Barbarians run: the seat exists and is routed by id, a camp and its first
 *        warrior appear at the spawn interval, the spawn ladder names real units and
 *        floors by strength, stepping onto a camp clears it for the reward, and a
 *        barbarian next to a player unit attacks it. Until 2026-09-05
 *        GameState::player(BARBARIAN_PLAYER) returned null, so none of this ever ran.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/core/Random.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/barbarian/BarbarianClans.hpp"
#include "aoc/simulation/barbarian/BarbarianController.hpp"
#include "aoc/simulation/turn/TurnEventLog.hpp"
#include "aoc/simulation/unit/Combat.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

#include <array>

using aoc::PlayerId;
using aoc::UnitTypeId;
using aoc::sim::BarbarianController;
using aoc::sim::TurnEventLog;
using aoc::sim::TurnEventType;

namespace {

int32_t countEvents(const TurnEventLog& log, TurnEventType type) {
    int32_t n = 0;
    for (const aoc::sim::TurnEvent& e : log.events()) {
        if (e.type == type) {
            ++n;
        }
    }
    return n;
}

void runTurns(aoc::test::World& w, BarbarianController& barbarians, TurnEventLog& log,
              aoc::Random& rng, int32_t turns) {
    for (int32_t i = 0; i < turns; ++i) {
        barbarians.executeTurn(w.gameState, w.grid, rng, &log);
    }
}

/// A 24x16 grassland world whose only city sits in a corner, so most of the map
/// is unowned land at least MIN_DISTANCE_FROM_CITY (7) tiles away from it.
aoc::test::World cornerWorld() {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 0, 0, "Corner");
    return w;
}

} // namespace

TEST_CASE("the barbarian seat exists, is routed by id and is not a major player") {
    aoc::test::World w = aoc::test::makeWorld(2);
    CHECK(aoc::BARBARIAN_PLAYER != aoc::INVALID_PLAYER);
    aoc::game::Player* barbarians = w.gameState.player(aoc::BARBARIAN_PLAYER);
    REQUIRE(barbarians != nullptr);
    CHECK(barbarians == w.gameState.barbarianPlayer());
    CHECK(barbarians->id() == aoc::BARBARIAN_PLAYER);
    CHECK(w.gameState.players().size() == 2);
    CHECK(w.gameState.player(aoc::INVALID_PLAYER) == nullptr);
}

TEST_CASE("a camp and its first warrior appear at the spawn interval") {
    aoc::test::World w = cornerWorld();
    BarbarianController barbarians;
    TurnEventLog log;
    aoc::Random rng{42};

    runTurns(w, barbarians, log, rng, 14);
    CHECK(barbarians.encampments().empty());
    CHECK(w.gameState.barbarianPlayer()->unitCount() == 0);

    runTurns(w, barbarians, log, rng, 1);
    REQUIRE(barbarians.encampments().size() == 1);
    REQUIRE(w.gameState.barbarianPlayer()->unitCount() == 1);
    CHECK(countEvents(log, TurnEventType::BarbarianCampSpawned) == 1);
    const aoc::hex::AxialCoord camp = barbarians.encampments()[0].location;
    CHECK(w.grid.distance(camp, aoc::hex::AxialCoord{0, 0}) >= 7);
    // The founding warrior patrols one step per turn, so it is on or next to the camp.
    const aoc::game::Unit& warrior = *w.gameState.barbarianPlayer()->units()[0];
    CHECK(w.grid.distance(warrior.position(), camp) <= 1);
}

TEST_CASE("the spawn ladder names real units and the era floor compares strength, not ids") {
    const std::array<int32_t, 12> turns = {0, 29, 30, 59, 60, 99, 100, 149, 150, 199, 200, 500};
    for (const int32_t turn : turns) {
        for (int32_t era = -1; era <= 7; ++era) {
            const UnitTypeId id = aoc::sim::barbarianSpawnUnit(turn, era);
            CHECK_MESSAGE(aoc::sim::unitTypeDef(id).id == id, "turn ", turn, " era ", era, " id ",
                          id.value);
        }
    }
    // Turn 149 alone gives a Musketman (id 34, strength 55); an Industrial-era
    // leader floors it to Infantry (id 15, strength 70) although 15 < 34.
    CHECK(aoc::sim::barbarianSpawnUnit(149, -1) == UnitTypeId{34});
    CHECK(aoc::sim::barbarianSpawnUnit(149, 4) == UnitTypeId{15});
    // A Medieval leader on turn 0 floors the Warrior to a Man-at-Arms.
    CHECK(aoc::sim::barbarianSpawnUnit(0, 2) == UnitTypeId{33});
}

TEST_CASE("stepping onto a camp clears it and pays the clearance reward") {
    aoc::test::World w = cornerWorld();
    BarbarianController barbarians;
    TurnEventLog log;
    aoc::Random rng{42};
    runTurns(w, barbarians, log, rng, 15);
    REQUIRE(barbarians.encampments().size() == 1);
    const aoc::hex::AxialCoord camp = barbarians.encampments()[0].location;

    aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{0}, camp.q, camp.r);
    const aoc::CurrencyAmount before = w.gameState.player(PlayerId{0})->treasury();
    runTurns(w, barbarians, log, rng, 1);

    CHECK(barbarians.encampments().empty());
    CHECK(w.gameState.player(PlayerId{0})->treasury() ==
          before + aoc::sim::encampmentDestroyReward(1));
    CHECK(countEvents(log, TurnEventType::BarbarianCampCleared) == 1);
}

TEST_CASE("a killed barbarian is removed from its seat and pays the clearance bonus once") {
    aoc::test::World w = cornerWorld();
    aoc::game::Unit& barbarian = aoc::test::addUnitAt(w, aoc::BARBARIAN_PLAYER, UnitTypeId{0}, 10, 8);
    barbarian.takeDamage(99); // one hit point left
    aoc::game::Unit& attacker = aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{0}, 11, 8);
    aoc::game::Player& player = *w.gameState.player(PlayerId{0});
    const aoc::CurrencyAmount before = player.treasury();
    aoc::Random rng{3};

    const aoc::sim::CombatResult result =
        aoc::sim::resolveMeleeCombat(w.gameState, rng, w.grid, attacker, barbarian);

    CHECK(result.defenderKilled);
    CHECK(w.gameState.barbarianPlayer()->unitCount() == 0);
    CHECK(w.gameState.barbarianPlayer()->unitAt({10, 8}) == nullptr);
    CHECK(player.treasury() == before + 25);
}

TEST_CASE("a barbarian next to a player unit attacks it") {
    aoc::test::World w = cornerWorld();
    aoc::test::addUnitAt(w, aoc::BARBARIAN_PLAYER, UnitTypeId{0}, 10, 8);
    aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{0}, 11, 8);
    const aoc::game::Unit* victimBefore = w.gameState.player(PlayerId{0})->unitAt({11, 8});
    REQUIRE(victimBefore != nullptr);
    const int32_t hpBefore = victimBefore->hitPoints();

    BarbarianController barbarians;
    TurnEventLog log;
    aoc::Random rng{7};
    barbarians.executeTurn(w.gameState, w.grid, rng, &log);

    const aoc::game::Unit* victim = w.gameState.player(PlayerId{0})->unitAt({11, 8});
    REQUIRE(victim != nullptr);
    CHECK(victim->hitPoints() < hpBefore);
}

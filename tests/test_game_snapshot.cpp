#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/debug/GameSnapshot.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/turn/TurnManager.hpp"

#include <string>

namespace {

constexpr aoc::UnitTypeId WARRIOR{0};

} // namespace

TEST_CASE("buildGameSnapshot: empty game (0 players) does not crash") {
    // Default-constructed GameState has an empty players vector.
    // initialize() asserts playerCount > 0, so do not call it here.
    aoc::game::GameState gs;
    aoc::sim::TurnManager tm;

    const aoc::debug::GameSnapshot snap = aoc::debug::buildGameSnapshot(gs, tm);
    CHECK(snap.players.empty());
    CHECK(snap.turnNumber == 0);
    CHECK(snap.phase == "PlayerInput");

    const std::string json = aoc::debug::toJson(snap);
    CHECK(json.find("\"players\":[]") != std::string::npos);
    CHECK(json.find("\"turnNumber\":0") != std::string::npos);
}

TEST_CASE("buildGameSnapshot: single player, 1 unit, 1 city round-trips") {
    aoc::game::GameState gs;
    gs.initialize(2);
    aoc::sim::TurnManager tm;

    aoc::game::Player& p0 = *gs.players()[0];
    aoc::game::Unit& u    = p0.addUnit(WARRIOR, {3, 5});
    u.setHitPoints(80);

    const aoc::debug::GameSnapshot snap = aoc::debug::buildGameSnapshot(gs, tm);
    REQUIRE(snap.players.size() == 2);

    const aoc::debug::PlayerSnapshot& ps = snap.players[0];
    CHECK(ps.id == 0);
    REQUIRE(ps.units.size() == 1);
    CHECK(ps.cities.empty());

    const aoc::debug::UnitSnapshot& us = ps.units[0];
    CHECK(us.q == 3);
    CHECK(us.r == 5);
    CHECK(us.hitPoints == 80);
    CHECK(us.state == "Idle");
    CHECK(!us.typeName.empty());

    const std::string json = aoc::debug::toJson(snap);
    CHECK(json.find("\"hitPoints\":80") != std::string::npos);
    CHECK(json.find("\"q\":3") != std::string::npos);
}

TEST_CASE("buildGameSnapshot: currentResearchTechId is -1 when no research active") {
    aoc::game::GameState gs;
    gs.initialize(1);
    aoc::sim::TurnManager tm;

    const aoc::debug::GameSnapshot snap = aoc::debug::buildGameSnapshot(gs, tm);
    REQUIRE(!snap.players.empty());
    CHECK(snap.players[0].currentResearchTechId == -1);
    CHECK(snap.players[0].researchProgress == doctest::Approx(0.0f));
}

TEST_CASE("buildGameSnapshot: fortified unit state serializes correctly") {
    aoc::game::GameState gs;
    gs.initialize(1);
    aoc::sim::TurnManager tm;

    aoc::game::Unit& u = gs.players()[0]->addUnit(WARRIOR, {0, 0});
    u.setState(aoc::sim::UnitState::Fortified);

    const aoc::debug::GameSnapshot snap = aoc::debug::buildGameSnapshot(gs, tm);
    REQUIRE(snap.players[0].units.size() == 1);
    CHECK(snap.players[0].units[0].state == "Fortified");

    const std::string json = aoc::debug::toJson(snap);
    CHECK(json.find("\"state\":\"Fortified\"") != std::string::npos);
}

TEST_CASE("toJson vector overloads: empty lists produce valid JSON arrays") {
    const std::vector<aoc::debug::UnitSnapshot> units;
    const std::vector<aoc::debug::CitySnapshot> cities;
    CHECK(aoc::debug::toJson(units) == "[]");
    CHECK(aoc::debug::toJson(cities) == "[]");
}

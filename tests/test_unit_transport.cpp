/**
 * @file test_unit_transport.cpp
 * @brief Naval units carry land units. cargoCapacity and cargo were written at
 *        unit creation and never read -- the serializer hardcoded 0 with a
 *        comment saying so, and there was no load or unload function anywhere.
 *        Self-embarkation was the only way across water.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/simulation/unit/UnitTransport.hpp"

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::UnitTypeId;

namespace {

constexpr UnitTypeId GALLEY{6};  // capacity 1
constexpr UnitTypeId CARAVEL{7}; // capacity 2
constexpr UnitTypeId WARRIOR{0};
constexpr aoc::hex::AxialCoord SEA{6, 5};
constexpr aoc::hex::AxialCoord SHORE{5, 5};

/// Grassland world with SEA turned to water.
aoc::test::World coastWorld() {
    aoc::test::World w = aoc::test::makeWorld(2);
    w.grid.setTerrain(w.grid.toIndex(SEA), aoc::map::TerrainType::Ocean);
    return w;
}

} // namespace

TEST_CASE("capacity comes from the unit type, and warships carry nothing") {
    CHECK(aoc::sim::transportCapacity(GALLEY) == 1);
    CHECK(aoc::sim::transportCapacity(CARAVEL) == 2);
    CHECK(aoc::sim::transportCapacity(CARAVEL) > aoc::sim::transportCapacity(GALLEY));
    // A land unit is not a hull.
    CHECK(aoc::sim::transportCapacity(WARRIOR) == 0);
}

TEST_CASE("a land unit alongside a transport can board it") {
    aoc::test::World w = coastWorld();
    aoc::test::addUnitAt(w, PlayerId{0}, GALLEY, SEA.q, SEA.r);
    aoc::game::Unit& soldier = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, SHORE.q, SHORE.r);
    soldier.setMovementRemaining(2);

    REQUIRE(aoc::sim::requestLoadUnit(w.gameState, w.grid, PlayerId{0}, SHORE, SEA) ==
            ErrorCode::Ok);

    CHECK(soldier.position() == SEA);
    CHECK(soldier.state() == aoc::sim::UnitState::Embarked);
}

TEST_CASE("a transport takes only as many as it has places for") {
    aoc::test::World w       = coastWorld();
    aoc::game::Unit& galley  = aoc::test::addUnitAt(w, PlayerId{0}, GALLEY, SEA.q, SEA.r);
    aoc::game::Player& owner = *w.gameState.player(PlayerId{0});

    aoc::game::Unit& first = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, SHORE.q, SHORE.r);
    first.setMovementRemaining(2);
    REQUIRE(aoc::sim::requestLoadUnit(w.gameState, w.grid, PlayerId{0}, SHORE, SEA) ==
            ErrorCode::Ok);
    CHECK(aoc::sim::freeTransportSlots(owner, galley) == 0);

    // A second soldier finds the galley full.
    aoc::game::Unit& second = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, SHORE.q, SHORE.r + 1);
    second.setMovementRemaining(2);
    CHECK(aoc::sim::requestLoadUnit(w.gameState, w.grid, PlayerId{0}, {SHORE.q, SHORE.r + 1},
                                    SEA) != ErrorCode::Ok);
}

TEST_CASE("a passenger rides along when the transport moves") {
    // Without this the transport sails out from under them.
    aoc::test::World w       = coastWorld();
    aoc::game::Unit& galley  = aoc::test::addUnitAt(w, PlayerId{0}, GALLEY, SEA.q, SEA.r);
    aoc::game::Unit& soldier = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, SHORE.q, SHORE.r);
    soldier.setMovementRemaining(2);
    REQUIRE(aoc::sim::requestLoadUnit(w.gameState, w.grid, PlayerId{0}, SHORE, SEA) ==
            ErrorCode::Ok);

    constexpr aoc::hex::AxialCoord ONWARD{7, 5};
    aoc::game::Player& owner = *w.gameState.player(PlayerId{0});
    galley.setPosition(ONWARD);
    aoc::sim::carryPassengers(owner, galley, SEA, ONWARD);

    CHECK(soldier.position() == ONWARD);
}

TEST_CASE("a passenger can be put ashore on adjacent land") {
    aoc::test::World w = coastWorld();
    aoc::test::addUnitAt(w, PlayerId{0}, GALLEY, SEA.q, SEA.r);
    aoc::game::Unit& soldier = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, SHORE.q, SHORE.r);
    soldier.setMovementRemaining(2);
    REQUIRE(aoc::sim::requestLoadUnit(w.gameState, w.grid, PlayerId{0}, SHORE, SEA) ==
            ErrorCode::Ok);

    constexpr aoc::hex::AxialCoord BEACH{6, 4};
    REQUIRE(aoc::sim::requestUnloadUnit(w.gameState, w.grid, PlayerId{0}, SEA, BEACH) ==
            ErrorCode::Ok);
    CHECK(soldier.position() == BEACH);
    CHECK(soldier.state() != aoc::sim::UnitState::Embarked);
}

TEST_CASE("nobody is put ashore into the sea") {
    aoc::test::World w = coastWorld();
    // A second water tile beside the transport.
    constexpr aoc::hex::AxialCoord MORE_SEA{7, 5};
    w.grid.setTerrain(w.grid.toIndex(MORE_SEA), aoc::map::TerrainType::Ocean);

    aoc::test::addUnitAt(w, PlayerId{0}, GALLEY, SEA.q, SEA.r);
    aoc::game::Unit& soldier = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, SHORE.q, SHORE.r);
    soldier.setMovementRemaining(2);
    REQUIRE(aoc::sim::requestLoadUnit(w.gameState, w.grid, PlayerId{0}, SHORE, SEA) ==
            ErrorCode::Ok);

    CHECK(aoc::sim::requestUnloadUnit(w.gameState, w.grid, PlayerId{0}, SEA, MORE_SEA) !=
          ErrorCode::Ok);
    CHECK(soldier.position() == SEA); // still aboard
}

TEST_CASE("everyone aboard goes down with the hull") {
    aoc::test::World w       = coastWorld();
    aoc::game::Unit& galley  = aoc::test::addUnitAt(w, PlayerId{0}, GALLEY, SEA.q, SEA.r);
    aoc::game::Unit& soldier = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, SHORE.q, SHORE.r);
    soldier.setMovementRemaining(2);
    REQUIRE(aoc::sim::requestLoadUnit(w.gameState, w.grid, PlayerId{0}, SHORE, SEA) ==
            ErrorCode::Ok);

    aoc::game::Player& owner = *w.gameState.player(PlayerId{0});
    REQUIRE(owner.units().size() == 2);

    aoc::sim::drownPassengers(owner, galley);
    CHECK(owner.units().size() == 1); // the galley alone
}

TEST_CASE("a warship is not a ferry") {
    aoc::test::World w = coastWorld();
    aoc::test::addUnitAt(w, PlayerId{0}, aoc::UnitTypeId{8}, SEA.q, SEA.r); // Battleship
    aoc::game::Unit& soldier = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, SHORE.q, SHORE.r);
    soldier.setMovementRemaining(2);

    CHECK(aoc::sim::requestLoadUnit(w.gameState, w.grid, PlayerId{0}, SHORE, SEA) != ErrorCode::Ok);
}

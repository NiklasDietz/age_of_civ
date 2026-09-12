/**
 * @file test_unit_orders.cpp
 * @brief Pillage marks an enemy improvement, heals and pays gold, the tile
 *        yields nothing until a Builder repairs it; deleting a unit refunds
 *        quarter of its cost at home; the alert stance sleeps a unit and wakes
 *        it when an enemy approaches. Civ VI plan Phase 2.5, 2026-09-05.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/automation/Automation.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/unit/UnitOrders.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::UnitTypeId;
using aoc::hex::AxialCoord;
using aoc::map::ImprovementType;

namespace {
constexpr UnitTypeId WARRIOR{0};
constexpr UnitTypeId BUILDER{5};
} // namespace

TEST_CASE("pillage needs war, marks the tile, heals and pays; the yield is gone until repaired") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::sim::DiplomacyManager diplomacy;
    diplomacy.initialize(2);
    const AxialCoord at{6, 5};
    const int32_t idx = w.grid.toIndex(at);
    w.grid.setOwner(idx, PlayerId{1});
    w.grid.setTerrain(idx, aoc::map::TerrainType::Grassland);
    w.grid.setImprovement(idx, ImprovementType::Farm);
    const aoc::map::TileYield full = w.grid.tileYield(idx);

    aoc::game::Unit& raider = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, at.q, at.r);
    raider.setHitPoints(40);
    aoc::game::Player& p0 = *w.gameState.player(PlayerId{0});
    aoc::game::Player& p1 = *w.gameState.player(PlayerId{1});
    p0.setTreasury(0, aoc::sim::MoneyFlow::external());
    p1.monetary().privateSpecie = 1000; // the farmers' savings are what gets looted

    CHECK(aoc::sim::requestPillage(w.gameState, w.grid, PlayerId{0}, at, &diplomacy) == ErrorCode::InvalidState);
    diplomacy.declareWar(PlayerId{0}, PlayerId{1});
    CHECK(aoc::sim::requestPillage(w.gameState, w.grid, PlayerId{0}, at, &diplomacy) == ErrorCode::Ok);
    CHECK(w.grid.isPillaged(idx));
    CHECK(raider.hitPoints() == 90);
    CHECK(p0.treasury() == aoc::sim::pillageGold(0));
    CHECK(p1.monetary().privateSpecie == 1000 - aoc::sim::pillageGold(0));
    CHECK(raider.movementRemaining() == 0);
    CHECK(w.grid.tileYield(idx).food < full.food);   // the Farm's bonus is suspended
    CHECK(aoc::sim::requestPillage(w.gameState, w.grid, PlayerId{0}, at, &diplomacy) == ErrorCode::InvalidUnitAction);
    raider.refreshMovement();
    CHECK(aoc::sim::requestPillage(w.gameState, w.grid, PlayerId{0}, at, &diplomacy) == ErrorCode::InvalidArgument); // already pillaged

    // The owner's Builder repairs it.
    aoc::test::addUnitAt(w, PlayerId{1}, BUILDER, at.q, at.r);
    CHECK(aoc::sim::requestRepair(w.gameState, w.grid, PlayerId{0}, at) == ErrorCode::InvalidArgument); // not P0's tile
    CHECK(aoc::sim::requestRepair(w.gameState, w.grid, PlayerId{1}, at) == ErrorCode::Ok);
    CHECK_FALSE(w.grid.isPillaged(idx));
    CHECK(w.grid.tileYield(idx).food == full.food);
}

TEST_CASE("deleting a unit refunds nothing, at home or abroad: money comes from nowhere no more") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::Player& p0 = *w.gameState.player(PlayerId{0});
    p0.setTreasury(0, aoc::sim::MoneyFlow::external());
    w.grid.setOwner(w.grid.toIndex(AxialCoord{5, 5}), PlayerId{0});
    aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, 5, 5);
    aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, 9, 9);
    const std::size_t before = p0.units().size();
    CHECK(aoc::sim::requestDeleteUnit(w.gameState, w.grid, PlayerId{0}, AxialCoord{5, 5}) == ErrorCode::Ok);
    CHECK(p0.units().size() == before - 1);
    CHECK(p0.treasury() == 0);
    CHECK(aoc::sim::requestDeleteUnit(w.gameState, w.grid, PlayerId{0}, AxialCoord{9, 9}) == ErrorCode::Ok);
    CHECK(p0.treasury() == 0);
    CHECK(aoc::sim::requestDeleteUnit(w.gameState, w.grid, PlayerId{0}, AxialCoord{9, 9}) == ErrorCode::InvalidArgument);
}

TEST_CASE("alert sleeps a unit and processAlertStance wakes it when an enemy comes close") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::Unit& sentry = aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, 5, 5);
    CHECK(aoc::sim::requestSetAlert(w.gameState, PlayerId{0}, AxialCoord{5, 5}, true) == ErrorCode::Ok);
    CHECK(sentry.alertStance);
    CHECK(sentry.state() == aoc::sim::UnitState::Sleeping);
    aoc::sim::processAlertStance(w.gameState, w.grid, PlayerId{0});
    CHECK(sentry.state() == aoc::sim::UnitState::Sleeping);   // nobody near
    aoc::test::addUnitAt(w, PlayerId{1}, WARRIOR, 7, 5);       // 2 tiles away
    aoc::sim::processAlertStance(w.gameState, w.grid, PlayerId{0});
    CHECK(sentry.state() != aoc::sim::UnitState::Sleeping);
    CHECK(aoc::sim::requestSetAlert(w.gameState, PlayerId{0}, AxialCoord{5, 5}, false) == ErrorCode::Ok);
    CHECK_FALSE(sentry.alertStance);
    aoc::test::addUnitAt(w, PlayerId{0}, BUILDER, 8, 8);
    CHECK(aoc::sim::requestSetAlert(w.gameState, PlayerId{0}, AxialCoord{8, 8}, true) == ErrorCode::InvalidUnitAction);
}

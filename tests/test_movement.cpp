/**
 * @file test_movement.cpp
 * @brief Movement rules that existed as data but were never read: river edges
 *        cost +1, ice shelves block ships and embarked units, another seat's
 *        unit blocks a tile, embarking needs Sailing (civilians) or
 *        Shipbuilding (military), promotions add movement and healing, and
 *        supply reaches ships along the water. Civ VI plan Phase 1.8, 2026-09-05.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/unit/Naval.hpp"
#include "aoc/simulation/unit/Promotion.hpp"
#include "aoc/simulation/unit/SupplyLines.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

#include <array>

using aoc::PlayerId;
using aoc::PromotionId;
using aoc::UnitTypeId;
using aoc::hex::AxialCoord;

TEST_CASE("crossing a river edge costs one extra movement point") {
    aoc::test::World w = aoc::test::makeWorld(2);
    const AxialCoord a{5, 5};
    const std::array<AxialCoord, 6> nbrs = aoc::hex::neighbors(a);
    const AxialCoord b = nbrs[0];
    const int32_t ai = w.grid.toIndex(a);
    const int32_t bi = w.grid.toIndex(b);
    const int32_t plain = w.grid.movementCost(bi);
    REQUIRE(plain > 0);
    CHECK(w.grid.movementCost(ai, bi) == plain);
    w.grid.setRiverEdges(ai, static_cast<uint8_t>(1u << 0));
    CHECK(w.grid.movementCost(ai, bi) == plain + 1);

    aoc::game::Unit& u = aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{0}, a.q, a.r);
    u.refreshMovement();
    const int32_t before = u.movementRemaining();
    u.pendingPath().push_back(b);
    CHECK(aoc::sim::moveUnitAlongPath(w.gameState, u, w.grid));
    CHECK(u.position() == b);
    CHECK(u.movementRemaining() == before - (plain + 1));
}

TEST_CASE("ice shelves block ships") {
    aoc::test::World w = aoc::test::makeWorld(2);
    const int32_t idx = w.grid.toIndex(AxialCoord{8, 8});
    w.grid.setTerrain(idx, aoc::map::TerrainType::Ocean);
    CHECK(w.grid.navalMovementCost(idx) == 1);
    CHECK(w.grid.navalMovementCostNoCanals(idx) == 1);
    w.grid.setFeature(idx, aoc::map::FeatureType::Ice);
    CHECK(w.grid.navalMovementCost(idx) == 0);
    CHECK(w.grid.navalMovementCostNoCanals(idx) == 0);
    CHECK(w.grid.shallowNavalMovementCost(idx) == 0);
}

TEST_CASE("a unit cannot step onto another seat's unit") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::Unit& mover = aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{0}, 5, 5);
    aoc::test::addUnitAt(w, PlayerId{1}, UnitTypeId{5}, 6, 5); // a foreign Builder
    mover.refreshMovement();
    mover.pendingPath().push_back(AxialCoord{6, 5});
    aoc::sim::moveUnitAlongPath(w.gameState, mover, w.grid);
    CHECK(mover.position() == AxialCoord{5, 5});

    // A civilian (a Trader bound for a foreign city) still passes.
    aoc::game::Unit& trader = aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{30}, 7, 5);
    trader.refreshMovement();
    trader.pendingPath().push_back(AxialCoord{6, 5});
    aoc::sim::moveUnitAlongPath(w.gameState, trader, w.grid);
    CHECK(trader.position() == AxialCoord{6, 5});

    // Own civilians still stack with own military.
    aoc::game::Unit& ally = aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{0}, 5, 6);
    aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{5}, 5, 7);
    ally.refreshMovement();
    ally.pendingPath().push_back(AxialCoord{5, 7});
    aoc::sim::moveUnitAlongPath(w.gameState, ally, w.grid);
    CHECK(ally.position() == AxialCoord{5, 7});
}

TEST_CASE("embarking needs Sailing for civilians and Shipbuilding for the military") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::sim::PlayerTechComponent& tech = w.gameState.player(PlayerId{0})->tech();
    const AxialCoord coast{6, 5};
    w.grid.setTerrain(w.grid.toIndex(coast), aoc::map::TerrainType::Coast);
    aoc::game::Unit& warrior = aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{0}, 5, 5);
    aoc::game::Unit& builder = aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{5}, 5, 5);

    CHECK_FALSE(aoc::sim::tryEmbark(warrior, coast, w.grid, &tech));
    CHECK_FALSE(aoc::sim::tryEmbark(builder, coast, w.grid, &tech));
    tech.completedTechs[31] = true; // Sailing
    CHECK(aoc::sim::tryEmbark(builder, coast, w.grid, &tech));
    CHECK_FALSE(aoc::sim::tryEmbark(warrior, coast, w.grid, &tech));
    tech.completedTechs[42] = true; // Shipbuilding
    CHECK(aoc::sim::tryEmbark(warrior, coast, w.grid, &tech));
    // Legacy callers without a tech component keep the old behaviour.
    aoc::game::Unit& scout = aoc::test::addUnitAt(w, PlayerId{1}, UnitTypeId{2}, 7, 5);
    CHECK(aoc::sim::tryEmbark(scout, coast, w.grid));
}

TEST_CASE("promotions add movement, healing and terrain defence") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::Unit& u = aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{0}, 5, 5);
    const int32_t base = u.typeDef().movementPoints;
    u.refreshMovement();
    CHECK(u.movementRemaining() == base);
    u.experience().promotions.push_back(PromotionId{2}); // Commando: +1 movement
    u.refreshMovement();
    CHECK(u.movementRemaining() == base + 1);
    CHECK(u.experience().totalHealingBonus() == 0);
    u.experience().promotions.push_back(PromotionId{3}); // Medic: +10 healing
    CHECK(u.experience().totalHealingBonus() == 10);
    u.experience().promotions.push_back(PromotionId{1}); // Tortoise: +15 percent terrain
    CHECK(u.experience().totalTerrainDefenseBonus() == doctest::Approx(0.15f));
}

TEST_CASE("supply reaches a ship beside its own port; ships never take attrition") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Port");
    const AxialCoord sea{6, 5};
    w.grid.setTerrain(w.grid.toIndex(sea), aoc::map::TerrainType::Coast);
    aoc::game::Unit& galley = aoc::test::addUnitAt(w, PlayerId{0}, UnitTypeId{6}, sea.q, sea.r);
    galley.supply().isSupplied = false;
    aoc::sim::computeSupplyLines(w.gameState, w.grid, PlayerId{0});
    CHECK(galley.supply().isSupplied);

    galley.supply().isSupplied = false;
    const int32_t hp = galley.hitPoints();
    aoc::sim::applySupplyAttrition(w.gameState, PlayerId{0});
    CHECK(galley.hitPoints() == hp);
}

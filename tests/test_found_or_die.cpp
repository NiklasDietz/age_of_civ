/**
 * @file test_found_or_die.cpp
 * @brief A civilization without a city must either found one or be removed.
 *
 * 2026-09-04 Tutorial playthrough: the AI settler bled to death from supply
 * attrition on its way to a far site (its escort had already deserted) and
 * the city-less, unit-less player was then never eliminated. These cases pin
 * the three rules that changed.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/unit/SupplyLines.hpp"
#include "aoc/simulation/victory/VictoryCondition.hpp"

namespace {

constexpr aoc::UnitTypeId WARRIOR{0};
constexpr aoc::UnitTypeId SETTLER{3};

void makeLand(aoc::map::HexGrid& grid) {
    grid.initialize(24, 16);
    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        grid.setTerrain(i, aoc::map::TerrainType::Grassland);
    }
}

} // namespace

TEST_CASE("a civilization without a city keeps every unit supplied") {
    aoc::game::GameState gs;
    gs.initialize(2);
    aoc::map::HexGrid grid;
    makeLand(grid);
    aoc::game::Player& walker = *gs.players()[1];
    aoc::game::Unit& settler  = walker.addUnit(SETTLER, {5, 5});
    aoc::game::Unit& escort   = walker.addUnit(WARRIOR, {6, 5});
    const int32_t settlerHp   = settler.hitPoints();
    const int32_t escortHp    = escort.hitPoints();

    aoc::sim::computeSupplyLines(gs, grid, aoc::PlayerId{1});
    CHECK(settler.supply().isSupplied);
    CHECK(escort.supply().isSupplied);

    aoc::sim::applySupplyAttrition(gs, aoc::PlayerId{1});
    CHECK(settler.hitPoints() == settlerHp);
    CHECK(escort.hitPoints() == escortHp);
    CHECK(walker.unitCount() == 2);
}

TEST_CASE("a player that never founded and has no units left is eliminated from turn 5") {
    aoc::game::GameState gs;
    gs.initialize(2);
    gs.players()[0]->addCity({5, 5}, "Home").setOriginalCapital(true);
    aoc::game::Player& ghost = *gs.players()[1];
    REQUIRE(ghost.unitCount() == 0);
    REQUIRE(ghost.ownedCityCount() == 0);

    aoc::sim::checkCollapseConditions(gs, 4);
    CHECK_FALSE(ghost.victoryTracker().isEliminated);

    aoc::sim::checkCollapseConditions(gs, 5);
    CHECK(ghost.victoryTracker().isEliminated);
    CHECK(ghost.victoryTracker().activeCollapse == aoc::sim::CollapseType::NeverFounded);
    CHECK_FALSE(gs.players()[0]->victoryTracker().isEliminated);
}

TEST_CASE("a settler still on the road is not eliminated") {
    aoc::game::GameState gs;
    gs.initialize(2);
    gs.players()[0]->addCity({5, 5}, "Home").setOriginalCapital(true);
    aoc::game::Player& walker = *gs.players()[1];
    walker.addUnit(SETTLER, {12, 9});

    aoc::sim::checkCollapseConditions(gs, 50);
    CHECK_FALSE(walker.victoryTracker().isEliminated);
}

/**
 * @file test_map_helpers.cpp
 * @brief The shared nearest-city and coastal-tile helpers. Five hand-written
 *        nearest-city loops and three coastal lambdas used to live in the
 *        simulation; one of the loops was not wrap-aware.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"

using aoc::PlayerId;

TEST_CASE("a player's nearest city is the closest of its own") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::City& far  = aoc::test::addCityAt(w, PlayerId{0}, 2, 2, "Far");
    aoc::game::City& near = aoc::test::addCityAt(w, PlayerId{0}, 10, 6, "Near");
    aoc::test::addCityAt(w, PlayerId{1}, 12, 6, "Rival");
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});

    int32_t dist = -1;
    CHECK(p.nearestCity(w.grid, {11, 6}, &dist) == &near);
    CHECK(dist == w.grid.distance({10, 6}, {11, 6}));
    CHECK(p.nearestCity(w.grid, {2, 3}) == &far);
    CHECK(w.gameState.player(PlayerId{1})->nearestCity(w.grid, {2, 3}) != &far);
}

TEST_CASE("a player with no city has no nearest city") {
    aoc::test::World w = aoc::test::makeWorld(1);
    int32_t dist       = 0;
    CHECK(w.gameState.player(PlayerId{0})->nearestCity(w.grid, {3, 3}, &dist) == nullptr);
}

TEST_CASE("the game's nearest city looks across every seat") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 2, 2, "Mine");
    aoc::game::City& theirs = aoc::test::addCityAt(w, PlayerId{1}, 12, 6, "Theirs");

    int32_t dist = -1;
    CHECK(w.gameState.nearestCity(w.grid, {13, 6}, &dist) == &theirs);
    CHECK(dist == 1);
    aoc::test::World empty = aoc::test::makeWorld(1);
    CHECK(empty.gameState.nearestCity(empty.grid, {1, 1}) == nullptr);
}

TEST_CASE("a tile is coastal when any neighbour is water") {
    aoc::test::World w = aoc::test::makeWorld(1);
    CHECK_FALSE(w.grid.isCoastal({5, 5}));
    const aoc::hex::AxialCoord shore{5, 5};
    const aoc::hex::AxialCoord sea = aoc::hex::neighbors(shore)[0];
    w.grid.setTerrain(w.grid.toIndex(sea), aoc::map::TerrainType::Coast);
    CHECK(w.grid.isCoastal(shore));
    CHECK_FALSE(w.grid.isCoastal({15, 10}));
}

/**
 * @file test_amphibious_path.cpp
 * @brief A route may leave the beach, cross, and land again.
 *
 *        findPath had two modes and neither served an embarking land unit:
 *        the default is land-only and `isNavalPath` is water-only, so there
 *        was no way to ask for a route from one landmass to another. The AI
 *        consequently had none -- tryEmbark is called from the human's
 *        right-click handler and nowhere else, requestLoadUnit from nowhere at
 *        all, so no AI unit could cross water by any route. Measured on seed
 *        42: of 4008 land tiles the civs could reach 1804, and the remaining
 *        2204 across seven landmasses, plus three city-states, were outside
 *        the game for every AI player.
 *
 *        This is the routing half only. It does not embark anything, does not
 *        check the Sailing and Shipbuilding gates that tryEmbark enforces, and
 *        does not know a loaded stack is defenceless.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Pathfinding.hpp"
#include "aoc/map/Terrain.hpp"

using aoc::map::TerrainType;

namespace {

constexpr int32_t WIDTH  = 16;
constexpr int32_t HEIGHT = 8;

/// Two land strips separated by a channel of open water running down q == 8.
/// Wide enough that no land detour exists, narrow enough to be crossed.
aoc::map::HexGrid twoShores() {
    aoc::map::HexGrid grid;
    grid.initialize(WIDTH, HEIGHT);
    for (int32_t r = 0; r < HEIGHT; ++r) {
        for (int32_t q = 0; q < WIDTH; ++q) {
            const int32_t idx  = grid.toIndex(aoc::hex::AxialCoord{q, r});
            const bool channel = (q >= 7 && q <= 9);
            grid.setTerrain(idx, channel ? TerrainType::ShallowWater : TerrainType::Plains);
        }
    }
    return grid;
}

constexpr aoc::hex::AxialCoord WEST{2, 4};
constexpr aoc::hex::AxialCoord EAST{13, 4};

} // namespace

TEST_CASE("a land route between two shores does not exist") {
    const aoc::map::HexGrid grid = twoShores();
    CHECK_FALSE(aoc::map::findPath(grid, WEST, EAST).has_value());
}

TEST_CASE("an amphibious route between two shores does") {
    const aoc::map::HexGrid grid = twoShores();
    const std::optional<aoc::map::PathResult> path =
        aoc::map::findPath(grid, WEST, EAST, /*maxCost=*/0, /*gameState=*/nullptr,
                           /*movingPlayer=*/aoc::INVALID_PLAYER, /*isNavalPath=*/false,
                           /*avoidCanals=*/false, /*amphibious=*/true);

    REQUIRE(path.has_value());
    CHECK(path->path.front() == WEST);
    CHECK(path->path.back() == EAST);

    // It must actually go through the water rather than round it: there is no
    // way round, but assert the crossing so a map change cannot quietly turn
    // this into a land walk that passes for the wrong reason.
    bool crossedWater = false;
    for (const aoc::hex::AxialCoord& step : path->path) {
        if (aoc::map::isWater(grid.terrain(grid.toIndex(step)))) {
            crossedWater = true;
        }
    }
    CHECK(crossedWater);
}

TEST_CASE("amphibious still refuses genuinely impassable ground") {
    aoc::map::HexGrid grid = twoShores();
    // Wall the eastern shore off with mountains, which are impassable ashore
    // and not water, so no mode can enter them.
    for (int32_t r = 0; r < HEIGHT; ++r) {
        for (int32_t q = 10; q < WIDTH; ++q) {
            grid.setTerrain(grid.toIndex(aoc::hex::AxialCoord{q, r}), TerrainType::Mountain);
        }
    }
    const std::optional<aoc::map::PathResult> path = aoc::map::findPath(
        grid, WEST, EAST, 0, nullptr, aoc::INVALID_PLAYER, false, false, /*amphibious=*/true);
    CHECK_FALSE(path.has_value());
}

TEST_CASE("the existing modes are untouched") {
    const aoc::map::HexGrid grid = twoShores();

    // Land-only still walks along one shore.
    const std::optional<aoc::map::PathResult> ashore =
        aoc::map::findPath(grid, aoc::hex::AxialCoord{1, 1}, aoc::hex::AxialCoord{5, 6});
    REQUIRE(ashore.has_value());
    for (const aoc::hex::AxialCoord& step : ashore->path) {
        CHECK_FALSE(aoc::map::isWater(grid.terrain(grid.toIndex(step))));
    }

    // Water-only still swims down the channel and will not climb ashore.
    const std::optional<aoc::map::PathResult> afloat =
        aoc::map::findPath(grid, aoc::hex::AxialCoord{8, 1}, aoc::hex::AxialCoord{8, 6}, 0, nullptr,
                           aoc::INVALID_PLAYER, /*isNavalPath=*/true);
    REQUIRE(afloat.has_value());
    for (const aoc::hex::AxialCoord& step : afloat->path) {
        CHECK(aoc::map::isWater(grid.terrain(grid.toIndex(step))));
    }
}

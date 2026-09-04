/**
 * @file test_start_placement.cpp
 * @brief Pins chooseStartPositions(): rivals share the largest landmass.
 *
 * A default 2-player game used to put the two civs on separate continents with
 * no land path, so they never met (sim-health H6 was red at 2 players). The
 * picker fills the largest landmass first; these cases pin that, the spacing
 * rule, the spill-over to the next landmass when spacing cannot be kept, and
 * determinism.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/core/Random.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/LandmassMetrics.hpp"
#include "aoc/map/StartPlacement.hpp"
#include "aoc/map/Terrain.hpp"

#include <cstddef>
#include <vector>

namespace {

constexpr int32_t WIDTH  = 40;
constexpr int32_t HEIGHT = 20;

/// Ocean everywhere, with two grassland blobs: a large one on the left and a
/// smaller one on the right, separated by open water.
aoc::map::HexGrid twoContinents() {
    aoc::map::HexGrid grid;
    grid.initialize(WIDTH, HEIGHT);
    for (int32_t i = 0; i < WIDTH * HEIGHT; ++i) {
        grid.setTerrain(i, aoc::map::TerrainType::Ocean);
    }
    for (int32_t row = 3; row < 17; ++row) {
        for (int32_t col = 2; col < 18; ++col) {
            grid.setTerrain(row * WIDTH + col, aoc::map::TerrainType::Grassland);
        }
        for (int32_t col = 26; col < 37; ++col) {
            grid.setTerrain(row * WIDTH + col, aoc::map::TerrainType::Grassland);
        }
    }
    return grid;
}

int32_t landmassOf(const aoc::map::LandmassMap& landmasses, const aoc::map::HexGrid& grid,
                   aoc::hex::AxialCoord c) {
    return landmasses.componentId[static_cast<std::size_t>(grid.toIndex(c))];
}

} // namespace

TEST_CASE("two players share the largest landmass with spacing kept") {
    const aoc::map::HexGrid grid       = twoContinents();
    const aoc::map::LandmassMap masses = aoc::map::computeLandmasses(grid);
    aoc::Random rng(42);

    const std::vector<aoc::hex::AxialCoord> starts = aoc::map::chooseStartPositions(grid, 2, rng);

    REQUIRE(starts.size() == 2);
    CHECK(landmassOf(masses, grid, starts[0]) == landmassOf(masses, grid, starts[1]));
    CHECK(grid.distance(starts[0], starts[1]) >= 8);
    for (const aoc::hex::AxialCoord& s : starts) {
        CHECK_FALSE(aoc::map::isWater(grid.terrain(grid.toIndex(s))));
    }
}

TEST_CASE("the largest landmass is preferred") {
    const aoc::map::HexGrid grid       = twoContinents();
    const aoc::map::LandmassMap masses = aoc::map::computeLandmasses(grid);
    aoc::Random rng(7);

    const std::vector<aoc::hex::AxialCoord> starts = aoc::map::chooseStartPositions(grid, 1, rng);

    REQUIRE(starts.size() == 1);
    const int32_t cid = landmassOf(masses, grid, starts[0]);
    int32_t largest   = 0;
    for (const int32_t size : masses.componentSize) {
        largest = size > largest ? size : largest;
    }
    CHECK(masses.componentSize[static_cast<std::size_t>(cid)] == largest);
}

TEST_CASE("spacing that no single landmass can keep spills to the next one") {
    const aoc::map::HexGrid grid       = twoContinents();
    const aoc::map::LandmassMap masses = aoc::map::computeLandmasses(grid);
    aoc::Random rng(3);
    aoc::map::StartPlacementOptions options;
    options.minStartDistance = 19; // wider than either blob

    const std::vector<aoc::hex::AxialCoord> starts =
        aoc::map::chooseStartPositions(grid, 2, rng, options);

    REQUIRE(starts.size() == 2);
    CHECK(landmassOf(masses, grid, starts[0]) != landmassOf(masses, grid, starts[1]));
}

TEST_CASE("placement is deterministic for the same seed") {
    const aoc::map::HexGrid grid = twoContinents();
    aoc::Random a(99);
    aoc::Random b(99);

    const std::vector<aoc::hex::AxialCoord> first  = aoc::map::chooseStartPositions(grid, 4, a);
    const std::vector<aoc::hex::AxialCoord> second = aoc::map::chooseStartPositions(grid, 4, b);

    REQUIRE(first.size() == 4);
    REQUIRE(second.size() == 4);
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].q == second[i].q);
        CHECK(first[i].r == second[i].r);
    }
}

TEST_CASE("an all-water map yields no starts instead of a water start") {
    aoc::map::HexGrid grid;
    grid.initialize(12, 8);
    for (int32_t i = 0; i < 12 * 8; ++i) {
        grid.setTerrain(i, aoc::map::TerrainType::Ocean);
    }
    aoc::Random rng(1);

    const std::vector<aoc::hex::AxialCoord> starts = aoc::map::chooseStartPositions(grid, 2, rng);

    CHECK(starts.empty());
}

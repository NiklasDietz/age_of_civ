/**
 * @file test_lakes.cpp
 * @brief Pins gen/Lakes.cpp -- closed-depression detection by priority flood.
 *
 * Why this test exists. Before this code existed, EVERY generated world had zero
 * lakes: the only writer of `lakeFlag` was gated on `orogeny[i] < -0.06f` while
 * `orogeny` only ever holds {0.0, 0.10, 1.0}, so the branch was unreachable and
 * five downstream passes that read the flag had been inert since it was
 * introduced. Nothing caught it because "no lakes" looks exactly like "no lakes
 * happened to form on this seed".
 *
 * So the cases below are the ones that distinguish a real depression fill from
 * something that merely produces water sometimes: a basin that CAN drain must
 * not flood, the flooded surface must sit at the spill point rather than at the
 * rim or the pit floor, and the whole thing must be anchored to the ocean so a
 * world without one does not flood everywhere.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/gen/Lakes.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

constexpr int32_t W             = 24;
constexpr int32_t H             = 16;
constexpr float ELEV_UNIT_TO_M  = 5000.0f;
constexpr float WATER_THRESHOLD = 0.0f;

aoc::map::HexGrid makeGrid() {
    aoc::map::HexGrid grid;
    grid.initialize(W, H, aoc::map::MapTopology::Cylindrical);
    return grid;
}

std::size_t at(int32_t col, int32_t row) {
    return static_cast<std::size_t>(row * W + col);
}

/// Land plateau at `plateau`, with an ocean column along col 0 so there is
/// always somewhere for water to drain to.
std::vector<float> plateauWithOceanEdge(float plateau) {
    std::vector<float> f(static_cast<std::size_t>(W * H), plateau);
    for (int32_t row = 0; row < H; ++row) {
        f[at(0, row)] = -0.20f; // ocean
    }
    return f;
}

/// Total flooded tiles, independent of how they are grouped into basins.
int32_t floodedCount(const aoc::map::gen::LakeResult& r) {
    int32_t n = 0;
    for (const uint8_t v : r.lakeFlag) {
        n += (v != 0u) ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("a flat plateau with an ocean edge has no lakes") {
    const aoc::map::HexGrid grid   = makeGrid();
    const std::vector<float> field = plateauWithOceanEdge(0.20f);
    const aoc::map::gen::LakeResult r =
        aoc::map::gen::findEndorheicLakes(grid, field, WATER_THRESHOLD, 10.0f, 1);
    CHECK(r.lakeCount == 0);
    CHECK(floodedCount(r) == 0);
}

TEST_CASE("a closed depression floods to its spill point, not to its rim") {
    // Plateau at 0.20 (1000 m). Carve a four-tile hollow down to 0.10 (500 m).
    // Water can only leave by climbing back to the plateau, so the surface
    // settles at 0.20 and every hollow tile ends up 500 m deep -- not 1000 m
    // (the rim measured from zero) and not 0 m (the pit floor).
    const aoc::map::HexGrid grid = makeGrid();
    std::vector<float> field     = plateauWithOceanEdge(0.20f);
    field[at(10, 7)]             = 0.10f;
    field[at(11, 7)]             = 0.10f;
    field[at(10, 8)]             = 0.10f;
    field[at(11, 8)]             = 0.10f;

    const aoc::map::gen::LakeResult r =
        aoc::map::gen::findEndorheicLakes(grid, field, WATER_THRESHOLD, 10.0f, 1);

    REQUIRE(r.lakeCount == 1);
    CHECK(floodedCount(r) == 4);
    CHECK(r.lakeFlag[at(10, 7)] == 1u);
    CHECK(r.lakeFlag[at(11, 8)] == 1u);
    // Spill 0.20 minus surface 0.10 = 0.10 unitless = 500 m.
    CHECK(r.lakeDepthM[at(10, 7)] == doctest::Approx(0.10f * ELEV_UNIT_TO_M));
    CHECK(r.lakeDepthM[at(11, 8)] == doctest::Approx(0.10f * ELEV_UNIT_TO_M));
    // The plateau itself is not under water.
    CHECK(r.lakeFlag[at(5, 3)] == 0u);
}

TEST_CASE("a basin with a downhill outlet to the ocean does not flood") {
    // THE case that separates a depression fill from "low ground is wet". The
    // hollow sits at 0.10, but a channel at 0.05 runs from it to the ocean, so
    // the water leaves and nothing floods.
    const aoc::map::HexGrid grid = makeGrid();
    std::vector<float> field     = plateauWithOceanEdge(0.20f);
    field[at(10, 7)]             = 0.10f;
    field[at(11, 7)]             = 0.10f;
    for (int32_t col = 1; col <= 9; ++col) {
        field[at(col, 7)] = 0.05f; // strictly below the hollow floor
    }

    const aoc::map::gen::LakeResult r =
        aoc::map::gen::findEndorheicLakes(grid, field, WATER_THRESHOLD, 10.0f, 1);
    CHECK(r.lakeCount == 0);
    CHECK(floodedCount(r) == 0);
}

TEST_CASE("the depth floor rejects shallow dips") {
    const aoc::map::HexGrid grid = makeGrid();
    std::vector<float> field     = plateauWithOceanEdge(0.20f);
    field[at(10, 7)]             = 0.198f; // 0.002 unitless = 10 m deep
    field[at(11, 7)]             = 0.198f;

    CHECK(aoc::map::gen::findEndorheicLakes(grid, field, WATER_THRESHOLD, 5.0f, 1).lakeCount == 1);

    const aoc::map::gen::LakeResult filtered =
        aoc::map::gen::findEndorheicLakes(grid, field, WATER_THRESHOLD, 50.0f, 1);
    CHECK(filtered.lakeCount == 0);
    CHECK(floodedCount(filtered) == 0);
}

TEST_CASE("the size floor rejects single-cell pits") {
    const aoc::map::HexGrid grid = makeGrid();
    std::vector<float> field     = plateauWithOceanEdge(0.20f);
    field[at(10, 7)]             = 0.10f; // one cell only

    CHECK(aoc::map::gen::findEndorheicLakes(grid, field, WATER_THRESHOLD, 10.0f, 1).lakeCount == 1);

    const aoc::map::gen::LakeResult filtered =
        aoc::map::gen::findEndorheicLakes(grid, field, WATER_THRESHOLD, 10.0f, 4);
    CHECK(filtered.lakeCount == 0);
    // Rejected basins must also have their depth cleared, or a consumer that
    // reads depth without checking the flag sees a lake that is not there.
    CHECK(filtered.lakeDepthM[at(10, 7)] == 0.0f);
}

TEST_CASE("a world with no ocean produces no lakes rather than flooding everywhere") {
    // The ocean is the algorithm's boundary condition. Without it there is no
    // drain, and a naive implementation would mark the entire map as one basin.
    const aoc::map::HexGrid grid = makeGrid();
    const std::vector<float> allLand(static_cast<std::size_t>(W * H), 0.20f);
    const aoc::map::gen::LakeResult r =
        aoc::map::gen::findEndorheicLakes(grid, allLand, WATER_THRESHOLD, 10.0f, 1);
    CHECK(r.lakeCount == 0);
    CHECK(floodedCount(r) == 0);
}

TEST_CASE("two separate depressions are two lakes") {
    const aoc::map::HexGrid grid = makeGrid();
    std::vector<float> field     = plateauWithOceanEdge(0.20f);
    field[at(8, 4)]              = 0.10f;
    field[at(9, 4)]              = 0.10f;
    field[at(16, 11)]            = 0.12f;
    field[at(17, 11)]            = 0.12f;

    const aoc::map::gen::LakeResult r =
        aoc::map::gen::findEndorheicLakes(grid, field, WATER_THRESHOLD, 10.0f, 1);
    CHECK(r.lakeCount == 2);
    CHECK(floodedCount(r) == 4);
}

TEST_CASE("repeated calls on identical input are identical") {
    // The priority queue tie-breaks on tile index precisely so that large
    // exactly-flat regions -- which this elevation field is full of -- cannot
    // make the outcome depend on heap internals. This repo has a byte-identical
    // determinism gate, so a flat-region ordering wobble would surface there as
    // an unexplained failure rather than here.
    const aoc::map::HexGrid grid = makeGrid();
    std::vector<float> field     = plateauWithOceanEdge(0.20f);
    for (int32_t col = 6; col <= 18; ++col) {
        for (int32_t row = 5; row <= 10; ++row) {
            field[at(col, row)] = 0.15f; // wide flat floor, many equal spills
        }
    }
    const aoc::map::gen::LakeResult a =
        aoc::map::gen::findEndorheicLakes(grid, field, WATER_THRESHOLD, 10.0f, 1);
    const aoc::map::gen::LakeResult b =
        aoc::map::gen::findEndorheicLakes(grid, field, WATER_THRESHOLD, 10.0f, 1);
    CHECK(a.lakeCount == b.lakeCount);
    CHECK(a.lakeFlag == b.lakeFlag);
    CHECK(a.lakeDepthM == b.lakeDepthM);
}

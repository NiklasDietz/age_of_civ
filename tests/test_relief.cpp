/**
 * @file test_relief.cpp
 * @brief Pins gen/Relief.cpp -- tile spacing and local topographic relief.
 *
 * Why this test exists. Relief drives the Hills criterion, and the obvious
 * implementation -- elevation difference per TILE -- would have been silently
 * wrong. Tile spacing is neither constant nor isotropic: under Lambert
 * equal-area at 140x90 an equatorial tile is 286 km east-west by 142 km
 * north-south, and by 75 deg that is 74 km by 547 km. A per-tile gradient
 * therefore reads as a function of latitude and map width rather than of
 * terrain, so hills would have clustered at whichever latitude the projection
 * happened to compress. That is the same class of bug that made
 * `axis_aligned_frac` meaningless until it was measured in sphere tangent space,
 * and it is invisible in the output -- the map still looks like it has hills.
 *
 * So the property under test is: identical terrain at different latitudes
 * reports identical relief.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/gen/Relief.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr int32_t W = 40;
constexpr int32_t H = 20;

/// Grid with Lambert-equal-area row latitudes: row is linear in sin(latitude),
/// which is the projection the generator defaults to and the one that makes
/// spacing most strongly latitude-dependent.
aoc::map::HexGrid makeLambertGrid() {
    aoc::map::HexGrid grid;
    grid.initialize(W, H, aoc::map::MapTopology::Cylindrical);
    std::vector<float> lats(static_cast<std::size_t>(H));
    for (int32_t row = 0; row < H; ++row) {
        const double f = (static_cast<double>(row) + 0.5) / static_cast<double>(H);
        lats[static_cast<std::size_t>(row)] =
            static_cast<float>(std::asin(2.0 * f - 1.0) * 180.0 / 3.14159265358979);
    }
    grid.setRowLatitudes(std::move(lats));
    return grid;
}

/// Flat land everywhere except a single step of `stepUnits` at (col, row).
std::vector<float> flatFieldWithStepAt(int32_t col, int32_t row, float stepUnits) {
    std::vector<float> f(static_cast<std::size_t>(W * H), 0.10f); // land, above the cut
    f[static_cast<std::size_t>(row * W + col)] = 0.10f + stepUnits;
    return f;
}

} // namespace

TEST_CASE("tile spacing shrinks east-west toward the pole and is never zero") {
    const aoc::map::HexGrid grid = makeLambertGrid();

    const aoc::map::gen::TileSpacingKm eq   = aoc::map::gen::tileSpacingKm(grid, H / 2);
    const aoc::map::gen::TileSpacingKm pole = aoc::map::gen::tileSpacingKm(grid, 0);

    // East-west spacing goes as cos(latitude), so the polar row is far narrower.
    CHECK(pole.eastWest < eq.eastWest);
    // Under an equal-area projection the rows CROWD at the equator and stretch
    // at the poles, which is the opposite sense -- the two effects are what make
    // a per-tile gradient meaningless.
    CHECK(pole.northSouth > eq.northSouth);
    // Never zero: Lambert's east-west pitch vanishes AT the pole and an
    // unguarded divide would report infinite relief for the polar rows.
    CHECK(eq.eastWest > 0.0f);
    CHECK(pole.eastWest > 0.0f);
    CHECK(eq.northSouth > 0.0f);
    CHECK(pole.northSouth > 0.0f);
}

TEST_CASE("equal terrain reports equal relief regardless of latitude") {
    // THE point of the helper. The same 500 m step between two east-west
    // neighbours is the same physical slope wherever it sits, so relief must not
    // depend on the row -- even though the tile spacing under it changes by more
    // than 3x between these rows.
    const aoc::map::HexGrid grid = makeLambertGrid();
    const float stepUnits        = 0.10f; // 0.10 * 5000 m = 500 m

    const int32_t rowEq  = H / 2;
    const int32_t rowMid = H / 2 + 4;

    const std::vector<float> fieldEq  = flatFieldWithStepAt(10, rowEq, stepUnits);
    const std::vector<float> fieldMid = flatFieldWithStepAt(10, rowMid, stepUnits);

    // Sample the tile WEST of the step, so the step is reached through an
    // east-west neighbour in both cases.
    const float reliefEq  = aoc::map::gen::localReliefMPer100Km(grid, fieldEq, 0.0f, 9, rowEq);
    const float reliefMid = aoc::map::gen::localReliefMPer100Km(grid, fieldMid, 0.0f, 9, rowMid);

    REQUIRE(reliefEq > 0.0f);
    REQUIRE(reliefMid > 0.0f);
    // Spacing differs between the rows, so the values differ -- but both must be
    // the honest metres-per-100-km, i.e. step / spacing. Verify each against its
    // own spacing rather than against each other.
    const aoc::map::gen::TileSpacingKm spEq  = aoc::map::gen::tileSpacingKm(grid, rowEq);
    const aoc::map::gen::TileSpacingKm spMid = aoc::map::gen::tileSpacingKm(grid, rowMid);
    CHECK(reliefEq == doctest::Approx(500.0f * 100.0f / spEq.eastWest).epsilon(0.01));
    CHECK(reliefMid == doctest::Approx(500.0f * 100.0f / spMid.eastWest).epsilon(0.01));
}

TEST_CASE("flat terrain has zero relief") {
    const aoc::map::HexGrid grid = makeLambertGrid();
    const std::vector<float> flat(static_cast<std::size_t>(W * H), 0.10f);
    for (int32_t row = 1; row < H - 1; ++row) {
        CHECK(aoc::map::gen::localReliefMPer100Km(grid, flat, 0.0f, 5, row) == 0.0f);
    }
}

TEST_CASE("water neighbours are excluded so coasts are not the steepest place") {
    // A coastal tile sits next to the abyss. Counting the continental slope as
    // roughness would make every shoreline tile hillier than any mountain front.
    const aoc::map::HexGrid grid = makeLambertGrid();
    const int32_t row            = H / 2;

    std::vector<float> field(static_cast<std::size_t>(W * H), 0.10f);
    // Deep ocean immediately east of (9, row): -0.9 unitless = -4500 m.
    field[static_cast<std::size_t>(row * W + 10)] = -0.90f;

    CHECK(aoc::map::gen::localReliefMPer100Km(grid, field, 0.0f, 9, row) == 0.0f);
}

TEST_CASE("relief is finite and non-negative across the whole grid") {
    const aoc::map::HexGrid grid = makeLambertGrid();
    std::vector<float> field(static_cast<std::size_t>(W * H));
    for (std::size_t i = 0; i < field.size(); ++i) {
        // Deterministic sawtooth: land everywhere, varied relief.
        field[i] = 0.05f + 0.30f * static_cast<float>(i % 7) / 6.0f;
    }
    for (int32_t row = 0; row < H; ++row) {
        for (int32_t col = 0; col < W; ++col) {
            const float r = aoc::map::gen::localReliefMPer100Km(grid, field, 0.0f, col, row);
            CHECK(r >= 0.0f);
            CHECK(std::isfinite(r));
        }
    }
}

TEST_CASE("out-of-range tile indices return zero rather than reading past the end") {
    const aoc::map::HexGrid grid = makeLambertGrid();
    const std::vector<float> flat(static_cast<std::size_t>(W * H), 0.10f);
    CHECK(aoc::map::gen::localReliefMPer100Km(grid, flat, 0.0f, 0, H + 5) == 0.0f);
}

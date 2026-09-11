/**
 * @file test_resource_geography.cpp
 * @brief The resource-geography instrument on grids whose answers are known:
 *        what each start can reach, the radius edge, the cylindrical seam, and
 *        the pairwise complementarity that the scarcity design is judged on.
 *        Real-map numbers live in tools/mapgen_metrics.py baselines, not here.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/LandmassMetrics.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

using aoc::hex::AxialCoord;
using aoc::map::ResourceGeography;
using aoc::sim::goods::COPPER_ORE;
using aoc::sim::goods::GEMS;
using aoc::sim::goods::HORSES;
using aoc::sim::goods::IRON_ORE;
using aoc::sim::goods::SILK;
using aoc::sim::goods::SPICES;

namespace {

constexpr int32_t WIDTH  = 40;
constexpr int32_t HEIGHT = 20;

aoc::map::HexGrid grassland(aoc::map::MapTopology topology = aoc::map::MapTopology::Flat) {
    aoc::map::HexGrid grid;
    grid.initialize(WIDTH, HEIGHT, topology);
    for (int32_t i = 0; i < WIDTH * HEIGHT; ++i) {
        grid.setTerrain(i, aoc::map::TerrainType::Grassland);
    }
    return grid;
}

[[nodiscard]] AxialCoord at(int32_t col, int32_t row) {
    return aoc::hex::offsetToAxial(aoc::hex::OffsetCoord{col, row});
}

void place(aoc::map::HexGrid& grid, int32_t col, int32_t row, uint16_t good) {
    grid.setResource(grid.toIndex(at(col, row)), aoc::ResourceId{good});
}

[[nodiscard]] std::vector<uint16_t> luxuryIds() {
    std::vector<uint16_t> ids;
    for (uint16_t id = 0; id < aoc::sim::goods::GOOD_COUNT; ++id) {
        if (aoc::sim::goodDef(id).category == aoc::sim::GoodCategory::RawLuxury) {
            ids.push_back(id);
        }
    }
    return ids;
}

const AxialCoord WEST = at(8, 9);
const AxialCoord EAST = at(31, 9);

} // namespace

TEST_CASE("each start sees the luxuries within reach, and complementary pairs count") {
    aoc::map::HexGrid grid = grassland();
    place(grid, 9, 9, SILK);    // west only
    place(grid, 32, 9, SPICES); // east only
    place(grid, 7, 9, GEMS);    // both
    place(grid, 30, 9, GEMS);

    const ResourceGeography geo = aoc::map::measureResourceGeography(grid, {WEST, EAST});
    const float types           = static_cast<float>(luxuryIds().size());
    REQUIRE(geo.luxuryTypes == static_cast<int32_t>(types));
    CHECK(geo.minLuxuryTypes == 2);
    CHECK(geo.luxuryTypesAbsent == doctest::Approx(1.0f - 2.0f / types));
    CHECK(geo.complementaryPairs == doctest::Approx(1.0f));
    CHECK_FALSE(geo.everyLuxuryOnMap);
    CHECK_FALSE(geo.copperOrIronEveryStart);
    CHECK(geo.horsesShare == doctest::Approx(0.0f));
}

TEST_CASE("copper or iron at every start satisfies D1, horses are a share of starts") {
    aoc::map::HexGrid grid = grassland();
    place(grid, 9, 9, COPPER_ORE);
    place(grid, 32, 9, IRON_ORE);
    place(grid, 33, 9, HORSES);
    const ResourceGeography geo = aoc::map::measureResourceGeography(grid, {WEST, EAST});
    CHECK(geo.copperOrIronEveryStart);
    CHECK(geo.horsesShare == doctest::Approx(0.5f));
}

TEST_CASE("B asks whether every luxury exists anywhere on the map") {
    aoc::map::HexGrid grid          = grassland();
    const std::vector<uint16_t> lux = luxuryIds();
    int32_t col                     = 0;
    for (const uint16_t id : lux) {
        place(grid, col++, 0, id);
    }
    CHECK(aoc::map::measureResourceGeography(grid, {WEST}).everyLuxuryOnMap);

    aoc::map::HexGrid missingOne = grassland();
    col                          = 0;
    for (std::size_t i = 1; i < lux.size(); ++i) {
        place(missingOne, col++, 0, lux[i]);
    }
    CHECK_FALSE(aoc::map::measureResourceGeography(missingOne, {WEST}).everyLuxuryOnMap);
}

TEST_CASE("a luxury just beyond the radius is out of reach") {
    aoc::map::HexGrid grid = grassland();
    place(grid, 8 + aoc::map::RESOURCE_REACH_RADIUS + 1, 9, SILK);
    CHECK(aoc::map::measureResourceGeography(grid, {WEST}).minLuxuryTypes == 0);
    place(grid, 8 + aoc::map::RESOURCE_REACH_RADIUS, 9, SILK);
    CHECK(aoc::map::measureResourceGeography(grid, {WEST}).minLuxuryTypes == 1);
}

TEST_CASE("reach crosses the seam of a cylindrical map and not the edge of a flat one") {
    const AxialCoord edge     = at(1, 9);
    aoc::map::HexGrid wrapped = grassland(aoc::map::MapTopology::Cylindrical);
    place(wrapped, WIDTH - 2, 9, SILK);
    CHECK(aoc::map::measureResourceGeography(wrapped, {edge}).minLuxuryTypes == 1);

    aoc::map::HexGrid flat = grassland(aoc::map::MapTopology::Flat);
    place(flat, WIDTH - 2, 9, SILK);
    CHECK(aoc::map::measureResourceGeography(flat, {edge}).minLuxuryTypes == 0);
}

TEST_CASE("E is the share of start pairs that each hold something the other lacks") {
    aoc::map::HexGrid grid = grassland();
    const AxialCoord north = at(20, 2);
    place(grid, 9, 9, SILK);    // west: silk
    place(grid, 32, 9, SILK);   // east: silk
    place(grid, 21, 2, SPICES); // north: spices
    const ResourceGeography geo = aoc::map::measureResourceGeography(grid, {WEST, EAST, north});
    CHECK(geo.complementaryPairs == doctest::Approx(2.0f / 3.0f));
}

TEST_CASE("no starts measures nothing; one start has no pairs") {
    aoc::map::HexGrid grid = grassland();
    place(grid, 9, 9, SILK);
    const ResourceGeography none = aoc::map::measureResourceGeography(grid, {});
    CHECK(none.minLuxuryTypes == 0);
    CHECK_FALSE(none.copperOrIronEveryStart);
    CHECK(none.complementaryPairs == doctest::Approx(0.0f));
    const ResourceGeography one = aoc::map::measureResourceGeography(grid, {WEST});
    CHECK(one.minLuxuryTypes == 1);
    CHECK(one.complementaryPairs == doctest::Approx(0.0f));
}

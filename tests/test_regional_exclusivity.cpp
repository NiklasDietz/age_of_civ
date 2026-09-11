/**
 * @file test_regional_exclusivity.cpp
 * @brief The regional exclusivity pass for Fair and Random placement (plan
 *        B3, step 1.6): nearest-start regions that wrap, the balanced
 *        strategics spread across them, each region denied a share of the
 *        luxury types with every type kept somewhere, same seed same map,
 *        and Realistic placement left to worldgen.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/core/Random.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/LandmassMetrics.hpp"
#include "aoc/map/MapGenerator.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <set>
#include <vector>

using aoc::hex::AxialCoord;
using aoc::map::MapGenerator;
using aoc::map::ResourcePlacementMode;

namespace {

constexpr int32_t WIDTH  = 40;
constexpr int32_t HEIGHT = 20;

[[nodiscard]] AxialCoord at(int32_t col, int32_t row) {
    return aoc::hex::offsetToAxial(aoc::hex::OffsetCoord{col, row});
}

/// Grassland everywhere. Every luxury type sits three times in each half of
/// the map, so both regions start with every type; eight iron tiles all sit
/// in the left half.
[[nodiscard]] aoc::map::HexGrid sprinkled(aoc::map::MapTopology topology = aoc::map::MapTopology::Flat) {
    aoc::map::HexGrid grid;
    grid.initialize(WIDTH, HEIGHT, topology);
    for (int32_t i = 0; i < WIDTH * HEIGHT; ++i) {
        grid.setTerrain(i, aoc::map::TerrainType::Grassland);
    }
    const std::vector<uint16_t>& luxuries = aoc::sim::luxuryGoodIds();
    for (std::size_t k = 0; k < luxuries.size(); ++k) {
        const int32_t row = static_cast<int32_t>(k % 18) + 1;
        for (int32_t copy = 0; copy < 3; ++copy) {
            grid.setResource(grid.toIndex(at(2 + copy * 5, row)), aoc::ResourceId{luxuries[k]});
            grid.setResource(grid.toIndex(at(22 + copy * 5, row)), aoc::ResourceId{luxuries[k]});
        }
    }
    for (int32_t k = 0; k < 8; ++k) {
        grid.setResource(grid.toIndex(at(1 + k * 2, 19)), aoc::ResourceId{aoc::sim::goods::IRON_ORE});
    }
    return grid;
}

[[nodiscard]] std::vector<AxialCoord> twoStarts() { return {at(8, 10), at(30, 10)}; }

[[nodiscard]] std::set<uint16_t> luxuryTypesIn(const aoc::map::HexGrid& grid, const std::vector<int32_t>& region,
                                              int32_t which) {
    std::set<uint16_t> types;
    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        const aoc::ResourceId r = grid.resource(i);
        if (region[static_cast<std::size_t>(i)] == which && r.isValid() && aoc::sim::isLuxuryGood(r.value)) {
            types.insert(r.value);
        }
    }
    return types;
}

[[nodiscard]] int32_t countOf(const aoc::map::HexGrid& grid, uint16_t good, const std::vector<int32_t>* region = nullptr,
                              int32_t which = -1) {
    int32_t n = 0;
    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        const aoc::ResourceId r = grid.resource(i);
        if (r.isValid() && r.value == good && (region == nullptr || (*region)[static_cast<std::size_t>(i)] == which)) {
            ++n;
        }
    }
    return n;
}

[[nodiscard]] std::vector<uint16_t> resources(const aoc::map::HexGrid& grid) {
    std::vector<uint16_t> out;
    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        out.push_back(grid.resource(i).isValid() ? grid.resource(i).value : 0xFFFFu);
    }
    return out;
}

} // namespace

TEST_CASE("each region lacks at least the denied share of luxury types, and every type survives") {
    aoc::map::HexGrid grid                = sprinkled();
    const std::vector<AxialCoord> starts  = twoStarts();
    const std::vector<int32_t> region     = MapGenerator::startRegions(grid, starts);
    const std::vector<uint16_t>& luxuries = aoc::sim::luxuryGoodIds();
    REQUIRE(luxuryTypesIn(grid, region, 0).size() == luxuries.size()); // both start with everything
    REQUIRE(luxuryTypesIn(grid, region, 1).size() == luxuries.size());

    aoc::Random rng(42);
    MapGenerator::balanceResourcesFair(grid, starts, ResourcePlacementMode::Fair, rng);

    const std::size_t denied = static_cast<std::size_t>(aoc::map::REGION_DENIED_FRACTION * luxuries.size() + 0.5f);
    const std::set<uint16_t> left  = luxuryTypesIn(grid, region, 0);
    const std::set<uint16_t> right = luxuryTypesIn(grid, region, 1);
    CHECK(left.size() <= luxuries.size() - denied);
    CHECK(right.size() <= luxuries.size() - denied);
    for (const uint16_t lux : luxuries) {
        CHECK(countOf(grid, lux) >= aoc::map::LUXURY_MIN_TILES);
    }
    // Each side holds something the other lacks.
    bool leftOnly = false, rightOnly = false;
    for (const uint16_t lux : luxuries) {
        leftOnly |= left.count(lux) == 1 && right.count(lux) == 0;
        rightOnly |= right.count(lux) == 1 && left.count(lux) == 0;
    }
    CHECK(leftOnly);
    CHECK(rightOnly);
}

TEST_CASE("a denied luxury becomes one of the same climate band, or bare land") {
    aoc::map::HexGrid grid               = sprinkled();
    const std::vector<uint16_t> before   = resources(grid);
    const std::vector<AxialCoord> starts = twoStarts();
    aoc::Random rng(42);
    MapGenerator::balanceResourcesFair(grid, starts, ResourcePlacementMode::Fair, rng);
    int32_t swapped = 0;
    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        const uint16_t was = before[static_cast<std::size_t>(i)];
        const aoc::ResourceId now = grid.resource(i);
        if (was == 0xFFFFu || !aoc::sim::isLuxuryGood(was) || !now.isValid() || now.value == was) {
            continue;
        }
        REQUIRE(aoc::sim::isLuxuryGood(now.value));
        CHECK(aoc::sim::goodDef(now.value).climateBand == aoc::sim::goodDef(was).climateBand);
        ++swapped;
    }
    CHECK(swapped > 0);
}

TEST_CASE("the balanced strategics are spread so each region holds its share") {
    aoc::map::HexGrid grid               = sprinkled();
    const std::vector<AxialCoord> starts = twoStarts();
    const std::vector<int32_t> region    = MapGenerator::startRegions(grid, starts);
    REQUIRE(countOf(grid, aoc::sim::goods::IRON_ORE, &region, 1) == 0);
    aoc::Random rng(42);
    MapGenerator::balanceResourcesFair(grid, starts, ResourcePlacementMode::Random, rng);
    const int32_t left  = countOf(grid, aoc::sim::goods::IRON_ORE, &region, 0);
    const int32_t right = countOf(grid, aoc::sim::goods::IRON_ORE, &region, 1);
    CHECK(left + right == 8);
    CHECK(left <= right + 1);
    CHECK(right <= left + 1);
}

TEST_CASE("the same seed gives the same map and another seed a different one") {
    aoc::map::HexGrid a = sprinkled();
    aoc::map::HexGrid b = sprinkled();
    aoc::map::HexGrid c = sprinkled();
    aoc::Random ra(7), rb(7), rc(8);
    MapGenerator::balanceResourcesFair(a, twoStarts(), ResourcePlacementMode::Fair, ra);
    MapGenerator::balanceResourcesFair(b, twoStarts(), ResourcePlacementMode::Fair, rb);
    MapGenerator::balanceResourcesFair(c, twoStarts(), ResourcePlacementMode::Fair, rc);
    CHECK(resources(a) == resources(b));
    CHECK(resources(a) != resources(c));
}

TEST_CASE("Realistic placement and a lone start are left alone") {
    aoc::map::HexGrid grid             = sprinkled();
    const std::vector<uint16_t> before = resources(grid);
    aoc::Random rng(42);
    MapGenerator::balanceResourcesFair(grid, twoStarts(), ResourcePlacementMode::Realistic, rng);
    CHECK(resources(grid) == before);
    MapGenerator::balanceResourcesFair(grid, {at(8, 10)}, ResourcePlacementMode::Fair, rng);
    const std::vector<int32_t> region = MapGenerator::startRegions(grid, {at(8, 10)});
    CHECK(luxuryTypesIn(grid, region, 0).size() == aoc::sim::luxuryGoodIds().size()); // nobody to complement
}

TEST_CASE("regions follow the nearest start and wrap on a cylinder") {
    const std::vector<AxialCoord> starts = {at(1, 10), at(20, 10)};
    const aoc::map::HexGrid flat         = sprinkled(aoc::map::MapTopology::Flat);
    const aoc::map::HexGrid cylinder     = sprinkled(aoc::map::MapTopology::Cylindrical);
    const std::vector<int32_t> onFlat    = MapGenerator::startRegions(flat, starts);
    const std::vector<int32_t> onCyl     = MapGenerator::startRegions(cylinder, starts);
    const std::size_t farEast            = static_cast<std::size_t>(flat.toIndex(at(39, 10)));
    CHECK(onFlat[farEast] == 1);  // 19 columns from the second start, 38 from the first
    CHECK(onCyl[farEast] == 0);   // two columns from the first start across the seam
    CHECK(onFlat[static_cast<std::size_t>(flat.toIndex(at(3, 10)))] == 0);
    aoc::map::HexGrid water = flat;
    water.setTerrain(water.toIndex(at(3, 10)), aoc::map::TerrainType::Ocean);
    CHECK(MapGenerator::startRegions(water, starts)[static_cast<std::size_t>(flat.toIndex(at(3, 10)))] == -1);
}

TEST_CASE("every start has a few allowed luxury types within reach, even on bare land") {
    aoc::map::HexGrid grid;
    grid.initialize(WIDTH, HEIGHT);
    for (int32_t i = 0; i < WIDTH * HEIGHT; ++i) {
        grid.setTerrain(i, aoc::map::TerrainType::Grassland);
    }
    const std::vector<AxialCoord> starts = twoStarts();
    aoc::Random rng(3);
    MapGenerator::balanceResourcesFair(grid, starts, ResourcePlacementMode::Fair, rng);
    for (const AxialCoord& start : starts) {
        std::set<uint16_t> near;
        for (int32_t i = 0; i < grid.tileCount(); ++i) {
            const aoc::ResourceId r = grid.resource(i);
            if (r.isValid() && aoc::sim::isLuxuryGood(r.value) &&
                grid.distance(grid.toAxial(i), start) <= aoc::map::RESOURCE_REACH_RADIUS) {
                near.insert(r.value);
            }
        }
        CHECK(static_cast<int32_t>(near.size()) >= aoc::map::REGION_MIN_LUXURY_TYPES);
    }
    const std::vector<int32_t> region = MapGenerator::startRegions(grid, starts);
    CHECK(luxuryTypesIn(grid, region, 0) != luxuryTypesIn(grid, region, 1)); // still complementary
}

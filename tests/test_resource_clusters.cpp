/**
 * @file test_resource_clusters.cpp
 * @brief The realistic luxury placer (plan B3, step 1.7): clusters seeded by
 *        farthest-point sampling among eligible tiles, kept apart, grown
 *        breadth-first through eligible tiles, wrapping across the seam;
 *        one climate-and-geology predicate per luxury; every luxury on the
 *        map and a few types within reach of every start.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/core/Random.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/LandmassMetrics.hpp"
#include "aoc/map/MapGenerator.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/gen/ResourceClusters.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <set>
#include <vector>

using aoc::hex::AxialCoord;
using aoc::map::ClusterSpec;
using aoc::sim::goods::SILK;

namespace {

constexpr int32_t WIDTH  = 60;
constexpr int32_t HEIGHT = 30;

[[nodiscard]] AxialCoord at(int32_t col, int32_t row) {
    return aoc::hex::offsetToAxial(aoc::hex::OffsetCoord{col, row});
}

/// Grassland everywhere with true latitudes from pole to pole.
[[nodiscard]] aoc::map::HexGrid world(aoc::map::MapTopology topology = aoc::map::MapTopology::Flat,
                                     int32_t width = WIDTH, int32_t height = HEIGHT) {
    aoc::map::HexGrid grid;
    grid.initialize(width, height, topology);
    for (int32_t i = 0; i < width * height; ++i) {
        grid.setTerrain(i, aoc::map::TerrainType::Grassland);
    }
    std::vector<float> lat(static_cast<std::size_t>(height));
    for (int32_t row = 0; row < height; ++row) {
        lat[static_cast<std::size_t>(row)] = -90.0f + 180.0f * (static_cast<float>(row) + 0.5f) / static_cast<float>(height);
    }
    grid.setRowLatitudes(lat);
    return grid;
}

[[nodiscard]] std::vector<int32_t> tilesOf(const aoc::map::HexGrid& grid, uint16_t good) {
    std::vector<int32_t> out;
    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        if (grid.resource(i).isValid() && grid.resource(i).value == good) {
            out.push_back(i);
        }
    }
    return out;
}

[[nodiscard]] std::vector<bool> everywhere(const aoc::map::HexGrid& grid) {
    return std::vector<bool>(static_cast<std::size_t>(grid.tileCount()), true);
}

} // namespace

TEST_CASE("the same seed lays the same clusters and another seed different ones") {
    aoc::map::HexGrid a = world(), b = world(), c = world();
    aoc::Random ra(5), rb(5), rc(6);
    const ClusterSpec spec{SILK, 3, 5};
    aoc::map::placeClusters(a, ra, spec, everywhere(a), 4000);
    aoc::map::placeClusters(b, rb, spec, everywhere(b), 4000);
    aoc::map::placeClusters(c, rc, spec, everywhere(c), 4000);
    CHECK(tilesOf(a, SILK) == tilesOf(b, SILK));
    CHECK(tilesOf(a, SILK) != tilesOf(c, SILK));
}

TEST_CASE("seeds scale with the land, keep their separation, and each cluster reaches its size") {
    aoc::map::HexGrid grid = world();
    aoc::Random rng(1);
    const ClusterSpec spec{SILK, 3, 5};
    const std::vector<int32_t> seeds = aoc::map::placeClusters(grid, rng, spec, everywhere(grid), 8000);
    CHECK(seeds.size() == 6); // 3 per 4000 land tiles
    for (std::size_t i = 0; i < seeds.size(); ++i) {
        for (std::size_t j = i + 1; j < seeds.size(); ++j) {
            CHECK(grid.distance(grid.toAxial(seeds[i]), grid.toAxial(seeds[j])) >= aoc::map::CLUSTER_MIN_SEPARATION);
        }
    }
    CHECK(tilesOf(grid, SILK).size() == seeds.size() * 5);

    aoc::map::HexGrid small = world();
    aoc::Random rng2(1);
    CHECK(aoc::map::placeClusters(small, rng2, spec, everywhere(small), 500).size() == 1); // never fewer than one
}

TEST_CASE("clusters use eligible tiles only and stop when the separation cannot be kept") {
    aoc::map::HexGrid grid = world();
    std::vector<bool> band(static_cast<std::size_t>(grid.tileCount()), false);
    for (int32_t row = 5; row < 10; ++row) {
        for (int32_t col = 0; col < 14; ++col) { // one 14 x 5 patch: room for a single seed
            band[static_cast<std::size_t>(grid.toIndex(at(col, row)))] = true;
        }
    }
    aoc::Random rng(9);
    const std::vector<int32_t> seeds = aoc::map::placeClusters(grid, rng, ClusterSpec{SILK, 3, 5}, band, 8000);
    CHECK(seeds.size() == 1);
    for (const int32_t i : tilesOf(grid, SILK)) {
        CHECK(band[static_cast<std::size_t>(i)]);
    }
}

TEST_CASE("a cluster grows across the seam on a cylinder and not on a flat map") {
    for (const aoc::map::MapTopology topology : {aoc::map::MapTopology::Cylindrical, aoc::map::MapTopology::Flat}) {
        aoc::map::HexGrid grid = world(topology, 40, 20);
        std::vector<bool> strip(static_cast<std::size_t>(grid.tileCount()), false);
        for (int32_t row = 8; row < 13; ++row) {
            for (const int32_t col : {0, 1, 38, 39}) {
                strip[static_cast<std::size_t>(grid.toIndex(at(col, row)))] = true;
            }
        }
        aoc::Random rng(2);
        aoc::map::placeClusters(grid, rng, ClusterSpec{SILK, 1, 12}, strip, 4000);
        bool west = false, east = false;
        for (const int32_t i : tilesOf(grid, SILK)) {
            west |= grid.toOffset(i).col <= 1;
            east |= grid.toOffset(i).col >= 38;
        }
        if (topology == aoc::map::MapTopology::Cylindrical) {
            CHECK((west && east));
        } else {
            CHECK(west != east);
        }
    }
}

TEST_CASE("each luxury's predicate follows climate and geology") {
    aoc::map::HexGrid grid = world();
    const int32_t equatorCoast = grid.toIndex(at(10, HEIGHT / 2));
    grid.setTerrain(grid.toIndex(at(11, HEIGHT / 2)), aoc::map::TerrainType::Ocean);
    CHECK(aoc::map::luxuryEligible(grid, aoc::sim::goods::SPICES, equatorCoast));
    CHECK(aoc::map::luxuryEligible(grid, aoc::sim::goods::PEARLS, equatorCoast));
    CHECK_FALSE(aoc::map::luxuryEligible(grid, aoc::sim::goods::SILK, equatorCoast)); // wrong band, coast
    const int32_t polar = grid.toIndex(at(10, 1));
    CHECK_FALSE(aoc::map::luxuryEligible(grid, aoc::sim::goods::SPICES, polar));
    grid.setTerrain(polar, aoc::map::TerrainType::Tundra);
    CHECK(aoc::map::luxuryEligible(grid, aoc::sim::goods::FURS, polar));
    const int32_t temperate = grid.toIndex(at(30, 3 * HEIGHT / 4)); // lat about 0.5
    grid.setFeature(temperate, aoc::map::FeatureType::Forest);
    CHECK(aoc::map::luxuryEligible(grid, aoc::sim::goods::SILK, temperate));
    grid.setFeature(temperate, aoc::map::FeatureType::Hills);
    CHECK_FALSE(aoc::map::luxuryEligible(grid, aoc::sim::goods::MARBLE, temperate));
    grid.setBoundaryTypeTile(temperate, 1u);
    CHECK(aoc::map::luxuryEligible(grid, aoc::sim::goods::MARBLE, temperate));
    CHECK(aoc::map::luxuryEligible(grid, aoc::sim::goods::GOLD_ORE, temperate));
    CHECK_FALSE(aoc::map::luxuryEligible(grid, aoc::sim::goods::GEMS, temperate)); // not a stable interior
    grid.setTerrain(temperate, aoc::map::TerrainType::Ocean);
    CHECK_FALSE(aoc::map::luxuryEligible(grid, aoc::sim::goods::MARBLE, temperate));
    // The band fallback knows only latitude.
    CHECK(aoc::map::luxuryBandEligible(grid, aoc::sim::goods::SPICES, equatorCoast));
    CHECK_FALSE(aoc::map::luxuryBandEligible(grid, aoc::sim::goods::SPICES, polar));
    CHECK(aoc::map::luxuryBandEligible(grid, aoc::sim::goods::FURS, polar));
}

TEST_CASE("every luxury ends up on the map and every start has a few types in reach") {
    aoc::map::HexGrid grid = world();
    for (int32_t row = 0; row < HEIGHT; ++row) { // hills every fourth column, a river every fifth row
        for (int32_t col = 0; col < WIDTH; ++col) {
            const int32_t i = grid.toIndex(at(col, row));
            if (col % 4 == 0) {
                grid.setFeature(i, aoc::map::FeatureType::Hills);
            }
            if (row % 5 == 0) {
                grid.setRiverEdges(i, 1u);
            }
        }
    }
    const std::vector<AxialCoord> starts = {at(10, 8), at(45, 21)};
    aoc::Random rng(4);
    aoc::map::placeLuxuryClusters(grid, starts, rng);
    for (const uint16_t lux : aoc::sim::luxuryGoodIds()) {
        CHECK(!tilesOf(grid, lux).empty());
    }
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
}

TEST_CASE("horses cluster on steppe and reach half the starts") {
    aoc::map::HexGrid grid = world();
    const int32_t steppe   = grid.toIndex(at(10, 3 * HEIGHT / 4)); // lat about 0.5, open grass
    std::vector<uint8_t> aridity(static_cast<std::size_t>(grid.tileCount()), 120u);
    grid.setAridityIndex(aridity);
    CHECK(aoc::map::strategicEligible(grid, aoc::sim::goods::HORSES, steppe));
    grid.setFeature(steppe, aoc::map::FeatureType::Forest);
    CHECK_FALSE(aoc::map::strategicEligible(grid, aoc::sim::goods::HORSES, steppe));
    CHECK_FALSE(aoc::map::strategicEligible(grid, aoc::sim::goods::HORSES, grid.toIndex(at(10, HEIGHT / 2)))); // equator
    CHECK_FALSE(aoc::map::strategicEligible(grid, aoc::sim::goods::IRON_ORE, steppe)); // not clustered

    const std::vector<AxialCoord> starts = {at(5, 5), at(25, 5), at(45, 5), at(15, 24)};
    aoc::Random rng(11);
    aoc::map::placeStrategicClusters(grid, starts, rng);
    int32_t served = 0;
    for (const AxialCoord& start : starts) {
        bool near = false;
        for (const int32_t i : tilesOf(grid, aoc::sim::goods::HORSES)) {
            near |= grid.distance(grid.toAxial(i), start) <= aoc::map::RESOURCE_REACH_RADIUS;
        }
        served += near ? 1 : 0;
    }
    CHECK(served >= 2);
    CHECK(!tilesOf(grid, aoc::sim::goods::HORSES).empty());
}

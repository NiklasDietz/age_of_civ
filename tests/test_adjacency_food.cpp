/**
 * @file test_adjacency_food.cpp
 * @brief District adjacency food reaches the city. AdjacencyBonus has six yield
 *        columns; five of them -- production, gold, science, culture, faith --
 *        reached city output, and food reached nothing. It was dead at both
 *        ends: no rule granted it and no pass read it.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/simulation/city/DistrictAdjacency.hpp"

using aoc::PlayerId;
using aoc::sim::AdjacencyBonus;
using aoc::sim::DistrictType;

namespace {

constexpr aoc::hex::AxialCoord CITY{5, 5};

/// Turn `n` of the city's neighbours into coast carrying a resource, which is
/// what `countNeighborTerrain` counts as a coastal resource.
void makeCoastalResources(aoc::test::World& w, int32_t n) {
    int32_t made = 0;
    for (const aoc::hex::AxialCoord& nbr : aoc::hex::neighbors(CITY)) {
        if (made >= n || !w.grid.isValid(nbr)) {
            continue;
        }
        const int32_t idx = w.grid.toIndex(nbr);
        w.grid.setTerrain(idx, aoc::map::TerrainType::Coast);
        w.grid.setResource(idx, aoc::ResourceId{aoc::sim::goods::FISH});
        ++made;
    }
}

} // namespace

TEST_CASE("a Harbor beside coastal resources earns food, not only gold") {
    aoc::test::World w = aoc::test::makeWorld(1);
    aoc::test::addCityAt(w, PlayerId{0}, CITY.q, CITY.r, "Alpha");

    aoc::sim::DistrictIndex idx;
    idx.build(*w.gameState.player(PlayerId{0}));

    const int32_t tile = w.grid.toIndex(CITY);
    const AdjacencyBonus dry =
        aoc::sim::computeAdjacencyBonus(w.grid, idx, DistrictType::Harbor, tile);
    CHECK(dry.food == doctest::Approx(0.0f));

    makeCoastalResources(w, 2);
    const AdjacencyBonus wet =
        aoc::sim::computeAdjacencyBonus(w.grid, idx, DistrictType::Harbor, tile);

    CHECK(wet.food > dry.food);
    CHECK(wet.gold > dry.gold); // the gold it always paid is unchanged in kind
}

TEST_CASE("cityAdjacencyYields carries food up from the districts") {
    // The summing step must not drop the column on its way to the city.
    aoc::test::World w = aoc::test::makeWorld(1);
    aoc::test::addCityAt(w, PlayerId{0}, CITY.q, CITY.r, "Alpha");
    aoc::game::City& city = *w.gameState.player(PlayerId{0})->cities()[0];
    makeCoastalResources(w, 3);

    // Give the city a Harbor standing on its centre tile.
    city.districts().districts.push_back({DistrictType::Harbor, CITY, {}});

    aoc::sim::DistrictIndex idx;
    idx.build(*w.gameState.player(PlayerId{0}));

    const AdjacencyBonus total = aoc::sim::cityAdjacencyYields(w.grid, idx, city);
    CHECK(total.food > 0.0f);
}

TEST_CASE("a city with no districts earns no adjacency food") {
    aoc::test::World w = aoc::test::makeWorld(1);
    aoc::test::addCityAt(w, PlayerId{0}, CITY.q, CITY.r, "Alpha");
    aoc::game::City& city = *w.gameState.player(PlayerId{0})->cities()[0];
    makeCoastalResources(w, 3);

    aoc::sim::DistrictIndex idx;
    idx.build(*w.gameState.player(PlayerId{0}));

    // Only the City Center it was founded with, which grants no food adjacency.
    const AdjacencyBonus total = aoc::sim::cityAdjacencyYields(w.grid, idx, city);
    CHECK(total.food == doctest::Approx(0.0f));
}

/**
 * @file test_district_placement.cpp
 * @brief District tiles: the placement rules, the deterministic scorer and what a district costs its tile.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/city/DistrictPlacement.hpp"
#include "aoc/simulation/city/ProductionSystem.hpp"

#include <vector>

using aoc::PlayerId;
using aoc::hex::AxialCoord;
using aoc::sim::DistrictTileReason;
using aoc::sim::DistrictType;

namespace {

/// Grassland everywhere, one city, its whole work radius owned by the player.
struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(2);
    aoc::game::City* city  = nullptr;

    Fixture() {
        this->city = &aoc::test::addCityAt(this->world, PlayerId{0}, 8, 6, "Alpha");
        this->city->setPopulation(4);
        std::vector<AxialCoord> nearby;
        aoc::hex::spiral(this->city->location(), aoc::sim::CITY_WORK_RADIUS, std::back_inserter(nearby));
        for (const AxialCoord& tile : nearby) {
            if (this->world.grid.isValid(tile)) {
                this->world.grid.setOwner(this->world.grid.toIndex(tile), PlayerId{0});
            }
        }
        // City construction already puts a City Center on the centre hex.
        REQUIRE(this->city->districts().districts.size() == 1);
        REQUIRE(this->city->districts().districts.front().location == this->city->location());
    }

    [[nodiscard]] DistrictTileReason reason(DistrictType type, AxialCoord at) const {
        return aoc::sim::districtTileReason(this->world.gameState, this->world.grid, *this->city, type, at);
    }
};

} // namespace

TEST_CASE("a district tile must be owned, near, free, on land and not the centre") {
    Fixture f;
    const AxialCoord centre = f.city->location();
    CHECK(f.reason(DistrictType::Campus, centre) == DistrictTileReason::CityCenter);
    CHECK(f.reason(DistrictType::Campus, {centre.q + 1, centre.r}) == DistrictTileReason::Ok);
    CHECK(f.reason(DistrictType::Campus, {-40, -40}) == DistrictTileReason::OffMap);
    CHECK(f.reason(DistrictType::Campus, {centre.q + aoc::sim::CITY_WORK_RADIUS + 1, centre.r})
          == DistrictTileReason::NotOwned); // beyond the claimed ring, so ownership fails first

    const AxialCoord far = {centre.q + aoc::sim::CITY_WORK_RADIUS + 1, centre.r};
    if (f.world.grid.isValid(far)) {
        f.world.grid.setOwner(f.world.grid.toIndex(far), PlayerId{0});
        CHECK(f.reason(DistrictType::Campus, far) == DistrictTileReason::TooFar);
    }

    const AxialCoord theirs = {centre.q - 1, centre.r};
    f.world.grid.setOwner(f.world.grid.toIndex(theirs), PlayerId{1});
    CHECK(f.reason(DistrictType::Campus, theirs) == DistrictTileReason::NotOwned);

    const AxialCoord taken = {centre.q + 2, centre.r};
    f.city->districts().districts.push_back({DistrictType::Campus, taken, {}});
    CHECK(f.reason(DistrictType::Industrial, taken) == DistrictTileReason::Occupied);
    CHECK(aoc::sim::tileHasDistrict(f.world.gameState, taken));
    CHECK_FALSE(aoc::sim::tileHasDistrict(f.world.gameState, {centre.q + 3, centre.r}));
}

TEST_CASE("a Harbor needs coastal water beside the city; everything else needs land") {
    Fixture f;
    const AxialCoord centre = f.city->location();
    const AxialCoord shore  = {centre.q + 1, centre.r};
    const AxialCoord open   = {centre.q + 2, centre.r};
    f.world.grid.setTerrain(f.world.grid.toIndex(shore), aoc::map::TerrainType::Coast);
    f.world.grid.setTerrain(f.world.grid.toIndex(open), aoc::map::TerrainType::Coast);

    CHECK(f.reason(DistrictType::Harbor, shore) == DistrictTileReason::Ok);
    CHECK(f.reason(DistrictType::Harbor, open) == DistrictTileReason::NeedsWater); // two tiles out
    CHECK(f.reason(DistrictType::Harbor, {centre.q, centre.r + 1}) == DistrictTileReason::NeedsWater);
    CHECK(f.reason(DistrictType::Campus, shore) == DistrictTileReason::NeedsLand);

    const AxialCoord peak = {centre.q, centre.r - 1};
    f.world.grid.setTerrain(f.world.grid.toIndex(peak), aoc::map::TerrainType::Mountain);
    CHECK(f.reason(DistrictType::Campus, peak) == DistrictTileReason::Impassable);
}

TEST_CASE("the scorer prefers adjacency and repeats its choice exactly") {
    Fixture f;
    const AxialCoord centre = f.city->location();
    // Mountains next to one candidate: a Campus scores +1 science per mountain.
    const AxialCoord scienceSite = {centre.q + 2, centre.r};
    for (const AxialCoord& n : aoc::hex::neighbors(scienceSite)) {
        if (f.world.grid.isValid(n) && n != centre) {
            f.world.grid.setTerrain(f.world.grid.toIndex(n), aoc::map::TerrainType::Mountain);
        }
    }
    const AxialCoord first  = aoc::sim::bestDistrictTile(f.world.gameState, f.world.grid, *f.city,
                                                         DistrictType::Campus);
    const AxialCoord second = aoc::sim::bestDistrictTile(f.world.gameState, f.world.grid, *f.city,
                                                         DistrictType::Campus);
    CHECK(first == second);
    CHECK(first == scienceSite);
    CHECK(aoc::sim::districtTileScore(f.world.gameState, f.world.grid, DistrictType::Campus, scienceSite)
          > aoc::sim::districtTileScore(f.world.gameState, f.world.grid, DistrictType::Campus,
                                        {centre.q, centre.r + 2}));

    // Boxed in: no candidate, so the centre stays the fallback.
    Fixture boxed;
    std::vector<AxialCoord> nearby;
    aoc::hex::spiral(boxed.city->location(), aoc::sim::CITY_WORK_RADIUS, std::back_inserter(nearby));
    for (const AxialCoord& tile : nearby) {
        if (boxed.world.grid.isValid(tile) && tile != boxed.city->location()) {
            boxed.world.grid.setOwner(boxed.world.grid.toIndex(tile), PlayerId{1});
        }
    }
    CHECK(aoc::sim::districtCandidateTiles(boxed.world.gameState, boxed.world.grid, *boxed.city,
                                           DistrictType::Campus).empty());
    CHECK(aoc::sim::bestDistrictTile(boxed.world.gameState, boxed.world.grid, *boxed.city,
                                     DistrictType::Campus) == boxed.city->location());
}

TEST_CASE("placing a district clears the tile's improvement and its worker") {
    Fixture f;
    const AxialCoord centre = f.city->location();
    const AxialCoord site   = {centre.q + 1, centre.r};
    const int32_t index     = f.world.grid.toIndex(site);
    f.world.grid.setImprovement(index, aoc::map::ImprovementType::Farm);
    f.world.grid.setPillaged(index, true);
    f.city->assignWorker(site);
    REQUIRE(f.city->isTileWorked(site));

    aoc::sim::placeDistrictOnTile(f.world.grid, *f.city, DistrictType::Campus, site);
    CHECK(f.world.grid.improvement(index) == aoc::map::ImprovementType::None);
    CHECK_FALSE(f.world.grid.isPillaged(index));
    CHECK_FALSE(f.city->isTileWorked(site));
    CHECK(f.city->hasDistrictOn(site));

    // A citizen cannot be put back on it, but the centre stays workable.
    f.city->assignWorker(site);
    CHECK_FALSE(f.city->isTileWorked(site));
    f.city->assignWorker(centre);
    CHECK(f.city->isTileWorked(centre));
}

TEST_CASE("a completed district lands on a real tile, not on the city centre") {
    Fixture f;
    aoc::sim::ProductionQueueItem item;
    item.type      = aoc::sim::ProductionItemType::District;
    item.itemId    = static_cast<uint16_t>(DistrictType::Campus);
    item.name      = "Campus";
    item.totalCost = 1.0f;
    item.progress  = 1000.0f; // completes on the first pass
    f.city->production().queue.push_back(item);

    aoc::sim::processProductionQueues(f.world.gameState, f.world.grid, PlayerId{0});

    const std::vector<aoc::sim::CityDistrictsComponent::PlacedDistrict>& placed =
        f.city->districts().districts;
    REQUIRE(placed.size() == 2); // City Center plus the new Campus
    CHECK(placed[1].type == DistrictType::Campus);
    CHECK(placed[1].location != f.city->location());
    CHECK(f.world.grid.distance(placed[1].location, f.city->location()) <= aoc::sim::CITY_WORK_RADIUS);
}

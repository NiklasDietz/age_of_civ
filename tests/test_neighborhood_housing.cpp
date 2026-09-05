/**
 * @file test_neighborhood_housing.cpp
 * @brief The Neighborhood (45) is plain housing next to the Aqueduct: its row and
 *        civic gate, the central `buildingHousing` table that replaced the id
 *        checks in `computeCityHousing`, the Aqueduct's connection rule kept, and
 *        the +4 housing a city gains once a Neighborhood stands.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/city/CityGrowth.hpp"
#include "aoc/simulation/city/District.hpp"

using aoc::BuildingId;
using aoc::CivicId;
using aoc::PlayerId;
using aoc::sim::BUILDING_DEFS;
using aoc::sim::buildingDef;
using aoc::sim::buildingHousing;
using aoc::sim::DistrictType;

namespace {

constexpr BuildingId GRANARY{15};
constexpr BuildingId HOSPITAL{22};
constexpr BuildingId AQUEDUCT{42};
constexpr BuildingId NEIGHBORHOOD{45};

} // namespace

TEST_CASE("the Neighborhood has a row, a City Center district and the Urbanization gate") {
    CHECK(BUILDING_DEFS.size() == 46);
    const aoc::sim::BuildingDef& def = buildingDef(NEIGHBORHOOD);
    CHECK(def.name == "Neighborhood");
    CHECK(def.requiredDistrict == DistrictType::CityCenter);
    CHECK(def.requiredCivic == CivicId{34});
    CHECK(def.productionCost == 120);
    for (std::size_t i = 0; i < BUILDING_DEFS.size(); ++i) {
        CHECK(BUILDING_DEFS[i].id.value == i);
    }
}

TEST_CASE("buildingHousing keeps the old credits and gives the Neighborhood four") {
    CHECK(buildingHousing(GRANARY) == 2);
    CHECK(buildingHousing(HOSPITAL) == 4);
    CHECK(buildingHousing(AQUEDUCT) == 4);
    CHECK(buildingHousing(NEIGHBORHOOD) == 4);
    CHECK(buildingHousing(BuildingId{0}) == 0);  // Forge
    CHECK(buildingHousing(BuildingId{43}) == 0); // Entertainment Complex: amenities, not housing
}

TEST_CASE("computeCityHousing credits the Neighborhood and only a connected Aqueduct") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::game::City& city = *w.gameState.player(PlayerId{0})->cities()[0];
    REQUIRE(!city.districts().districts.empty());
    std::vector<BuildingId>& center = city.districts().districts[0].buildings;

    const int32_t base = aoc::sim::computeCityHousing(city, w.grid);
    CHECK(base >= 4);

    center.push_back(AQUEDUCT);
    city.setAqueductConnected(false);
    CHECK(aoc::sim::computeCityHousing(city, w.grid) == base); // dry aqueduct: nothing
    city.setAqueductConnected(true);
    CHECK(aoc::sim::computeCityHousing(city, w.grid) == base + 4);

    center.push_back(NEIGHBORHOOD);
    CHECK(aoc::sim::computeCityHousing(city, w.grid) == base + 8);

    center.push_back(GRANARY);
    center.push_back(HOSPITAL);
    CHECK(aoc::sim::computeCityHousing(city, w.grid) == base + 14);
}

/**
 * @file test_neighborhood_housing.cpp
 * @brief The Neighborhood (45) housing depends on appeal. Tests cover the tier
 *        thresholds, the base city housing tiers (dry/coastal/fresh-water), the
 *        `buildingHousing` display hint, and the Aqueduct's connection rule.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/city/CityGrowth.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/DistrictAdjacency.hpp"

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
    CHECK(BUILDING_DEFS.size() >= 46); // later rows (Spaceport 46) may follow
    const aoc::sim::BuildingDef& def = buildingDef(NEIGHBORHOOD);
    CHECK(def.name == "Neighborhood");
    CHECK(def.requiredDistrict == DistrictType::CityCenter);
    CHECK(def.requiredCivic == CivicId{34});
    CHECK(def.productionCost == 120);
    for (std::size_t i = 0; i < BUILDING_DEFS.size(); ++i) {
        CHECK(BUILDING_DEFS[i].id.value == i);
    }
}

TEST_CASE("buildingHousing keeps the ordinary value for display and non-nil checks") {
    CHECK(buildingHousing(GRANARY) == 2);
    CHECK(buildingHousing(HOSPITAL) == 4);
    CHECK(buildingHousing(AQUEDUCT) == 4);
    // The Neighborhood's `buildingHousing` returns the ordinary (middle) tier so
    // the encyclopedia and tooltips show a meaningful number.  The actual value in
    // `computeCityHousing` is gated on appeal and overrides this via `continue`.
    CHECK(buildingHousing(NEIGHBORHOOD) == 4);
    CHECK(buildingHousing(BuildingId{0}) == 0);  // Forge
    CHECK(buildingHousing(BuildingId{43}) == 0); // Entertainment Complex: amenities, not housing
}

TEST_CASE("neighborhoodHousing returns the right tier for each appeal bracket") {
    // squalid: appeal <= APPEAL_SQUALID (-2)
    CHECK(aoc::sim::neighborhoodHousing(-3) == aoc::sim::NEIGHBORHOOD_SQUALID);
    CHECK(aoc::sim::neighborhoodHousing(aoc::sim::APPEAL_SQUALID) ==
          aoc::sim::NEIGHBORHOOD_SQUALID);
    // ordinary: -1, 0, 1
    CHECK(aoc::sim::neighborhoodHousing(-1) == aoc::sim::NEIGHBORHOOD_ORDINARY);
    CHECK(aoc::sim::neighborhoodHousing(0) == aoc::sim::NEIGHBORHOOD_ORDINARY);
    CHECK(aoc::sim::neighborhoodHousing(1) == aoc::sim::NEIGHBORHOOD_ORDINARY);
    // pleasant: appeal >= APPEAL_PLEASANT (2)
    CHECK(aoc::sim::neighborhoodHousing(aoc::sim::APPEAL_PLEASANT) ==
          aoc::sim::NEIGHBORHOOD_PLEASANT);
    CHECK(aoc::sim::neighborhoodHousing(5) == aoc::sim::NEIGHBORHOOD_PLEASANT);
    // tiers are ordered
    CHECK(aoc::sim::NEIGHBORHOOD_SQUALID < aoc::sim::NEIGHBORHOOD_ORDINARY);
    CHECK(aoc::sim::NEIGHBORHOOD_ORDINARY < aoc::sim::NEIGHBORHOOD_PLEASANT);
}

TEST_CASE("computeCityHousing base is HOUSING_DRY on a dry grassland grid") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::game::City& city = *w.gameState.player(PlayerId{0})->cities()[0];
    // makeWorld produces all-grassland, no rivers, no water neighbours
    const int32_t base = aoc::sim::computeCityHousing(city, w.grid);
    CHECK(base == aoc::sim::HOUSING_DRY);
    CHECK(aoc::sim::HOUSING_DRY < aoc::sim::HOUSING_COASTAL);
    CHECK(aoc::sim::HOUSING_COASTAL < aoc::sim::HOUSING_FRESH_WATER);
}

TEST_CASE("computeCityHousing credits the Neighborhood and only a connected Aqueduct") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::game::City& city = *w.gameState.player(PlayerId{0})->cities()[0];
    REQUIRE(!city.districts().districts.empty());
    std::vector<BuildingId>& center = city.districts().districts[0].buildings;

    const int32_t base = aoc::sim::computeCityHousing(city, w.grid);
    // dry grassland: HOUSING_DRY
    CHECK(base == aoc::sim::HOUSING_DRY);

    center.push_back(AQUEDUCT);
    city.setAqueductConnected(false);
    CHECK(aoc::sim::computeCityHousing(city, w.grid) == base); // dry aqueduct: nothing
    city.setAqueductConnected(true);
    CHECK(aoc::sim::computeCityHousing(city, w.grid) == base + 4);

    // Neighborhood without gameState: appeal = 0, tier = NEIGHBORHOOD_ORDINARY (4)
    center.push_back(NEIGHBORHOOD);
    CHECK(aoc::sim::computeCityHousing(city, w.grid) == base + 4 + aoc::sim::NEIGHBORHOOD_ORDINARY);

    center.push_back(GRANARY);
    center.push_back(HOSPITAL);
    CHECK(aoc::sim::computeCityHousing(city, w.grid) ==
          base + 4 + aoc::sim::NEIGHBORHOOD_ORDINARY + 2 + 4);
}

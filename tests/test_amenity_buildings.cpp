/**
 * @file test_amenity_buildings.cpp
 * @brief The Entertainment Complex (43) and the Water Park (44) are amenity
 *        sources: their rows, their civic gates, the central `buildingAmenities`
 *        table that replaced the id checks in the happiness pass, and the +2
 *        amenities a city gains once one stands.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/Happiness.hpp"

using aoc::BuildingId;
using aoc::CivicId;
using aoc::PlayerId;
using aoc::sim::BUILDING_DEFS;
using aoc::sim::buildingAmenities;
using aoc::sim::buildingDef;
using aoc::sim::DistrictType;

namespace {

constexpr BuildingId ENTERTAINMENT_COMPLEX{43};
constexpr BuildingId WATER_PARK{44};

} // namespace

TEST_CASE("the two amenity buildings have rows, districts and civic gates") {
    CHECK(BUILDING_DEFS.size() == 45);
    const aoc::sim::BuildingDef& complex = buildingDef(ENTERTAINMENT_COMPLEX);
    CHECK(complex.name == "Entertainment Complex");
    CHECK(complex.requiredDistrict == DistrictType::CityCenter);
    CHECK(complex.requiredCivic == CivicId{20}); // Games and Recreation
    CHECK(complex.maintenanceCost == 2);

    const aoc::sim::BuildingDef& park = buildingDef(WATER_PARK);
    CHECK(park.name == "Water Park");
    CHECK(park.requiredDistrict == DistrictType::Harbor); // coastal by construction
    CHECK(park.requiredCivic == CivicId{34});             // Urbanization

    // Every row still sits at the index of its own id.
    for (std::size_t i = 0; i < BUILDING_DEFS.size(); ++i) {
        CHECK(BUILDING_DEFS[i].id.value == i);
    }
}

TEST_CASE("buildingAmenities keeps the old credits and adds +2 for the new buildings") {
    CHECK(buildingAmenities(BuildingId{6}) == doctest::Approx(0.5f));  // Market
    CHECK(buildingAmenities(BuildingId{15}) == doctest::Approx(0.5f)); // Granary
    CHECK(buildingAmenities(BuildingId{16}) == doctest::Approx(0.5f)); // Monument
    CHECK(buildingAmenities(BuildingId{22}) == doctest::Approx(1.0f)); // Hospital
    CHECK(buildingAmenities(ENTERTAINMENT_COMPLEX) == doctest::Approx(2.0f));
    CHECK(buildingAmenities(WATER_PARK) == doctest::Approx(2.0f));
    CHECK(buildingAmenities(BuildingId{0}) == doctest::Approx(0.0f)); // Forge: none
    CHECK(buildingAmenities(BuildingId{42}) ==
          doctest::Approx(0.0f)); // Aqueduct: housing, not amenities
}

TEST_CASE("a city with an Entertainment Complex has two more amenities") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::game::Player& player = *w.gameState.player(PlayerId{0});
    aoc::game::City& city     = *player.cities()[0];
    REQUIRE(!city.districts().districts.empty()); // the City Center every city starts with
    REQUIRE(city.districts().districts[0].type == DistrictType::CityCenter);

    aoc::sim::computeCityHappiness(player);
    const float before = city.happiness().amenities;

    city.districts().districts[0].buildings.push_back(ENTERTAINMENT_COMPLEX);
    CHECK(city.hasBuilding(ENTERTAINMENT_COMPLEX));
    aoc::sim::computeCityHappiness(player);
    CHECK(city.happiness().amenities == doctest::Approx(before + 2.0f));

    city.districts().districts[0].buildings.push_back(WATER_PARK); // placement is not checked here
    aoc::sim::computeCityHappiness(player);
    CHECK(city.happiness().amenities == doctest::Approx(before + 4.0f));
}

/**
 * @file test_tech_gating.cpp
 * @brief Districts, buildings and improvements are gated: a district needs its tech
 *        or civic and fits under the one-per-three-citizens cap, a building needs
 *        its tech, civic and earlier tier, and a Builder cannot place an
 *        improvement whose tech is unknown. Until 2026-09-05 every district and
 *        30 of 48 buildings were buildable from turn 1 and requiredTech on
 *        improvements had no reader.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/map/Improvement.hpp"
#include "aoc/simulation/tech/TechGating.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"

using aoc::BuildingId;
using aoc::PlayerId;
using aoc::sim::BuildLockReason;
using aoc::sim::DistrictType;

namespace {

uint8_t reason(BuildLockReason r) { return static_cast<uint8_t>(r); }

void addDistrict(aoc::game::City& city, DistrictType type) {
    city.districts().districts.push_back({type, city.location(), {}});
}

void addBuilding(aoc::game::City& city, DistrictType district, BuildingId id) {
    for (aoc::sim::CityDistrictsComponent::PlacedDistrict& d : city.districts().districts) {
        if (d.type == district) {
            d.buildings.push_back(id);
            return;
        }
    }
    city.districts().districts.push_back({district, city.location(), {id}});
}

} // namespace

TEST_CASE("a district needs its tech or civic and a free specialty slot") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::City& city = aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    aoc::game::Player& p  = *w.gameState.player(PlayerId{0});
    city.setStage(aoc::game::CitySize::Town);
    city.setPopulation(6);

    const uint8_t campus  = static_cast<uint8_t>(DistrictType::Campus);
    const uint8_t theatre = static_cast<uint8_t>(DistrictType::Theatre);
    CHECK(aoc::sim::districtLockReason(w.gameState, PlayerId{0}, city, campus, &w.grid)
          == reason(BuildLockReason::TechMissing));
    p.tech().completedTechs[3] = true; // Writing
    CHECK(aoc::sim::districtLockReason(w.gameState, PlayerId{0}, city, campus, &w.grid)
          == reason(BuildLockReason::None));
    CHECK(aoc::sim::districtLockReason(w.gameState, PlayerId{0}, city, theatre, &w.grid)
          == reason(BuildLockReason::CivicMissing));
    p.civics().completedCivics[17] = true; // Drama and Poetry
    CHECK(aoc::sim::districtLockReason(w.gameState, PlayerId{0}, city, theatre, &w.grid)
          == reason(BuildLockReason::None));

    // Six citizens allow two specialty districts; the third waits for seven.
    CHECK(aoc::sim::maxSpecialtyDistricts(1) == 1);
    CHECK(aoc::sim::maxSpecialtyDistricts(6) == 2);
    CHECK(aoc::sim::maxSpecialtyDistricts(7) == 3);
    addDistrict(city, DistrictType::Campus);
    addDistrict(city, DistrictType::Commercial);
    CHECK(aoc::sim::districtLockReason(w.gameState, PlayerId{0}, city, theatre, &w.grid)
          == reason(BuildLockReason::PopulationCap));
    city.setPopulation(7);
    CHECK(aoc::sim::districtLockReason(w.gameState, PlayerId{0}, city, theatre, &w.grid)
          == reason(BuildLockReason::None));
    CHECK(aoc::sim::districtUnlockedByTech(aoc::TechId{3}));
    CHECK_FALSE(aoc::sim::districtUnlockedByTech(aoc::TechId{2}));
}

TEST_CASE("a building needs its tech, its civic and the earlier tier") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::City& city = aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Home");
    aoc::game::Player& p  = *w.gameState.player(PlayerId{0});
    city.setStage(aoc::game::CitySize::Town);
    city.setPopulation(6);
    addDistrict(city, DistrictType::Campus);
    addDistrict(city, DistrictType::HolySite);

    const BuildingId university{19};
    CHECK(aoc::sim::buildingLockReason(w.gameState, PlayerId{0}, city, university, &w.grid)
          == reason(BuildLockReason::TechMissing));
    p.tech().completedTechs[44] = true; // Education
    CHECK(aoc::sim::buildingLockReason(w.gameState, PlayerId{0}, city, university, &w.grid)
          == reason(BuildLockReason::NeedBuilding));
    CHECK_FALSE(aoc::sim::canBuildBuilding(w.gameState, PlayerId{0}, city, university, &w.grid));
    addBuilding(city, DistrictType::Campus, BuildingId{7}); // Library
    CHECK(aoc::sim::buildingLockReason(w.gameState, PlayerId{0}, city, university, &w.grid)
          == reason(BuildLockReason::None));
    CHECK(aoc::sim::canBuildBuilding(w.gameState, PlayerId{0}, city, university, &w.grid));

    const BuildingId shrine{36};
    const BuildingId temple{37};
    CHECK(aoc::sim::buildingLockReason(w.gameState, PlayerId{0}, city, shrine, &w.grid)
          == reason(BuildLockReason::TechMissing));
    p.tech().completedTechs[32] = true; // Astrology
    CHECK(aoc::sim::canBuildBuilding(w.gameState, PlayerId{0}, city, shrine, &w.grid));
    addBuilding(city, DistrictType::HolySite, shrine);
    CHECK(aoc::sim::buildingLockReason(w.gameState, PlayerId{0}, city, temple, &w.grid)
          == reason(BuildLockReason::CivicMissing));
    p.civics().completedCivics[21] = true; // Theology
    CHECK(aoc::sim::canBuildBuilding(w.gameState, PlayerId{0}, city, temple, &w.grid));
    // Walls now sit behind Masonry, Granary behind Pottery.
    addDistrict(city, DistrictType::Encampment);
    CHECK(aoc::sim::buildingLockReason(w.gameState, PlayerId{0}, city, BuildingId{17}, &w.grid)
          == reason(BuildLockReason::TechMissing));
    CHECK(aoc::sim::buildingLockReason(w.gameState, PlayerId{0}, city, BuildingId{15}, &w.grid)
          == reason(BuildLockReason::TechMissing));
    p.tech().completedTechs[2] = true; // Pottery
    CHECK(aoc::sim::canBuildBuilding(w.gameState, PlayerId{0}, city, BuildingId{15}, &w.grid));
}

TEST_CASE("a Builder cannot place an improvement whose tech is unknown") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});
    const int32_t coast = w.grid.toIndex(aoc::hex::AxialCoord{4, 4});
    w.grid.setTerrain(coast, aoc::map::TerrainType::Coast);
    // Every fourth hill index picks the (untechnical) Observatory; take a Mine hill.
    int32_t hill = w.grid.toIndex(aoc::hex::AxialCoord{8, 8});
    if (hill % 4 == 0) {
        hill = w.grid.toIndex(aoc::hex::AxialCoord{9, 8});
    }
    REQUIRE(hill % 4 != 0);
    w.grid.setFeature(hill, aoc::map::FeatureType::Hills);

    // Without a tech component the terrain rules alone decide (legacy callers).
    CHECK(aoc::sim::canPlaceImprovement(w.grid, coast, aoc::map::ImprovementType::KelpFarm));
    // With the turn-1 human's techs the Kelp Farm (Electricity) and the Mine (Mining) wait.
    CHECK_FALSE(aoc::sim::canPlaceImprovement(w.grid, coast, aoc::map::ImprovementType::KelpFarm,
                                              &p.tech()));
    CHECK_FALSE(aoc::sim::canPlaceImprovement(w.grid, hill, aoc::map::ImprovementType::Mine,
                                              &p.tech()));
    CHECK(aoc::sim::bestImprovementForTile(w.grid, hill, &p.tech())
          == aoc::map::ImprovementType::None);
    p.tech().completedTechs[0] = true; // Mining
    CHECK(aoc::sim::canPlaceImprovement(w.grid, hill, aoc::map::ImprovementType::Mine, &p.tech()));
    CHECK(aoc::sim::bestImprovementForTile(w.grid, hill, &p.tech()) == aoc::map::ImprovementType::Mine);
    // Farms never had a tech and stay available on grassland.
    const int32_t grass = w.grid.toIndex(aoc::hex::AxialCoord{10, 10});
    CHECK(aoc::sim::canPlaceImprovement(w.grid, grass, aoc::map::ImprovementType::Farm, &p.tech()));
}

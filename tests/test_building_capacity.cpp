/**
 * @file test_building_capacity.cpp
 * @brief Smoke test for BuildingCapacity tier tables and upgrade path.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/simulation/production/BuildingCapacity.hpp"

using aoc::BuildingId;
using aoc::sim::BuildingTierClass;
using aoc::sim::CityBuildingLevelsComponent;
using aoc::sim::MAX_BUILDING_LEVEL;
using aoc::sim::buildingTierClass;

TEST_CASE("default level is one") {
    CityBuildingLevelsComponent c{};
    BuildingId forge{0};
    CHECK(c.getLevel(forge) == 1);
    CHECK(c.capacity(forge) == 3);
}

TEST_CASE("upgrade raises capacity") {
    CityBuildingLevelsComponent c{};
    BuildingId factory{3};
    CHECK(buildingTierClass(factory) == BuildingTierClass::Mid);
    CHECK(c.capacity(factory) == 2);
    CHECK(c.upgrade(factory) == aoc::ErrorCode::Ok);
    CHECK(c.capacity(factory) == 4);
    CHECK(c.upgrade(factory) == aoc::ErrorCode::Ok);
    CHECK(c.capacity(factory) == 6);
}

TEST_CASE("upgrade beyond max level fails") {
    CityBuildingLevelsComponent c{};
    BuildingId researchLab{12};
    for (int i = 0; i < MAX_BUILDING_LEVEL - 1; ++i) {
        CHECK(c.upgrade(researchLab) == aoc::ErrorCode::Ok);
    }
    CHECK(c.upgrade(researchLab) == aoc::ErrorCode::InvalidArgument);
    CHECK(c.upgradeCost(researchLab) == 0);
}

TEST_CASE("upgrade costs increase per level") {
    CityBuildingLevelsComponent c{};
    BuildingId workshop{1};
    int32_t lv1Cost = c.upgradeCost(workshop);
    static_cast<void>(c.upgrade(workshop));
    int32_t lv2Cost = c.upgradeCost(workshop);
    CHECK(lv2Cost > lv1Cost);
}

/**
 * @file test_building_capacity.cpp
 * @brief Smoke test for BuildingCapacity tier tables and upgrade path.
 */

#include "aoc/simulation/production/BuildingCapacity.hpp"

#include <cassert>
#include <cstdio>

using aoc::BuildingId;
using aoc::sim::BuildingTierClass;
using aoc::sim::CityBuildingLevelsComponent;
using aoc::sim::MAX_BUILDING_LEVEL;
using aoc::sim::buildingTierClass;

namespace {

void test_defaultLevelIsOne() {
    CityBuildingLevelsComponent c{};
    BuildingId forge{0};
    assert(c.getLevel(forge) == 1);
    assert(c.capacity(forge) == 3);
}

void test_upgradeRaisesCapacity() {
    CityBuildingLevelsComponent c{};
    BuildingId factory{3};
    assert(buildingTierClass(factory) == BuildingTierClass::Mid);
    assert(c.capacity(factory) == 2);
    assert(c.upgrade(factory) == aoc::sim::ErrorCode::Ok);
    assert(c.capacity(factory) == 4);
    assert(c.upgrade(factory) == aoc::sim::ErrorCode::Ok);
    assert(c.capacity(factory) == 6);
}

void test_upgradeBeyondMaxFails() {
    CityBuildingLevelsComponent c{};
    BuildingId researchLab{12};
    for (int i = 0; i < MAX_BUILDING_LEVEL - 1; ++i) {
        assert(c.upgrade(researchLab) == aoc::sim::ErrorCode::Ok);
    }
    assert(c.upgrade(researchLab) == aoc::sim::ErrorCode::InvalidArgument);
    assert(c.upgradeCost(researchLab) == 0);
}

void test_upgradeCostsIncrease() {
    CityBuildingLevelsComponent c{};
    BuildingId workshop{1};
    int32_t lv1Cost = c.upgradeCost(workshop);
    (void)c.upgrade(workshop);
    int32_t lv2Cost = c.upgradeCost(workshop);
    assert(lv2Cost > lv1Cost);
}

} // namespace

int main() {
    test_defaultLevelIsOne();
    test_upgradeRaisesCapacity();
    test_upgradeBeyondMaxFails();
    test_upgradeCostsIncrease();
    std::printf("test_building_capacity: OK\n");
    return 0;
}

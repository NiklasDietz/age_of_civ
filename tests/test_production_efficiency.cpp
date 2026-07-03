/**
 * @file test_production_efficiency.cpp
 * @brief Smoke test for recipe experience curve.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/simulation/production/ProductionEfficiency.hpp"

using aoc::sim::CityProductionExperienceComponent;

namespace {

bool approxGreaterEqual(float a, float b, float eps = 1e-4f) {
    return a + eps >= b;
}

} // namespace

TEST_CASE("fresh recipe has no bonus") {
    CityProductionExperienceComponent c{};
    CHECK(c.efficiencyMultiplier(1) == 1.0f);
}

TEST_CASE("bonus is monotonically increasing with experience") {
    CityProductionExperienceComponent c{};
    float last = c.efficiencyMultiplier(42);
    for (int i = 0; i < 100; ++i) {
        c.addExperience(42);
        float current = c.efficiencyMultiplier(42);
        CHECK(approxGreaterEqual(current, last));
        last = current;
    }
}

TEST_CASE("bonus asymptote stays below 40 percent") {
    CityProductionExperienceComponent c{};
    for (int i = 0; i < 5000; ++i) { c.addExperience(7); }
    float m = c.efficiencyMultiplier(7);
    CHECK(m < 1.40f);
    CHECK(m > 1.30f);
}

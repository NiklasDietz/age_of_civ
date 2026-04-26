/**
 * @file test_production_efficiency.cpp
 * @brief Smoke test for recipe experience curve.
 */

#include "aoc/simulation/production/ProductionEfficiency.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

using aoc::sim::CityProductionExperienceComponent;

namespace {

bool approxGreaterEqual(float a, float b, float eps = 1e-4f) {
    return a + eps >= b;
}

void test_freshRecipeNoBonus() {
    CityProductionExperienceComponent c{};
    assert(c.efficiencyMultiplier(1) == 1.0f);
}

void test_bonusMonotonicallyIncreasing() {
    CityProductionExperienceComponent c{};
    float last = c.efficiencyMultiplier(42);
    for (int i = 0; i < 100; ++i) {
        c.addExperience(42);
        float current = c.efficiencyMultiplier(42);
        assert(approxGreaterEqual(current, last));
        last = current;
    }
}

void test_bonusAsymptoteBelow40Percent() {
    CityProductionExperienceComponent c{};
    for (int i = 0; i < 5000; ++i) { c.addExperience(7); }
    float m = c.efficiencyMultiplier(7);
    assert(m < 1.40f);
    assert(m > 1.30f);
}

} // namespace

int main() {
    test_freshRecipeNoBonus();
    test_bonusMonotonicallyIncreasing();
    test_bonusAsymptoteBelow40Percent();
    std::printf("test_production_efficiency: OK\n");
    return 0;
}

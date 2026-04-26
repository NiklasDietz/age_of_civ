/**
 * @file test_unemployment.cpp
 * @brief Smoke test for TechUnemployment::updateUnemployment bounds/behaviour.
 */

#include "aoc/simulation/economy/TechUnemployment.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

using aoc::sim::CityUnemploymentComponent;
using aoc::sim::updateUnemployment;

namespace {

bool nearly(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) < eps;
}

void test_zeroPopulationZeroRate() {
    CityUnemploymentComponent u{};
    u.unemploymentRate = 0.20f;
    updateUnemployment(u, /*automation*/ 4, /*population*/ 0,
                       /*education*/ 0.0f, /*industrialLevel*/ 0);
    assert(nearly(u.unemploymentRate, 0.0f));
}

void test_rateClampedBelowHalf() {
    CityUnemploymentComponent u{};
    // Saturate automation displacement well past population.
    updateUnemployment(u, /*automation*/ 10000, /*population*/ 5,
                       /*education*/ 0.0f, /*industrialLevel*/ 5);
    assert(u.unemploymentRate <= 0.50f);
    assert(u.unemploymentRate >= 0.0f);
}

void test_educationMitigatesTarget() {
    CityUnemploymentComponent noEd{};
    CityUnemploymentComponent fullEd{};
    updateUnemployment(noEd,  /*automation*/ 4, /*population*/ 8,
                       /*education*/ 0.0f, /*industrialLevel*/ 1);
    updateUnemployment(fullEd, /*automation*/ 4, /*population*/ 8,
                       /*education*/ 0.8f, /*industrialLevel*/ 1);
    assert(fullEd.unemploymentRate < noEd.unemploymentRate);
}

void test_smoothingTowardTarget() {
    CityUnemploymentComponent u{};
    u.unemploymentRate = 0.0f;
    // High automation drives a positive target; after one tick the rate
    // should have moved a little toward it but nowhere near fully.
    updateUnemployment(u, /*automation*/ 5, /*population*/ 10,
                       /*education*/ 0.0f, /*industrialLevel*/ 0);
    assert(u.unemploymentRate > 0.0f);
    assert(u.unemploymentRate < 0.5f);
}

} // namespace

int main() {
    test_zeroPopulationZeroRate();
    test_rateClampedBelowHalf();
    test_educationMitigatesTarget();
    test_smoothingTowardTarget();
    std::printf("test_unemployment: OK\n");
    return 0;
}

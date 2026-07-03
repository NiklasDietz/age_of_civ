/**
 * @file test_unemployment.cpp
 * @brief Smoke test for TechUnemployment::updateUnemployment bounds/behaviour.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/simulation/economy/TechUnemployment.hpp"

using aoc::sim::CityUnemploymentComponent;
using aoc::sim::updateUnemployment;

TEST_CASE("zero population forces zero rate") {
    CityUnemploymentComponent u{};
    u.unemploymentRate = 0.20f;
    updateUnemployment(u, /*automation*/ 4, /*population*/ 0,
                       /*education*/ 0.0f, /*industrialLevel*/ 0);
    CHECK(u.unemploymentRate == doctest::Approx(0.0f).epsilon(1e-4));
}

TEST_CASE("rate clamped to [0, 0.5]") {
    CityUnemploymentComponent u{};
    // Saturate automation displacement well past population.
    updateUnemployment(u, /*automation*/ 10000, /*population*/ 5,
                       /*education*/ 0.0f, /*industrialLevel*/ 5);
    CHECK(u.unemploymentRate <= 0.50f);
    CHECK(u.unemploymentRate >= 0.0f);
}

TEST_CASE("education mitigates the unemployment target") {
    CityUnemploymentComponent noEd{};
    CityUnemploymentComponent fullEd{};
    updateUnemployment(noEd,  /*automation*/ 4, /*population*/ 8,
                       /*education*/ 0.0f, /*industrialLevel*/ 1);
    updateUnemployment(fullEd, /*automation*/ 4, /*population*/ 8,
                       /*education*/ 0.8f, /*industrialLevel*/ 1);
    CHECK(fullEd.unemploymentRate < noEd.unemploymentRate);
}

TEST_CASE("rate smooths toward target, does not jump") {
    CityUnemploymentComponent u{};
    u.unemploymentRate = 0.0f;
    // High automation drives a positive target; after one tick the rate
    // should have moved a little toward it but nowhere near fully.
    updateUnemployment(u, /*automation*/ 5, /*population*/ 10,
                       /*education*/ 0.0f, /*industrialLevel*/ 0);
    CHECK(u.unemploymentRate > 0.0f);
    CHECK(u.unemploymentRate < 0.5f);
}

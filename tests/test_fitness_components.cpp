/**
 * @file test_fitness_components.cpp
 * @brief Pins playerOutcomeScore (ml/cpp/FitnessEvaluator.hpp) — the
 *        outcome-component fitness formula the GA optimises against.
 *
 * Pins:
 *   - an eliminated player (had cities, holds none) scores exactly -1.0,
 *     short-circuiting all component terms;
 *   - the 1.0/0.25/0.25/0.20/0.15 weighting on a fully hand-computed case;
 *   - economicHealth clamps to [-1, 1];
 *   - zero income yields balancedFlow 0 (no divide-by-zero).
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "FitnessEvaluator.hpp"

#include <cstdint>
#include <vector>

using aoc::ga::SimulationResult;

namespace {

/// A 2-player result with all component vectors sized; caller overrides fields.
SimulationResult make2p() {
    SimulationResult r{};
    const std::size_t n = 2;
    r.eraVP.assign(n, 0);
    r.compositeCSI.assign(n, 0.0f);
    r.treasury.assign(n, 0);
    r.cityCount.assign(n, 0);
    r.peakCityCount.assign(n, 0);
    r.population.assign(n, 0);
    r.gdp.assign(n, 0.0f);
    r.avgHappiness.assign(n, 0.0f);
    r.totalIncome.assign(n, 0.0f);
    r.totalExpense.assign(n, 0.0f);
    r.valid = true;
    return r;
}

}  // namespace

TEST_CASE("playerOutcomeScore: eliminated player scores exactly -1.0") {
    SimulationResult r = make2p();
    r.eraVP = {200, 10};          // would otherwise be a strong lead
    r.treasury = {9999, 0};
    r.peakCityCount = {4, 4};
    r.cityCount = {0, 4};         // held 0 of a peak of 4 -> eliminated
    r.avgHappiness = {5.0f, 0.0f};
    r.totalIncome = {100.0f, 0.0f};
    const float s = aoc::ga::playerOutcomeScore(r, 0, 100);
    CHECK(s == doctest::Approx(-1.0f));
}

TEST_CASE("playerOutcomeScore: hand-computed weighting") {
    SimulationResult r = make2p();
    r.eraVP = {100, 50};          // relativeWin = (100-50)/100 = 0.5
    r.treasury = {500, 0};        // economicHealth = 500/(100*5) = 1.0
    r.peakCityCount = {4, 4};
    r.cityCount = {4, 4};         // survival = 4/4 = 1.0
    r.totalIncome = {100.0f, 0.0f};
    r.totalExpense = {50.0f, 0.0f};  // balancedFlow = 0.5 + 0.5*0.5 = 0.75
    r.avgHappiness = {5.0f, 0.0f};   // happiness = (5+5)/10 = 1.0

    // 1.0*0.5 + 0.25*1.0 + 0.25*1.0 + 0.20*0.75 + 0.15*1.0 = 1.30
    const float s = aoc::ga::playerOutcomeScore(r, 0, 100);
    CHECK(s == doctest::Approx(1.30f));
}

TEST_CASE("playerOutcomeScore: economicHealth clamps to [-1, 1]") {
    SUBCASE("huge treasury clamps the economic term to +1") {
        SimulationResult r = make2p();
        r.eraVP = {100, 100};      // relativeWin = 0
        r.treasury = {1'000'000, 0};
        r.peakCityCount = {1, 1};
        r.cityCount = {1, 1};      // survival 1.0
        r.totalIncome = {0.0f, 0.0f};  // balancedFlow 0
        r.avgHappiness = {-5.0f, 0.0f}; // happiness 0
        // 0 + 0.25*1.0 + 0.25*1.0 + 0 + 0 = 0.50
        const float s = aoc::ga::playerOutcomeScore(r, 0, 100);
        CHECK(s == doctest::Approx(0.50f));
    }
    SUBCASE("large debt clamps the economic term to -1") {
        SimulationResult r = make2p();
        r.eraVP = {100, 100};
        r.treasury = {-1'000'000, 0};
        r.peakCityCount = {1, 1};
        r.cityCount = {1, 1};
        r.totalIncome = {0.0f, 0.0f};
        r.avgHappiness = {-5.0f, 0.0f};
        // 0 + 0.25*(-1.0) + 0.25*1.0 + 0 + 0 = 0.0
        const float s = aoc::ga::playerOutcomeScore(r, 0, 100);
        CHECK(s == doctest::Approx(0.0f));
    }
}

TEST_CASE("playerOutcomeScore: zero income -> balancedFlow 0, no divide-by-zero") {
    SimulationResult r = make2p();
    r.eraVP = {100, 100};          // relativeWin 0
    r.treasury = {0, 0};           // economicHealth 0
    r.peakCityCount = {1, 1};
    r.cityCount = {1, 1};          // survival 1.0
    r.totalIncome = {0.0f, 0.0f};  // <- balancedFlow must be 0, not NaN/inf
    r.totalExpense = {50.0f, 0.0f};
    r.avgHappiness = {-5.0f, 0.0f}; // happiness 0
    // 0 + 0 + 0.25*1.0 + 0.20*0 + 0 = 0.25
    const float s = aoc::ga::playerOutcomeScore(r, 0, 100);
    CHECK(s == doctest::Approx(0.25f));
}

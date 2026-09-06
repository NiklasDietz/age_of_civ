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

TEST_CASE("the evaluated genome is seated on the civ it is being tuned for") {
    // --seed-leader N tunes leader N's genes, and those genes only mean
    // something alongside civ N's abilities and agenda. Player 0 used to be
    // civ 0 unconditionally, so tuning Montezuma evolved his genome while
    // playing Rome. The mapping must stay a permutation: the override table
    // is keyed by civId, so two players sharing one would collide.
    const int32_t civCount = static_cast<int32_t>(aoc::sim::CIV_COUNT);

    SUBCASE("no subject civ keeps the plain player-index mapping") {
        for (int32_t p = 0; p < 8; ++p) {
            CHECK(aoc::ga::civForPlayer(p, -1)
                  == static_cast<aoc::sim::CivId>(p % civCount));
        }
    }

    SUBCASE("a subject civ seats player 0 and swaps the displaced player") {
        constexpr int32_t MONTEZUMA = 8;
        CHECK(aoc::ga::civForPlayer(0, MONTEZUMA)
              == static_cast<aoc::sim::CivId>(MONTEZUMA));
        CHECK(aoc::ga::civForPlayer(MONTEZUMA, MONTEZUMA) == static_cast<aoc::sim::CivId>(0));
        CHECK(aoc::ga::civForPlayer(3, MONTEZUMA) == static_cast<aoc::sim::CivId>(3));
    }

    SUBCASE("every subject civ yields a permutation, so no two players collide") {
        for (int32_t subject = 0; subject < 12; ++subject) {
            std::vector<bool> seen(static_cast<std::size_t>(civCount), false);
            for (int32_t p = 0; p < civCount; ++p) {
                const auto civ = static_cast<std::size_t>(aoc::ga::civForPlayer(p, subject));
                REQUIRE(civ < seen.size());
                CHECK_FALSE(seen[civ]); // never handed out twice
                seen[civ] = true;
            }
        }
    }
}

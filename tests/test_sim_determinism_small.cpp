/**
 * @file test_sim_determinism_small.cpp
 * @brief Small, fast determinism gate for the embedded GA simulation
 *        (ml/cpp/FitnessEvaluator.hpp runSimulation).
 *
 * The cheap lib-level cousin of the release-only scripts/test_determinism.sh:
 * the same seed run twice must produce a byte-identical structured result.
 * This is the safety net every later ml/cpp phase (CRN, elite re-eval, the
 * balance-params TLS change) leans on -- if a change smuggles in hidden
 * entropy, this goes red in every build, not just Release.
 *
 * NB: this compares two runs against EACH OTHER (no golden), so it is valid
 * regardless of whether external data files are present.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "FitnessEvaluator.hpp"

#include <cstdint>

using aoc::ga::SimulationResult;

TEST_CASE("runSimulation is deterministic for a fixed seed") {
    constexpr int32_t kTurns = 40;
    constexpr int32_t kPlayers = 4;
    constexpr uint64_t kSeed = 123;

    const SimulationResult a = aoc::ga::runSimulation(kTurns, kPlayers, kSeed);
    const SimulationResult b = aoc::ga::runSimulation(kTurns, kPlayers, kSeed);

    REQUIRE(a.valid);
    REQUIRE(b.valid);
    CHECK(a.eraVP == b.eraVP);
    CHECK(a.treasury == b.treasury);
    CHECK(a.cityCount == b.cityCount);
    CHECK(a.peakCityCount == b.peakCityCount);
    CHECK(a.population == b.population);
    CHECK(a.victoryType == b.victoryType);
    CHECK(a.winner == b.winner);
}

TEST_CASE("runSimulation different seeds are independent runs (sanity)") {
    // Not a determinism claim -- just guards against runSimulation ignoring
    // its seed entirely (which would make the determinism check vacuous).
    const SimulationResult a = aoc::ga::runSimulation(40, 4, 123);
    const SimulationResult b = aoc::ga::runSimulation(40, 4, 999);
    REQUIRE(a.valid);
    REQUIRE(b.valid);
    // At least one structured field should differ across distinct seeds.
    const bool anyDiff = (a.eraVP != b.eraVP) || (a.treasury != b.treasury)
                       || (a.cityCount != b.cityCount) || (a.winner != b.winner);
    CHECK(anyDiff);
}

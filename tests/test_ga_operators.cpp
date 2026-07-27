/**
 * @file test_ga_operators.cpp
 * @brief Pins the ml/cpp genetic-algorithm operators (GeneticAlgorithm.hpp).
 *
 * These operators had ZERO coverage before this file. They pin:
 *   - clampGenes forces every gene inside defaultBounds();
 *   - crossover produces only parent-A-or-B genes (provenance);
 *   - mutate at rate 0 is the identity; at rate 1 it stays in bounds;
 *   - tournamentSelect returns the fittest of its sample and is index-safe
 *     for a population of one;
 *   - createInitialPopulation is correctly sized and every hand-crafted
 *     archetype (via seedLeader) already satisfies defaultBounds() -- a latent
 *     invariant the seeding relies on but never enforced;
 *   - LeaderBehavior toArray/fromArray round-trips, and fromArrayPadded keeps
 *     defaults for a short (v1, 32-gene) dump and ignores an over-long one.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "GeneticAlgorithm.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"

#include <array>
#include <random>

using aoc::ga::NUM_PARAMS;
using aoc::ga::Individual;
using aoc::ga::ParamBounds;
using aoc::ga::defaultBounds;

namespace {

bool withinBounds(const std::array<float, NUM_PARAMS>& g, const ParamBounds& b) {
    for (int32_t i = 0; i < NUM_PARAMS; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        if (g[k] < b.min[k] || g[k] > b.max[k]) { return false; }
    }
    return true;
}

}  // namespace

TEST_CASE("clampGenes forces every gene inside bounds") {
    const ParamBounds b = defaultBounds();
    std::array<float, NUM_PARAMS> g{};
    for (int32_t i = 0; i < NUM_PARAMS; ++i) {
        // Alternate wildly out of range on both sides.
        g[static_cast<std::size_t>(i)] = (i % 2 == 0) ? -1000.0f : 1000.0f;
    }
    aoc::ga::clampGenes(g, b);
    CHECK(withinBounds(g, b));
    // An already-valid gene is untouched.
    std::array<float, NUM_PARAMS> mid{};
    for (int32_t i = 0; i < NUM_PARAMS; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        mid[k] = 0.5f * (b.min[k] + b.max[k]);
    }
    std::array<float, NUM_PARAMS> midCopy = mid;
    aoc::ga::clampGenes(mid, b);
    CHECK(mid == midCopy);
}

TEST_CASE("crossover child genes come only from a parent (provenance)") {
    std::mt19937 rng(12345);
    Individual a{};
    Individual bb{};
    a.genes.fill(1.0f);
    bb.genes.fill(2.0f);
    for (int32_t iter = 0; iter < 50; ++iter) {
        const Individual child = aoc::ga::crossover(a, bb, rng);
        for (float v : child.genes) {
            CHECK((v == doctest::Approx(1.0f) || v == doctest::Approx(2.0f)));
        }
    }
}

TEST_CASE("mutate: rate 0 is identity, rate 1 stays in bounds") {
    const ParamBounds b = defaultBounds();
    std::mt19937 rng(777);
    Individual ind{};
    for (int32_t i = 0; i < NUM_PARAMS; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        ind.genes[k] = 0.5f * (b.min[k] + b.max[k]);
    }

    SUBCASE("rate 0 leaves genes unchanged") {
        const Individual out = aoc::ga::mutate(ind, 0.0f, 0.15f, 0.1f, b, rng);
        CHECK(out.genes == ind.genes);
    }

    SUBCASE("rate 1 always produces in-bounds genes") {
        for (int32_t iter = 0; iter < 500; ++iter) {
            const Individual out = aoc::ga::mutate(ind, 1.0f, 0.15f, 0.1f, b, rng);
            CHECK(withinBounds(out.genes, b));
        }
    }
}

TEST_CASE("tournamentSelect returns the fittest sampled, index-safe for pop of 1") {
    std::mt19937 rng(2024);
    std::vector<Individual> pop(10);
    for (std::size_t i = 0; i < pop.size(); ++i) {
        pop[i].fitness = static_cast<float>(i);  // strictly increasing
    }
    // A large tournament almost surely includes the global best (fitness 9).
    const Individual best = aoc::ga::tournamentSelect(pop, 200, rng);
    CHECK(best.fitness == doctest::Approx(9.0f));

    std::vector<Individual> one(1);
    one[0].fitness = 3.0f;
    const Individual only = aoc::ga::tournamentSelect(one, 3, rng);
    CHECK(only.fitness == doctest::Approx(3.0f));
}

TEST_CASE("createInitialPopulation is sized and every archetype is in bounds") {
    const ParamBounds b = defaultBounds();
    std::mt19937 rng(9);

    const std::vector<Individual> pop = aoc::ga::createInitialPopulation(20, rng, b, -1);
    CHECK(pop.size() == 20u);
    for (const Individual& ind : pop) { CHECK(withinBounds(ind.genes, b)); }

    // seedLeader=k seeds slot 0 with the UN-mutated archetype k. Pin that
    // every hand-crafted archetype already satisfies defaultBounds().
    for (int32_t k = 0; k < 12; ++k) {
        std::mt19937 r(static_cast<std::mt19937::result_type>(100 + k));
        const std::vector<Individual> p = aoc::ga::createInitialPopulation(4, r, b, k);
        REQUIRE(p.size() == 4u);
        INFO("archetype ", k, " out of defaultBounds()");
        CHECK(withinBounds(p[0].genes, b));
    }
}

TEST_CASE("LeaderBehavior toArray/fromArray round-trips") {
    aoc::sim::LeaderBehavior in{};
    std::array<float, aoc::sim::LeaderBehavior::PARAM_COUNT> src{};
    for (std::size_t i = 0; i < src.size(); ++i) {
        src[i] = 0.13f * static_cast<float>(i) + 0.07f;  // distinct values
    }
    in.fromArray(src.data());

    std::array<float, aoc::sim::LeaderBehavior::PARAM_COUNT> out{};
    in.toArray(out.data());
    CHECK(out == src);
}

TEST_CASE("fromArrayPadded keeps defaults for short dumps, ignores long ones") {
    const aoc::sim::LeaderBehavior defaults{};

    SUBCASE("v1 32-gene dump leaves mil* knobs at defaults") {
        std::array<float, 32> v1{};
        for (std::size_t i = 0; i < v1.size(); ++i) { v1[i] = 0.5f + 0.01f * static_cast<float>(i); }
        aoc::sim::LeaderBehavior b{};
        b.fromArrayPadded(v1.data(), 32);
        // Indices 32-35 (the mil* knobs) must retain their in-class defaults.
        CHECK(b.milBaseWeight == doctest::Approx(defaults.milBaseWeight));
        CHECK(b.milThreatSensitivity == doctest::Approx(defaults.milThreatSensitivity));
        CHECK(b.milEmergencySlope == doctest::Approx(defaults.milEmergencySlope));
        CHECK(b.milOverstockPenalty == doctest::Approx(defaults.milOverstockPenalty));
        // ...while index 0 came from the dump.
        CHECK(b.militaryAggression == doctest::Approx(v1[0]));
    }

    SUBCASE("over-long dump ignores the tail beyond PARAM_COUNT") {
        std::array<float, 40> longDump{};
        for (std::size_t i = 0; i < longDump.size(); ++i) { longDump[i] = 1.0f + 0.02f * static_cast<float>(i); }
        aoc::sim::LeaderBehavior b{};
        b.fromArrayPadded(longDump.data(), 40);
        std::array<float, aoc::sim::LeaderBehavior::PARAM_COUNT> out{};
        b.toArray(out.data());
        for (std::size_t i = 0; i < out.size(); ++i) {
            CHECK(out[i] == doctest::Approx(longDump[i]));  // first PARAM_COUNT copied
        }
    }
}

TEST_CASE("parseOpponentMode / parseMapType accept and reject") {
    using aoc::ga::OpponentMode;
    for (OpponentMode m : {OpponentMode::Fixed, OpponentMode::CoEvolve,
                           OpponentMode::Champion, OpponentMode::Mixed}) {
        OpponentMode parsed{};
        REQUIRE(aoc::ga::parseOpponentMode(aoc::ga::opponentModeName(m), parsed));
        CHECK(parsed == m);
    }
    OpponentMode bad{};
    CHECK_FALSE(aoc::ga::parseOpponentMode("not_a_mode", bad));

    aoc::map::MapType mt{};
    CHECK(aoc::ga::parseMapType("continents", mt));
    CHECK(mt == aoc::map::MapType::Continents);
    // Non-empty unknown names are accepted and remapped to Continents.
    CHECK(aoc::ga::parseMapType("fractal", mt));
    CHECK(mt == aoc::map::MapType::Continents);
    // Empty string is rejected.
    CHECK_FALSE(aoc::ga::parseMapType("", mt));
}

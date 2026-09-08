/**
 * @file test_balance_metrics.cpp
 * @brief Pins the balance-GA scoring metrics (ml/cpp/BalanceMetrics.hpp) and
 *        the BalanceGenome <-> BalanceParams conversion.
 *
 * Pins:
 *   - gini: 0 for empty and for all-equal; a hand-computed asymmetric value;
 *   - normalisedEntropy: 0 for a single occupied bucket, 1 for two equal ones;
 *   - triangleReward: 1 at the target, 0 at/outside the clamp endpoints, 0.5
 *     at the half-way points on each side;
 *   - BalanceGenome round-trips through BalanceParams (the manifold is stable).
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "BalanceMetrics.hpp"

#include "aoc/balance/BalanceParams.hpp"

#include <cmath>
#include "aoc/simulation/victory/VictoryCondition.hpp"

#include <array>
#include <cstdint>
#include <vector>

TEST_CASE("gini: empty and all-equal are 0; asymmetric is hand-computed") {
    CHECK(aoc::ga::gini({}) == doctest::Approx(0.0f));
    CHECK(aoc::ga::gini({5.0f, 5.0f, 5.0f, 5.0f}) == doctest::Approx(0.0f));
    // Sorted [0,0,0,10], n=4, sum=10: cum = 4*10 = 40;
    // g = (2*40)/(4*10) - (4+1)/4 = 2.0 - 1.25 = 0.75.
    CHECK(aoc::ga::gini({0.0f, 0.0f, 0.0f, 10.0f}) == doctest::Approx(0.75f));
    // Negatives are floored to 0, so this equals the all-equal (single-value) case.
    CHECK(aoc::ga::gini({-3.0f, 0.0f, 0.0f, 0.0f}) == doctest::Approx(0.0f));
}

TEST_CASE("normalisedEntropy: 0 for one bucket, 1 for two equal buckets") {
    std::array<int32_t, aoc::sim::VICTORY_TYPE_COUNT> hist{};

    SUBCASE("all games in a single bucket") {
        hist[1] = 10;
        CHECK(aoc::ga::normalisedEntropy(hist) == doctest::Approx(0.0f));
    }
    SUBCASE("empty histogram") {
        CHECK(aoc::ga::normalisedEntropy(hist) == doctest::Approx(0.0f));
    }
    SUBCASE("two equally-populated buckets -> max entropy 1") {
        hist[1] = 5;
        hist[4] = 5;
        CHECK(aoc::ga::normalisedEntropy(hist) == doctest::Approx(1.0f));
    }
}

TEST_CASE("triangleReward: peak at target, 0 outside, 0.5 half-way") {
    const float low = 0.0f, target = 0.6f, high = 1.0f;
    CHECK(aoc::ga::triangleReward(target, target, low, high) == doctest::Approx(1.0f));
    CHECK(aoc::ga::triangleReward(low, target, low, high) == doctest::Approx(0.0f));
    CHECK(aoc::ga::triangleReward(high, target, low, high) == doctest::Approx(0.0f));
    CHECK(aoc::ga::triangleReward(-0.5f, target, low, high) == doctest::Approx(0.0f));
    CHECK(aoc::ga::triangleReward(1.5f, target, low, high) == doctest::Approx(0.0f));
    // Half-way up the rising edge: (0.3-0)/(0.6-0) = 0.5.
    CHECK(aoc::ga::triangleReward(0.3f, target, low, high) == doctest::Approx(0.5f));
    // Half-way down the falling edge: 1 - (0.8-0.6)/(1.0-0.6) = 0.5.
    CHECK(aoc::ga::triangleReward(0.8f, target, low, high) == doctest::Approx(0.5f));
}

TEST_CASE("BalanceGenome round-trips through BalanceParams") {
    // Start from the shipping defaults; the genome derived from them must be
    // stable across a params round-trip (int knobs survive the cast, floats
    // survive exactly).
    const aoc::balance::BalanceParams defaults{};
    aoc::balance::BalanceGenome g;
    g.fromParams(defaults);

    const aoc::balance::BalanceParams p2 = g.toParams();
    aoc::balance::BalanceGenome g2;
    g2.fromParams(p2);

    CHECK(g.g == g2.g);
}

TEST_CASE("every genome slot maps to a live parameter") {
    // Slots 7 and 8 used to hold integrationThreshold and
    // integrationTurnsRequired, tuning a Global Integration Project victory that
    // does not exist -- there is no Integration in VictoryType and no check
    // anywhere -- so two genes were mutated every generation for nothing.
    //
    // Round-tripping every slot through params and back proves each one still
    // reaches a field: a slot that mapped to nothing would come back as zero
    // and break the equality.
    aoc::balance::BalanceGenome g;
    for (int32_t i = 0; i < aoc::balance::BALANCE_PARAM_COUNT; ++i) {
        g.g[static_cast<std::size_t>(i)] = 1.0f + static_cast<float>(i);
    }
    aoc::balance::BalanceGenome back;
    back.fromParams(g.toParams());
    CHECK(g.g == back.g);
}

TEST_CASE("the search bounds cover every slot and are ordered") {
    const aoc::balance::BalanceBounds b = aoc::balance::defaultBalanceBounds();
    for (int32_t i = 0; i < aoc::balance::BALANCE_PARAM_COUNT; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        // A slot left behind by a renumbering would keep its default 0/0 here.
        CHECK(b.min[idx] < b.max[idx]);
    }
}

// ============================================================================
// Every balance parameter must be reachable by the tuner
// ============================================================================

TEST_CASE("every genome slot round-trips through toParams and back") {
    // The genome is a flat array and the mapping is written by hand twice, in
    // toParams and fromParams. A slot missing from either is a parameter the GA
    // silently cannot search -- which is exactly how workerCapacityPerPop spent
    // its life as a constexpr in Automation.hpp while being, by measurement,
    // the binding constraint on the entire production chain.
    aoc::balance::BalanceGenome g;
    for (int32_t i = 0; i < aoc::balance::BALANCE_PARAM_COUNT; ++i) {
        // Distinct, in-range-ish values so a mis-wired slot cannot coincide.
        g.g[static_cast<std::size_t>(i)] = 1.0f + static_cast<float>(i);
    }
    aoc::balance::BalanceGenome round;
    round.fromParams(g.toParams());

    for (int32_t i = 0; i < aoc::balance::BALANCE_PARAM_COUNT; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        // Integer-typed parameters truncate on the way through, so compare with
        // a tolerance of one rather than demanding bit equality.
        CHECK(std::abs(round.g[idx] - g.g[idx]) <= 1.0f);
    }
}

TEST_CASE("every balance parameter has usable bounds") {
    const aoc::balance::BalanceBounds b = aoc::balance::defaultBalanceBounds();
    for (int32_t i = 0; i < aoc::balance::BALANCE_PARAM_COUNT; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        // A slot left at {0, 0} is a parameter the search cannot move, which is
        // indistinguishable from not having exposed it at all.
        CHECK(b.max[idx] > b.min[idx]);
    }
}

TEST_CASE("the shipped defaults sit inside their own search bounds") {
    // religionDominanceFrac spent a long time at 0.08 while its bounds were
    // [0.3, 0.8], so the tuner could never explore the shipped value and every
    // tuned genome jumped it. Assert the general property rather than that one
    // case.
    aoc::balance::BalanceGenome shipped;
    shipped.fromParams(aoc::balance::BalanceParams{});
    const aoc::balance::BalanceBounds b = aoc::balance::defaultBalanceBounds();
    for (int32_t i = 0; i < aoc::balance::BALANCE_PARAM_COUNT; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        CHECK(shipped.g[idx] >= b.min[idx]);
        CHECK(shipped.g[idx] <= b.max[idx]);
    }
}

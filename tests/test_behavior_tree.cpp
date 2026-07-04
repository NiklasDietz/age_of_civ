/**
 * @file test_behavior_tree.cpp
 * @brief Pins WeightedChance RNG semantics and Random::nextInt bounds.
 *
 * WeightedChance historically rolled `fmod(tick_counter, 1.0f)`, which is
 * always 0 for the integer-valued counter, so moderate-weight nodes fired
 * unconditionally. These cases pin the fixed behavior: moderate weights
 * (including the 0.3 boundary) consume the blackboard RNG exactly once per
 * tick and fire with probability equal to the weight; the extreme-weight
 * fast paths and NaN weights consume nothing. A firing node propagates its
 * child's status unchanged.
 *
 * nextInt(min, max) with min > max used to wrap the range through uint64_t
 * to 0 and divide by zero, and the full int32 span overflowed `max - min`.
 * Pinned: min > max returns min without consuming a draw, min == max still
 * consumes one (RNG stream stability), extreme spans stay in range, and
 * both endpoints of a small range are reachable.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/core/Random.hpp"
#include "aoc/simulation/ai/BehaviorTree.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>

namespace {

/// Leaf that counts its ticks and returns a fixed status.
class CountingLeaf final : public aoc::sim::bt::Node {
public:
    explicit CountingLeaf(int32_t& counter,
                          aoc::sim::bt::Status result = aoc::sim::bt::Status::Success)
        : m_counter(counter), m_result(result) {}

    aoc::sim::bt::Status tick(aoc::sim::bt::Blackboard&) override {
        ++this->m_counter;
        return this->m_result;
    }

    [[nodiscard]] std::string_view name() const override { return "CountingLeaf"; }

private:
    int32_t& m_counter;
    aoc::sim::bt::Status m_result;
};

/// Drives a moderate-weight node against a same-seed replay oracle: every
/// tick must fire iff Random::chance(weight) fires, consuming one draw.
void checkModerateWeightMatchesChance(float weight, int32_t ticks) {
    constexpr uint64_t SEED = 42u;

    aoc::Random treeRng{SEED};
    aoc::Random replayRng{SEED};

    int32_t childTicks = 0;
    aoc::sim::bt::WeightedChance node{weight, std::make_unique<CountingLeaf>(childTicks)};
    aoc::sim::bt::Blackboard bb;
    bb.rng = &treeRng;

    int32_t expectedTicks = 0;
    for (int32_t i = 0; i < ticks; ++i) {
        CAPTURE(i);
        const bool expectFire = replayRng.chance(weight);
        if (expectFire) { ++expectedTicks; }
        const aoc::sim::bt::Status s = node.tick(bb);
        CHECK(s == (expectFire ? aoc::sim::bt::Status::Success
                               : aoc::sim::bt::Status::Failure));
    }
    CHECK(childTicks == expectedTicks);

    // The node must have consumed exactly one draw per tick.
    CHECK(treeRng.state() == replayRng.state());

    // Regression guard for the fmod(tick, 1) bug: a moderate weight must
    // not fire unconditionally, and must fire sometimes.
    CHECK(childTicks > 0);
    CHECK(childTicks < ticks);
}

} // namespace

TEST_CASE("WeightedChance with moderate weight consumes the RNG and matches chance()") {
    checkModerateWeightMatchesChance(0.6f, 200);
}

TEST_CASE("WeightedChance at the 0.3 boundary behaves as a moderate weight") {
    checkModerateWeightMatchesChance(0.3f, 200);
}

TEST_CASE("WeightedChance propagates the child's status when it fires") {
    constexpr uint64_t SEED = 42u;
    constexpr float WEIGHT = 0.9f;

    aoc::Random treeRng{SEED};
    aoc::Random replayRng{SEED};
    aoc::sim::bt::Blackboard bb;
    bb.rng = &treeRng;

    int32_t runningTicks = 0;
    aoc::sim::bt::WeightedChance node{
        WEIGHT, std::make_unique<CountingLeaf>(runningTicks, aoc::sim::bt::Status::Running)};

    int32_t firedTicks = 0;
    for (int32_t i = 0; i < 50; ++i) {
        CAPTURE(i);
        const bool expectFire = replayRng.chance(WEIGHT);
        if (expectFire) { ++firedTicks; }
        // A firing node must surface the child's Running, not Success.
        CHECK(node.tick(bb) == (expectFire ? aoc::sim::bt::Status::Running
                                           : aoc::sim::bt::Status::Failure));
    }
    CHECK(runningTicks == firedTicks);
    CHECK(firedTicks > 0);

    // A firing child that fails must still have been ticked: the node's
    // Failure has to come from the child, not from skipping it.
    aoc::Random failTreeRng{SEED};
    aoc::Random failReplayRng{SEED};
    bb.rng = &failTreeRng;
    int32_t failTicks = 0;
    aoc::sim::bt::WeightedChance failNode{
        WEIGHT, std::make_unique<CountingLeaf>(failTicks, aoc::sim::bt::Status::Failure)};
    int32_t expectedFailTicks = 0;
    for (int32_t i = 0; i < 50; ++i) {
        CAPTURE(i);
        if (failReplayRng.chance(WEIGHT)) { ++expectedFailTicks; }
        CHECK(failNode.tick(bb) == aoc::sim::bt::Status::Failure);
    }
    CHECK(failTicks == expectedFailTicks);
    CHECK(failTicks > 0);
}

TEST_CASE("WeightedChance extreme weights bypass the RNG") {
    aoc::Random rng{7u};
    const std::array<uint64_t, 4> before = rng.state();
    aoc::sim::bt::Blackboard bb;
    bb.rng = &rng;

    SUBCASE("weight below 0.3 never fires") {
        int32_t childTicks = 0;
        aoc::sim::bt::WeightedChance node{0.1f, std::make_unique<CountingLeaf>(childTicks)};
        for (int32_t i = 0; i < 50; ++i) {
            CAPTURE(i);
            CHECK(node.tick(bb) == aoc::sim::bt::Status::Failure);
        }
        CHECK(childTicks == 0);
        CHECK(rng.state() == before);
    }

    SUBCASE("weight at or above 1.0 always fires") {
        int32_t childTicks = 0;
        aoc::sim::bt::WeightedChance node{1.0f, std::make_unique<CountingLeaf>(childTicks)};
        for (int32_t i = 0; i < 50; ++i) {
            CAPTURE(i);
            CHECK(node.tick(bb) == aoc::sim::bt::Status::Success);
        }
        CHECK(childTicks == 50);
        CHECK(rng.state() == before);
    }

    SUBCASE("NaN weight fails closed without consuming a draw") {
        int32_t childTicks = 0;
        aoc::sim::bt::WeightedChance node{std::numeric_limits<float>::quiet_NaN(),
                                          std::make_unique<CountingLeaf>(childTicks)};
        CHECK(node.tick(bb) == aoc::sim::bt::Status::Failure);
        CHECK(childTicks == 0);
        CHECK(rng.state() == before);
    }
}

TEST_CASE("WeightedChance with no RNG fails closed") {
    int32_t childTicks = 0;
    aoc::sim::bt::WeightedChance node{0.6f, std::make_unique<CountingLeaf>(childTicks)};
    aoc::sim::bt::Blackboard bb;
    REQUIRE(bb.rng == nullptr);

    CHECK(node.tick(bb) == aoc::sim::bt::Status::Failure);
    CHECK(childTicks == 0);
}

TEST_CASE("nextInt returns min for an inverted range without consuming a draw") {
    aoc::Random rng{123u};
    const std::array<uint64_t, 4> before = rng.state();

    CHECK(rng.nextInt(0, -1) == 0);   // empty-container idiom: size()-1 == -1
    CHECK(rng.nextInt(5, 2) == 5);
    CHECK(rng.state() == before);
}

TEST_CASE("nextInt with min == max returns min and consumes one draw") {
    aoc::Random rng{123u};
    aoc::Random replay{123u};

    CHECK(rng.nextInt(7, 7) == 7);
    static_cast<void>(replay.next());
    CHECK(rng.state() == replay.state());

    CHECK(rng.nextInt(std::numeric_limits<int32_t>::min(),
                      std::numeric_limits<int32_t>::min())
          == std::numeric_limits<int32_t>::min());
}

TEST_CASE("nextInt stays within an inclusive range and reaches both endpoints") {
    aoc::Random rng{2026u};
    bool sawMin = false;
    bool sawMax = false;
    for (int32_t i = 0; i < 1000; ++i) {
        CAPTURE(i);
        const int32_t v = rng.nextInt(-3, 3);
        CHECK(v >= -3);
        CHECK(v <= 3);
        sawMin = sawMin || (v == -3);
        sawMax = sawMax || (v == 3);
    }
    // Deterministic with the pinned seed; kills [min, max-1] rewrites.
    CHECK(sawMin);
    CHECK(sawMax);
}

TEST_CASE("nextInt covers extreme spans without overflow") {
    constexpr int32_t INT_MIN_V = std::numeric_limits<int32_t>::min();
    constexpr int32_t INT_MAX_V = std::numeric_limits<int32_t>::max();

    aoc::Random rng{9000u};
    for (int32_t i = 0; i < 100; ++i) {
        CAPTURE(i);
        // Full span: every int32 is in range; UBSan pins the old
        // `max - min` signed overflow.
        static_cast<void>(rng.nextInt(INT_MIN_V, INT_MAX_V));
        const int32_t nonNegative = rng.nextInt(0, INT_MAX_V);
        CHECK(nonNegative >= 0);
    }
}

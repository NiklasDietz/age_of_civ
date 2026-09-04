/**
 * @file test_random_fork.cpp
 * @brief Pins Random::fork(), the per-subsystem substream used by the turn loop.
 *
 * The turn loop hands each subsystem a forked stream instead of the master
 * generator, so a subsystem that adds or removes a draw can no longer shift
 * the sequence every other subsystem sees. These cases pin the three
 * properties that guarantee that: a fork costs exactly one master draw, the
 * fork is a deterministic function of the master state, and draws taken from
 * the fork leave the master untouched.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/core/Random.hpp"

#include <array>
#include <cstdint>

TEST_CASE("fork consumes exactly one master draw") {
    aoc::Random forked(42);
    aoc::Random advanced(42);

    [[maybe_unused]] const aoc::Random fork = forked.fork();
    [[maybe_unused]] const uint64_t oneDraw = advanced.next();

    CHECK(forked.state() == advanced.state());
}

TEST_CASE("fork is a deterministic function of the master state") {
    aoc::Random a(7);
    aoc::Random b(7);

    aoc::Random forkA = a.fork();
    aoc::Random forkB = b.fork();

    for (int i = 0; i < 16; ++i) {
        CHECK(forkA.next() == forkB.next());
    }
    CHECK(a.state() == b.state());
}

TEST_CASE("draws from a fork do not touch the master") {
    aoc::Random master(1234);
    aoc::Random reference(1234);

    aoc::Random fork                       = master.fork();
    [[maybe_unused]] const uint64_t costed = reference.next(); // the one draw the fork cost

    for (int i = 0; i < 1000; ++i) {
        [[maybe_unused]] const uint64_t v = fork.next();
    }

    CHECK(master.state() == reference.state());
    CHECK(master.next() == reference.next());
}

TEST_CASE("consecutive forks and the master are distinct streams") {
    aoc::Random master(99);
    aoc::Random first  = master.fork();
    aoc::Random second = master.fork();

    const uint64_t f = first.next();
    const uint64_t s = second.next();
    const uint64_t m = master.next();

    CHECK(f != s);
    CHECK(f != m);
    CHECK(s != m);
}

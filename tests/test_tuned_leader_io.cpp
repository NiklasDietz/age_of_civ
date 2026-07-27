/**
 * @file test_tuned_leader_io.cpp
 * @brief Pins the tuned-leader file parser (aoc/simulation/ai/TunedLeaderIO.hpp),
 *        with emphasis on the security hardening: a NaN/Inf token must never
 *        reach a gene field.
 *
 * Pins:
 *   - a well-formed Hard AI block sets the named fields;
 *   - non-finite tokens (nan / inf) are rejected per-field, leaving the
 *     LeaderBehavior default;
 *   - a missing file / empty block returns false and leaves out untouched.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/simulation/ai/TunedLeaderIO.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

/// Write `content` to a fresh temp file and return its path.
std::string writeTemp(const std::string& stem, const std::string& content) {
    const std::filesystem::path p =
        std::filesystem::temp_directory_path() / ("aoc_tunedio_" + stem + ".txt");
    std::ofstream out(p);
    out << content;
    out.close();
    return p.string();
}

}  // namespace

TEST_CASE("parseTunedLeader: well-formed block sets named fields") {
    const std::string path = writeTemp("ok",
        "Hard AI\n"
        "militaryAggression = 2.0\n"
        "prodMilitary = 1.5\n"
        "Medium AI\n"
        "militaryAggression = 9.9\n");  // must NOT be read (past Hard block)
    aoc::sim::LeaderBehavior b{};
    REQUIRE(aoc::sim::parseTunedLeader(path, b));
    CHECK(b.militaryAggression == doctest::Approx(2.0f));
    CHECK(b.prodMilitary == doctest::Approx(1.5f));
    std::filesystem::remove(path);
}

TEST_CASE("parseTunedLeader: non-finite tokens are rejected per-field") {
    const aoc::sim::LeaderBehavior defaults{};
    const std::string path = writeTemp("nan",
        "Hard AI\n"
        "militaryAggression = 2.0\n"
        "expansionism = nan\n"
        "nukeWillingness = inf\n"
        "scienceFocus = -inf\n"
        "prodMilitary = 1.25\n");
    aoc::sim::LeaderBehavior b{};
    REQUIRE(aoc::sim::parseTunedLeader(path, b));
    // Finite values applied.
    CHECK(b.militaryAggression == doctest::Approx(2.0f));
    CHECK(b.prodMilitary == doctest::Approx(1.25f));
    // Non-finite values rejected -> defaults retained.
    CHECK(b.expansionism == doctest::Approx(defaults.expansionism));
    CHECK(b.nukeWillingness == doctest::Approx(defaults.nukeWillingness));
    CHECK(b.scienceFocus == doctest::Approx(defaults.scienceFocus));
    std::filesystem::remove(path);
}

TEST_CASE("parseTunedLeader: missing file and empty block return false") {
    aoc::sim::LeaderBehavior b{};
    CHECK_FALSE(aoc::sim::parseTunedLeader("/no/such/tuned/file.txt", b));

    const std::string path = writeTemp("empty", "Header only\nno hard block here\n");
    CHECK_FALSE(aoc::sim::parseTunedLeader(path, b));
    std::filesystem::remove(path);
}

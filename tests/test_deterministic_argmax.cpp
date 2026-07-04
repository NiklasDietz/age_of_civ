/**
 * @file test_deterministic_argmax.cpp
 * @brief Pins aoc::core::argMaxByValueLowestKey (include/aoc/core/Deterministic.hpp).
 *
 * The sim used to pick a maximum by iterating an std::unordered_map with a
 * strict `>`, so value ties were resolved by whichever element the iterator
 * happened to reach first -- an implementation-defined, non-portable order
 * (WP-A2 determinism). argMaxByValueLowestKey breaks ties by the smallest key
 * so the winner is a pure function of the map contents. These cases pin:
 *   - max value wins; ties broken by lowest key;
 *   - the result is independent of insertion order (permutation test);
 *   - a value that merely equals the initial 0 threshold never wins (the
 *     Secession "zero-pressure neighbour stays a Free City" edge);
 *   - exact float ties are broken deterministically (Secession pressure);
 *   - an empty / all-threshold map returns the noneKey sentinel.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/core/Deterministic.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using Key = std::uint8_t;
constexpr Key NONE = 255;  // mirrors INVALID_PLAYER / NO_RELIGION

/// Build an unordered_map by inserting pairs in the given order.
std::unordered_map<Key, std::int32_t> makeMap(
    const std::vector<std::pair<Key, std::int32_t>>& pairs) {
    std::unordered_map<Key, std::int32_t> m;
    for (const std::pair<Key, std::int32_t>& p : pairs) { m[p.first] = p.second; }
    return m;
}

/// Brute-force reference: max value, ties broken by lowest key; NONE if no
/// entry has a value greater than 0.
std::pair<Key, std::int32_t> reference(
    const std::vector<std::pair<Key, std::int32_t>>& pairs) {
    Key bestKey = NONE;
    std::int32_t bestVal = 0;
    for (const std::pair<Key, std::int32_t>& p : pairs) {
        if (p.second > bestVal || (p.second == bestVal && bestKey != NONE && p.first < bestKey)) {
            bestVal = p.second;
            bestKey = p.first;
        }
    }
    return {bestKey, bestVal};
}

}  // namespace

TEST_CASE("argMaxByValueLowestKey: max value wins, ties broken by lowest key") {
    SUBCASE("unique maximum") {
        const std::unordered_map<Key, std::int32_t> m = makeMap({{1, 5}, {2, 9}, {3, 7}});
        const std::pair<Key, std::int32_t> r = aoc::core::argMaxByValueLowestKey(m, NONE);
        CHECK(r.first == 2);
        CHECK(r.second == 9);
    }
    SUBCASE("tie at the maximum -> lowest key") {
        const std::unordered_map<Key, std::int32_t> m = makeMap({{5, 9}, {2, 9}, {3, 7}});
        const std::pair<Key, std::int32_t> r = aoc::core::argMaxByValueLowestKey(m, NONE);
        CHECK(r.first == 2);
        CHECK(r.second == 9);
    }
    SUBCASE("tie including key 0") {
        const std::unordered_map<Key, std::int32_t> m = makeMap({{7, 9}, {0, 9}});
        const std::pair<Key, std::int32_t> r = aoc::core::argMaxByValueLowestKey(m, NONE);
        CHECK(r.first == 0);
        CHECK(r.second == 9);
    }
}

TEST_CASE("argMaxByValueLowestKey: independent of insertion order") {
    // A three-way tie at the max plus a lower entry; every insertion-order
    // permutation must yield the same winner as the brute-force reference.
    std::vector<std::pair<Key, std::int32_t>> pairs = {{10, 8}, {4, 8}, {7, 8}, {2, 3}};
    std::sort(pairs.begin(), pairs.end());
    const std::pair<Key, std::int32_t> expected = reference(pairs);  // {4, 8}
    REQUIRE(expected.first == 4);
    REQUIRE(expected.second == 8);

    std::size_t permutations = 0;
    do {
        const std::unordered_map<Key, std::int32_t> m = makeMap(pairs);
        const std::pair<Key, std::int32_t> r = aoc::core::argMaxByValueLowestKey(m, NONE);
        CHECK(r.first == expected.first);
        CHECK(r.second == expected.second);
        ++permutations;
    } while (std::next_permutation(pairs.begin(), pairs.end()));
    CHECK(permutations == 24);  // 4! insertion orders exercised
}

TEST_CASE("argMaxByValueLowestKey: threshold entries never win (Free-City guard)") {
    SUBCASE("all entries at the 0 threshold -> no winner") {
        // Mirrors the Secession edge: a zero-pressure neighbour must not become
        // the gainer; the city stays a Free City (winner == noneKey).
        const std::unordered_map<Key, std::int32_t> m = makeMap({{3, 0}, {7, 0}});
        const std::pair<Key, std::int32_t> r = aoc::core::argMaxByValueLowestKey(m, NONE);
        CHECK(r.first == NONE);
        CHECK(r.second == 0);
    }
    SUBCASE("a positive entry beats a zero entry regardless of key") {
        const std::unordered_map<Key, std::int32_t> m = makeMap({{3, 0}, {7, 5}});
        const std::pair<Key, std::int32_t> r = aoc::core::argMaxByValueLowestKey(m, NONE);
        CHECK(r.first == 7);
        CHECK(r.second == 5);
    }
}

TEST_CASE("argMaxByValueLowestKey: empty map returns the sentinel") {
    const std::unordered_map<Key, std::int32_t> m;
    const std::pair<Key, std::int32_t> r = aoc::core::argMaxByValueLowestKey(m, NONE);
    CHECK(r.first == NONE);
    CHECK(r.second == 0);
}

TEST_CASE("argMaxByValueLowestKey: exact float ties (Secession pressure)") {
    SUBCASE("bit-identical float tie -> lowest key") {
        std::unordered_map<Key, float> m;
        m[5] = 1.5f;
        m[2] = 1.5f;
        m[9] = 0.5f;
        const std::pair<Key, float> r = aoc::core::argMaxByValueLowestKey(m, NONE);
        CHECK(r.first == 2);
        CHECK(r.second == doctest::Approx(1.5f));
    }
    SUBCASE("a strictly larger float wins without invoking the tie-break") {
        std::unordered_map<Key, float> m;
        m[2] = 1.5f;
        m[5] = 1.5001f;
        const std::pair<Key, float> r = aoc::core::argMaxByValueLowestKey(m, NONE);
        CHECK(r.first == 5);
    }
}

#pragma once

/// @file Deterministic.hpp
/// @brief Order-independent reductions over hash-ordered associative containers.
///
/// Iterating a std::unordered_map / unordered_set visits elements in an
/// implementation-defined order, so a running-max loop that resolves value
/// ties with a plain `>` keeps whichever tied element the iterator happened to
/// reach first. The winner then depends on the STL implementation and on the
/// map's insertion/rehash history rather than on the game state alone -- a
/// cross-platform reproducibility and fairness hazard, even though any single
/// build is self-consistent (the determinism gate compares one build to
/// itself, so it stays green). argMaxByValueLowestKey resolves ties by the
/// smallest key, making the result a pure function of the map's contents.

#include <utility>

namespace aoc::core {

/// Return {key, value} of the entry with the greatest value, breaking value
/// ties by the smallest key. Entries whose value does not exceed a
/// value-initialised mapped_type (0 for arithmetic types) never win, matching
/// the historical `best = 0`-seeded loops this replaces. For an empty map -- or
/// one in which no entry's value exceeds that initial threshold -- returns
/// {noneKey, mapped_type{}}.
///
/// @param map     the associative container to scan.
/// @param noneKey the reserved "no winner" key sentinel (e.g. INVALID_PLAYER).
///                Must not collide with any real key so the tie-break can tell
///                "no winner yet" from a genuine low-keyed winner; this guard is
///                what keeps a value that merely equals the initial 0 threshold
///                from winning before any real entry has (e.g. a zero-pressure
///                neighbour must not beat "no gainer").
template <typename Map>
[[nodiscard]] std::pair<typename Map::key_type, typename Map::mapped_type>
argMaxByValueLowestKey(const Map& map, typename Map::key_type noneKey) {
    using Key = Map::key_type;
    using Val = Map::mapped_type;
    Key bestKey = noneKey;
    Val bestVal = Val{};
    for (const std::pair<const Key, Val>& entry : map) {
        // Exact `==` on the value is intentional: a value differing by even one
        // ULP is already ordered by `>`, so the tie branch fires only on a true
        // bit-for-bit tie, where the smallest key is the deterministic winner.
        const bool greater = entry.second > bestVal;
        const bool tieLowerKey =
            entry.second == bestVal && bestKey != noneKey && entry.first < bestKey;
        if (greater || tieLowerKey) {
            bestVal = entry.second;
            bestKey = entry.first;
        }
    }
    return {bestKey, bestVal};
}

}  // namespace aoc::core

#pragma once

/**
 * @file Random.hpp
 * @brief Deterministic PRNG for reproducible simulation.
 *
 * Uses xoshiro256** which has excellent statistical quality, is fast,
 * and produces identical sequences across platforms given the same seed.
 * Critical for multiplayer lockstep and save/load determinism.
 */

#include <array>
#include <cstdint>

namespace aoc {

class Random {
public:
    /// Seed the generator. Same seed always produces the same sequence.
    explicit constexpr Random(uint64_t seed) {
        // SplitMix64 to expand a single seed into 4 state words
        this->m_state[0] = splitmix64(seed);
        this->m_state[1] = splitmix64(this->m_state[0]);
        this->m_state[2] = splitmix64(this->m_state[1]);
        this->m_state[3] = splitmix64(this->m_state[2]);
    }

    /// Generate a uniform random uint64_t.
    [[nodiscard]] constexpr uint64_t next() {
        const uint64_t result = rotl(this->m_state[1] * 5, 7) * 9;
        const uint64_t t = this->m_state[1] << 17;

        this->m_state[2] ^= this->m_state[0];
        this->m_state[3] ^= this->m_state[1];
        this->m_state[1] ^= this->m_state[2];
        this->m_state[0] ^= this->m_state[3];

        this->m_state[2] ^= t;
        this->m_state[3] = rotl(this->m_state[3], 45);

        return result;
    }

    /// Uniform int32_t in [min, max] (inclusive).
    [[nodiscard]] constexpr int32_t nextInt(int32_t min, int32_t max) {
        uint64_t range = static_cast<uint64_t>(max - min) + 1;
        return min + static_cast<int32_t>(this->next() % range);
    }

    /// Uniform float in [0.0f, 1.0f).
    [[nodiscard]] constexpr float nextFloat() {
        // Use upper 24 bits for float mantissa precision
        return static_cast<float>(this->next() >> 40) * (1.0f / 16777216.0f);
    }

    /// Uniform float in [min, max).
    [[nodiscard]] constexpr float nextFloat(float min, float max) {
        return min + this->nextFloat() * (max - min);
    }

    /// Returns true with the given probability [0.0f, 1.0f].
    [[nodiscard]] constexpr bool chance(float probability) {
        return this->nextFloat() < probability;
    }

    /// Get the full state for serialization.
    [[nodiscard]] constexpr std::array<uint64_t, 4> state() const {
        return this->m_state;
    }

    /// Restore state from serialization.
    constexpr void setState(const std::array<uint64_t, 4>& state) {
        this->m_state = state;
    }

private:
    static constexpr uint64_t rotl(uint64_t x, int k) {
        return (x << k) | (x >> (64 - k));
    }

    static constexpr uint64_t splitmix64(uint64_t x) {
        x += 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        return x ^ (x >> 31);
    }

    std::array<uint64_t, 4> m_state{};
};

} // namespace aoc

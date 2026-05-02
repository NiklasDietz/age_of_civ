#pragma once

/**
 * @file Noise.hpp
 * @brief Deterministic value noise + fractal noise used across map-gen passes.
 *
 * Extracted 2026-05-02 from MapGenerator.cpp during the gen/ split. The
 * helpers (hashNoise / smoothstep / lerp) are header-inline so every gen/
 * pass can use them without link friction. noise2D / fractalNoise are
 * out-of-line in Noise.cpp.
 */

#include "aoc/core/Random.hpp"

#include <cmath>
#include <cstdint>

namespace aoc::map::gen {

/// Hash-based value noise. Deterministic for given (ix, iy, seed).
inline float hashNoise(int32_t ix, int32_t iy, uint64_t seed) {
    uint64_t h = seed;
    h ^= static_cast<uint64_t>(ix) * 0x517cc1b727220a95ULL;
    h ^= static_cast<uint64_t>(iy) * 0x6c62272e07bb0142ULL;
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
    h = h ^ (h >> 31);
    return static_cast<float>(h & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

inline float smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

/// Bilinearly-interpolated value noise sampled at (x,y). Uses rng.next()
/// as the per-generation seed so output is deterministic per generate() call.
[[nodiscard]] float noise2D(float x, float y, float frequency, aoc::Random& rng);

/// Multi-octave fractal noise built from noise2D. Output is normalised to [0,1].
[[nodiscard]] float fractalNoise(float x, float y, int octaves, float frequency,
                                  float persistence, aoc::Random& rng);

} // namespace aoc::map::gen

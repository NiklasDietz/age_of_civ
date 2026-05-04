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

/// Splitmix64 finalisation. Use to mix a poorly-distributed seed (e.g.
/// `static_cast<uint64_t>(plate.seedX * 1e6f)` which clusters in low bits
/// for nearby plates) into a well-spread 64-bit value before passing to
/// `hashNoise`. Plates with similar input seeds produced correlated jitter
/// before this mix was added (2026-05-04 fix).
inline uint64_t mixSeed(uint64_t s) {
    s ^= s >> 30;
    s *= 0xbf58476d1ce4e5b9ULL;
    s ^= s >> 27;
    s *= 0x94d049bb133111ebULL;
    s ^= s >> 31;
    return s;
}

/// Smooth (bilinearly-interpolated) hash-based value noise sampled at
/// continuous (x, y). Eliminates the piecewise-constant stair steps that
/// plain `hashNoise(floor(x), floor(y), seed)` produces -- boundaries
/// derived from this drift smoothly across cell edges instead of snapping.
inline float smoothHashNoise(float x, float y, uint64_t seed) {
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const int32_t ix = static_cast<int32_t>(fx);
    const int32_t iy = static_cast<int32_t>(fy);
    const float tx = smoothstep(x - fx);
    const float ty = smoothstep(y - fy);
    const float v00 = hashNoise(ix,     iy,     seed);
    const float v10 = hashNoise(ix + 1, iy,     seed);
    const float v01 = hashNoise(ix,     iy + 1, seed);
    const float v11 = hashNoise(ix + 1, iy + 1, seed);
    return lerp(lerp(v00, v10, tx),
                lerp(v01, v11, tx),
                ty);
}

/// Bilinearly-interpolated value noise sampled at (x,y). Uses rng.next()
/// as the per-generation seed so output is deterministic per generate() call.
[[nodiscard]] float noise2D(float x, float y, float frequency, aoc::Random& rng);

/// Multi-octave fractal noise built from noise2D. Output is normalised to [0,1].
[[nodiscard]] float fractalNoise(float x, float y, int octaves, float frequency,
                                  float persistence, aoc::Random& rng);

} // namespace aoc::map::gen

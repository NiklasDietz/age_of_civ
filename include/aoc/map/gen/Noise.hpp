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

/// Splitmix64 finalisation. Mixes a poorly-distributed seed into a
/// well-spread 64-bit value before passing to `hashNoise`. Required
/// when seeds cluster in low bits (nearby plate coords used as input).
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

/// Hash-based value noise on a 3D lattice. Deterministic for given
/// (ix, iy, iz, seed).
inline float hashNoise3(int32_t ix, int32_t iy, int32_t iz, uint64_t seed) {
    uint64_t h = seed;
    h ^= static_cast<uint64_t>(ix) * 0x517cc1b727220a95ULL;
    h ^= static_cast<uint64_t>(iy) * 0x6c62272e07bb0142ULL;
    h ^= static_cast<uint64_t>(iz) * 0x9e3779b97f4a7c15ULL;
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
    h = h ^ (h >> 31);
    return static_cast<float>(h & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

/// Trilinearly-interpolated hash value noise at continuous (x, y, z).
/// Sampling on unit-sphere coordinates gives seam-free fields on the
/// globe (no antimeridian/pole discontinuity, unlike lat/lon-space
/// sampling). Pure function of its arguments -- safe inside parallel
/// loops.
inline float smoothHashNoise3(float x, float y, float z, uint64_t seed) {
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const float fz = std::floor(z);
    const int32_t ix = static_cast<int32_t>(fx);
    const int32_t iy = static_cast<int32_t>(fy);
    const int32_t iz = static_cast<int32_t>(fz);
    const float tx = smoothstep(x - fx);
    const float ty = smoothstep(y - fy);
    const float tz = smoothstep(z - fz);
    const float v000 = hashNoise3(ix,     iy,     iz,     seed);
    const float v100 = hashNoise3(ix + 1, iy,     iz,     seed);
    const float v010 = hashNoise3(ix,     iy + 1, iz,     seed);
    const float v110 = hashNoise3(ix + 1, iy + 1, iz,     seed);
    const float v001 = hashNoise3(ix,     iy,     iz + 1, seed);
    const float v101 = hashNoise3(ix + 1, iy,     iz + 1, seed);
    const float v011 = hashNoise3(ix,     iy + 1, iz + 1, seed);
    const float v111 = hashNoise3(ix + 1, iy + 1, iz + 1, seed);
    const float c00 = lerp(v000, v100, tx);
    const float c10 = lerp(v010, v110, tx);
    const float c01 = lerp(v001, v101, tx);
    const float c11 = lerp(v011, v111, tx);
    return lerp(lerp(c00, c10, ty), lerp(c01, c11, ty), tz);
}

/// Bilinearly-interpolated value noise sampled at (x,y). Uses rng.next()
/// as the per-generation seed so output is deterministic per generate() call.
[[nodiscard]] float noise2D(float x, float y, float frequency, aoc::Random& rng);

/// Multi-octave fractal noise built from noise2D. Output is normalised to [0,1].
[[nodiscard]] float fractalNoise(float x, float y, int octaves, float frequency,
                                  float persistence, aoc::Random& rng);

} // namespace aoc::map::gen

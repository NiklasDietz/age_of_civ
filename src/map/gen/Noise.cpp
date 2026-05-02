/**
 * @file Noise.cpp
 * @brief Out-of-line bodies for value/fractal noise.
 */

#include "aoc/map/gen/Noise.hpp"

namespace aoc::map::gen {

float noise2D(float x, float y, float frequency, aoc::Random& rng) {
    uint64_t noiseSeed = rng.next();

    float fx = x * frequency;
    float fy = y * frequency;

    int32_t ix = static_cast<int32_t>(std::floor(fx));
    int32_t iy = static_cast<int32_t>(std::floor(fy));

    float tx = fx - static_cast<float>(ix);
    float ty = fy - static_cast<float>(iy);

    tx = smoothstep(tx);
    ty = smoothstep(ty);

    float c00 = hashNoise(ix,     iy,     noiseSeed);
    float c10 = hashNoise(ix + 1, iy,     noiseSeed);
    float c01 = hashNoise(ix,     iy + 1, noiseSeed);
    float c11 = hashNoise(ix + 1, iy + 1, noiseSeed);

    float top    = lerp(c00, c10, tx);
    float bottom = lerp(c01, c11, tx);
    return lerp(top, bottom, ty);
}

float fractalNoise(float x, float y, int octaves, float frequency,
                    float persistence, aoc::Random& rng) {
    float value     = 0.0f;
    float amplitude = 1.0f;
    float maxValue  = 0.0f;
    float freq      = frequency;

    for (int i = 0; i < octaves; ++i) {
        value    += noise2D(x, y, freq, rng) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        freq      *= 2.0f;
    }

    return value / maxValue;
}

} // namespace aoc::map::gen

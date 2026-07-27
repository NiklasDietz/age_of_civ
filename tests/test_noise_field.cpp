/**
 * @file test_noise_field.cpp
 * @brief Pins the field properties of aoc::map::gen::noise2D / fractalNoise.
 *
 * Why this test exists. These functions used to take an `aoc::Random&` and draw
 * `rng.next()` as the lattice seed INSIDE the call. Every call site is inside a
 * per-tile loop, so consecutive tiles interpolated over completely different
 * lattices: the bilinear blend averaged unrelated values and the result was
 * per-tile white noise, not a coherent field. Temperature, moisture, shelf
 * width, forest cover and hill clustering were all affected, and the bug was
 * invisible because white noise still LOOKS like noise -- the only symptom was
 * that "low-frequency" fields had no low frequencies.
 *
 * The properties below are what distinguish a field from white noise, so they
 * are what a regression would break:
 *
 *   - purity: same arguments give the same value, call order irrelevant;
 *   - continuity: neighbouring samples are close (white noise fails this);
 *   - seed separation: different seeds give different fields;
 *   - range and the octaves <= 0 guard.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/map/gen/Noise.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr uint64_t SEED_A = 0x9E3779B97F4A7C15ULL;
constexpr uint64_t SEED_B = 0xBF58476D1CE4E5B9ULL;

} // namespace

TEST_CASE("fractalNoise is pure: same arguments give the same value") {
    // The defining property the old signature could not have. Interleaving two
    // different sample points would previously advance a shared stream and make
    // the second reading of the first point differ.
    const float first  = aoc::map::gen::fractalNoise(3.25f, 7.5f, 4, 2.0f, 0.5f, SEED_A);
    const float other  = aoc::map::gen::fractalNoise(11.0f, 2.0f, 4, 2.0f, 0.5f, SEED_A);
    const float repeat = aoc::map::gen::fractalNoise(3.25f, 7.5f, 4, 2.0f, 0.5f, SEED_A);
    CHECK(first == repeat);
    CHECK(other != repeat); // distinct points really are distinct
}

TEST_CASE("noise2D is pure: same arguments give the same value") {
    const float a = aoc::map::gen::noise2D(1.5f, 2.5f, 3.0f, SEED_A);
    const float b = aoc::map::gen::noise2D(1.5f, 2.5f, 3.0f, SEED_A);
    CHECK(a == b);
}

TEST_CASE("fractalNoise is continuous: neighbouring samples are close") {
    // THIS is the test that would have caught the original bug. A coherent
    // field sampled a hundredth of a lattice cell apart moves very little; the
    // old per-call-reseeded version returned two independent uniform draws, so
    // the mean absolute step was ~1/3 of the full range.
    //
    // A single lattice cell of the base octave spans 1/frequency in x, so
    // stepping 0.01 cells must move the value far less than the ~0.33 expected
    // of independent uniforms.
    constexpr float STEP = 0.01f;
    float worstStep      = 0.0f;
    double totalStep     = 0.0;
    int32_t samples      = 0;
    for (float y = 0.0f; y < 4.0f; y += 0.37f) {
        for (float x = 0.0f; x < 4.0f; x += 0.23f) {
            const float here = aoc::map::gen::fractalNoise(x, y, 4, 2.0f, 0.5f, SEED_A);
            const float next = aoc::map::gen::fractalNoise(x + STEP, y, 4, 2.0f, 0.5f, SEED_A);
            const float step = std::abs(next - here);
            worstStep        = std::max(worstStep, step);
            totalStep += static_cast<double>(step);
            ++samples;
        }
    }
    REQUIRE(samples > 100);
    const double meanStep = totalStep / static_cast<double>(samples);
    // Independent uniform draws would average 1/3. A field must be an order of
    // magnitude below that.
    CHECK(meanStep < 0.03);
    CHECK(worstStep < 0.15);
}

TEST_CASE("fractalNoise separates seeds: a different seed is a different field") {
    int32_t differing = 0;
    int32_t total     = 0;
    for (float y = 0.0f; y < 5.0f; y += 0.5f) {
        for (float x = 0.0f; x < 5.0f; x += 0.5f) {
            const float a = aoc::map::gen::fractalNoise(x, y, 3, 2.0f, 0.5f, SEED_A);
            const float b = aoc::map::gen::fractalNoise(x, y, 3, 2.0f, 0.5f, SEED_B);
            if (std::abs(a - b) > 1e-6f) {
                ++differing;
            }
            ++total;
        }
    }
    CHECK(differing == total);
}

TEST_CASE("fractalNoise octaves add detail rather than reinforcing one lattice") {
    // Octaves must not share a lattice. If they did, octave n+1 at doubled
    // coordinates would hit the same hash cells as octave n and the sum would
    // reinforce itself, so adding octaves would barely change the value.
    // Requiring the 1-octave and 4-octave fields to differ pins that.
    int32_t differing = 0;
    int32_t total     = 0;
    for (float y = 0.0f; y < 5.0f; y += 0.5f) {
        for (float x = 0.0f; x < 5.0f; x += 0.5f) {
            const float one  = aoc::map::gen::fractalNoise(x, y, 1, 2.0f, 0.5f, SEED_A);
            const float four = aoc::map::gen::fractalNoise(x, y, 4, 2.0f, 0.5f, SEED_A);
            if (std::abs(one - four) > 1e-4f) {
                ++differing;
            }
            ++total;
        }
    }
    CHECK(differing == total);
}

TEST_CASE("fractalNoise stays in [0, 1] and guards octaves <= 0") {
    for (float y = -3.0f; y < 6.0f; y += 0.31f) {
        for (float x = -3.0f; x < 6.0f; x += 0.29f) {
            for (const int32_t octaves : {1, 2, 4, 8}) {
                const float v = aoc::map::gen::fractalNoise(x, y, octaves, 2.0f, 0.5f, SEED_A);
                CHECK(v >= 0.0f);
                CHECK(v <= 1.0f);
                CHECK(std::isfinite(v));
            }
        }
    }
    // octaves <= 0 would leave maxValue == 0 and produce NaN from the
    // normalisation, which would silently poison every biome threshold.
    CHECK(aoc::map::gen::fractalNoise(1.0f, 1.0f, 0, 2.0f, 0.5f, SEED_A) == 0.0f);
    CHECK(aoc::map::gen::fractalNoise(1.0f, 1.0f, -5, 2.0f, 0.5f, SEED_A) == 0.0f);
}

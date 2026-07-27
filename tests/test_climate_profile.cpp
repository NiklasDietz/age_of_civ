/**
 * @file test_climate_profile.cpp
 * @brief Pins the zonal climate calibration in src/map/gen/ClimateBiome.cpp.
 *
 * Why this test exists. The biome bands are not stored anywhere -- they are
 * wherever the zonal temperature profile crosses the isotherms, so the band
 * positions are a derived property of two pure functions plus four constants.
 * That is exactly what went wrong before: the thresholds had been tuned while
 * latitude was reconstructed as `2 * |row/height - 0.5|`, which equals sin|lat|
 * rather than |lat|/90. When the projection work supplied a true latitude the
 * thresholds stayed put and every band moved -- Tundra's onset went from ~57 deg
 * to 75.5 deg, which collapsed Tundra to 0.1 % of land and left Grassland above
 * 66 deg. Nothing failed, because nothing was pinned.
 *
 * So this file asserts the two things a future latitude or projection change
 * could silently break:
 *
 *   1. the temperature profile reproduces the OBSERVED zonal annual mean at
 *      every 10-degree band (this is the citable half of the calibration);
 *   2. each isotherm lands at the latitude the code comments claim, which is
 *      what the biome map is actually built on.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/map/gen/ClimateBiome.hpp"

#include <cmath>

namespace {

/// Latitude at which the zonal profile crosses `targetC`, by bisection on
/// [0, 90]. The profile is strictly decreasing in |latitude|, so the root is
/// unique wherever it exists.
float latitudeOfIsotherm(float targetC) {
    float lo = 0.0f;
    float hi = 90.0f;
    for (int32_t i = 0; i < 60; ++i) {
        const float mid = 0.5f * (lo + hi);
        if (aoc::map::gen::zonalMeanAnnualTempC(mid) > targetC) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5f * (lo + hi);
}

} // namespace

TEST_CASE("zonal temperature reproduces the observed annual mean within 2 C") {
    // Observed hemisphere-averaged zonal annual mean near-surface air
    // temperature at sea level, degrees Celsius. The fit is documented as
    // accurate to 1.7 C; 2.0 C is the assertion tolerance.
    struct Observed {
        float latDeg;
        float tempC;
    };
    const Observed OBSERVED[] = {
        {0.0f, 26.0f}, {30.0f, 20.0f}, {40.0f, 14.0f},  {50.0f, 7.5f},
        {60.0f, 0.0f}, {70.0f, -8.0f}, {80.0f, -16.0f}, {90.0f, -22.0f},
    };
    for (const Observed& o : OBSERVED) {
        const float modelled = aoc::map::gen::zonalMeanAnnualTempC(o.latDeg);
        CHECK(std::abs(modelled - o.tempC) <= 2.0f);
    }
}

TEST_CASE("zonal temperature is monotone decreasing away from the equator") {
    float previous = aoc::map::gen::zonalMeanAnnualTempC(0.0f);
    for (float lat = 1.0f; lat <= 90.0f; lat += 1.0f) {
        const float current = aoc::map::gen::zonalMeanAnnualTempC(lat);
        CHECK(current < previous);
        previous = current;
    }
}

TEST_CASE("zonal temperature is symmetric and clamped outside [0, 90]") {
    CHECK(aoc::map::gen::zonalMeanAnnualTempC(-45.0f) ==
          doctest::Approx(aoc::map::gen::zonalMeanAnnualTempC(45.0f)));
    CHECK(aoc::map::gen::zonalMeanAnnualTempC(120.0f) ==
          doctest::Approx(aoc::map::gen::zonalMeanAnnualTempC(90.0f)));
}

TEST_CASE("each biome isotherm lands at the latitude the calibration claims") {
    // These four latitudes ARE the biome bands. A change here means every
    // generated world's climate zones moved, which is a deliberate decision, not
    // a refactor -- update the numbers together with the comments in
    // ClimateBiome.cpp and say why in the commit message.
    CHECK(latitudeOfIsotherm(aoc::map::gen::ISO_SUBTROPICAL_C) ==
          doctest::Approx(37.9f).epsilon(0.02));
    CHECK(latitudeOfIsotherm(aoc::map::gen::ISO_TEMPERATE_C) ==
          doctest::Approx(57.8f).epsilon(0.02));
    CHECK(latitudeOfIsotherm(aoc::map::gen::ISO_TREE_LINE_C) ==
          doctest::Approx(69.7f).epsilon(0.02));
    CHECK(latitudeOfIsotherm(aoc::map::gen::ISO_PERMANENT_ICE_C) ==
          doctest::Approx(80.0f).epsilon(0.02));
}

TEST_CASE("isotherms are strictly ordered coldest to warmest") {
    CHECK(aoc::map::gen::ISO_PERMANENT_ICE_C < aoc::map::gen::ISO_TREE_LINE_C);
    CHECK(aoc::map::gen::ISO_TREE_LINE_C < aoc::map::gen::ISO_TEMPERATE_C);
    CHECK(aoc::map::gen::ISO_TEMPERATE_C < aoc::map::gen::ISO_SUBTROPICAL_C);
}

TEST_CASE("moisture profile is driest in the subtropical high, not the temperate band") {
    // The defect this replaced: the dry minimum sat at a single latitude
    // (28.8 deg) with a wet ramp either side, so the driest 10-degree BAND came
    // out at 30-40 deg and the 20-30 deg subtropics were wetter than it.
    // Measured consequence was 8.1 % desert in 20-30 deg against 10.5 % in
    // 30-40 deg -- the wrong way round.
    const float subtropics = aoc::map::gen::zonalMoisture(25.0f);
    const float temperate  = aoc::map::gen::zonalMoisture(35.0f);
    const float midlat     = aoc::map::gen::zonalMoisture(50.0f);
    const float itcz       = aoc::map::gen::zonalMoisture(4.0f);

    CHECK(subtropics < temperate);
    CHECK(subtropics < midlat);
    CHECK(itcz > midlat);

    // The arid belt is a PLATEAU: every latitude in 18-32 deg is equally dry,
    // which is what puts the desert peak in the right band.
    for (float lat = 18.0f; lat <= 32.0f; lat += 1.0f) {
        CHECK(aoc::map::gen::zonalMoisture(lat) == doctest::Approx(subtropics));
    }
}

TEST_CASE("moisture profile stays in [0, 1] across the whole hemisphere") {
    for (float lat = 0.0f; lat <= 90.0f; lat += 0.5f) {
        const float m = aoc::map::gen::zonalMoisture(lat);
        CHECK(m >= 0.0f);
        CHECK(m <= 1.0f);
    }
}

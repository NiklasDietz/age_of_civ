/**
 * @file test_crust_mass_conservation.cpp
 * @brief Crustal thickening moves material; it does not create it.
 *
 *        thickenFromClosingRate used to add thickness at every convergent
 *        continental cell and debit nobody, fabricating crustal volume each
 *        epoch. Over a 3 Gy run that drove the whole convergent belt into the
 *        70 km cap, so peak crust read p95 through p100 = exactly 70.0 on every
 *        seed and the top of the distribution carried no information. The
 *        mountain criterion thresholds against that distribution, so mountains
 *        came out at 0.00 % of land on every seed measured.
 *
 *        Volume is what conserves, not thickness: cells are equal in DEGREES,
 *        so their area goes as cos(lat) and a kilometre of thickness is worth
 *        more volume at the equator than near a pole.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/map/gen/SphereField.hpp"
#include "aoc/map/gen/SphereFieldPhysics.hpp"
#include "aoc/map/gen/PlatePhysics.hpp"

#include <cmath>
#include <vector>

using aoc::map::gen::SphereField;

namespace {

constexpr double DEG2RAD = 0.01745329252;

/// Total continental crustal volume in arbitrary consistent units: thickness
/// weighted by the cell's area, which on an equal-degree grid goes as cos(lat).
[[nodiscard]] double continentalVolume(const SphereField& f) {
    double total = 0.0;
    for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
        if (f.continentalFraction[i] <= 0.5f) { continue; }
        const int32_t latIdx =
            static_cast<int32_t>(i / static_cast<std::size_t>(SphereField::LON_CELLS));
        const double latDeg =
            (static_cast<double>(latIdx) + 0.5) * static_cast<double>(SphereField::CELL_DEG) - 90.0;
        total += static_cast<double>(f.crustThicknessKm[i]) * std::cos(latDeg * DEG2RAD);
    }
    return total;
}

/// A continental band straddling a latitude, every cell convergent, so the pass
/// has both cells to thicken and neighbours to take material from.
[[nodiscard]] SphereField convergentBand(int32_t latIdx, float thicknessKm, float rate) {
    SphereField f;
    f.resize();
    for (int32_t lat = latIdx - 2; lat <= latIdx + 2; ++lat) {
        for (int32_t lon = 0; lon < SphereField::LON_CELLS; ++lon) {
            const std::size_t idx = SphereField::cellIndex(lon, lat);
            f.continentalFraction[idx]        = 1.0f;
            f.crustThicknessKm[idx]           = thicknessKm;
            f.boundaryType[idx]               = 1u; // convergent
            f.convergenceRateRadPerMy[idx]    = rate;
        }
    }
    return f;
}

} // namespace

TEST_CASE("thickening conserves continental crustal volume") {
    SphereField f            = convergentBand(180, 40.0f, 0.002f);
    const double volumeBefore = continentalVolume(f);
    REQUIRE(volumeBefore > 0.0);

    aoc::map::gen::thickenFromClosingRate(f, 10.0f);

    const double volumeAfter = continentalVolume(f);
    // Not bit-exact: the pass accumulates in float. A tenth of a percent is far
    // tighter than the fabrication it replaces, which added volume without
    // bound on every epoch of a 3 Gy run.
    CHECK(volumeAfter == doctest::Approx(volumeBefore).epsilon(0.001));
}

TEST_CASE("thickening still redistributes, it does not merely do nothing") {
    // The cheapest way to pass a conservation test is to change nothing at all,
    // so pin that material actually moved.
    SphereField f = convergentBand(180, 40.0f, 0.002f);
    std::vector<float> before(f.crustThicknessKm);

    aoc::map::gen::thickenFromClosingRate(f, 10.0f);

    bool anyThickened = false;
    bool anyThinned   = false;
    for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
        if (f.crustThicknessKm[i] > before[i] + 1e-4f) { anyThickened = true; }
        if (f.crustThicknessKm[i] < before[i] - 1e-4f) { anyThinned = true; }
    }
    CHECK(anyThickened);
    CHECK(anyThinned); // the debit side: somebody paid for it
}

TEST_CASE("an isolated convergent cell cannot pile up out of nothing") {
    // No continental neighbours means no crust to shorten, so nothing to gain.
    SphereField f;
    f.resize();
    const std::size_t idx = SphereField::cellIndex(100, 180);
    f.continentalFraction[idx]     = 1.0f;
    f.crustThicknessKm[idx]        = 40.0f;
    f.boundaryType[idx]            = 1u;
    f.convergenceRateRadPerMy[idx] = 0.01f;

    aoc::map::gen::thickenFromClosingRate(f, 10.0f);

    CHECK(f.crustThicknessKm[idx] == doctest::Approx(40.0f));
}

TEST_CASE("donors are not shortened without limit") {
    // A long run must not drive the neighbourhood to nothing. Repeated epochs
    // on the same band should settle rather than hollow it out.
    SphereField f = convergentBand(180, 40.0f, 0.004f);
    for (int32_t epoch = 0; epoch < 200; ++epoch) {
        aoc::map::gen::thickenFromClosingRate(f, 10.0f);
    }
    float minThickness = 1e9f;
    for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
        if (f.continentalFraction[i] <= 0.5f) { continue; }
        minThickness = std::min(minThickness, f.crustThicknessKm[i]);
        // And nothing may exceed the physical ceiling.
        CHECK(f.crustThicknessKm[i] <=
              doctest::Approx(aoc::map::gen::PhysicsConstants::maxCrustThicknessKm));
    }
    CHECK(minThickness > 0.0f);
}

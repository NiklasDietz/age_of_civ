/**
 * @file test_isostasy.cpp
 * @brief Pins the isostatic elevation law in gen/PlatePhysics.hpp.
 *
 * Why this test exists. Until 2026-08-10 elevation was
 * `h * (1 - rho/rhoM) - mantleDatumM` with a 3549 m datum derived from the
 * OCEANIC branch alone; the continental branch was never calibrated against
 * anything. The consequence was not a small offset, it was categorical:
 *
 *   crust thickness that sits at sea level ....... 14.51 km
 *   crust thickness at erosion's base level ...... 17.81 km  (seaLevel + 600)
 *   thinnest continental crust measured anywhere .. 15.8 km  (p1, seed 42)
 *
 * so no continental cell on the planet could ever be under water. Land area
 * was continental-crust area by construction, every coastline was the
 * continentalFraction = 0.5 contour, and a continental shelf was impossible
 * rather than rare -- the first water tile next to land measured a median
 * depth of 2837 m and the second 4926 m.
 *
 * A law that maps crustal thickness to elevation is the kind of thing that
 * silently drifts when a density constant is edited, and every downstream
 * threshold -- the erosion base level, the mountain mask, the shelf-break
 * depth -- is anchored through it. So the law is pinned against values taken
 * from the literature rather than from the implementation, including one
 * (the ~5 km plateau ceiling) that the law was NOT fitted to and therefore
 * tests it rather than restating it.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/map/gen/PlatePhysics.hpp"

#include <cmath>

using aoc::map::gen::continentalElevationPerKmM;
using aoc::map::gen::gdh1DepthM;
using aoc::map::gen::isostaticElevationM;
using aoc::map::gen::PhysicsConstants;

namespace {

/// Pure continental column of the given thickness. Age is irrelevant on this
/// branch (continental lithosphere does not follow a seafloor cooling curve),
/// and passing a nonzero one here is deliberate: it must not matter.
float continental(float km) {
    return isostaticElevationM(km, 1.0f, 2500.0f);
}

/// Pure oceanic column of normal 7 km thickness at the given age.
float oceanic(float ageMy) {
    return isostaticElevationM(PhysicsConstants::initialOceanicThicknessKm, 0.0f, ageMy);
}

} // namespace

TEST_CASE("continental branch is anchored on Earth's mean crust and mean land elevation") {
    // The anchor itself: Earth's 41 km mean continental thickness stands at
    // its ~840 m mean land elevation (Christensen & Mooney 1995; Turcotte &
    // Schubert 2014 hypsometry).
    CHECK(continental(41.0f) == doctest::Approx(840.0f).epsilon(0.001));

    // Airy slope, 1000 * (1 - 2830/3300). Pinned separately from the values
    // below because every km-to-metre threshold in worldgen goes through it.
    CHECK(continentalElevationPerKmM() == doctest::Approx(142.42f).epsilon(0.001));

    // The ladder either side of the anchor.
    CHECK(continental(30.0f) == doctest::Approx(-726.7f).epsilon(0.002));
    CHECK(continental(35.0f) == doctest::Approx(-14.5f).epsilon(0.05));
    CHECK(continental(60.0f) == doctest::Approx(3546.0f).epsilon(0.002));

    // INDEPENDENT CHECK, not a restatement: the law was anchored only at
    // 41 km / 840 m, yet it puts the 70 km maximum-thickness column at
    // ~4970 m -- the gravitational-potential-energy ceiling for orogenic
    // plateaux derived on entirely separate grounds (Rey et al. 2010, and
    // observed: Tibet ~5000 m, Altiplano ~3800 m). If a density edit breaks
    // this row while leaving the anchor intact, the slope is wrong.
    CHECK(continental(PhysicsConstants::maxCrustThicknessKm) ==
          doctest::Approx(4970.0f).epsilon(0.002));
}

TEST_CASE("continental branch admits a submerged population -- the whole point") {
    // The defect this law replaced made submerged continental crust
    // impossible. Assert the property directly rather than trusting the
    // numbers above to imply it.
    const float seaLevelCrustKm =
        PhysicsConstants::refContinentalThicknessKm -
        PhysicsConstants::refContinentalElevationM / continentalElevationPerKmM();
    CHECK(seaLevelCrustKm == doctest::Approx(35.1f).epsilon(0.01));

    // A shelf is continental crust standing between the shelf break (-140 m)
    // and sea level. Under the old law that band was 0.77 km wide in crustal
    // thickness AND sat at 13.7-14.5 km, below anything the simulation ever
    // produced. It is now a band around the ~35 km stretched-margin thickness
    // that rifting actually creates.
    CHECK(continental(35.1f) > -140.0f);
    CHECK(continental(35.1f) < 140.0f);
    CHECK(continental(34.0f) < -140.0f); // below the shelf break: slope
    CHECK(continental(36.0f) > 0.0f);    // emergent coastal plain
}

TEST_CASE("oceanic branch follows GDH1 depth-age") {
    // Stein & Stein 1992 (Nature 359:123), table of the published curve.
    CHECK(gdh1DepthM(0.0f) == doctest::Approx(2600.0f).epsilon(0.001));
    CHECK(gdh1DepthM(20.0f) == doctest::Approx(4233.0f).epsilon(0.005));
    CHECK(gdh1DepthM(80.0f) == doctest::Approx(5383.0f).epsilon(0.005));
    CHECK(gdh1DepthM(180.0f) == doctest::Approx(5634.0f).epsilon(0.005));

    // Monotone deepening, and saturating rather than diverging. This matters
    // more here than on Earth: subduction does not recycle this simulation's
    // seafloor fast enough, so oceanic crust reaches the full 3 Gy run length
    // (measured: oceanic crustAgeMy p75 = 3000 My against Earth's ~180 My
    // maximum). An unsaturated sqrt(age) law would put that floor at -22 km.
    // At 3 Gy the exponential underflows and the depth is exactly the plate-
    // model equilibrium, which is the correct limit, not a clamp.
    CHECK(gdh1DepthM(400.0f) < 5651.0f);
    CHECK(gdh1DepthM(3000.0f) == doctest::Approx(5651.0f).epsilon(0.001));
    CHECK(gdh1DepthM(3000.0f) > gdh1DepthM(180.0f));

    // Negative ages are clamped, not reflected into a sqrt of a negative.
    CHECK(gdh1DepthM(-5.0f) == doctest::Approx(2600.0f).epsilon(0.001));
    CHECK(std::isfinite(gdh1DepthM(-5.0f)));

    // Ridge crest and old abyssal floor, as elevations.
    CHECK(oceanic(0.0f) == doctest::Approx(-2600.0f).epsilon(0.001));
    CHECK(oceanic(180.0f) == doctest::Approx(-5634.0f).epsilon(0.005));
}

TEST_CASE("both branches respond to crustal thickness") {
    // The oceanic branch MUST keep a thickness term. An age-only branch would
    // make crustal thinning at rifted margins and sediment loading of the
    // shelf no-ops on exactly the cells where they have to act, silently.
    const float normal = isostaticElevationM(7.0f, 0.0f, 80.0f);
    const float thick  = isostaticElevationM(20.0f, 0.0f, 80.0f); // oceanic plateau
    const float thin   = isostaticElevationM(4.0f, 0.0f, 80.0f);  // stretched crust
    CHECK(thick > normal);
    CHECK(thin < normal);
    // Oceanic Airy slope, 1000 * (1 - 2900/3300) = 121.2 m/km.
    CHECK((thick - normal) / 13.0f == doctest::Approx(121.2f).epsilon(0.01));
}

TEST_CASE("composition blends continuously between the branches") {
    // No step at the continent-ocean transition: a cell that is half one and
    // half the other lands between them, not on either.
    const float land  = isostaticElevationM(41.0f, 1.0f, 100.0f);
    const float sea   = isostaticElevationM(41.0f, 0.0f, 100.0f);
    const float mixed = isostaticElevationM(41.0f, 0.5f, 100.0f);
    CHECK(mixed == doctest::Approx(0.5f * (land + sea)).epsilon(0.001));

    // contFrac outside [0, 1] is clamped rather than extrapolated -- an
    // extrapolated blend would invent elevations beyond either branch.
    CHECK(isostaticElevationM(41.0f, 1.5f, 100.0f) == doctest::Approx(land).epsilon(0.001));
    CHECK(isostaticElevationM(41.0f, -0.5f, 100.0f) == doctest::Approx(sea).epsilon(0.001));
}

TEST_CASE("continental branch ignores crustal age") {
    // Continental lithosphere does not follow a seafloor cooling curve. If the
    // GDH1 term ever leaks into the continental branch, a 3 Gy craton would
    // sink 5.6 km.
    CHECK(isostaticElevationM(41.0f, 1.0f, 0.0f) ==
          doctest::Approx(isostaticElevationM(41.0f, 1.0f, 3000.0f)).epsilon(0.001));
}

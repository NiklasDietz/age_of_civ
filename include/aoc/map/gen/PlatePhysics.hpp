#pragma once

/**
 * @file PlatePhysics.hpp
 * @brief Physical constants and the isostatic elevation law used by the
 *        SphereField plate-tectonic simulation. Values cite Turcotte &
 *        Schubert 2014 and the derivations in docs/PHYSICS_FIRST_REWRITE.md.
 *
 * Densities follow the standard 3-layer convention:
 *   rho_continental = 2830 kg/m^3 (bulk continental crust)
 *   rho_oceanic     = 2900 kg/m^3 (basaltic)
 *   rho_mantle      = 3300 kg/m^3 (peridotite)
 *
 * 2026-05-06 cleanup: per-plate Lagrangian PhysicsGrid + helpers
 * (initialisePlatePhysicsGrid, accumulateConvergenceStrain,
 * thickenCrustFromStrain, applySurfaceErosion, recomputeIsostatic-
 * Elevation, peakSample, plateLocalToCell, bilinearSample,
 * PlateSphereIndex) deleted; orphaned post-P6.1 once the SphereField
 * raster pipeline became authoritative.
 */

#include <algorithm>
#include <cmath>

namespace aoc::map::gen {

/// Physical constants used by the plate-physics module. Values are SI
/// or canonical geological literature units; comments cite the source.
struct PhysicsConstants {
    /// Bulk continental-crust density (Christensen & Mooney 1995, JGR
    /// 100:9761 -- whole-crust average, not the granitic upper crust alone).
    ///
    /// 2026-08-10: was 2700, the upper-crust value. The elevation law is
    /// anchored on the WHOLE continental column, so the whole-column density
    /// is the consistent choice; using 2700 there overstates the elevation
    /// gained per km of crust by 28 % (181.8 vs 142.4 m/km).
    static constexpr float rhoContinentalKgM3 = 2830.0f;
    /// Basaltic oceanic-crust density (Turcotte & Schubert 2014, table 2.1).
    static constexpr float rhoOceanicKgM3 = 2900.0f;
    /// Mantle (peridotite) density (Turcotte & Schubert 2014, table 2.1).
    static constexpr float rhoMantleKgM3 = 3300.0f;
    /// Reference continental crustal thickness (km) -- the global mean
    /// (Christensen & Mooney 1995). The elevation law is anchored here.
    static constexpr float refContinentalThicknessKm = 41.0f;
    /// Elevation of reference-thickness continental crust, metres above
    /// present-day Earth sea level. Earth's mean land elevation is ~840 m
    /// (Turcotte & Schubert 2014, ch. 2 hypsometry).
    static constexpr float refContinentalElevationM = 840.0f;
    /// Initial continental crust thickness (km). Set to the reference
    /// thickness so a fresh craton starts at the anchor rather than at an
    /// arbitrary offset from it.
    static constexpr float initialContinentalThicknessKm = 41.0f;
    /// Initial oceanic crust thickness (km). Earth-mean is ~7 km.
    static constexpr float initialOceanicThicknessKm = 7.0f;
    /// Maximum sustainable crust thickness (km). 2026-05-06 P6.3:
    /// Tibet observed steady-state ceiling 70-75 km (Turcotte &
    /// Schubert 2014, ch. 4). Without delamination simulation the
    /// cap is the physical ceiling, not the steady-state mean.
    static constexpr float maxCrustThicknessKm = 70.0f;
    /// Earth radius in km, used by lat/lon -> km conversions.
    static constexpr float earthRadiusKm = 6371.0f;
};

/// Metres of surface elevation gained per km of continental crust, by Airy
/// isostasy: 1000 * (1 - rho_c / rho_m). 142.4 m/km at the densities above.
///
/// This number is load-bearing in a way that is easy to get wrong: it converts
/// between "how thick is the crust" and "how high does it stand", so every
/// threshold expressed in km and every threshold expressed in metres are
/// related through it. Changing rhoContinentalKgM3 moves it.
[[nodiscard]] inline constexpr float continentalElevationPerKmM() noexcept {
    return 1000.0f *
           (1.0f - PhysicsConstants::rhoContinentalKgM3 / PhysicsConstants::rhoMantleKgM3);
}

/// Metres of surface elevation gained per km of oceanic crust.
[[nodiscard]] inline constexpr float oceanicElevationPerKmM() noexcept {
    return 1000.0f * (1.0f - PhysicsConstants::rhoOceanicKgM3 / PhysicsConstants::rhoMantleKgM3);
}

/// Depth of normal (7 km crust) oceanic lithosphere below present-day sea
/// level, metres, as a function of crustal age in My. Stein & Stein 1992
/// GDH1 (Nature 359:123): a sqrt(age) half-space regime out to ~20 Ma, then
/// a plate-model exponential approach to a ~5650 m equilibrium depth.
///
///   0 Ma -> 2600 m   (ridge crest)
///  20 Ma -> 4233 m
///  80 Ma -> 5383 m
/// 180 Ma -> 5634 m
///
/// Ages are clamped at zero; there is no upper clamp because the exponential
/// already saturates.
[[nodiscard]] inline float gdh1DepthM(float ageMy) noexcept {
    const float t = std::max(0.0f, ageMy);
    if (t < 20.0f) {
        return 2600.0f + 365.0f * std::sqrt(t);
    }
    return 5651.0f - 2473.0f * std::exp(-0.0278f * t);
}

/// Isostatic surface elevation of a crustal column, metres relative to
/// PRESENT-DAY EARTH sea level (not relative to the simulated planet's own
/// solved sea level, which SphereField::seaLevelM carries separately).
///
/// Two independently observed anchors, one per branch, and no free datum:
///
///   continental:  e(Hc) = 840 + (Hc - 41) * continentalElevationPerKmM()
///                 41 km <-> +840 m, Earth's mean crustal thickness and mean
///                 land elevation. Checks out against a constraint it was not
///                 fitted to: 70 km -> +4970 m, the ~5 km gravitational-
///                 potential-energy ceiling for orogenic plateaux
///                 (Rey et al. 2010).
///   oceanic:      e(Ho, t) = -GDH1(t) + (Ho - 7) * oceanicElevationPerKmM()
///                 GDH1 IS the observed depth-age relation for 7 km crust, so
///                 it needs no anchoring; the thickness term carries oceanic
///                 plateaux, thinned crust and sediment load off that curve.
///
/// The two branches are blended by `contFrac`. Both keep a thickness term on
/// purpose: an oceanic branch that depended on age alone would make crustal
/// thinning and sediment loading no-ops on ocean floor, which is exactly where
/// rifted margins and shelf prisms have to act.
///
/// 2026-08-10: replaces `h * (1 - rho/rhoM) - mantleDatumM`, whose 3549 m datum
/// was derived from the oceanic branch alone and left the continental branch
/// uncalibrated. Under it, the crust thickness that sits at sea level was
/// 14.51 km while erosion's base level stopped at 17.81 km and the thinnest
/// continental crust measured anywhere was 15.8 km -- so no continental cell
/// could ever be submerged, land area was continental-crust area by
/// construction, and a continental shelf was impossible rather than merely
/// rare.
///
/// Note the deliberate omission: sea level is NOT pinned to zero here. The
/// fixed-water-volume solve (solveSeaLevelFixedVolume) is free to place the
/// stand wherever this planet's hypsometry puts it, and the distance from zero
/// is a diagnostic of how un-Earth-like the crust budget is. Anchoring both
/// branches to observed sea level AND imposing a fixed water volume would be
/// two constraints on one unknown.
[[nodiscard]] inline float isostaticElevationM(float crustThicknessKm, float contFrac,
                                               float crustAgeMy) noexcept {
    const float c            = std::clamp(contFrac, 0.0f, 1.0f);
    const float continentalM = PhysicsConstants::refContinentalElevationM +
                               (crustThicknessKm - PhysicsConstants::refContinentalThicknessKm) *
                                   continentalElevationPerKmM();
    const float oceanicM =
        -gdh1DepthM(crustAgeMy) +
        (crustThicknessKm - PhysicsConstants::initialOceanicThicknessKm) * oceanicElevationPerKmM();
    return c * continentalM + (1.0f - c) * oceanicM;
}

} // namespace aoc::map::gen

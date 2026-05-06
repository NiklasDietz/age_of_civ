#pragma once

/**
 * @file PlatePhysics.hpp
 * @brief Physical constants used by the SphereField plate-tectonic
 *        simulation. Values cite Turcotte & Schubert 2014 and the
 *        derivations in docs/PHYSICS_FIRST_REWRITE.md.
 *
 * Densities follow the standard 3-layer convention:
 *   rho_continental = 2700 kg/m^3 (granitic upper crust)
 *   rho_oceanic     = 2900 kg/m^3 (basaltic)
 *   rho_mantle      = 3300 kg/m^3 (peridotite)
 *
 * Airy isostasy gives surface elevation z = h * (1 - rho_c / rho_m)
 * above the mantle datum. The datum (3549 m) is rederived in P6.2 so
 * a 7 km oceanic crust column reads -2700 m below sea level (the
 * Earth-mean mid-ocean depth).
 *
 * 2026-05-06 cleanup: per-plate Lagrangian PhysicsGrid + helpers
 * (initialisePlatePhysicsGrid, accumulateConvergenceStrain,
 * thickenCrustFromStrain, applySurfaceErosion, recomputeIsostatic-
 * Elevation, peakSample, plateLocalToCell, bilinearSample,
 * PlateSphereIndex) deleted; orphaned post-P6.1 once the SphereField
 * raster pipeline became authoritative.
 */

namespace aoc::map::gen {

/// Physical constants used by the plate-physics module. Values are SI
/// or canonical geological literature units; comments cite the source.
struct PhysicsConstants {
    /// Granitic upper-crust density (Turcotte & Schubert 2014, table 2.1).
    static constexpr float rhoContinentalKgM3 = 2700.0f;
    /// Basaltic oceanic-crust density (Turcotte & Schubert 2014, table 2.1).
    static constexpr float rhoOceanicKgM3     = 2900.0f;
    /// Mantle (peridotite) density (Turcotte & Schubert 2014, table 2.1).
    static constexpr float rhoMantleKgM3      = 3300.0f;
    /// Mantle reference depth datum (m below sea level).
    /// 2026-05-06 P6.2 rederivation:
    ///   Airy isostasy: surface elevation above datum
    ///       = h_crust * (1 - rho_crust / rho_mantle)
    ///   Oceanic: 7000 * (1 - 2900/3300) = 849 m above datum.
    ///   Real Earth mid-ocean depth (Turcotte & Schubert 2014, ch. 2):
    ///       z_ocean = -2700 m below sea level.
    ///   Datum = 849 - (-2700) = 3549 m.
    static constexpr float mantleDatumM       = 3549.0f;
    /// Initial continental crust thickness (km). Earth-mean is ~35 km.
    static constexpr float initialContinentalThicknessKm = 35.0f;
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

} // namespace aoc::map::gen

#pragma once

/**
 * @file PlatePhysics.hpp
 * @brief Per-plate Lagrangian physics grid for the geophysics-first
 *        map-generation rewrite (started 2026-05-05).
 *
 * Each plate carries a PhysicsGrid covering its body in plate-local
 * tangent-plane coordinates around the plate centroid. Cell size is
 * 10 km (PHYSICS_CELL_SIZE_KM). The grid is Lagrangian: it rotates
 * rigidly with the plate via Euler-pole motion; cells do not move
 * within the grid. Cells at the divergent edge are activated as the
 * plate boundary expands; cells at the convergent edge are
 * deactivated as oceanic crust is consumed by subduction.
 *
 * State variables per cell are real-world quantities:
 *   - crustThicknessKm     km, ~7 oceanic, ~35 continental, up to 75 in collision zones
 *   - continentalFraction  0..1 (0 = pure basaltic oceanic, 1 = pure granitic continental)
 *   - crustAgeMy           Myr since formation at ridge or sim init
 *   - cumulativeStrain     dimensionless, accumulates from convergent-margin shortening
 *   - sedimentThicknessM   metres of sediment overlying basement
 *   - surfaceElevationM    derived from isostasy each step (NOT independent state)
 *
 * Densities (constants): rho_continental = 2700 kg/m^3 (granitic upper crust),
 * rho_oceanic = 2900 (basaltic), rho_mantle = 3300 (peridotite). Airy isostasy
 * gives surface elevation z = h * (1 - rho_c / rho_m) above the mantle datum.
 *
 * The grid is sized at construction to a max of MAX_GRID_DIM x MAX_GRID_DIM
 * cells (256 x 256 = 2560 km half-extent, enough for the largest realistic
 * plates). DEBT: the cap is uniform; very large plates (Pacific-class,
 * ~10000 km) would need either a larger cap or a non-uniform grid. The
 * 256-cap is fine for the current 80-plate-cap regime; if plate sizes grow
 * the cap should grow too.
 *
 * Memory budget: 256 * 256 * 6 floats * 4 bytes = 1.5 MB per plate. At 80
 * plates that is 120 MB, within budget per docs/PHYSICS_PIPELINE.md.
 */

#include "aoc/map/gen/SphereGeometry.hpp"

#include <cstdint>
#include <vector>

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
    /// Mantle reference depth datum (m below sea level). Origin-of-
    /// magnitude: with 7 km basaltic oceanic crust + rho_o/rho_m =
    /// 2900/3300, Airy gives z_rock = 7000 * (1 - 2900/3300) = 849 m,
    /// so cell elevation = 849 - 2900 = -2051 m. Real Earth mid-ocean
    /// is -2500 to -2800 m; current datum produces ~20 % shallower
    /// abyssal plain. Datum was historically labelled "tuned for
    /// -2500 m" but math doesn't hit -2500 (would need datum=2350).
    /// Left at 2900 because audit baseline locked here; Phase 13b
    /// only corrected the comment, did not retune.
    static constexpr float mantleDatumM       = 2900.0f;
    /// Initial continental crust thickness (km). Earth-mean is ~35 km.
    static constexpr float initialContinentalThicknessKm = 35.0f;
    /// Initial oceanic crust thickness (km). Earth-mean is ~7 km.
    static constexpr float initialOceanicThicknessKm = 7.0f;
    /// Maximum sustainable crust thickness (km). Beyond this the lower
    /// crust delaminates into the mantle. 2026-05-05 Phase 3.5: lowered
    /// 75 -> 65. Tibet's 70-75 km is the OBSERVED ceiling AFTER active
    /// convergence; the steady-state mean continental crust over time
    /// is 30-50 km because delamination + erosion peel the cap.
    /// Without a delamination event in the sim yet, the cap doubles as
    /// the steady-state ceiling.
    static constexpr float maxCrustThicknessKm = 65.0f;
    /// Earth radius in km, used by lat/lon -> km conversions.
    static constexpr float earthRadiusKm = 6371.0f;
};

/// Fixed cell size for per-plate physics grids. 10 km matches geological
/// process scales (typical flexural wavelength ~100-400 km gives 10-40
/// cells per wavelength) without exploding memory. Game-tile rasterisation
/// later averages a 3x3 footprint per 30 km hex.
inline constexpr float PHYSICS_CELL_SIZE_KM = 10.0f;

/// Maximum grid dimension. See file-level doc for rationale. Increase
/// when plate-size cap is raised.
inline constexpr int32_t MAX_GRID_DIM = 256;

/// Per-plate Lagrangian state grid. Indexing convention: (ix, iy) where
/// ix increases to the plate-local east, iy to the plate-local north.
/// Cells are SoA for SIMD/cache-friendly traversal; each array is sized
/// cellsX * cellsY.
struct PhysicsGrid {
    /// Grid extent in cells. Both <= MAX_GRID_DIM after resize().
    int32_t cellsX = 0;
    int32_t cellsY = 0;

    /// Plate-local tangent-plane half-extent in radians. The grid covers
    /// (-halfExtentRadX, +halfExtentRadX) east-west and similarly N-S.
    /// Cell (ix, iy) centre sits at:
    ///   lxRad = -halfExtentRadX + (ix + 0.5) * cellSizeRadX
    ///   lyRad = -halfExtentRadY + (iy + 0.5) * cellSizeRadY
    float halfExtentRadX = 0.0f;
    float halfExtentRadY = 0.0f;

    /// SoA fields. Each vector is sized cellsX * cellsY. cellActive == 0
    /// marks cells outside the plate boundary or consumed by subduction;
    /// physics passes skip them.
    std::vector<float> crustThicknessKm;
    std::vector<float> continentalFraction;
    std::vector<float> cumulativeStrain;
    std::vector<float> surfaceElevationM;
    std::vector<uint8_t> cellActive;

    /// Resize all SoA vectors to cellsX_ * cellsY_, fill with zeros and
    /// mark all cells active. Caller initialises field values afterwards.
    void resize(int32_t cellsX_, int32_t cellsY_,
                float halfExtentRadX_, float halfExtentRadY_);

    /// Linear cell index for (ix, iy). No bounds check (caller's job).
    [[nodiscard]] inline std::size_t cellIndex(int32_t ix, int32_t iy) const {
        return static_cast<std::size_t>(iy * cellsX + ix);
    }

    /// Convert a plate-local (lxRad, lyRad) tangent-plane coord to
    /// fractional cell coords (gx, gy). Out-of-grid points return
    /// (-1, -1). Used by sampling and scattering passes.
    void plateLocalToCell(float lxRad, float lyRad,
                          float& gx, float& gy) const;

    /// Bilinear sample of `field` (must be one of the SoA arrays) at
    /// plate-local (lxRad, lyRad). Returns 0 when out of grid or all
    /// four neighbours are inactive.
    [[nodiscard]] float bilinearSample(
        const std::vector<float>& field,
        float lxRad, float lyRad) const;

    /// Peak (max) sample of `field` over a (2*halfSearch+1)^2 window
    /// of cells centred on the cell nearest to plate-local (lxRad,
    /// lyRad). Used by the world-frame elevation pass to capture
    /// narrow mountain spikes that bilinearSample averages away
    /// (Phase 7 sub-step 7c). Returns 0 when out of grid or window
    /// contains no active cells.
    [[nodiscard]] float peakSample(
        const std::vector<float>& field,
        float lxRad, float lyRad,
        int32_t halfSearch) const;

    /// Total active-cell count for diagnostics.
    [[nodiscard]] std::size_t activeCellCount() const;
};

/// Initialise a plate's PhysicsGrid at sim start. Allocates the grid
/// sized to cover the plate's expected angular extent (derived from
/// plate weight; weight 1.0 ~ 1500 km radius). Each cell is set to:
///   continentalFraction: from a per-plate noise lookup mirroring the
///                        legacy crust-mask noise; > 0.5 -> continental
///   crustThicknessKm:    initialContinentalThicknessKm or
///                        initialOceanicThicknessKm
///   crustAgeMy:          continental random 500..2000 My, oceanic 0..100 My
///   cumulativeStrain:    0
///   sedimentThicknessM:  0
///   surfaceElevationM:   0 (filled by recomputeIsostaticElevation later)
/// The active flag is set everywhere inside the grid; the boundary-driven
/// activation (Phase 5) refines this.
void initialisePlatePhysicsGrid(struct Plate& plate, std::uint64_t seed);

/// Recompute surfaceElevationM from crustThicknessKm + continentalFraction
/// + sedimentThicknessM via Airy isostasy. Pure function of state, called
/// at the end of every physics step.
void recomputeIsostaticElevation(PhysicsGrid& grid);

/// Phase 2: accumulate convergence strain into each plate's PhysicsGrid.
/// For every cell, identify the nearest *other* plate by haversine distance
/// from the cell's sphere position, compute the projected relative
/// tangent-plane velocity of the two plates at that cell, and add the
/// convergent component (closing rate, rad/My) times dtMy to
/// cumulativeStrain. Divergent and tangential motion contribute zero.
/// Reads plate latDeg/lonDeg, eulerPoleLatDeg/eulerPoleLonDeg,
/// angularVelDeg, weight; writes only grid.cumulativeStrain.
void accumulateConvergenceStrain(std::vector<struct Plate>& plates,
                                 float dtMy);

/// Phase 2: thicken continental crust from accumulated strain. For each
/// active continental cell, dCrustKm = STRAIN_TO_THICKENING_KM_PER_RAD *
/// strain * dtMy_factor; capped at maxCrustThicknessKm. Strain is then
/// partially relaxed (multiplied by 1 - STRAIN_RELEASE_FRAC) modelling
/// post-orogenic isostatic + erosional unloading. Recomputes isostasy.
void thickenCrustFromStrain(PhysicsGrid& grid, float dtMy);

/// Phase 6: sphere-bucket spatial index over plate centroids. Replaces
/// the O(N) inner walk in accumulateConvergenceStrain with an O(K)
/// bucket query where K is the plate count in a fixed bucket-radius
/// neighbourhood. Buckets are 10° lat x 10° lon
/// (18 x 36 = 648 cells) on the sphere; each holds the indices of
/// plates whose centroid falls in that bucket. Bucket queries walk
/// (2*r+1)^2 buckets and append all member plate indices.
///
/// Cylindrical wrap on longitude (-180/+180 stitch); clamp on latitude.
struct PlateSphereIndex {
    static constexpr int32_t LAT_BUCKETS = 18;
    static constexpr int32_t LON_BUCKETS = 36;
    /// buckets[lat_idx * LON_BUCKETS + lon_idx] holds the plates[]
    /// indices whose centroid latDeg/lonDeg fell into that bucket
    /// at the most recent buildPlateSphereIndex call.
    std::vector<std::vector<int32_t>> buckets;
};

/// Build the spatial index over plates' lat/lon centroids. Call once
/// per epoch (cheap: ~N pushes). The index is invalidated whenever
/// plate positions change.
void buildPlateSphereIndex(const std::vector<struct Plate>& plates,
                           PlateSphereIndex& index);

/// Append every plate index in buckets within `bucketRadius` of the
/// (latDeg, lonDeg) query into `out`. bucketRadius=1 -> 9 buckets
/// (3x3); bucketRadius=3 -> 49 buckets (7x7, ~70° angular reach).
/// Caller deduplicates if needed (large radius may include same plate
/// from multiple buckets only if buckets ever overlap, which they do
/// not by construction -- so dedup is unnecessary).
void queryPlatesBucketRadius(const PlateSphereIndex& index,
                             float latDeg, float lonDeg,
                             int32_t bucketRadius,
                             std::vector<int32_t>& out);

/// Phase 3: surface erosion. Cells above EROSION_REF_M are eroded
/// proportional to (z - ref) per dtMy; eroded rock thins crust via the
/// Airy ratio (1 m surface drop ~ 5.5 m crust removed). Naturally caps
/// peaks at a steady state where uplift rate = erosion rate. The
/// removed mass is sedimentThicknessM transfer (deposited at the same
/// cell for now -- proper D8 routing arrives in a later sub-phase).
/// Recomputes isostasy.
void applySurfaceErosion(PhysicsGrid& grid, float dtMy);

} // namespace aoc::map::gen

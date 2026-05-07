#pragma once

/**
 * @file SphereField.hpp
 * @brief Authoritative global lat/lon raster for the physics-first plate
 *        tectonic simulation. Replaces the per-plate `PhysicsGrid` (10 km
 *        Lagrangian tangent-plane grid) and the legacy 96x96 `orogenyLocal`
 *        scatter buffer as the single source of truth for crustal state.
 *
 * Resolution: 0.5 deg per cell, 720 lon x 360 lat = 259200 cells.
 *
 * Indexing convention (row-major, latitude is the slow axis):
 *   index(lonIdx, latIdx) = latIdx * LON_CELLS + lonIdx
 *   cell center (lonIdx, latIdx) maps to
 *       lonDeg = -180 + (lonIdx + 0.5) * CELL_DEG
 *       latDeg =  -90 + (latIdx + 0.5) * CELL_DEG
 *
 * Boundary behaviour for sampling/neighbour walks:
 *   longitude wraps periodically (lonIdx -1 -> LON_CELLS - 1, etc.)
 *   latitude clamps at the poles (latIdx -1 -> 0, latIdx LAT_CELLS -> LAT_CELLS - 1)
 *
 * SoA layout (struct of arrays) keeps each field contiguous so per-cell
 * passes hit predictable cache lines and OpenMP parallel-for over the row
 * dimension does not false-share across fields.
 */

#include "aoc/map/gen/SphereGeometry.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace aoc::map::gen {

/// Mountain biome threshold in metres above sea level.
/// Alpine / nival biome floor (~4 km treeline at low latitudes,
/// Tibet / Andes high-alpine zone).
inline constexpr float MOUNTAIN_THRESHOLD_M = 4000.0f;

struct SphereField {
    static constexpr int32_t LON_CELLS = 720;
    static constexpr int32_t LAT_CELLS = 360;
    static constexpr float   CELL_DEG  = 0.5f;
    static constexpr std::size_t CELL_COUNT =
        static_cast<std::size_t>(LON_CELLS) * static_cast<std::size_t>(LAT_CELLS);

    // Surface elevation in metres above the mantle datum (i.e. above
    // the sea-floor reference; tile elevation == surfaceElevationM less the
    // sea-level constant resolved by isostatic equilibrium).
    std::vector<float>   surfaceElevationM;
    // Crustal column thickness in km. Together with continentalFraction this
    // determines the isostatic surface elevation via Airy compensation.
    std::vector<float>   crustThicknessKm;
    // Composition fraction in [0, 1]: 0 = pure oceanic, 1 = pure continental.
    std::vector<float>   continentalFraction;
    // Owning plate id, -1 = unowned (polar voids outside the Mollweide ellipse
    // are still valid sphere cells; the -1 sentinel is reserved for cells
    // marked inactive by subduction or never assigned).
    std::vector<int16_t> plateId;
    // Instantaneous closing rate at the cell, radians/My (sphere arc rate).
    // NOT a strain integral; written each epoch by the convergence pass and
    // consumed by the thickening pass within the same epoch.
    std::vector<float>   convergenceRateRadPerMy;
    // Crust age in My since last creation (ridge spawn / accretion).
    std::vector<float>   crustAgeMy;
    // Wilson-cycle thermal-blanketing age, in My. Counter advanced
    // each epoch for every continental cell that sits inside a plate
    // currently classified as a "supercontinent fragment" (continental
    // area above SUPERCONTINENT_FRACTION of the globe). Reset to 0 at
    // ridge accretion or when the cell flips to oceanic via
    // subduction. Continental rifting probability ramps in once the
    // mean of this counter for a plate exceeds the Stein & Stein 1992
    // breakup threshold (~150 My).
    std::vector<float>   thermalAgeMy;
    // Boundary-type classification per cell. Updated each epoch by
    // accumulateClosingRate alongside the magnitude. Values:
    //   0 NotBoundary      cell sits in plate interior
    //   1 Convergent       closing rate dominates over shear
    //   2 Divergent        closing rate dominates with negative sign
    //   3 Transform        |shear| > |closing| (strike-slip motion)
    // Downstream passes (subduction / accretion / thicken / Wilson)
    // gate on this field instead of inferring from the signed rate.
    std::vector<uint8_t> boundaryType;

    /// Allocate all SoA fields to CELL_COUNT and zero-initialise them.
    /// plateId is set to -1 (unowned). Idempotent.
    void resize();

    /// Fast row-major linear index. Caller asserts that lonIdx in
    /// [0, LON_CELLS) and latIdx in [0, LAT_CELLS).
    [[nodiscard]] static constexpr std::size_t cellIndex(
        int32_t lonIdx, int32_t latIdx) noexcept {
        return static_cast<std::size_t>(latIdx) * static_cast<std::size_t>(LON_CELLS)
             + static_cast<std::size_t>(lonIdx);
    }

    /// Cell-centre lat/lon for the given grid coordinate.
    [[nodiscard]] static LatLon cellCenter(int32_t lonIdx, int32_t latIdx) noexcept;

    /// Locate the cell containing (latDeg, lonDeg). Longitude wraps,
    /// latitude clamps. Returns (lonIdx, latIdx) of the containing cell.
    struct CellCoord { int32_t lonIdx; int32_t latIdx; };
    [[nodiscard]] static CellCoord locate(float latDeg, float lonDeg) noexcept;

    /// Bilinear-sample one of the SoA fields at an arbitrary (lat, lon).
    /// Longitude wraps (so antimeridian samples blend cells 719 and 0);
    /// latitude clamps at +/-90.
    [[nodiscard]] float bilinearSample(
        const std::vector<float>& field, float latDeg, float lonDeg) const noexcept;

    /// Peak (max) sample of `field` over a (2*halfSearchCells+1)^2 cell
    /// window centred on the cell containing (latDeg, lonDeg).
    /// Required for mountain detection by hex-tile assignment: hex tiles
    /// (~3° at standard size) span many SphereField cells, and bilinear
    /// averaging dilutes narrow mountain belts (e.g. Andes 200 km wide
    /// = 4 cells) below the visibility threshold. Peak sampling captures
    /// the local maximum so any 4 km+ peak inside the tile flags it.
    /// Longitude wraps; latitude clamps.
    [[nodiscard]] float peakSample(
        const std::vector<float>& field, float latDeg, float lonDeg,
        int32_t halfSearchCells) const noexcept;
};

} // namespace aoc::map::gen

#pragma once

/**
 * @file SphereFieldPhysics.hpp
 * @brief Physics-first plate-tectonic passes that operate on the global
 *        lat/lon raster (`SphereField`). Replaces the per-plate
 *        `PhysicsGrid` Lagrangian path inherited from the Voronoi era.
 *
 * Pass order per epoch:
 *   1. assignPlateOwnership      cell -> nearest plate by haversine
 *   2. markBoundaryCells          cell flagged if any 4-neighbour differs
 *   3. accumulateClosingRate     instantaneous (NOT integrated) closing
 *                                rate per boundary cell from Euler-pole
 *                                tangent velocities of A vs B
 *   4. thickenFromClosingRate    dCrust = K * rate * dt (continental only),
 *                                capped at maxCrustThicknessKm
 *   5. applySubduction           lower-density side at convergent margin
 *                                loses ownership to overrider
 *   6. recomputeIsostaticElevationOnRaster
 *                                Airy compensation, sea-level datum
 *   7. applySurfaceErosionOnRaster
 *                                relief-proportional removal (P6 will
 *                                replace the placeholder rate constant
 *                                with Whipple & Tucker 1999)
 *
 * `convergenceRateRadPerMy` is INSTANTANEOUS by design: it is written by
 * pass 3 and consumed by passes 4 and 5 within the same epoch. There is
 * no cumulative strain integral.
 *
 * Constant derivations live in source comments next to the constants.
 */

#include "aoc/map/gen/Plate.hpp"
#include "aoc/map/gen/SphereField.hpp"

#include <cstdint>
#include <vector>

namespace aoc::map::gen {

/// Phase 1.1: per-cell nearest-plate ownership via haversine distance to
/// each plate's (latDeg, lonDeg) centroid. Writes `field.plateId`. The
/// inner loop is O(N_cells * N_plates); at 259200 cells * 80 plates this
/// is ~20 M haversine ops -- single-pass, parallelisable across rows.
void assignPlateOwnership(SphereField& field,
                          const std::vector<Plate>& plates);

/// Phase 1.2: flag every cell whose 4-connected neighbourhood (N/S/E/W
/// with longitude wrap, latitude clamp) contains a different plate id.
/// `isBoundary` is sized to `SphereField::CELL_COUNT` and overwritten.
void markBoundaryCells(const SphereField& field,
                       std::vector<uint8_t>& isBoundary);

/// Phase 1.3: write the INSTANTANEOUS closing rate (positive when plates
/// converge) into `field.convergenceRateRadPerMy` for every boundary
/// cell. The rate is the dot product of (vA - vB) -- the difference of
/// Euler-pole tangent velocities at the cell -- with the unit vector
/// pointing from cell A toward the neighbour cell B that flipped plate
/// ownership. Non-boundary cells are zeroed.
void accumulateClosingRate(SphereField& field,
                           const std::vector<Plate>& plates,
                           const std::vector<uint8_t>& isBoundary);

/// Phase 1.4: thicken continental crust at convergent boundary cells.
/// dCrustKm = K_THICKEN_KM_PER_RAD * rate * dtMy, applied only when the
/// cell is continental (`continentalFraction > 0.5`). Capped at
/// `PhysicsConstants::maxCrustThicknessKm`. Oceanic cells are left
/// untouched here -- they are handled by `applySubduction`.
///
/// `K_THICKEN_KM_PER_RAD` is the empirical vertical-thickening efficiency
/// for active continent-continent collisions, derived from the observed
/// Tibet-Himalaya record:
///   - Plate convergence (India-Asia): ~5 cm/yr -> ~7.85e-3 rad/My
///     after dividing by Earth radius (50 km arc-closing per My).
///   - Tibet thickness gain: ~30 km in 50 My -> 0.6 km/My average.
///   - Vertical efficiency = (0.6 km/My) / (50 km/My) = 0.012, i.e.
///     ~1.2 % of horizontal closing converts to vertical thickening
///     (the rest accommodates as horizontal extension + erosion).
///   - K = 0.012 * R_earth_km = 0.012 * 6371 = 76.5 km / (rad/My) / My
/// Reference: DeCelles, Robinson, Zandt 2002 (Tibet plateau growth);
/// Turcotte & Schubert 2014 ch. 6 (mass-balance derivation).
void thickenFromClosingRate(SphereField& field, float dtMy);

/// Phase 1.5: subduction. At convergent boundary cells, the side with
/// the lower continental fraction (denser oceanic crust) is consumed
/// by the overriding plate. The consumed cell's plate id flips to the
/// overrider, crust is reset to the overrider's local oceanic-arc
/// thickness, and `continentalFraction` is reset (arc volcanism builds
/// new continental fragments slowly via thickening in subsequent epochs).
///
/// Subduction is gated by closing rate * dtMy exceeding one cell width
/// at the cell's latitude, ensuring the operation matches the physics
/// timescale rather than firing every epoch.
///
/// Single-threaded for determinism: ownership transfers between cells
/// must observe a consistent order.
void applySubduction(SphereField& field,
                     const std::vector<Plate>& plates,
                     float dtMy);

/// Phase 1.6: Airy isostasy on the raster. Surface elevation is derived
/// from `crustThicknessKm` and `continentalFraction` against the mantle
/// datum. This pass is pure -- state (h, c) is not modified.
void recomputeIsostaticElevationOnRaster(SphereField& field);

/// Phase 1.7: stream-power surface erosion. dh = -K_EROSION * z * dtMy
/// applied where z > 0 (above sea level). Each metre of surface lowering
/// removes (rhoMantle / rhoCrust) metres of crustal column via the Airy
/// compensation. Recomputes elevation at end.
///
/// Phase 6 will swap K_EROSION for the Whipple & Tucker 1999 stream-power
/// coefficient with a documented derivation. Current value is a coarse
/// placeholder calibrated to keep peaks below 8 km at steady state.
void applySurfaceErosionOnRaster(SphereField& field, float dtMy);

/// Single-step Phase 1 epoch driver. Sequences the seven passes in the
/// order documented in the file header. Used by MapGenerator under the
/// AOC_PHYSICS_ON_SPHEREFIELD compile flag.
void stepSpherePhysicsEpoch(SphereField& field,
                            std::vector<Plate>& plates,
                            std::vector<uint8_t>& boundaryScratch,
                            float dtMy);

} // namespace aoc::map::gen

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

/// Initial Lagrangian ownership assignment. Runs ONCE at sim init: each
/// cell binds to the plate whose centroid is haversine-closest. After
/// the initial cut, plate boundaries evolve through physics only —
/// subduction, ridge accretion, continental docking — never via
/// re-Voronoi. This is the polygon-physics path the project switched to
/// after deleting the per-epoch centroid Voronoi: cells carry plate
/// identity, plates drift via centroid-of-cells, and shapes are
/// non-convex from the start because they emerge from boundary dynamics
/// rather than from nearest-centroid mathematics.
void assignPlateOwnershipInitial(SphereField& field,
                                 const std::vector<Plate>& plates);

/// Recompute every plate's centroid (`Plate.latDeg`, `Plate.lonDeg`) as
/// the area-weighted mean of the cells currently assigned to it. Run
/// once per epoch, after subduction and ridge accretion have rewritten
/// `field.plateId`. With Lagrangian cell tracking the plate "drifts"
/// because its cell set changes — boundary cells flip to neighbours
/// (subduction) or new oceanic cells appear at ridges, and the
/// centroid moves to follow.
void recomputePlateCentroidsFromCells(SphereField& field,
                                      std::vector<Plate>& plates);

/// Compact the plate list: any plate with zero cells (last cell
/// consumed by subduction or merger) is removed from `plates`, and the
/// surviving plates' indices are remapped in `field.plateId`. Returns
/// the number of plates removed.
int32_t compactPlateList(SphereField& field, std::vector<Plate>& plates);

/// Continental docking. When two continental plates remain in mutual
/// convergence over many epochs without subduction (continental crust
/// is too buoyant to subduct), the smaller plate accretes into the
/// larger one — terrane accretion in real Earth (e.g. India docking
/// into Asia). This pass scans plate-pair convergent boundaries and
/// merges plate pairs where:
///   - both sides have continentalFraction > 0.5 along the contact,
///   - the contact has been stable (closing) for >= dockingMyThreshold,
///   - the smaller plate's continental area is below the larger's.
/// The smaller plate's cells inherit the larger plate's plateId and
/// the smaller plate is dropped by the next compactPlateList pass.
/// `boundaryEpochsContact` is per-plate-pair age tracking; the caller
/// owns it and passes it back each epoch.
void applyContinentalDocking(SphereField& field,
                             std::vector<Plate>& plates,
                             std::vector<float>& contactAgeByPlatePair,
                             float dtMy);

/// Wilson-cycle continental rifting. Mantle thermal blanketing under a
/// supercontinent (Anderson 1982; Stein & Stein 1992) accumulates
/// stress over ~150-200 My, eventually exceeding the lithospheric
/// breakup threshold and splitting the plate. This pass:
///   1. Advances `thermalAgeMy` for every cell whose owning plate is
///      classified as supercontinent (continental area >= threshold
///      fraction of the globe).
///   2. For any plate whose mean thermalAgeMy exceeds a stochastic
///      breakup threshold, splits the plate along the longest interior
///      principal axis (PCA on cell positions), assigns half the cells
///      to a fresh plate id, and gives the new plate a perturbed
///      Euler pole so the two halves diverge.
/// Returns the number of new plates created.
int32_t applyWilsonRifting(SphereField& field,
                           std::vector<Plate>& plates,
                           uint32_t& rngState,
                           float dtMy);

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

/// Single-step epoch driver. Sequences ownership / boundary /
/// closing-rate / thicken / subduct / docking / Wilson rifting /
/// isostasy / erosion. The two state vectors carried across epochs
/// are `boundaryScratch` (re-allocated per call but reused for size)
/// and `contactAgeByPlatePair` (per-plate-pair docking timer). The
/// `rngState` is a single uint32_t XorShift seed advanced by Wilson
/// rifting each epoch — kept outside the call so it remains
/// deterministic across runs with the same map seed.
void stepSpherePhysicsEpoch(SphereField& field,
                            std::vector<Plate>& plates,
                            std::vector<uint8_t>& boundaryScratch,
                            std::vector<float>& contactAgeByPlatePair,
                            uint32_t& rngState,
                            float dtMy);

} // namespace aoc::map::gen

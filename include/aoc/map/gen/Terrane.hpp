#pragma once

/**
 * @file Terrane.hpp
 * @brief Continental crust as rigid bodies that rotate with their plate,
 *        instead of as raster values resampled every substep.
 *
 * Why this exists, measured rather than asserted. `advectPlateOwnership` is the
 * single cause of both of this generator's headline shape defects. Freezing it
 * (AOC_NO_ADVECT) moves, on seeds 42/7/100, the crust footprint's
 * perimeter/equal-area-disc from 3.34/4.56/3.29 to 1.48/1.23/1.21 and the
 * largest connected crust component from 0.92/0.97/0.81 to 0.42/0.48/0.53 --
 * five gates into band by disabling one function. Its own incumbent-wins rule
 * means it cannot move a plate boundary, so its whole effect is to resample the
 * field whose 0.5 contour is the coastline, ~900 times per world, with an
 * orphan-fill path that copies from an arbitrary nearby source whenever a
 * destination has no valid departure point.
 *
 * Two cheaper repairs were measured and rejected first: raising the CFL bound
 * to a single substep per epoch (no better, mostly worse -- the mechanism is
 * the problem, not the step count) and disabling only the orphan-claim pass
 * (helps one seed of three).
 *
 * The representation here is Lagrangian. A terrane owns a set of cells in its
 * own BODY frame, fixed at birth, plus the accumulated rotation from that frame
 * to the present. Transport is then a coordinate change, not a resampling: for
 * each destination cell we rotate backwards into the body frame and ask whether
 * that point belongs to the terrane. Membership is a yes/no test, so there are
 * no orphans, no wake, no echo pairing and no smear -- the shape is exactly
 * preserved under arbitrary rotation, however many epochs run.
 *
 * A raster stencil is used rather than a spherical polygon deliberately: it
 * gives the same rigidity with no polygon-boolean geometry, which the plan
 * identified as the one part of the terrane design with real implementation
 * risk.
 */

#include "aoc/map/gen/SphereGeometry.hpp"

#include <cstdint>
#include <vector>

namespace aoc::map::gen {

/// One rigid continental block. `bodyCells` is implicit: the body-frame raster
/// records which terrane owns each cell, so a terrane needs only its identity,
/// its plate binding and its accumulated rotation.
struct Terrane {
    int16_t id      = -1;
    int16_t plateId = -1;
    /// Rotation taking a BODY-frame direction to its present-day direction,
    /// as a 3x3 matrix in row-major order. Composed each epoch with the
    /// plate's rotation for that epoch. A matrix rather than an axis-angle
    /// pair because composition is then a single multiply with no
    /// renormalisation policy to get wrong.
    float rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    /// Cells this terrane occupies in the body frame. Kept so growth and
    /// welding can be applied without rescanning the whole raster.
    std::vector<int32_t> bodyCells;
    float birthMy    = 0.0f;
    int32_t weldCount = 0;
    bool alive       = true;
    /// Prescribed dispersal: the Euler pole and rate that carry this block
    /// away from the world's continental centroid. See assignTerraneDrift.
    float driftPoleLatDeg  = 0.0f;
    float driftPoleLonDeg  = 0.0f;
    float driftRateDegPerMy = 0.0f;
};

/// The body-frame raster: the world as it stood when terranes were seeded.
/// Each cell records which terrane owns it there and that cell's crustal state,
/// so a terrane's geometry and its crust ride together under rotation.
struct TerraneBody {
    std::vector<int16_t> terraneId;
    std::vector<float> crustKm;
    std::vector<float> ageMy;
};

/// Apply `v' = R * v`.
[[nodiscard]] Vec3 applyRot(const float rot[9], Vec3 v) noexcept;
/// Apply the transpose (= inverse, for a rotation) `v' = R^T * v`.
[[nodiscard]] Vec3 applyRotT(const float rot[9], Vec3 v) noexcept;
/// `out = a * b`, both row-major 3x3. Aliasing-safe.
void composeRot(const float a[9], const float b[9], float out[9]) noexcept;
/// Rotation matrix for `angleDeg` about the Euler pole at (poleLat, poleLon).
void eulerRotMatrix(float poleLatDeg, float poleLonDeg, float angleDeg, float out[9]) noexcept;

} // namespace aoc::map::gen

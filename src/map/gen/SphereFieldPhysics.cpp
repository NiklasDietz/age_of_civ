#include "aoc/map/gen/SphereFieldPhysics.hpp"

#include "aoc/map/gen/PlatePhysics.hpp"
#include "aoc/map/gen/PlateReference.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::map::gen {

// Vertical-thickening efficiency at convergent boundaries, in
// kilometres of crust thickening per (radians/My) closing rate per My
// of elapsed time. Derived in the header doc from the Tibet record;
// reference Turcotte & Schubert 2014 ch. 6 + DeCelles et al. 2002.
inline constexpr float K_THICKEN_KM_PER_RADMY = 76.5f;

// Bulk erosion coefficient per My per metre of elevation above sea
// level. Derived from the stream-power incision model (Whipple &
// Tucker 1999, J. Geophys. Res.) calibrated to the global mean
// denudation rate of ~50 m/My at mean continental elevation ~840 m
// (Wilkinson & McElroy 2007, J. Geophys. Res. 112, F02017):
//   K = E_mean / z_mean = 50 m/My / 840 m ≈ 0.060 /My.
// Each 0.5° cell (~55 km) averages many drainage basins; using the
// global mean K avoids per-basin drainage-area assumptions. Active-
// orogen end-member (Himalaya, orographic enhancement) is ~0.4/My —
// captured indirectly by higher convergence-driven uplift rates in
// those cells. 2026-05-06 P6.4.
inline constexpr float K_EROSION_PER_MY = 0.06f;

// 2026-05-06 P6.6 plate-extent gate. Real plates have finite size:
// the Pacific (largest) spans ~1.5 rad, microplates ~0.2 rad. A
// centroid-Voronoi assignment grants every cell to its nearest plate
// regardless of actual extent, so a microplate sandwiched between
// giants ends up "owning" a thin sliver far from its centroid where
// the giants physically dominate.
//
// Calibration: 50-plate sphere has mean plate area 4π/50 = 0.25 sr,
// mean angular radius sqrt(0.25/π) = 0.282 rad. With weight = 1.0
// representing a mean-area plate, plate angular reach scales as
// sqrt(weight) (area ~ weight, so radius ~ sqrt(weight)). The
// constant 0.6 rad is the per-sqrt(weight) reach with a generous
// 2x margin so most cells inside the Voronoi cell still pass the
// gate; only far-side antipodal artefacts and large/microplate
// sandwich slivers are filtered.
inline constexpr float PLATE_REACH_RAD_PER_SQRT_WEIGHT = 0.6f;

// Effective subduction-zone width in kilometres. A cell is consumed by
// subduction when (closing rate * dtMy * R_earth) exceeds this width.
// Real-world trench-to-arc distance ~50-100 km; choose 50 km so the
// pass fires at credibly Earth-like timescales for closing rates of
// ~5 cm/yr.
inline constexpr float SUBDUCTION_CELL_WIDTH_KM = 50.0f;

void assignPlateOwnership(SphereField& field,
                          const std::vector<Plate>& plates) {
    if (plates.empty()) {
        std::fill(field.plateId.begin(), field.plateId.end(),
                  static_cast<int16_t>(-1));
        return;
    }
#if defined(AOC_HAS_OPENMP)
    #pragma omp parallel for schedule(static)
#endif
    for (int32_t latIdx = 0; latIdx < SphereField::LAT_CELLS; ++latIdx) {
        for (int32_t lonIdx = 0; lonIdx < SphereField::LON_CELLS; ++lonIdx) {
            const LatLon p = SphereField::cellCenter(lonIdx, latIdx);
            int16_t bestId   = -1;
            float   bestDist = 1e9f;
            for (std::size_t i = 0; i < plates.size(); ++i) {
                const LatLon centroid{plates[i].latDeg, plates[i].lonDeg};
                const float d = haversineRadians(p, centroid);
                if (d < bestDist) {
                    bestDist = d;
                    bestId   = static_cast<int16_t>(i);
                }
            }
            field.plateId[SphereField::cellIndex(lonIdx, latIdx)] = bestId;
        }
    }
}

void markBoundaryCells(const SphereField& field,
                       std::vector<uint8_t>& isBoundary) {
    isBoundary.assign(SphereField::CELL_COUNT, 0u);
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
#if defined(AOC_HAS_OPENMP)
    #pragma omp parallel for schedule(static)
#endif
    for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
        for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
            const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
            const int16_t self = field.plateId[idx];
            const int32_t lonW = (lonIdx == 0)       ? LON - 1 : lonIdx - 1;
            const int32_t lonE = (lonIdx == LON - 1) ? 0       : lonIdx + 1;
            const int32_t latS = (latIdx == 0)       ? 0       : latIdx - 1;
            const int32_t latN = (latIdx == LAT - 1) ? LAT - 1 : latIdx + 1;
            const int16_t nW = field.plateId[SphereField::cellIndex(lonW, latIdx)];
            const int16_t nE = field.plateId[SphereField::cellIndex(lonE, latIdx)];
            const int16_t nS = field.plateId[SphereField::cellIndex(lonIdx, latS)];
            const int16_t nN = field.plateId[SphereField::cellIndex(lonIdx, latN)];
            if (nW != self || nE != self || nS != self || nN != self) {
                isBoundary[idx] = 1u;
            }
        }
    }
}

void accumulateClosingRate(SphereField& field,
                           const std::vector<Plate>& plates,
                           const std::vector<uint8_t>& isBoundary) {
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    std::fill(field.convergenceRateRadPerMy.begin(),
              field.convergenceRateRadPerMy.end(), 0.0f);
    if (plates.empty()) return;
#if defined(AOC_HAS_OPENMP)
    #pragma omp parallel for schedule(static)
#endif
    for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
        for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
            const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
            if (!isBoundary[idx]) continue;
            const int16_t selfId = field.plateId[idx];
            if (selfId < 0) continue;

            // Find the dominant differing-id neighbour direction. Use
            // the four cardinal neighbours; the first one whose plate
            // id differs becomes the "B" plate for closing-rate
            // computation. This is intentionally simple -- a discrete
            // raster is not the right place for sub-cell normal
            // estimation.
            const int32_t lonW = (lonIdx == 0)       ? LON - 1 : lonIdx - 1;
            const int32_t lonE = (lonIdx == LON - 1) ? 0       : lonIdx + 1;
            const int32_t latS = (latIdx == 0)       ? 0       : latIdx - 1;
            const int32_t latN = (latIdx == LAT - 1) ? LAT - 1 : latIdx + 1;

            int16_t otherId = -1;
            int32_t nLon = lonIdx;
            int32_t nLat = latIdx;
            const int16_t nW = field.plateId[SphereField::cellIndex(lonW, latIdx)];
            const int16_t nE = field.plateId[SphereField::cellIndex(lonE, latIdx)];
            const int16_t nS = field.plateId[SphereField::cellIndex(lonIdx, latS)];
            const int16_t nN = field.plateId[SphereField::cellIndex(lonIdx, latN)];
            if      (nW != selfId && nW >= 0) { otherId = nW; nLon = lonW; }
            else if (nE != selfId && nE >= 0) { otherId = nE; nLon = lonE; }
            else if (nS != selfId && nS >= 0) { otherId = nS; nLat = latS; }
            else if (nN != selfId && nN >= 0) { otherId = nN; nLat = latN; }
            if (otherId < 0) continue;

            const Plate& A = plates[static_cast<std::size_t>(selfId)];
            const Plate& B = plates[static_cast<std::size_t>(otherId)];
            const LatLon p = SphereField::cellCenter(lonIdx, latIdx);

            // P6.6 plate-extent gate: skip closing-rate accumulation if
            // either plate's centroid is beyond its sqrt(weight)-scaled
            // angular reach. Filters microplate-sliver artefacts where
            // centroid-Voronoi grants ownership across an unrealistic
            // distance.
            const float reachA = std::sqrt(std::max(0.1f, A.weight))
                                * PLATE_REACH_RAD_PER_SQRT_WEIGHT;
            const float reachB = std::sqrt(std::max(0.1f, B.weight))
                                * PLATE_REACH_RAD_PER_SQRT_WEIGHT;
            const float dA = haversineRadians(p, {A.latDeg, A.lonDeg});
            const float dB = haversineRadians(p, {B.latDeg, B.lonDeg});
            if (dA > reachA || dB > reachB) continue;

            const TangentVelocity vA = eulerVelocityAt(
                p, {A.eulerPoleLatDeg, A.eulerPoleLonDeg}, A.angularVelDeg);
            const TangentVelocity vB = eulerVelocityAt(
                p, {B.eulerPoleLatDeg, B.eulerPoleLonDeg}, B.angularVelDeg);
            const float dvE = vA.east  - vB.east;
            const float dvN = vA.north - vB.north;

            // Boundary-normal direction in the local east/north basis,
            // pointing FROM cell (lonIdx, latIdx) TOWARD the neighbour
            // cell (nLon, nLat). For purely E/W or N/S neighbours the
            // normal is one of the four cardinal unit vectors.
            const float nE_dir = static_cast<float>(nLon - lonIdx);
            const float nN_dir = static_cast<float>(nLat - latIdx);
            // Renormalise (handles longitude wrap where lonW=LON-1
            // produced a -719 difference instead of +1 west).
            float nx = (nE_dir > 0) ? 1.0f : (nE_dir < 0) ? -1.0f : 0.0f;
            float ny = (nN_dir > 0) ? 1.0f : (nN_dir < 0) ? -1.0f : 0.0f;
            // Special-case the wrap: if abs(nE_dir) > 1 then we wrapped.
            if (std::fabs(nE_dir) > 1.0f) {
                nx = (nE_dir > 0) ? -1.0f : 1.0f;
            }

            // Closing rate is the component of (vA - vB) along (nx, ny):
            // positive => A moves toward B (convergent), negative =>
            // divergent. Tangential motion contributes nothing.
            const float closing = dvE * nx + dvN * ny;
            if (closing > 0.0f) {
                field.convergenceRateRadPerMy[idx] = closing;
            }
        }
    }
}

void thickenFromClosingRate(SphereField& field, float dtMy) {
    const float maxCrust = PhysicsConstants::maxCrustThicknessKm;
#if defined(AOC_HAS_OPENMP)
    #pragma omp parallel for schedule(static)
#endif
    for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
        const float rate = field.convergenceRateRadPerMy[i];
        if (rate <= 0.0f) continue;
        if (field.continentalFraction[i] <= 0.5f) continue;
        const float dCrustKm = K_THICKEN_KM_PER_RADMY * rate * dtMy;
        float h = field.crustThicknessKm[i] + dCrustKm;
        if (h > maxCrust) h = maxCrust;
        field.crustThicknessKm[i] = h;
    }
}

void applySubduction(SphereField& field,
                     const std::vector<Plate>& plates,
                     float dtMy) {
    if (plates.empty()) return;
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    const float R = PhysicsConstants::earthRadiusKm;

    // Single-threaded: ownership transfers must observe a consistent
    // order so a cell consumed in the same epoch cannot also itself
    // consume a neighbour.
    for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
        for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
            const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
            const float rate = field.convergenceRateRadPerMy[idx];
            if (rate <= 0.0f) continue;
            const int16_t selfId = field.plateId[idx];
            if (selfId < 0) continue;

            // Identify a differing neighbour.
            const int32_t lonW = (lonIdx == 0)       ? LON - 1 : lonIdx - 1;
            const int32_t lonE = (lonIdx == LON - 1) ? 0       : lonIdx + 1;
            const int32_t latS = (latIdx == 0)       ? 0       : latIdx - 1;
            const int32_t latN = (latIdx == LAT - 1) ? LAT - 1 : latIdx + 1;
            int16_t otherId = -1;
            std::size_t otherIdx = idx;
            const std::size_t idxW = SphereField::cellIndex(lonW, latIdx);
            const std::size_t idxE = SphereField::cellIndex(lonE, latIdx);
            const std::size_t idxS = SphereField::cellIndex(lonIdx, latS);
            const std::size_t idxN = SphereField::cellIndex(lonIdx, latN);
            const int16_t nW = field.plateId[idxW];
            const int16_t nE = field.plateId[idxE];
            const int16_t nS = field.plateId[idxS];
            const int16_t nN = field.plateId[idxN];
            if      (nW != selfId && nW >= 0) { otherId = nW; otherIdx = idxW; }
            else if (nE != selfId && nE >= 0) { otherId = nE; otherIdx = idxE; }
            else if (nS != selfId && nS >= 0) { otherId = nS; otherIdx = idxS; }
            else if (nN != selfId && nN >= 0) { otherId = nN; otherIdx = idxN; }
            if (otherId < 0) continue;

            // Subduct the lower continental-fraction side.
            const float selfFrac  = field.continentalFraction[idx];
            const float otherFrac = field.continentalFraction[otherIdx];
            std::size_t consumedIdx;
            int16_t     overriderId;
            if (selfFrac < otherFrac) { consumedIdx = idx;      overriderId = otherId; }
            else                      { consumedIdx = otherIdx; overriderId = selfId;  }
            // Pure-continental collisions do not subduct -- both sides
            // thicken instead (handled by thickenFromClosingRate).
            const float lowFrac = std::min(selfFrac, otherFrac);
            if (lowFrac >= 0.5f) continue;

            // Gate by closing distance: only consume when the closing
            // rate * dt would have advanced past one trench width.
            const float closingKm = rate * dtMy * R;
            if (closingKm < SUBDUCTION_CELL_WIDTH_KM) continue;

            field.plateId[consumedIdx] = overriderId;
            field.crustThicknessKm[consumedIdx] =
                PhysicsConstants::initialOceanicThicknessKm;
            field.continentalFraction[consumedIdx] = 0.0f;
            field.crustAgeMy[consumedIdx] = 0.0f;
        }
    }
}

void recomputeIsostaticElevationOnRaster(SphereField& field) {
    const float rhoM = PhysicsConstants::rhoMantleKgM3;
    const float rhoC = PhysicsConstants::rhoContinentalKgM3;
    const float rhoO = PhysicsConstants::rhoOceanicKgM3;
    const float datumM = PhysicsConstants::mantleDatumM;
#if defined(AOC_HAS_OPENMP)
    #pragma omp parallel for schedule(static)
#endif
    for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
        const float h = field.crustThicknessKm[i];           // km
        const float c = field.continentalFraction[i];        // 0..1
        // Composition-weighted bulk crust density.
        const float rho = c * rhoC + (1.0f - c) * rhoO;
        // Airy: column rises h * (1 - rho/rhoMantle) above the datum.
        const float zAboveDatumM = h * 1000.0f * (1.0f - rho / rhoM);
        field.surfaceElevationM[i] = zAboveDatumM - datumM;
    }
}

void applySurfaceErosionOnRaster(SphereField& field, float dtMy) {
    const float rhoM = PhysicsConstants::rhoMantleKgM3;
    const float rhoC = PhysicsConstants::rhoContinentalKgM3;
    const float rhoO = PhysicsConstants::rhoOceanicKgM3;
    const float airyRatio_cont = rhoM / (rhoM - rhoC); // ~5.5 m crust per m relief
    const float airyRatio_oce  = rhoM / (rhoM - rhoO); // ~8.25 m crust per m relief
#if defined(AOC_HAS_OPENMP)
    #pragma omp parallel for schedule(static)
#endif
    for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
        const float z = field.surfaceElevationM[i];
        if (z <= 0.0f) continue;
        const float dz = K_EROSION_PER_MY * z * dtMy; // metres surface lowering
        const float c  = field.continentalFraction[i];
        const float airy = c * airyRatio_cont + (1.0f - c) * airyRatio_oce;
        const float dCrustKm = (dz * airy) * 1e-3f;
        float h = field.crustThicknessKm[i] - dCrustKm;
        if (h < 0.0f) h = 0.0f;
        field.crustThicknessKm[i] = h;
    }
}

void stepSpherePhysicsEpoch(SphereField& field,
                            std::vector<Plate>& plates,
                            std::vector<uint8_t>& boundaryScratch,
                            float dtMy) {
    assignPlateOwnership(field, plates);
    markBoundaryCells(field, boundaryScratch);
    accumulateClosingRate(field, plates, boundaryScratch);
    thickenFromClosingRate(field, dtMy);
    applySubduction(field, plates, dtMy);
    recomputeIsostaticElevationOnRaster(field);
    applySurfaceErosionOnRaster(field, dtMy);

    // Crust age advances monotonically for cells that did not flip
    // ownership in the subduction pass (those were reset to 0 already).
    for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
        field.crustAgeMy[i] += dtMy;
    }
}

} // namespace aoc::map::gen

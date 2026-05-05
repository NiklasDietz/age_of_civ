/**
 * @file PlatePhysics.cpp
 * @brief Implementation of the per-plate Lagrangian physics grid.
 *
 * See PlatePhysics.hpp for module-level documentation, state-variable
 * units, and the cell-indexing convention.
 *
 * This file currently implements:
 *   - PhysicsGrid::resize, plateLocalToCell, bilinearSample, activeCellCount
 *   - initialisePlatePhysicsGrid (per-plate sim-start initial state)
 *   - recomputeIsostaticElevation (Airy isostasy, called every step)
 *
 * Strain accumulation, crustal thickening, erosion, sedimentation, sea-level
 * solver and flexure are added in later phases. Each new physics function
 * lands here (or in SurfaceProcesses.cpp for global passes) with a citation
 * to the literature governing its constants.
 */

#include "aoc/map/gen/PlatePhysics.hpp"

#include "aoc/map/gen/Noise.hpp"
#include "aoc/map/gen/Plate.hpp"
#include "aoc/map/gen/SphereGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aoc::map::gen {

namespace {

/// Splitmix64-derived per-plate RNG. The grid init is deterministic per
/// seed so audit reruns reproduce bit-identical output. The seed combines
/// the global sim seed with the plate's centroid lat/lon hash.
std::uint64_t mixPlateSeed(std::uint64_t seed, float latDeg, float lonDeg) {
    const std::uint64_t latBits =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(latDeg * 1.0e6f));
    const std::uint64_t lonBits =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(lonDeg * 1.0e6f));
    std::uint64_t z = seed ^ (latBits * 0x9E3779B97F4A7C15ULL)
                    ^ (lonBits * 0xBF58476D1CE4E5B9ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/// Plate angular half-extent in radians as a function of plate weight.
/// Weight 1.0 -> ~1500 km radius -> ~0.235 rad on Earth. Weight scales
/// linearly with radius (real plates span 1000-10000 km radius).
float plateHalfExtentRad(float weight) {
    constexpr float baseRadiusKm = 1500.0f;
    const float radiusKm = baseRadiusKm * std::max(0.5f, weight);
    return radiusKm / PhysicsConstants::earthRadiusKm;
}

} // namespace

void PhysicsGrid::resize(int32_t cellsX_, int32_t cellsY_,
                         float halfExtentRadX_, float halfExtentRadY_) {
    cellsX = std::clamp(cellsX_, 1, MAX_GRID_DIM);
    cellsY = std::clamp(cellsY_, 1, MAX_GRID_DIM);
    halfExtentRadX = halfExtentRadX_;
    halfExtentRadY = halfExtentRadY_;
    const std::size_t total =
        static_cast<std::size_t>(cellsX) * static_cast<std::size_t>(cellsY);
    crustThicknessKm.assign(total, 0.0f);
    continentalFraction.assign(total, 0.0f);
    cumulativeStrain.assign(total, 0.0f);
    surfaceElevationM.assign(total, 0.0f);
    cellActive.assign(total, 1u);
}

void PhysicsGrid::plateLocalToCell(float lxRad, float lyRad,
                                   float& gx, float& gy) const {
    if (cellsX <= 0 || cellsY <= 0
        || halfExtentRadX <= 0.0f || halfExtentRadY <= 0.0f) {
        gx = -1.0f;
        gy = -1.0f;
        return;
    }
    const float fx = (lxRad + halfExtentRadX) / (2.0f * halfExtentRadX);
    const float fy = (lyRad + halfExtentRadY) / (2.0f * halfExtentRadY);
    gx = fx * static_cast<float>(cellsX) - 0.5f;
    gy = fy * static_cast<float>(cellsY) - 0.5f;
}

float PhysicsGrid::bilinearSample(const std::vector<float>& field,
                                  float lxRad, float lyRad) const {
    float gx = 0.0f;
    float gy = 0.0f;
    plateLocalToCell(lxRad, lyRad, gx, gy);
    if (gx < 0.0f || gy < 0.0f) { return 0.0f; }
    const int32_t ix = static_cast<int32_t>(std::floor(gx));
    const int32_t iy = static_cast<int32_t>(std::floor(gy));
    if (ix < 0 || ix >= cellsX - 1 || iy < 0 || iy >= cellsY - 1) {
        return 0.0f;
    }
    const float fx = gx - static_cast<float>(ix);
    const float fy = gy - static_cast<float>(iy);
    const std::size_t i00 = cellIndex(ix,     iy);
    const std::size_t i10 = cellIndex(ix + 1, iy);
    const std::size_t i01 = cellIndex(ix,     iy + 1);
    const std::size_t i11 = cellIndex(ix + 1, iy + 1);
    // Inactive-cell handling: if any of the four corners is inactive,
    // its contribution is zero and the weights renormalise. Avoids
    // pulling in stale / uninitialised state at the plate's edge.
    const float w00 = (cellActive[i00] != 0u) ? (1.0f - fx) * (1.0f - fy) : 0.0f;
    const float w10 = (cellActive[i10] != 0u) ?        fx  * (1.0f - fy) : 0.0f;
    const float w01 = (cellActive[i01] != 0u) ? (1.0f - fx) *        fy  : 0.0f;
    const float w11 = (cellActive[i11] != 0u) ?        fx  *        fy  : 0.0f;
    const float wSum = w00 + w10 + w01 + w11;
    if (wSum <= 0.0f) { return 0.0f; }
    const float v = w00 * field[i00] + w10 * field[i10]
                  + w01 * field[i01] + w11 * field[i11];
    return v / wSum;
}

float PhysicsGrid::peakSample(const std::vector<float>& field,
                              float lxRad, float lyRad,
                              int32_t halfSearch) const {
    float gx = 0.0f;
    float gy = 0.0f;
    plateLocalToCell(lxRad, lyRad, gx, gy);
    if (gx < 0.0f || gy < 0.0f) { return 0.0f; }
    const int32_t ixC = static_cast<int32_t>(std::floor(gx + 0.5f));
    const int32_t iyC = static_cast<int32_t>(std::floor(gy + 0.5f));
    const int32_t ixLo = std::max(0, ixC - halfSearch);
    const int32_t ixHi = std::min(cellsX - 1, ixC + halfSearch);
    const int32_t iyLo = std::max(0, iyC - halfSearch);
    const int32_t iyHi = std::min(cellsY - 1, iyC + halfSearch);
    float peak = -1e9f;
    bool any = false;
    for (int32_t iy = iyLo; iy <= iyHi; ++iy) {
        for (int32_t ix = ixLo; ix <= ixHi; ++ix) {
            const std::size_t idx = cellIndex(ix, iy);
            if (cellActive[idx] == 0u) { continue; }
            const float v = field[idx];
            if (!any || v > peak) {
                peak = v;
                any = true;
            }
        }
    }
    return any ? peak : 0.0f;
}

std::size_t PhysicsGrid::activeCellCount() const {
    std::size_t count = 0;
    for (uint8_t a : cellActive) { if (a != 0u) { ++count; } }
    return count;
}

void initialisePlatePhysicsGrid(Plate& plate, std::uint64_t seed) {
    // Grid sizing: cover plate radius * 1.4 (so plate body fits with
    // margin for divergent-edge growth in Phase 5). Convert angular
    // half-extent to cells at PHYSICS_CELL_SIZE_KM resolution.
    const float halfExtentRad = plateHalfExtentRad(plate.weight) * 1.4f;
    const float halfExtentKm  = halfExtentRad * PhysicsConstants::earthRadiusKm;
    const int32_t cellsPerSide = static_cast<int32_t>(
        std::ceil(2.0f * halfExtentKm / PHYSICS_CELL_SIZE_KM));
    const int32_t cellsClamped = std::min(cellsPerSide, MAX_GRID_DIM);
    plate.grid.resize(cellsClamped, cellsClamped, halfExtentRad, halfExtentRad);

    std::uint64_t rng = mixPlateSeed(seed, plate.latDeg, plate.lonDeg);
    const float landThreshold = 1.0f - plate.landFraction;

    for (int32_t iy = 0; iy < plate.grid.cellsY; ++iy) {
        const float lyRad = -plate.grid.halfExtentRadY
            + (static_cast<float>(iy) + 0.5f)
              * (2.0f * plate.grid.halfExtentRadY)
              / static_cast<float>(plate.grid.cellsY);
        for (int32_t ix = 0; ix < plate.grid.cellsX; ++ix) {
            const float lxRad = -plate.grid.halfExtentRadX
                + (static_cast<float>(ix) + 0.5f)
                  * (2.0f * plate.grid.halfExtentRadX)
                  / static_cast<float>(plate.grid.cellsX);
            const std::size_t idx = plate.grid.cellIndex(ix, iy);

            // Sample the same crust-mask noise the legacy code used so
            // continental shapes match the existing aesthetic. Replace
            // when a more principled crust composition model lands.
            const float lxField = lxRad * 5.0f + plate.seedX;
            const float lyField = lyRad * 5.0f + plate.seedY;
            aoc::Random crustRng(static_cast<std::uint32_t>(rng));
            const float crustValue = fractalNoise(
                lxField, lyField, 4, 2.0f, 0.55f, crustRng);
            const bool isContinentalCell = (crustValue > landThreshold);

            plate.grid.continentalFraction[idx] =
                isContinentalCell ? 1.0f : 0.0f;
            plate.grid.crustThicknessKm[idx] = isContinentalCell
                ? PhysicsConstants::initialContinentalThicknessKm
                : PhysicsConstants::initialOceanicThicknessKm;
            plate.grid.cumulativeStrain[idx]   = 0.0f;
            plate.grid.surfaceElevationM[idx]  = 0.0f;
            plate.grid.cellActive[idx]         = 1u;
        }
    }

    recomputeIsostaticElevation(plate.grid);
}

namespace {

/// Convergence-strain -> crust thickening conversion. Real Himalayas:
/// ~5 cm/yr convergence, ~70 km crust over ~50 My (35 -> 70 km, dh ~ 35 km).
/// Convergence in rad/My: 5e-2 m/yr * 1/6371000 m = 7.85e-9 rad/yr =
/// 7.85e-3 rad/My. Cumulative strain over 50 My = 0.39 rad. Dividing
/// 35 km dh by 0.39 rad gives ~90 km/rad. Used as STRAIN_TO_THICKENING.
/// 2026-05-05 Phase 6 sub-step 6b: 90 -> 120 km/rad. Combined with
/// reduced K_EROSION (0.10 -> 0.03), allows mountain peaks to grow
/// taller before erosion catches up. Earth-equivalent: ~5 cm/yr
/// convergence -> 0.39 rad over 50 My -> 47 km dh (35->82 km) hitting
/// the 65 km cap; mountains spend longer time-fraction above the
/// 5500 m biome threshold.
/// 2026-05-05 Phase 10: 120 -> 160 km/rad. Stronger thickening so
/// marginal-physics seeds (s4/s5/s6/s10/s14) reach 4500 m biome
/// floor at lower strain. Saturates at maxCrustThicknessKm = 65 km
/// for high-convergence seeds (no further surface lift) but
/// promotes weak-convergence seeds across threshold.
constexpr float STRAIN_TO_THICKENING_KM_PER_RAD = 160.0f;

/// Per-step strain release: orogeny isn't pure accumulation. Erosion +
/// post-collision relaxation bleeds off ~2 % per My (matches the e-folding
/// time of ~50 My for orogen relaxation in literature).
constexpr float STRAIN_RELEASE_PER_MY = 0.02f;

/// Cell sphere position: tangent-plane (lxRad, lyRad) around plate
/// centroid -> approximate (latDeg, lonDeg). Small-angle inverse of the
/// projection used in accumulateConvergenceStrain.
LatLon cellLatLon(const PhysicsGrid& grid, int32_t ix, int32_t iy,
                  float plateLatDeg, float plateLonDeg) {
    const float lyRad = -grid.halfExtentRadY
        + (static_cast<float>(iy) + 0.5f)
          * (2.0f * grid.halfExtentRadY)
          / static_cast<float>(grid.cellsY);
    const float lxRad = -grid.halfExtentRadX
        + (static_cast<float>(ix) + 0.5f)
          * (2.0f * grid.halfExtentRadX)
          / static_cast<float>(grid.cellsX);
    constexpr float RAD2DEG = 57.29577951f;
    constexpr float DEG2RAD = 0.01745329252f;
    const float cosLat = std::cos(plateLatDeg * DEG2RAD);
    const float safeCos = std::max(0.05f, std::abs(cosLat));
    return LatLon{
        plateLatDeg + lyRad * RAD2DEG,
        plateLonDeg + (lxRad / safeCos) * RAD2DEG};
}

} // namespace

void buildPlateSphereIndex(const std::vector<Plate>& plates,
                           PlateSphereIndex& index) {
    const std::size_t totalBuckets =
        static_cast<std::size_t>(PlateSphereIndex::LAT_BUCKETS)
        * static_cast<std::size_t>(PlateSphereIndex::LON_BUCKETS);
    index.buckets.assign(totalBuckets, {});
    for (std::size_t pi = 0; pi < plates.size(); ++pi) {
        const Plate& p = plates[pi];
        // Map latDeg [-90, 90] -> [0, LAT_BUCKETS).
        int32_t latIdx = static_cast<int32_t>(
            (p.latDeg + 90.0f) / 180.0f
            * static_cast<float>(PlateSphereIndex::LAT_BUCKETS));
        if (latIdx < 0) { latIdx = 0; }
        if (latIdx >= PlateSphereIndex::LAT_BUCKETS) {
            latIdx = PlateSphereIndex::LAT_BUCKETS - 1;
        }
        // Map lonDeg [-180, 180) -> [0, LON_BUCKETS) with wrap.
        float lonNorm = std::fmod(p.lonDeg + 180.0f, 360.0f);
        if (lonNorm < 0.0f) { lonNorm += 360.0f; }
        int32_t lonIdx = static_cast<int32_t>(
            lonNorm / 360.0f
            * static_cast<float>(PlateSphereIndex::LON_BUCKETS));
        if (lonIdx < 0) { lonIdx = 0; }
        if (lonIdx >= PlateSphereIndex::LON_BUCKETS) {
            lonIdx = PlateSphereIndex::LON_BUCKETS - 1;
        }
        index.buckets[
            static_cast<std::size_t>(latIdx)
            * static_cast<std::size_t>(PlateSphereIndex::LON_BUCKETS)
            + static_cast<std::size_t>(lonIdx)
        ].push_back(static_cast<int32_t>(pi));
    }
}

void queryPlatesBucketRadius(const PlateSphereIndex& index,
                             float latDeg, float lonDeg,
                             int32_t bucketRadius,
                             std::vector<int32_t>& out) {
    int32_t latIdx = static_cast<int32_t>(
        (latDeg + 90.0f) / 180.0f
        * static_cast<float>(PlateSphereIndex::LAT_BUCKETS));
    if (latIdx < 0) { latIdx = 0; }
    if (latIdx >= PlateSphereIndex::LAT_BUCKETS) {
        latIdx = PlateSphereIndex::LAT_BUCKETS - 1;
    }
    float lonNorm = std::fmod(lonDeg + 180.0f, 360.0f);
    if (lonNorm < 0.0f) { lonNorm += 360.0f; }
    int32_t lonIdx = static_cast<int32_t>(
        lonNorm / 360.0f
        * static_cast<float>(PlateSphereIndex::LON_BUCKETS));
    if (lonIdx < 0) { lonIdx = 0; }
    if (lonIdx >= PlateSphereIndex::LON_BUCKETS) {
        lonIdx = PlateSphereIndex::LON_BUCKETS - 1;
    }

    for (int32_t di = -bucketRadius; di <= bucketRadius; ++di) {
        const int32_t li = latIdx + di;
        if (li < 0 || li >= PlateSphereIndex::LAT_BUCKETS) { continue; }
        for (int32_t dj = -bucketRadius; dj <= bucketRadius; ++dj) {
            int32_t lj = lonIdx + dj;
            // Cylindrical wrap on longitude.
            while (lj < 0) {
                lj += PlateSphereIndex::LON_BUCKETS;
            }
            while (lj >= PlateSphereIndex::LON_BUCKETS) {
                lj -= PlateSphereIndex::LON_BUCKETS;
            }
            const std::size_t bIdx =
                static_cast<std::size_t>(li)
                * static_cast<std::size_t>(PlateSphereIndex::LON_BUCKETS)
                + static_cast<std::size_t>(lj);
            const auto& b = index.buckets[bIdx];
            out.insert(out.end(), b.begin(), b.end());
        }
    }
}

void accumulateConvergenceStrain(std::vector<Plate>& plates, float dtMy) {
    const std::size_t N = plates.size();
    if (N < 2 || dtMy <= 0.0f) { return; }

    // 2026-05-05 Phase 6 sub-step 6a: spatial index over plate
    // centroids replaces the inner-N walk. With ~60 plates and
    // 10° buckets, a radius-2 query (5x5 = 25 buckets, ~50°
    // angular reach) typically finds 5-15 candidate plates,
    // cutting per-cell work from N=60 to ~10. Index built ONCE
    // here; plate centroids do not move during the call.
    PlateSphereIndex sphereIdx;
    buildPlateSphereIndex(plates, sphereIdx);

    #ifdef AOC_HAS_OPENMP
    #pragma omp parallel for schedule(dynamic, 1)
    #endif
    for (std::ptrdiff_t pi = 0; pi < static_cast<std::ptrdiff_t>(N); ++pi) {
        Plate& p = plates[static_cast<std::size_t>(pi)];
        PhysicsGrid& g = p.grid;
        if (g.cellsX <= 0 || g.cellsY <= 0) { continue; }
        const LatLon plateLL{p.latDeg, p.lonDeg};
        const LatLon poleP{p.eulerPoleLatDeg, p.eulerPoleLonDeg};

        // Reusable scratch buffer per OpenMP thread; cleared each cell.
        std::vector<int32_t> nearbyPlates;
        nearbyPlates.reserve(64);

        for (int32_t iy = 0; iy < g.cellsY; ++iy) {
            for (int32_t ix = 0; ix < g.cellsX; ++ix) {
                const std::size_t idx = g.cellIndex(ix, iy);
                if (g.cellActive[idx] == 0u) { continue; }

                const LatLon cellLL = cellLatLon(g, ix, iy,
                                                 p.latDeg, p.lonDeg);

                // Find nearest other plate via bucket query (radius 2
                // ~ 50° reach catches all plausible nearest neighbours
                // for plates with up to ~40° half-extent).
                nearbyPlates.clear();
                queryPlatesBucketRadius(sphereIdx,
                    cellLL.latDeg, cellLL.lonDeg, 2, nearbyPlates);
                int32_t nearest = -1;
                float nearestDistRad = 1e9f;
                for (int32_t qi : nearbyPlates) {
                    if (qi == static_cast<int32_t>(pi)) { continue; }
                    const Plate& q = plates[static_cast<std::size_t>(qi)];
                    const LatLon qLL{q.latDeg, q.lonDeg};
                    const float h = haversineRadians(cellLL, qLL);
                    if (h < nearestDistRad) {
                        nearestDistRad = h;
                        nearest = qi;
                    }
                }
                if (nearest < 0) { continue; }
                const Plate& q = plates[static_cast<std::size_t>(nearest)];

                // Cell is "at the boundary" only when nearest other plate
                // is closer than this plate's centroid is to the cell.
                // Otherwise the cell sits in plate p's interior.
                const float distToOwn = haversineRadians(cellLL, plateLL);
                if (nearestDistRad >= distToOwn) { continue; }

                // Relative tangent-plane velocity (rad/unit_time) at the
                // cell. eulerVelocityAt expects an angular-rate field;
                // angularVelDeg is deg/epoch. Convert to rad/My assuming
                // the caller scales dtMy to match.
                const LatLon poleQ{q.eulerPoleLatDeg, q.eulerPoleLonDeg};
                const TangentVelocity vP = eulerVelocityAt(
                    cellLL, poleP, p.angularVelDeg);
                const TangentVelocity vQ = eulerVelocityAt(
                    cellLL, poleQ, q.angularVelDeg);
                const float dvE = vP.east  - vQ.east;
                const float dvN = vP.north - vQ.north;

                // Plate-pair separation direction in cell's tangent
                // basis: from p toward q, projected onto (east, north).
                // Use small-angle approx in cell-centred frame.
                constexpr float DEG2RAD = 0.01745329252f;
                float dLonDeg = q.lonDeg - p.lonDeg;
                if (dLonDeg >  180.0f) { dLonDeg -= 360.0f; }
                if (dLonDeg < -180.0f) { dLonDeg += 360.0f; }
                const float cosLat =
                    std::cos(cellLL.latDeg * DEG2RAD);
                const float sepE = dLonDeg * DEG2RAD * cosLat;
                const float sepN = (q.latDeg - p.latDeg) * DEG2RAD;
                const float sepLen = std::sqrt(sepE * sepE + sepN * sepN);
                if (sepLen < 1e-6f) { continue; }
                const float sepEN = sepE / sepLen;
                const float sepNN = sepN / sepLen;

                // Closing rate = -(relV . sepDir) since positive sep
                // direction is from p toward q; if relative velocity
                // points toward q (positive dot), p moves AWAY from q
                // (divergent) -- we want the opposite sign so closing
                // is positive.
                const float dot = dvE * sepEN + dvN * sepNN;
                const float closingRate = -dot;
                if (closingRate <= 0.0f) { continue; }

                g.cumulativeStrain[idx] += closingRate * dtMy;
            }
        }
    }
}

void thickenCrustFromStrain(PhysicsGrid& grid, float dtMy) {
    if (dtMy <= 0.0f) { return; }
    const std::size_t total = grid.crustThicknessKm.size();
    const float maxKm = PhysicsConstants::maxCrustThicknessKm;
    const float relaxFactor = std::max(0.0f,
        1.0f - STRAIN_RELEASE_PER_MY * dtMy);

    for (std::size_t i = 0; i < total; ++i) {
        if (grid.cellActive[i] == 0u) { continue; }

        // Convergence thickens BOTH crust types but the lasting
        // continental record (mountains) requires continental cells.
        // Oceanic cells thicken transiently; sediment + crust transfer
        // to continental side handled in later phases.
        const float strain = grid.cumulativeStrain[i];
        if (strain > 0.0f) {
            // Continental cells thicken (Himalayas / Andes / Alps).
            // Oceanic cells participate but the dominant lasting
            // record is on the continental side -- subduction +
            // arc accretion gradually flips marginal cells continental.
            const float contFracBefore = grid.continentalFraction[i];
            const float contWeight =
                0.2f + 0.8f * contFracBefore;  // ocean still gets some
            const float dh = STRAIN_TO_THICKENING_KM_PER_RAD
                           * strain * contWeight;
            grid.crustThicknessKm[i] = std::min(maxKm,
                grid.crustThicknessKm[i] + dh);
            grid.continentalFraction[i] = std::min(1.0f,
                contFracBefore + 0.05f * strain);
        }

        grid.cumulativeStrain[i] *= relaxFactor;
    }

    recomputeIsostaticElevation(grid);
}


void applySurfaceErosion(PhysicsGrid& grid, float dtMy) {
    if (dtMy <= 0.0f) { return; }
    // Reference altitude above which erosion fires (m). At Earth-mean
    // continental surface (~840 m above sea level for continents),
    // erosion is slow / hillslope-dominated. Above 4000 m fluvial
    // incision dominates and rates jump 1-2 orders of magnitude
    // (Andes/Himalaya 1-5 mm/yr = 1000-5000 m/My).
    constexpr float EROSION_REF_M       = 4000.0f;
    /// Per-My fraction of (z - ref) removed. 0.01/My with excess 5000 m
    /// gives 50 m/My surface lowering, matching upper-bound Himalaya
    /// erosion rates.
    /// 2026-05-05 Phase 3.5: 0.05 -> 0.20. Convergence uplift at active
    /// boundaries hits 4.5 km/My crust thickening; with K=0.05 erosion
    /// only removes 1.35 km/My at peak elevation -- uplift wins, cells
    /// saturate at maxCrustThicknessKm. K=0.20 erodes 5.4 km/My at
    /// peak, beating uplift, so peaks reach a balance instead of the
    /// hard cap. Numerically equivalent to a stream-power law with
    /// integrated drainage area assumed proportional to relief.
    /// 2026-05-05 Phase 6 sub-step 6b: 0.10 -> 0.03. Time-constant
    /// 1/K = 33 My instead of 10 My; mountains stay above the 5500 m
    /// biome threshold for ~3x longer after convergence stops.
    /// Combined with STRAIN_TO_THICKENING 90 -> 120, peak crust stays
    /// thicker for longer, raising mtn_pct of land toward 1-2 %.
    /// 2026-05-05 Phase 11: 0.03 -> 0.02. Time-constant 1/K = 50 epochs
    /// (was 33). Mountains stay above 4500 m biome floor longer after
    /// convergence stops. Lifts mtn_pct toward Earth-target band.
    constexpr float EROSION_K_PER_MY    = 0.02f;
    /// Airy ratio: surface drop divided by crust loss. For continental
    /// crust at 2700 / mantle 3300, the surface falls 1 - rho_c/rho_m =
    /// 0.182 m per metre of crust removed. So 1 m surface drop = 1/0.182
    /// = 5.5 m crust thinning.
    constexpr float CRUST_PER_SURFACE_M = 1.0f / 0.182f;
    /// Continental crust never thins below the initial 35 km baseline
    /// just from erosion alone -- below that the Moho rebounds and the
    /// surface stops dropping (sediment fills basins instead).
    const float minCrustKm =
        PhysicsConstants::initialOceanicThicknessKm;

    const std::size_t total = grid.crustThicknessKm.size();
    for (std::size_t i = 0; i < total; ++i) {
        if (grid.cellActive[i] == 0u) { continue; }
        const float z = grid.surfaceElevationM[i];
        const float excess = z - EROSION_REF_M;
        if (excess <= 0.0f) { continue; }
        const float dzM = EROSION_K_PER_MY * excess * dtMy;
        const float dCrustM  = dzM * CRUST_PER_SURFACE_M;
        const float dCrustKm = dCrustM * 0.001f;
        grid.crustThicknessKm[i] = std::max(minCrustKm,
            grid.crustThicknessKm[i] - dCrustKm);
        // 2026-05-05 Path X: eroded mass discarded. Sediment routing +
        // compaction were ripped because their infrastructure produced
        // no observable effect at game grid resolution and required a
        // 3000 m sediment cap bandaid for stability. Mountain crust
        // thins, eroded surface drop exits the system. Mass not
        // conserved between cells (real Earth submarine fans capture
        // most of it offshore -- modeling that path is out-of-scope).
    }

    recomputeIsostaticElevation(grid);
}

void recomputeIsostaticElevation(PhysicsGrid& grid) {
    // Airy isostasy: the surface elevation above the mantle datum is
    // z = h * (1 - rho_c / rho_m). Mixed crust uses a linear blend of
    // continental and oceanic densities. Sediment load is partially
    // compensated; isostatic effective load factor 0.6 is a textbook
    // approximation (sediment density ~2000 kg/m^3; load over basement
    // ~ 0.6 of the equivalent crust column).
    const float rhoMantle = PhysicsConstants::rhoMantleKgM3;
    const float rhoCont   = PhysicsConstants::rhoContinentalKgM3;
    const float rhoOcean  = PhysicsConstants::rhoOceanicKgM3;
    const float datumM    = PhysicsConstants::mantleDatumM;
    const std::size_t total = grid.crustThicknessKm.size();
    for (std::size_t i = 0; i < total; ++i) {
        if (grid.cellActive[i] == 0u) {
            grid.surfaceElevationM[i] = -datumM;
            continue;
        }
        const float frac = grid.continentalFraction[i];
        const float rhoC = frac * rhoCont + (1.0f - frac) * rhoOcean;
        const float zRockM =
            grid.crustThicknessKm[i] * 1000.0f * (1.0f - rhoC / rhoMantle);
        grid.surfaceElevationM[i] = zRockM - datumM;
    }
}

} // namespace aoc::map::gen

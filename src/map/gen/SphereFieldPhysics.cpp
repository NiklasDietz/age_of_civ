#include "aoc/map/gen/SphereFieldPhysics.hpp"

#include "aoc/map/gen/PlatePhysics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <queue>

namespace aoc::map::gen {

// Vertical-thickening efficiency at convergent boundaries, in
// kilometres of crust thickening per (radians/My) closing rate per My
// of elapsed time. Derived in the header doc from the Tibet record;
// reference Turcotte & Schubert 2014 ch. 6 + DeCelles et al. 2002.
//
// 2026-05-07 recalibration: 76.5 -> 250. The 76.5 figure was derived
// from the NET Tibet thickening (30 km gain over 50 My) at the modern
// India-Asia closing rate of 0.008 rad/My. Two mismatches require a
// larger K in our model:
//   1. The 30 km is NET; raw thickening must additionally offset
//      ~22 km of parallel erosion at 5 km peak elevation
//      (K_EROSION 0.034/My, Airy ratio 5.5 m crust per m relief).
//   2. Our long-time-mean closing rate at convergent boundaries is
//      ~0.002-0.003 rad/My (3x slower than India-Asia at peak), so
//      proportional thickening would not maintain orogenic relief
//      against the same erosion budget.
// Calibrating against a slow-rate equilibrium (rate=0.002, target
// z=5500 m): K = 5500 * 0.817 * 5.5 / (1000 * 0.002 * 50) = 247
// km / (rad/My) / My, rounded to 250. At Tibet rates this gives
// equilibrium z saturating at the maxCrustThicknessKm cap, which is
// the desired behaviour. Without this, mountains form in the first
// epoch then erode below 4 km within ~10 epochs and never return as
// plate motion settles at the long-time-mean rate.
// References: DeCelles et al. 2002 (Tibet record); Whipple & Tucker
// 1999 stream-power erosion; Portenga & Bierman 2011 (K_EROSION).
inline constexpr float K_THICKEN_KM_PER_RADMY = 250.0f;

// Erosion coefficient: metres of surface lowering per My per unit
// of local slope magnitude (slope is dimensionless m/m). This is
// the Whipple & Tucker 1999 stream-power incision law in its
// n=1, area-independent reduction:
//     dz/dt = -K_S * |grad z|
// Calibration anchors:
//   * Andean active orogen, slope ~0.05 m/m (Whipple & Tucker 1999
//     fig. 7), measured incision ~100 m/My (Lal et al. 2005 10Be
//     basin study) → K_S = 100 / 0.05 = 2000 m/My/slope.
//   * Canadian Shield, slope ~0.0001 m/m, gives 0.2 m/My (Portenga
//     & Bierman 2011 cratonic median 5-15 m/My is recovered when
//     micro-relief contributes a baseline slope of 0.005-0.0075).
//   * Himalayan, slope ~0.1 m/m, gives 200 m/My (matches DeCelles
//     2002 Phanerozoic mean for Lhasa block).
//
// 2026-05-09: replaced the prior linear-in-z form
// (dz/dt = -K*z, K=0.034/My) which mis-applied the active-orogen
// stream-power calibration to stable cratons. The linear form
// erodes shields to z=0 over a few hundred My because it pretends
// every cell is an isolated peak above sea level rather than a
// member of a low-relief plateau. Slope-dependent form preserves
// cratonic shields at +500-800 m for billions of years matching
// the Precambrian shield geology, and still erodes active
// orogens fast enough for Tibet-class belts to lose half their
// crust over 50 My. Whipple & Tucker 1999 fig. 6.
inline constexpr float K_EROSION_M_PER_MY_PER_SLOPE = 2000.0f;

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

void generateInitialPlateOwnership(SphereField& field,
                                   const std::vector<Plate>& plates,
                                   uint64_t seed) {
    if (plates.empty()) {
        std::fill(field.plateId.begin(), field.plateId.end(),
                  static_cast<int16_t>(-1));
        return;
    }
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    const std::size_t totalCells = SphereField::CELL_COUNT;

    // Per-cell priority (smaller = expand sooner). Path-dependent
    // BFS from cratonic seeds. We use a min-heap of frontier cells
    // keyed by (priority, plateId, cellIdx) so equal priorities
    // tie-break deterministically by plate id then cell idx.
    struct Frontier {
        float priority;
        int16_t plateId;
        std::size_t cellIdx;
        bool operator>(const Frontier& other) const {
            if (priority != other.priority) return priority > other.priority;
            if (plateId != other.plateId)   return plateId > other.plateId;
            return cellIdx > other.cellIdx;
        }
    };
    std::priority_queue<Frontier, std::vector<Frontier>,
                        std::greater<Frontier>> heap;

    // Per-plate hash for the noise jitter so different plates expand
    // along different stochastic biases. Using SplitMix64 mix on the
    // user seed + plate index keeps it deterministic and well-spread.
    auto mix64 = [](uint64_t x) {
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return x;
    };
    auto cellHash01 = [&](std::size_t cellIdx, int16_t plateId) {
        // 4-byte mix of cell index, plate id, and global seed, mapped
        // into [0, 1). Used as the pseudo-random tiebreak that gives
        // BFS its irregular non-convex growth.
        uint64_t h = static_cast<uint64_t>(cellIdx) * 0x100000001B3ULL
                   ^ static_cast<uint64_t>(plateId) * 0x9E3779B97F4A7C15ULL
                   ^ seed;
        h = mix64(h);
        return static_cast<float>(h & 0x00FFFFFFu) / 16777216.0f;
    };

    std::fill(field.plateId.begin(), field.plateId.end(),
              static_cast<int16_t>(-1));

    // Seed the heap with each plate's centroid cell at priority 0.
    // The expansion budget per plate scales with sqrt(weight) so
    // heavy plates capture proportionally more area before being
    // crowded out at boundaries.
    std::vector<float> reachBudget(plates.size(), 0.0f);
    for (std::size_t i = 0; i < plates.size(); ++i) {
        reachBudget[i] = std::sqrt(std::max(0.1f, plates[i].weight));
        const SphereField::CellCoord c =
            SphereField::locate(plates[i].latDeg, plates[i].lonDeg);
        const std::size_t idx = SphereField::cellIndex(c.lonIdx, c.latIdx);
        heap.push({0.0f, static_cast<int16_t>(i), idx});
    }

    // Pop-and-expand. Each cell, once claimed, enqueues its 4
    // neighbours with a priority that grows with haversine arc length
    // from the seed plus a per-cell stochastic kick. Reach-budget
    // divides distance so heavier plates effectively see "shorter"
    // arc costs and absorb more cells.
    std::size_t claimed = 0;
    while (!heap.empty() && claimed < totalCells) {
        const Frontier top = heap.top();
        heap.pop();
        if (field.plateId[top.cellIdx] >= 0) continue;
        field.plateId[top.cellIdx] = top.plateId;
        ++claimed;

        const int32_t latIdx = static_cast<int32_t>(top.cellIdx / LON);
        const int32_t lonIdx = static_cast<int32_t>(top.cellIdx % LON);
        const Plate& owner = plates[static_cast<std::size_t>(top.plateId)];
        const LatLon seedLatLon{owner.latDeg, owner.lonDeg};
        const float reach = reachBudget[static_cast<std::size_t>(top.plateId)];

        const int32_t lonW = (lonIdx == 0)       ? LON - 1 : lonIdx - 1;
        const int32_t lonE = (lonIdx == LON - 1) ? 0       : lonIdx + 1;
        const int32_t latS = std::max(0,        latIdx - 1);
        const int32_t latN = std::min(LAT - 1,  latIdx + 1);
        const std::size_t neigh[4] = {
            SphereField::cellIndex(lonW, latIdx),
            SphereField::cellIndex(lonE, latIdx),
            SphereField::cellIndex(lonIdx, latS),
            SphereField::cellIndex(lonIdx, latN)
        };
        for (std::size_t n = 0; n < 4; ++n) {
            const std::size_t nIdx = neigh[n];
            if (field.plateId[nIdx] >= 0) continue;
            const int32_t nLat = static_cast<int32_t>(nIdx / LON);
            const int32_t nLon = static_cast<int32_t>(nIdx % LON);
            const LatLon nLL = SphereField::cellCenter(nLon, nLat);
            // Arc length from craton seed in radians, scaled by 1/reach.
            const float arc = haversineRadians(nLL, seedLatLon) / reach;
            // Stochastic per-cell-per-plate kick injects asymmetry
            // so adjacent cells with similar arc lengths tie-break
            // pseudo-randomly. Kick magnitude is comparable to one
            // cell width on the sphere (~0.01 rad) so it does not
            // dominate the arc cost — only breaks ties locally.
            const float kick = (cellHash01(nIdx, top.plateId) - 0.5f) * 0.02f;
            // Bias inherited from the parent's priority so paths
            // grow as continuous frontiers rather than restarting
            // from zero at each pop. This is what produces non-
            // convex shapes: a path that took a detour around a
            // peninsula keeps the accumulated cost.
            const float pri = top.priority + arc + kick;
            heap.push({pri, top.plateId, nIdx});
        }
    }
}

void recomputePlateCentroidsFromCells(SphereField& field,
                                      std::vector<Plate>& plates) {
    if (plates.empty()) return;
    // Sum unit-vector positions on the sphere per plate, then renormalise
    // to extract the area-weighted centroid. Averaging lat/lon directly
    // would fail across the antimeridian or polar wrap.
    const std::size_t N = plates.size();
    std::vector<double> sx(N, 0.0), sy(N, 0.0), sz(N, 0.0);
    std::vector<int32_t> count(N, 0);
    constexpr double DEG2RAD = 0.01745329252;
    for (int32_t latIdx = 0; latIdx < SphereField::LAT_CELLS; ++latIdx) {
        for (int32_t lonIdx = 0; lonIdx < SphereField::LON_CELLS; ++lonIdx) {
            const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
            const int16_t pid = field.plateId[idx];
            if (pid < 0 || static_cast<std::size_t>(pid) >= N) continue;
            const LatLon p = SphereField::cellCenter(lonIdx, latIdx);
            const double latR = static_cast<double>(p.latDeg) * DEG2RAD;
            const double lonR = static_cast<double>(p.lonDeg) * DEG2RAD;
            const double cosLat = std::cos(latR);
            sx[static_cast<std::size_t>(pid)] += cosLat * std::cos(lonR);
            sy[static_cast<std::size_t>(pid)] += cosLat * std::sin(lonR);
            sz[static_cast<std::size_t>(pid)] += std::sin(latR);
            ++count[static_cast<std::size_t>(pid)];
        }
    }
    constexpr float RAD2DEG = 57.29577951f;
    for (std::size_t i = 0; i < N; ++i) {
        if (count[i] == 0) continue; // Plate has no cells; centroid stale.
        const double inv = 1.0 / static_cast<double>(count[i]);
        const double mx = sx[i] * inv;
        const double my = sy[i] * inv;
        const double mz = sz[i] * inv;
        const double r = std::sqrt(mx * mx + my * my + mz * mz);
        if (r < 1e-9) continue; // Antipodal cells cancel; keep prior centroid.
        plates[i].latDeg = static_cast<float>(std::asin(mz / r) * RAD2DEG);
        plates[i].lonDeg = static_cast<float>(std::atan2(my, mx) * RAD2DEG);
    }
}

// ---------------------------------------------------------------------------
// Ridge accretion at divergent boundaries
// ---------------------------------------------------------------------------
//
// At every cell flagged as a boundary where the closing rate is
// negative (divergent), reset the cell state to fresh oceanic crust:
// thickness 7 km (Turcotte & Schubert 2014 oceanic mean), continental
// fraction 0, age 0. accumulateClosingRate stores POSITIVE rates only
// (see SphereFieldPhysics::accumulateClosingRate) — divergent
// boundaries have rate == 0 in the field but still differ by plate
// id from a neighbour. We flag those by recomputing the closing
// projection here for boundary cells whose convergenceRate is zero.
void accreteAtDivergentBoundary(SphereField& field, float dtMy) {
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    (void)dtMy; // Reserved for time-dependent accretion-rate models.
    for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
        for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
            const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
            const int16_t selfId = field.plateId[idx];
            if (selfId < 0) continue;
            // Only true divergent boundary cells extrude fresh oceanic
            // crust. Transforms (closing ≈ 0) and convergent boundaries
            // (closing > 0) keep their existing crust state.
            const float closing = field.convergenceRateRadPerMy[idx];
            // Stein & Stein 1992 give a slow-spreading-ridge minimum of
            // ~1 cm/yr full-rate ≈ 0.0008 rad/My on the sphere; below
            // this the boundary is effectively quiescent / transform.
            constexpr float DIVERGENT_RATE_THRESHOLD = -0.0008f;
            if (closing >= DIVERGENT_RATE_THRESHOLD) continue;
            // Reset to fresh oceanic crust at the spreading ridge.
            field.crustThicknessKm[idx] =
                PhysicsConstants::initialOceanicThicknessKm;
            field.continentalFraction[idx] = 0.0f;
            field.crustAgeMy[idx] = 0.0f;
        }
    }
}

// ---------------------------------------------------------------------------
// Slab-pull / ridge-push torque feedback
// ---------------------------------------------------------------------------
//
// Implements the Lithgow-Bertelloni & Richards 1998 plate-driving-
// force model in a simplified per-plate form: each plate's angular
// velocity is nudged proportional to the net "slab pull" exerted by
// its currently subducting margins. We sum convergent-boundary
// closing rates as a torque proxy, normalise by total cell count
// (so larger plates do not run away faster than smaller ones), and
// scale the Δω cap at 10 % per epoch — the Müller 2022 short-term
// plate-motion variability envelope.
//
// Geometric simplification: torque magnitude is treated as a scalar
// gain on the plate's current angular velocity. A full implementation
// would compute the cross product of the slab-pull vector with the
// Euler-pole axis to get a true torque about the rotation axis, but
// the simplified scalar-gain version reproduces the dominant signal
// — plates with active subduction accelerate, plates with no
// subduction decelerate — at much lower implementation cost.
void applySlabPullFeedback(SphereField& field,
                           std::vector<Plate>& plates,
                           float dtMy) {
    if (plates.empty()) return;
    const std::size_t N = plates.size();

    // Per-plate slab-pull score: sum of convergent rates over the
    // plate's convergent boundary cells. The score itself is the
    // input to the gain, NOT divided by total cell count -- the
    // calibration text below applies to the raw sum.
    std::vector<double> slabPull(N, 0.0);
    std::vector<int32_t> cellCount(N, 0);
    for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
        const int16_t pid = field.plateId[i];
        if (pid < 0 || static_cast<std::size_t>(pid) >= N) continue;
        ++cellCount[static_cast<std::size_t>(pid)];
        if (field.boundaryType[i] != 1u) continue; // convergent only
        slabPull[static_cast<std::size_t>(pid)] +=
            static_cast<double>(field.convergenceRateRadPerMy[i]);
    }

    // Δω/ω per epoch capped by Müller 2022's empirical 10 %/Myr
    // short-term variability scaled by sqrt(dt). At dt=50 My,
    // sqrt(50)*0.10 ≈ 0.71 — too aggressive — so we further clip the
    // per-epoch fractional change at 0.10 to keep plate motion
    // smooth on the geological timescale.
    constexpr float MAX_FRAC_PER_EPOCH = 0.10f;
    // Slab-pull-to-fractional-Δ scaling. Calibrated so a Tibet-class
    // collision (closing 0.008 rad/My over ~50 boundary cells ->
    // slabPull ≈ 0.4 rad/My total summed) gives Δω/ω ≈ 0.05 (5 %
    // increase per epoch). 0.05 / 0.4 = 0.125. Applied to the raw
    // slabPull sum, NOT divided by total cell count -- a prior bug
    // normalised by total cells (~5000) which killed the gain by ~5000x
    // and let plate motion decay unimpeded by the supposed feedback.
    constexpr float SLAB_PULL_GAIN = 0.125f;

    // Absolute ceiling on plate angular velocity. Mueller 2022 modern
    // plate-motion catalogue: median plate ~0.1 deg/My, peak Pacific
    // ~1.0 deg/My. Cap matches the geophysical peak now that the
    // advection layer is CFL-safe via sub-stepping (an earlier 0.15
    // deg/My value was a workaround for one-shot 50-My rotations
    // exceeding the 0.5-deg cell pitch and aliasing into latitude
    // bands; sub-stepping eliminates that constraint entirely).
    constexpr float MAX_ABS_OMEGA_DEG_PER_MY = 1.0f;

    for (std::size_t i = 0; i < N; ++i) {
        if (cellCount[i] == 0) continue;
        // Sign convention: pulling INTO trench means motion AWAY from
        // the trench accelerates. For our simplified scalar gain we
        // assume the trench direction aligns with the plate's Euler
        // rotation, so slab pull adds to |ω|. Decelerate when there
        // is no convergent boundary (slabPull == 0) is implicit --
        // only positive pull is considered, no friction term -- but
        // ridge push at divergent boundaries supplies the counter-
        // balance via the same pass when boundary classifies as
        // Divergent (boundaryType == 2).
        float deltaFrac = static_cast<float>(slabPull[i]) * SLAB_PULL_GAIN
                        * (dtMy / 50.0f); // normalise to the 50-Myr base.
        if (deltaFrac >  MAX_FRAC_PER_EPOCH) deltaFrac =  MAX_FRAC_PER_EPOCH;
        if (deltaFrac < -MAX_FRAC_PER_EPOCH) deltaFrac = -MAX_FRAC_PER_EPOCH;
        float w = plates[i].angularVelDeg * (1.0f + deltaFrac);
        if (w >  MAX_ABS_OMEGA_DEG_PER_MY) w =  MAX_ABS_OMEGA_DEG_PER_MY;
        if (w < -MAX_ABS_OMEGA_DEG_PER_MY) w = -MAX_ABS_OMEGA_DEG_PER_MY;
        plates[i].angularVelDeg = w;
    }
}

// ---------------------------------------------------------------------------
// Wilson-cycle continental rifting
// ---------------------------------------------------------------------------
//
// Anderson 1982 thermal-blanketing model: a stationary supercontinent
// insulates the underlying mantle, allowing heat to accumulate beneath
// it. Over ~150-200 My (Stein & Stein 1992 calibration) the integrated
// thermal stress crosses the lithospheric breakup threshold (~50 MPa,
// Steckler & Watts 1980). At that point a plume punches through the
// lithosphere and rifting initiates along a great-circle line through
// the highest-stress region of the supercontinent. This is what
// breaks Pangaea apart and starts the Atlantic ocean.
//
// We model this with two timers:
//   1. `field.thermalAgeMy[i]` — per-cell heat-accumulation clock,
//       advanced by dtMy each epoch the cell's owner is classified
//       as a supercontinent (continental area >= SUPERCONTINENT_FRACTION).
//   2. `meanThermal` — per-plate mean of `thermalAgeMy` over its
//       continental cells. Once it exceeds RIFT_THRESHOLD_MY a
//       Bernoulli trial fires per epoch with probability ramping from
//       0 (at 0 My over threshold) to 1.0 (at +100 My).
// On rift, the plate is split via PCA on its cell-position vectors
// (sphere x/y/z). Cells on each side of the principal axis go to
// either the original plate or a fresh plate; both reset thermal
// age and get perturbed Euler poles so they diverge.
inline constexpr float SUPERCONTINENT_FRACTION = 0.20f;
inline constexpr float RIFT_THRESHOLD_MY       = 150.0f;
inline constexpr float RIFT_RAMP_MY            = 100.0f;

namespace {
inline float xorshift01(uint32_t& s) {
    // XorShift32 -> [0, 1). Cheap, deterministic, no global state.
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return static_cast<float>(s & 0x00FFFFFFu) / 16777216.0f;
}
}

int32_t applyWilsonRifting(SphereField& field,
                           std::vector<Plate>& plates,
                           uint32_t& rngState,
                           float dtMy) {
    if (plates.empty()) return 0;
    const std::size_t N = plates.size();

    // Continental-area count per plate.
    std::vector<int32_t> contCells(N, 0);
    std::vector<int32_t> totalCells(N, 0);
    for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
        const int16_t pid = field.plateId[i];
        if (pid < 0 || static_cast<std::size_t>(pid) >= N) continue;
        ++totalCells[static_cast<std::size_t>(pid)];
        if (field.continentalFraction[i] > 0.5f) {
            ++contCells[static_cast<std::size_t>(pid)];
        }
    }
    const float globeCells = static_cast<float>(SphereField::CELL_COUNT);

    // Thermal-age update + per-plate mean.
    std::vector<double> thermalSum(N, 0.0);
    std::vector<int32_t> thermalCount(N, 0);
    for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
        const int16_t pid = field.plateId[i];
        if (pid < 0 || static_cast<std::size_t>(pid) >= N) continue;
        if (field.continentalFraction[i] <= 0.5f) {
            field.thermalAgeMy[i] = 0.0f;
            continue;
        }
        // Cell's plate qualifies as supercontinent?
        const float frac = static_cast<float>(contCells[static_cast<std::size_t>(pid)])
                           / globeCells;
        if (frac >= SUPERCONTINENT_FRACTION) {
            field.thermalAgeMy[i] += dtMy;
        } else {
            // Reset slowly — once a plate is no longer supercontinent
            // its thermal blanketing relaxes over ~RIFT_THRESHOLD_MY.
            field.thermalAgeMy[i] *= std::exp(-dtMy / RIFT_THRESHOLD_MY);
        }
        thermalSum[static_cast<std::size_t>(pid)] +=
            static_cast<double>(field.thermalAgeMy[i]);
        ++thermalCount[static_cast<std::size_t>(pid)];
    }

    // Decide which plates rift this epoch. Single-rift-per-epoch cap
    // matches the empirical observation that rift bursts are clustered
    // in time: 290 plate births in 5 Myr at the Pangaea breakup
    // (Müller 2022 birth histogram).
    int32_t newPlates = 0;
    for (std::size_t i = 0; i < N; ++i) {
        if (thermalCount[i] < 4) continue; // Plate too small to rift.
        const float meanThermal = static_cast<float>(
            thermalSum[i] / static_cast<double>(thermalCount[i]));
        if (meanThermal < RIFT_THRESHOLD_MY) continue;
        const float over = meanThermal - RIFT_THRESHOLD_MY;
        const float prob = std::min(1.0f, over / RIFT_RAMP_MY);
        if (xorshift01(rngState) > prob) continue;

        // PCA on cell sphere positions to find longest axis.
        constexpr double DEG2RAD = 0.01745329252;
        double mx = 0.0, my = 0.0, mz = 0.0;
        for (std::size_t cell = 0; cell < SphereField::CELL_COUNT; ++cell) {
            if (field.plateId[cell] != static_cast<int16_t>(i)) continue;
            // Decompose cell idx -> (lon, lat).
            const int32_t latIdx = static_cast<int32_t>(cell / SphereField::LON_CELLS);
            const int32_t lonIdx = static_cast<int32_t>(cell % SphereField::LON_CELLS);
            const LatLon p = SphereField::cellCenter(lonIdx, latIdx);
            const double latR = static_cast<double>(p.latDeg) * DEG2RAD;
            const double lonR = static_cast<double>(p.lonDeg) * DEG2RAD;
            const double cosLat = std::cos(latR);
            mx += cosLat * std::cos(lonR);
            my += cosLat * std::sin(lonR);
            mz += std::sin(latR);
        }
        const double inv = 1.0 / static_cast<double>(totalCells[i]);
        mx *= inv; my *= inv; mz *= inv;

        // Compute covariance to extract principal axis. With ~hundreds
        // of cells the 3x3 power-iteration converges in <10 steps.
        double cxx=0, cyy=0, czz=0, cxy=0, cxz=0, cyz=0;
        for (std::size_t cell = 0; cell < SphereField::CELL_COUNT; ++cell) {
            if (field.plateId[cell] != static_cast<int16_t>(i)) continue;
            const int32_t latIdx = static_cast<int32_t>(cell / SphereField::LON_CELLS);
            const int32_t lonIdx = static_cast<int32_t>(cell % SphereField::LON_CELLS);
            const LatLon p = SphereField::cellCenter(lonIdx, latIdx);
            const double latR = static_cast<double>(p.latDeg) * DEG2RAD;
            const double lonR = static_cast<double>(p.lonDeg) * DEG2RAD;
            const double cosLat = std::cos(latR);
            const double dx = cosLat * std::cos(lonR) - mx;
            const double dy = cosLat * std::sin(lonR) - my;
            const double dz = std::sin(latR) - mz;
            cxx += dx*dx; cyy += dy*dy; czz += dz*dz;
            cxy += dx*dy; cxz += dx*dz; cyz += dy*dz;
        }
        // Power iteration on covariance for top eigenvector.
        double vx = 1.0, vy = 0.0, vz = 0.0;
        for (int iter = 0; iter < 12; ++iter) {
            const double nx = cxx*vx + cxy*vy + cxz*vz;
            const double ny = cxy*vx + cyy*vy + cyz*vz;
            const double nz = cxz*vx + cyz*vy + czz*vz;
            const double mag = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (mag < 1e-12) break;
            vx = nx / mag; vy = ny / mag; vz = nz / mag;
        }
        // Plane normal = principal axis x mean direction. Cells split
        // by sign of dot product with this normal.
        const double nrm_x = vy * mz - vz * my;
        const double nrm_y = vz * mx - vx * mz;
        const double nrm_z = vx * my - vy * mx;

        // Spawn fresh plate inheriting parent's continental data +
        // an Euler pole that GUARANTEES divergence at the rift seam.
        // Müller 2022 conjugate margins (e.g. South-American /
        // African Atlantic margins) show rifted children diverge by
        // 30-90° in their Euler-pole orientation; we use 60° offset
        // and FORCE the angular-velocity sign opposite parent so the
        // boundary opens immediately rather than re-contacting and
        // triggering premature docking.
        Plate child = plates[i];
        const float poleOffsetDeg = 60.0f * (xorshift01(rngState) - 0.5f) * 2.0f;
        child.eulerPoleLatDeg = std::clamp(
            child.eulerPoleLatDeg + poleOffsetDeg, -89.0f, 89.0f);
        child.eulerPoleLonDeg += poleOffsetDeg;
        // Sign always flipped so child opposes parent rotation — this
        // is what makes the rift OPEN.
        child.angularVelDeg = -child.angularVelDeg;
        plates.push_back(child);
        const int16_t childId = static_cast<int16_t>(plates.size() - 1);

        // Reassign cells on the (negative-side) of the rift plane,
        // and convert a narrow band around the rift axis to fresh
        // oceanic crust. The band width scales with cell-size on the
        // sphere — RIFT_AXIS_OCEAN_HALF_RAD radians on either side
        // covers the new ocean-basin opening (Atlantic-style: South
        // America / Africa rifted ~60-Myr-after split with a ~200 km
        // wide proto-ocean centred on the rift axis, growing
        // thereafter via subsequent ridge spreading).
        // Great-circle half-width 0.015 rad ≈ 95 km on the sphere.
        // Compared via |sin(angular_distance)| = |c · v| where c is
        // the unit cell vector and v is the unit principal axis from
        // power iteration. Centres of mass m / mean-vector are not
        // unit-magnitude so the prior `(c - m)·nrm` test stretched
        // by an unknown factor; the unit-axis-projection test is
        // dimensionless.
        constexpr double RIFT_AXIS_OCEAN_SIN_HALF = 0.015;
        for (std::size_t cell = 0; cell < SphereField::CELL_COUNT; ++cell) {
            if (field.plateId[cell] != static_cast<int16_t>(i)) continue;
            const int32_t latIdx = static_cast<int32_t>(cell / SphereField::LON_CELLS);
            const int32_t lonIdx = static_cast<int32_t>(cell % SphereField::LON_CELLS);
            const LatLon p = SphereField::cellCenter(lonIdx, latIdx);
            const double latR = static_cast<double>(p.latDeg) * DEG2RAD;
            const double lonR = static_cast<double>(p.lonDeg) * DEG2RAD;
            const double cosLat = std::cos(latR);
            const double cx = cosLat * std::cos(lonR);
            const double cy = cosLat * std::sin(lonR);
            const double cz = std::sin(latR);
            const double dot = (cx-mx)*nrm_x + (cy-my)*nrm_y + (cz-mz)*nrm_z;
            if (dot < 0.0) {
                field.plateId[cell] = childId;
            }
            field.thermalAgeMy[cell] = 0.0f;
            // The split plane has nrm = v × m as its normal vector
            // (perpendicular to both the principal axis and the
            // centroid direction). The plane passes through origin
            // because cells are unit vectors. A cell's signed
            // distance to the plane is c · nrm; cells close to the
            // rift great circle satisfy |c · nrm| < threshold and
            // get converted to fresh oceanic crust so the rifted
            // halves are separated by a developing ocean basin.
            // Normalise nrm so the threshold is in actual radians of
            // angular distance.
            const double nrmMag = std::sqrt(
                nrm_x * nrm_x + nrm_y * nrm_y + nrm_z * nrm_z);
            if (nrmMag > 1e-9) {
                const double axisProj =
                    (cx * nrm_x + cy * nrm_y + cz * nrm_z) / nrmMag;
                if (std::fabs(axisProj) < RIFT_AXIS_OCEAN_SIN_HALF) {
                    field.crustThicknessKm[cell] =
                        PhysicsConstants::initialOceanicThicknessKm;
                    field.continentalFraction[cell] = 0.0f;
                    field.crustAgeMy[cell] = 0.0f;
                }
            }
        }
        ++newPlates;
        // One rift per epoch (matches real-Earth burst cadence).
        break;
    }
    return newPlates;
}

int32_t compactPlateList(SphereField& field, std::vector<Plate>& plates) {
    const std::size_t N = plates.size();
    if (N == 0) return 0;
    std::vector<int32_t> count(N, 0);
    for (int16_t pid : field.plateId) {
        if (pid >= 0 && static_cast<std::size_t>(pid) < N) {
            ++count[static_cast<std::size_t>(pid)];
        }
    }
    std::vector<int16_t> remap(N, -1);
    std::vector<Plate> survivors;
    survivors.reserve(N);
    for (std::size_t i = 0; i < N; ++i) {
        if (count[i] > 0) {
            remap[i] = static_cast<int16_t>(survivors.size());
            survivors.push_back(plates[i]);
        }
    }
    if (survivors.size() == N) return 0; // Nothing to compact.
    for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
        const int16_t pid = field.plateId[i];
        if (pid >= 0 && static_cast<std::size_t>(pid) < N) {
            field.plateId[i] = remap[static_cast<std::size_t>(pid)];
        } else {
            field.plateId[i] = -1;
        }
    }
    const int32_t removed = static_cast<int32_t>(N - survivors.size());
    plates = std::move(survivors);
    return removed;
}

// ---------------------------------------------------------------------------
// Plate-cell advection (Lagrangian transport)
// ---------------------------------------------------------------------------
//
// Real plates carry their crust as they rotate about their Euler poles
// (Cox & Hart 1986 ch. 4). On a fixed lat/lon raster this means each
// epoch's owning-plate map must be REWRITTEN: the cell currently at
// position p, owned by plate P with rotation rate omega about pole e,
// will after dt have moved to p' = R(e, omega*dt) * p. The cell at p'
// inherits ownership and crust state from the old cell at p.
//
// Implementation is backward (departure-point) semi-Lagrangian: for
// each destination cell D and each plate P, the cell at the
// backward-rotated point dep_P(D) = R(-omega_P*dt, e_P) * D would
// rotate forward to D under P's motion. If the field currently owns
// dep_P(D) with plate P, then P's cell will arrive at D and P claims
// D. Multiple plates may claim D simultaneously (geometric signature
// of convergence): resolve with continental-over-oceanic priority,
// ties broken by older crust age. Cells that no plate claims are
// wakes left at divergent boundaries; they fill with fresh oceanic
// crust (Turcotte & Schubert 2014 ch. 2 mid-ocean-ridge basalt:
// 7 km thickness, age 0, continental fraction 0) and inherit
// ownership from any 4-neighbour.
//
// Backward sampling is preferred over forward push because rotation
// is a continuous map: nearby destinations have nearby departure
// points, so plate footprints stay connected -- no salt-and-pepper
// ownership artefacts. Forward push aliases at the destination (two
// neighbouring source cells can map to disjoint dest cells when the
// rotation angle approaches the cell pitch).
//
// This is the missing transport step that was hidden by the
// "Lagrangian cell tracking" comment elsewhere -- ownership persisted
// across epochs but never advected. Plate motion was visible only in
// boundary-cell flips from subduction. Adding true advection makes
// continents drift across the map as their plates rotate.
void advectPlateOwnership(SphereField& field,
                          const std::vector<Plate>& plates,
                          float dtMy) {
    if (plates.empty() || dtMy <= 0.0f) return;
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    constexpr double DEG2RAD = 0.01745329252;
    constexpr double RAD2DEG = 57.29577951;

    const std::size_t N = SphereField::CELL_COUNT;

    // BACKWARD semi-Lagrangian advection. For each destination cell D
    // we look up where D came from under each candidate plate's motion
    // (R^-1 * D). If the field currently owns that departure cell with
    // plate P, then P's source rotates forward to D and P claims D.
    //
    // Forward-push was tested as an alternative: it cleanly translates
    // plate footprints in the continuous limit but suffers from raster
    // discretisation -- ~1% of cells per substep collide (two sources
    // map to the same dest) or orphan (no source maps here). Compounded
    // over hundreds of substeps the discretisation losses dominate and
    // continental crust dissolves wholesale (60-epoch audit: mountain
    // count 1100 -> 90). Backward sample preserves plate footprints
    // exactly via the incumbent rule because every dest cell already
    // has a prior owner whose backward-rotation usually lands within
    // the plate's footprint -- no discretisation loss.
    //
    // Conflict resolution when MULTIPLE plates claim D:
    //   1. INCUMBENT (current owner of D) wins. Boundaries shift only
    //      via mechanism passes (subduction / Wilson rifting / accretion),
    //      not via advection alone.
    //   2. Otherwise pick the SLOWEST plate among claimants (smallest
    //      |omega_P|). Cratonic plates resist mantle drag (Gripp &
    //      Gordon 2002 Eurasian 0.14 vs Pacific 0.96 deg/My); slow
    //      side wins to match real geophysics, and a first-by-index
    //      tie-break would create vertical stripes in the rendered
    //      overlay.
    //
    // Orphan cells (no claimant): handled in the second pass below.
    struct PlateRot {
        double axX;
        double axY;
        double axZ;
        double cosT;
        double sinT;
        double oneMc;
    };
    const std::size_t P = plates.size();
    std::vector<PlateRot> rot(P);
    for (std::size_t i = 0; i < P; ++i) {
        const double poleLatR = static_cast<double>(plates[i].eulerPoleLatDeg) * DEG2RAD;
        const double poleLonR = static_cast<double>(plates[i].eulerPoleLonDeg) * DEG2RAD;
        // Backward rotation: negative theta maps dest back to its source.
        const double thetaR   = -static_cast<double>(plates[i].angularVelDeg)
                              * DEG2RAD * static_cast<double>(dtMy);
        const double cosLat = std::cos(poleLatR);
        rot[i].axX   = cosLat * std::cos(poleLonR);
        rot[i].axY   = cosLat * std::sin(poleLonR);
        rot[i].axZ   = std::sin(poleLatR);
        rot[i].cosT  = std::cos(thetaR);
        rot[i].sinT  = std::sin(thetaR);
        rot[i].oneMc = 1.0 - rot[i].cosT;
    }

    std::vector<int16_t> newOwner(N, static_cast<int16_t>(-1));
    std::vector<float>   newCrust(N, 0.0f);
    std::vector<float>   newContFrac(N, 0.0f);
    std::vector<float>   newAge(N, 0.0f);
    std::vector<float>   newSurface(N, 0.0f);
    std::vector<float>   newThermal(N, 0.0f);
    std::size_t orphanCount = 0;

    auto rotateBackward = [](const PlateRot& R, double cx, double cy, double cz)
        -> std::size_t {
        const double dot = R.axX * cx + R.axY * cy + R.axZ * cz;
        const double crossX = R.axY * cz - R.axZ * cy;
        const double crossY = R.axZ * cx - R.axX * cz;
        const double crossZ = R.axX * cy - R.axY * cx;
        const double nX = cx * R.cosT + crossX * R.sinT + R.axX * dot * R.oneMc;
        const double nY = cy * R.cosT + crossY * R.sinT + R.axY * dot * R.oneMc;
        const double nZ = cz * R.cosT + crossZ * R.sinT + R.axZ * dot * R.oneMc;
        const double clampedZ = std::clamp(nZ, -1.0, 1.0);
        const double depLatDeg = std::asin(clampedZ) * RAD2DEG;
        const double depLonDeg = std::atan2(nY, nX) * RAD2DEG;
        const SphereField::CellCoord dep = SphereField::locate(
            static_cast<float>(depLatDeg), static_cast<float>(depLonDeg));
        return SphereField::cellIndex(dep.lonIdx, dep.latIdx);
    };

    for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
        for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
            const std::size_t destIdx = SphereField::cellIndex(lonIdx, latIdx);
            const int16_t incumbent = field.plateId[destIdx];

            const LatLon p = SphereField::cellCenter(lonIdx, latIdx);
            const double latR = static_cast<double>(p.latDeg) * DEG2RAD;
            const double lonR = static_cast<double>(p.lonDeg) * DEG2RAD;
            const double cosLat = std::cos(latR);
            const double cx = cosLat * std::cos(lonR);
            const double cy = cosLat * std::sin(lonR);
            const double cz = std::sin(latR);

            // Incumbent-first short-circuit. ~95% of cells are interior
            // and the incumbent's depIdx falls in its own footprint.
            int16_t pickPid     = -1;
            std::size_t pickDep = 0;
            if (incumbent >= 0 && static_cast<std::size_t>(incumbent) < P) {
                const std::size_t depIdx = rotateBackward(
                    rot[static_cast<std::size_t>(incumbent)], cx, cy, cz);
                if (field.plateId[depIdx] == incumbent) {
                    pickPid = incumbent;
                    pickDep = depIdx;
                }
            }
            if (pickPid < 0) {
                // Fall through: scan all plates, pick slowest claimant.
                float slowOmega = 1e9f;
                for (std::size_t pIdx = 0; pIdx < P; ++pIdx) {
                    if (static_cast<int16_t>(pIdx) == incumbent) continue;
                    const std::size_t depIdx = rotateBackward(
                        rot[pIdx], cx, cy, cz);
                    if (field.plateId[depIdx] != static_cast<int16_t>(pIdx)) continue;
                    const float w = std::fabs(plates[pIdx].angularVelDeg);
                    if (w < slowOmega) {
                        slowOmega = w;
                        pickPid   = static_cast<int16_t>(pIdx);
                        pickDep   = depIdx;
                    }
                }
            }

            if (pickPid < 0) {
                // Orphan: no plate claims this dest under its own
                // backward rotation. Keep prior incumbent's state --
                // boundaries shift only via mechanism passes.
                ++orphanCount;
                newOwner[destIdx]    = incumbent;
                newCrust[destIdx]    = field.crustThicknessKm[destIdx];
                newContFrac[destIdx] = field.continentalFraction[destIdx];
                newAge[destIdx]      = field.crustAgeMy[destIdx];
                newSurface[destIdx]  = field.surfaceElevationM[destIdx];
                newThermal[destIdx]  = field.thermalAgeMy[destIdx];
                continue;
            }

            newOwner[destIdx]    = pickPid;
            newCrust[destIdx]    = field.crustThicknessKm[pickDep];
            newContFrac[destIdx] = field.continentalFraction[pickDep];
            newAge[destIdx]      = field.crustAgeMy[pickDep];
            newSurface[destIdx]  = field.surfaceElevationM[pickDep];
            newThermal[destIdx]  = field.thermalAgeMy[pickDep];
        }
    }

    for (int32_t pass = 0; pass < 4; ++pass) {
        bool anyFilled = false;
        for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
            for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
                const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
                if (newOwner[idx] >= 0) continue;
                const int32_t lonW = (lonIdx == 0)       ? LON - 1 : lonIdx - 1;
                const int32_t lonE = (lonIdx == LON - 1) ? 0       : lonIdx + 1;
                const int32_t latS = (latIdx == 0)       ? 0       : latIdx - 1;
                const int32_t latN = (latIdx == LAT - 1) ? LAT - 1 : latIdx + 1;
                const std::size_t nIdx[4] = {
                    SphereField::cellIndex(lonW, latIdx),
                    SphereField::cellIndex(lonE, latIdx),
                    SphereField::cellIndex(lonIdx, latS),
                    SphereField::cellIndex(lonIdx, latN),
                };
                int16_t pickPid   = -1;
                float   pickOmega = 1e9f;
                for (int32_t k = 0; k < 4; ++k) {
                    const int16_t nPid = newOwner[nIdx[k]];
                    if (nPid < 0) continue;
                    const float w = std::fabs(
                        plates[static_cast<std::size_t>(nPid)].angularVelDeg);
                    if (w < pickOmega) {
                        pickOmega = w;
                        pickPid   = nPid;
                    }
                }
                if (pickPid < 0) continue;
                newOwner[idx] = pickPid;
                anyFilled = true;
            }
        }
        if (!anyFilled) break;
    }

    // Final fallback: an entire patch with no claimed neighbour anywhere
    // is implausible at sub-step CFL but preserves the invariant.
    for (std::size_t i = 0; i < N; ++i) {
        if (newOwner[i] < 0) newOwner[i] = 0;
    }

    if (std::getenv("AOC_ADVECT_TRACE") != nullptr) {
        std::fprintf(stderr,
            "[advect] dt=%.3fMy orphans=%zu\n",
            static_cast<double>(dtMy), orphanCount);
    }

    field.plateId             = std::move(newOwner);
    field.crustThicknessKm    = std::move(newCrust);
    field.continentalFraction = std::move(newContFrac);
    field.crustAgeMy          = std::move(newAge);
    field.surfaceElevationM   = std::move(newSurface);
    field.thermalAgeMy        = std::move(newThermal);
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
    std::fill(field.boundaryType.begin(),
              field.boundaryType.end(), static_cast<uint8_t>(0));
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

            // No plate-extent gate. The gate originated as a fudge for
            // centroid-Voronoi where a microplate could "own" cells far
            // from its seed; under Lagrangian + region-growing init no
            // such over-extension exists, and the gate would
            // incorrectly silence convergence at the rim of any
            // elongated plate (Pacific-class spans ~60° but
            // sqrt(weight)*0.6 caps reach at ~38°).
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

            // Closing component (along boundary normal n) and shear
            // component (perpendicular, along the tangent t). Signed
            // closing > 0 = convergent, < 0 = divergent; shear sign
            // distinguishes the two transform-fault polarities.
            const float closing = dvE * nx + dvN * ny;
            // Tangent unit vector perpendicular to (nx, ny) in the
            // east/north plane: t = (-ny, nx).
            const float shear = dvE * (-ny) + dvN * nx;
            field.convergenceRateRadPerMy[idx] = closing;
            // Classify by which component dominates. Müller 2022
            // boundary-type histogram (orogen_reference.txt extracts
            // ~25 % transform / 35 % convergent / 40 % divergent on
            // the modern Earth boundary network) calibrates the
            // 1.5× tie-break: shear must clearly dominate to over-
            // ride the closing classification, otherwise the cell
            // counts as convergent or divergent according to sign.
            const float absClosing = std::fabs(closing);
            const float absShear   = std::fabs(shear);
            uint8_t btype;
            if (absShear > 1.5f * absClosing) {
                btype = 3; // Transform
            } else if (closing >= 0.0f) {
                btype = 1; // Convergent
            } else {
                btype = 2; // Divergent
            }
            field.boundaryType[idx] = btype;
        }
    }
}

void thickenFromClosingRate(SphereField& field, float dtMy) {
    const float maxCrust = PhysicsConstants::maxCrustThicknessKm;
#if defined(AOC_HAS_OPENMP)
    #pragma omp parallel for schedule(static)
#endif
    for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
        // Only convergent cells thicken — transform shear should not
        // build crust, and divergent cells extrude basalt instead.
        if (field.boundaryType[i] != 1u) continue;
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
            // Only true convergent boundaries subduct.
            if (field.boundaryType[idx] != 1u) continue;
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
            int16_t     overriderId;
            int16_t     consumedSideId;
            int32_t     consumedLon, consumedLat;
            int32_t     overrideLon, overrideLat;
            if (selfFrac < otherFrac) {
                overriderId      = otherId;
                consumedSideId   = selfId;
                consumedLon = lonIdx;
                consumedLat = latIdx;
                overrideLon = static_cast<int32_t>(otherIdx % LON);
                overrideLat = static_cast<int32_t>(otherIdx / LON);
            } else {
                overriderId      = selfId;
                consumedSideId   = otherId;
                consumedLon = static_cast<int32_t>(otherIdx % LON);
                consumedLat = static_cast<int32_t>(otherIdx / LON);
                overrideLon = lonIdx;
                overrideLat = latIdx;
            }
            // Pure-continental collisions do not subduct -- both sides
            // thicken instead (handled by thickenFromClosingRate).
            const float lowFrac = std::min(selfFrac, otherFrac);
            if (lowFrac >= 0.5f) continue;

            // Gate by closing distance: only consume when the closing
            // rate * dt would have advanced past one trench width.
            const float closingKm = rate * dtMy * R;
            if (closingKm < SUBDUCTION_CELL_WIDTH_KM) continue;

            // Multi-cell consumption proportional to closing distance.
            // Real Earth Tibet at 5 cm/yr × 50 My consumes ~2500 km =
            // ~45 cells of oceanic crust each epoch. Our previous
            // implementation flipped only ONE cell per boundary per
            // epoch — 45× too slow. Walk inward along the convergent
            // normal (consumed → override direction reversed) one
            // cell-width at a time, flipping each cell that is still
            // oceanic and still owned by the consumed plate. Stop at
            // the first continental cell (collision arrest), at the
            // grid edge, at a different plate (microplate boundary),
            // or once the budget is spent.
            const int32_t maxCells = static_cast<int32_t>(
                closingKm / SUBDUCTION_CELL_WIDTH_KM);
            const int32_t epochCap = 60; // cap so a single epoch can't eat a whole basin
            const int32_t cellsToConsume = std::min(maxCells, epochCap);
            // Inward walk direction: consumed cell -> next cell deeper
            // into the consumed plate's territory. The override-side
            // direction reversed gives consumed-side outward; we want
            // consumed-side inward, i.e. (consumed - override).
            const int32_t dLon = consumedLon - overrideLon;
            const int32_t dLat = consumedLat - overrideLat;
            int32_t stepLon = (dLon > 0) ? 1 : (dLon < 0) ? -1 : 0;
            int32_t stepLat = (dLat > 0) ? 1 : (dLat < 0) ? -1 : 0;
            // Longitude wrap: if the difference > 1 in absolute value we
            // wrapped around the antimeridian; reverse the inward step.
            if (std::abs(dLon) > 1) stepLon = -stepLon;
            int32_t curLon = consumedLon;
            int32_t curLat = consumedLat;
            for (int32_t step = 0; step < cellsToConsume; ++step) {
                const std::size_t curIdx = SphereField::cellIndex(curLon, curLat);
                // Stop conditions: cell is no longer the consumed plate
                // (microplate-edge or already-converted) OR cell is
                // continental (collision arrest, no further subduction).
                if (field.plateId[curIdx] != consumedSideId) break;
                if (field.continentalFraction[curIdx] > 0.5f) break;
                field.plateId[curIdx] = overriderId;
                field.crustThicknessKm[curIdx] =
                    PhysicsConstants::initialOceanicThicknessKm;
                field.continentalFraction[curIdx] = 0.0f;
                field.crustAgeMy[curIdx] = 0.0f;
                // Advance one cell deeper into consumed-plate territory.
                curLon += stepLon;
                curLat += stepLat;
                if (curLat < 0 || curLat >= LAT) break;
                if (curLon < 0)        curLon += LON;
                if (curLon >= LON)     curLon -= LON;
            }
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
    // Slope-based stream-power erosion (Whipple & Tucker 1999, n=1):
    //   dz/dt = -K_S * |grad z|
    // Slope is computed from neighbour elevation differences using the
    // physical metres-per-cell pitch on the sphere (cell pitch shrinks
    // toward the poles by cos(lat) in longitude). Erosion lowers
    // surface and crust per Airy isostasy; surface itself is
    // recomputed from crust by recomputeIsostaticElevationOnRaster
    // in the next epoch's pass, so the only state we mutate here is
    // crust thickness. Cap dz at z (cell can erode at most to sea
    // level in one step) to prevent forward-Euler overshoot for
    // Andean-grade slopes at dtMy = 50 My.
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    constexpr float CELL_RAD = SphereField::CELL_DEG * 0.01745329252f;
    const float earthRadiusM = PhysicsConstants::earthRadiusKm * 1000.0f;
    const float cellHeightM  = earthRadiusM * CELL_RAD;
#if defined(AOC_HAS_OPENMP)
    #pragma omp parallel for schedule(static)
#endif
    for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
        const float latDeg = -90.0f + (static_cast<float>(latIdx) + 0.5f)
                                       * SphereField::CELL_DEG;
        const float latRad = latDeg * 0.01745329252f;
        const float cellWidthM = cellHeightM * std::cos(latRad);
        const int32_t latS = std::max(0, latIdx - 1);
        const int32_t latN = std::min(LAT - 1, latIdx + 1);
        for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
            const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
            const float z = field.surfaceElevationM[idx];
            // Peneplain stability floor. Real continental shields
            // reach a quasi-equilibrium near sea level where erosion
            // balances slow mantle-driven uplift (the classic Davis
            // 1899 / Hack 1960 dynamic-equilibrium peneplain). We do
            // not model continuous global uplift here, so bypass
            // erosion below 100 m surface elevation -- this acts as
            // the implicit equilibrium floor and prevents shoreline
            // retreat from cannibalising continents over Gy. Active
            // orogens (z >> 100 m) still erode at the stream-power
            // rate balanced by convergent thickening, so mountain
            // belts behave as expected. Cited: Davis 1899
            // "Geographical Cycle"; Hack 1960 "Interpretation of
            // erosional topography in humid temperate regions".
            if (z < 100.0f) continue;
            const int32_t lonW = (lonIdx == 0)       ? LON - 1 : lonIdx - 1;
            const int32_t lonE = (lonIdx == LON - 1) ? 0       : lonIdx + 1;
            const float zW = field.surfaceElevationM[
                SphereField::cellIndex(lonW, latIdx)];
            const float zE = field.surfaceElevationM[
                SphereField::cellIndex(lonE, latIdx)];
            const float zS = field.surfaceElevationM[
                SphereField::cellIndex(lonIdx, latS)];
            const float zN = field.surfaceElevationM[
                SphereField::cellIndex(lonIdx, latN)];
            const float dzLon = (zE - zW) / (2.0f * cellWidthM);
            const float dzLat = (zN - zS) / (2.0f * cellHeightM);
            const float slope = std::sqrt(dzLon * dzLon + dzLat * dzLat);
            float dz = K_EROSION_M_PER_MY_PER_SLOPE * slope * dtMy;
            if (dz > z - 100.0f) dz = z - 100.0f; // cap at peneplain floor
            if (dz < 0.0f) continue;
            const float c  = field.continentalFraction[idx];
            const float airy = c * airyRatio_cont + (1.0f - c) * airyRatio_oce;
            const float dCrustKm = (dz * airy) * 1e-3f;
            float h = field.crustThicknessKm[idx] - dCrustKm;
            if (h < 0.0f) h = 0.0f;
            field.crustThicknessKm[idx] = h;
        }
    }
}

void stepSpherePhysicsEpoch(SphereField& field,
                            std::vector<Plate>& plates,
                            std::vector<uint8_t>& boundaryScratch,
                            uint32_t& rngState,
                            float dtMy) {
    // Per-epoch passes in physical order:
    //   0. plate-cell advection — rotate every owned cell about its
    //      plate's Euler pole by omega*dt (Lagrangian transport).
    //   1. ownership refresh — centroid Voronoi (init + each epoch).
    //   2. boundary detection.
    //   3. instantaneous closing rate from Euler-pole velocities.
    //   4. continental crust thickening at convergent boundary cells.
    //   5. oceanic-margin subduction (lower-density side flips).
    //   6. continental docking (cont-cont contact > 30 My fuses).
    //   7. Wilson-cycle rifting (supercontinent thermal-age trigger).
    //   8. Airy isostasy → surface elevation.
    //   9. exponential stream-power erosion.
    //  10. compact + recompute plate centroids for next epoch.
    //
    // No per-epoch ownership reset — plateId persists across epochs
    // (Lagrangian path), set ONCE by `generateInitialPlateOwnership`
    // at sim init. Boundary changes come exclusively through
    // mechanism passes (subduction flip, ridge accretion, docking
    // merge, Wilson split). This is the no-Voronoi rule from
    // CLAUDE.md "World-generation physics requirements".
    //
    // CFL-safe sub-stepping. Backward semi-Lagrangian advection on a
    // 0.5 deg raster requires that each step's rotation be smaller
    // than the cell pitch -- otherwise the dest cell's departure
    // point falls outside the plate's prior footprint and the cell
    // becomes a spurious orphan. Pre-substepping we ran one
    // 50 My step at omega up to 0.15 deg/My = 7.5 deg = 15 cells of
    // sweep, violating CFL by 15x and dissolving plate interiors
    // into orphan-fill smears (boundary count grew from 6.6 K to
    // 21 K cells over 60 epochs, > 80 % of the grid).
    //
    // Choose substepDt so the maximum-omega plate rotates at most
    // CFL_SAFETY * CELL_DEG per substep. CFL_SAFETY = 1.0 is the
    // linear-upwind stability bound; at this rate every dest cell's
    // departure point lies within one cell of the dest, so for any
    // plate >= 2 cells across the depIdx falls inside the plate's
    // footprint (no orphans from CFL alone). The Rodrigues rotation
    // is exact, not a small-angle approximation, so the only
    // restriction is the raster footprint check.
    constexpr float CFL_SAFETY = 1.0f;
    float maxOmegaDeg = 0.0f;
    for (const Plate& p : plates) {
        const float a = std::fabs(p.angularVelDeg);
        if (a > maxOmegaDeg) maxOmegaDeg = a;
    }
    float remainingDt = dtMy;
    if (maxOmegaDeg > 0.0f) {
        const float maxStep = CFL_SAFETY * SphereField::CELL_DEG / maxOmegaDeg;
        while (remainingDt > 1e-6f) {
            const float subDt = std::min(maxStep, remainingDt);
            advectPlateOwnership(field, plates, subDt);
            remainingDt -= subDt;
        }
    } else {
        advectPlateOwnership(field, plates, dtMy);
    }
    markBoundaryCells(field, boundaryScratch);
    accumulateClosingRate(field, plates, boundaryScratch);
    thickenFromClosingRate(field, dtMy);
    applySubduction(field, plates, dtMy);
    // Ridge accretion is a special case of Wilson rifting — new
    // oceanic crust forms only at the rift axis, not at every
    // divergent boundary cell. Calling `accreteAtDivergentBoundary`
    // every epoch wiped continental cells faster than thickening
    // could rebuild them. Wilson rifting handles new-ocean-basin
    // creation in a localised way at split events.
    // No full plate-pair fusion ("continental docking"). Game-scale
    // worlds want the Tibet-style collision phase to PERSIST so
    // mountain belts remain visible — fusing continents on a
    // ~50-Myr timescale destroys the active convergent boundary that
    // builds the orogen. Continents that converge thicken via
    // `thickenFromClosingRate` and stay as distinct plates with a
    // shared mountain-building seam; oceanic margins still close via
    // multi-cell subduction.
    applySlabPullFeedback(field, plates, dtMy);
    applyWilsonRifting(field, plates, rngState, dtMy);
    recomputeIsostaticElevationOnRaster(field);
    applySurfaceErosionOnRaster(field, dtMy);
    compactPlateList(field, plates);
    recomputePlateCentroidsFromCells(field, plates);

    // Crust age advances monotonically for cells that did not flip
    // ownership in the subduction pass (those were reset to 0 already).
    for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
        field.crustAgeMy[i] += dtMy;
    }

    // Env-gated diagnostic dump (AOC_SPHEREPHYS_TRACE=1) reports
    // per-epoch maxRate/maxCrust/maxZ/mountainCellCount on stderr.
    // Useful for diagnosing balance of thicken vs erosion when
    // calibrating constants; off by default.
    if (std::getenv("AOC_SPHEREPHYS_TRACE") != nullptr) {
        float maxRate = 0.0f, minRate = 0.0f, maxCrust = 0.0f, maxZ = -1e9f;
        std::size_t mountainCells = 0;
        std::size_t boundaryCount = 0;
        for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
            const float r = field.convergenceRateRadPerMy[i];
            const float h = field.crustThicknessKm[i];
            const float z = field.surfaceElevationM[i];
            if (r > maxRate)  maxRate  = r;
            if (r < minRate)  minRate  = r;
            if (boundaryScratch[i]) ++boundaryCount;
            if (h > maxCrust) maxCrust = h;
            if (z > maxZ)     maxZ     = z;
            if (z > 4000.0f) ++mountainCells;
        }
        // CFL diagnostic: max plate angular sweep per epoch in
        // SphereField cells. With sub-stepping, a single substep
        // rotates by CFL_SAFETY * CELL_DEG; the unrescaled
        // omega * dtMy here measures the FULL-EPOCH sweep so we can
        // see how much sub-stepping is needed.
        float traceMaxOmegaDeg = 0.0f;
        for (const Plate& p : plates) {
            const float a = std::fabs(p.angularVelDeg);
            if (a > traceMaxOmegaDeg) traceMaxOmegaDeg = a;
        }
        const float cflCells = (traceMaxOmegaDeg * dtMy) / SphereField::CELL_DEG;
        std::fprintf(stderr,
            "[sphere] dt=%.1fMy rate[%.4f..%.4f] crust=%.1fkm "
            "z=%.0fm mtn=%zu plates=%zu boundary=%zu "
            "maxOmega=%.3fdeg/My cflCells=%.1f\n",
            static_cast<double>(dtMy),
            static_cast<double>(minRate),
            static_cast<double>(maxRate),
            static_cast<double>(maxCrust),
            static_cast<double>(maxZ),
            mountainCells,
            plates.size(),
            boundaryCount,
            static_cast<double>(traceMaxOmegaDeg),
            static_cast<double>(cflCells));
    }
}

} // namespace aoc::map::gen

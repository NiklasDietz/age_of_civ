#include "aoc/map/gen/SphereFieldPhysics.hpp"

#include "aoc/map/gen/PlatePhysics.hpp"
#include "aoc/map/gen/PlateReference.hpp"

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
inline constexpr float K_THICKEN_KM_PER_RADMY = 76.5f;

// Bulk erosion coefficient per My per metre of elevation above sea
// level. Derived from the stream-power incision model (Whipple &
// Tucker 1999) calibrated to the global continental denudation
// median from Portenga & Bierman 2011 ("Understanding Earth's
// eroding surface with 10Be", GSA Today 21, 8) — a compilation of
// 1599 published basin-scale 10Be cosmogenic erosion rates spanning
// all continents and elevation classes:
//   median basin denudation 17 m/My at median basin elevation 500 m
//   → K = E / z = 17 / 500 ≈ 0.034 /My.
// Wilkinson & McElroy 2007 reported 60 m/My but for "uplifted
// continental regions" only, biased toward active orogens; Portenga
// & Bierman captures the full continent average (cratons + uplands).
// Each 0.5° cell (~55 km) averages many basins, so the global K is
// the right scale.
// 2026-05-07 P6.4 recalibration: 0.06 → 0.034.
inline constexpr float K_EROSION_PER_MY = 0.034f;

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
            // Convergent boundary cells already get stored rate > 0;
            // skip them so we do not undo the thicken pass.
            if (field.convergenceRateRadPerMy[idx] > 0.0f) continue;
            // Detect divergent boundary by neighbour mismatch only:
            // any 4-neighbour with a different plate id qualifies as a
            // boundary cell, and (since rate isn't positive) it must
            // be divergent or transform. Both regenerate fresh oceanic
            // basalt at slow spread (transform faults still leak basalt
            // along bend-line segments — Müller 2008).
            const int32_t lonW = (lonIdx == 0)       ? LON - 1 : lonIdx - 1;
            const int32_t lonE = (lonIdx == LON - 1) ? 0       : lonIdx + 1;
            const int32_t latS = (latIdx == 0)       ? 0       : latIdx - 1;
            const int32_t latN = (latIdx == LAT - 1) ? LAT - 1 : latIdx + 1;
            const int16_t nW = field.plateId[SphereField::cellIndex(lonW, latIdx)];
            const int16_t nE = field.plateId[SphereField::cellIndex(lonE, latIdx)];
            const int16_t nS = field.plateId[SphereField::cellIndex(lonIdx, latS)];
            const int16_t nN = field.plateId[SphereField::cellIndex(lonIdx, latN)];
            const bool isBoundary = (nW != selfId || nE != selfId
                                  || nS != selfId || nN != selfId);
            if (!isBoundary) continue;
            // Reset to fresh oceanic crust. Owning plate keeps the cell.
            field.crustThicknessKm[idx] =
                PhysicsConstants::initialOceanicThicknessKm;
            field.continentalFraction[idx] = 0.0f;
            field.crustAgeMy[idx] = 0.0f;
        }
    }
}

// ---------------------------------------------------------------------------
// Continental docking
// ---------------------------------------------------------------------------
//
// Real Earth's mountain-building events are continental docking: the
// India-Asia collision (50 Ma+) is the textbook case. When buoyant
// continental crust meets buoyant continental crust at a convergent
// boundary, neither side subducts — instead the smaller block accretes
// to the larger one along a suture. The merged plate's interior
// thickens (Tibet plateau) and the boundary is consumed.
//
// This implementation tracks the cumulative epoch-time spent in
// continent-continent contact for each plate pair. Once that contact
// time exceeds DOCKING_MY_THRESHOLD (Lithgow-Bertelloni & Richards
// 1998 give a few-tens-of-Myr timescale for accretion completion),
// the smaller continental plate is merged into the larger one by
// rewriting `field.plateId`. The pair-age tracker is keyed by
// (smallId * MAX_PLATES + largeId) so identical pairs don't double-
// count.
inline constexpr float DOCKING_MY_THRESHOLD = 30.0f;
inline constexpr int32_t DOCKING_MAX_PLATES = 1024;

void applyContinentalDocking(SphereField& field,
                             std::vector<Plate>& plates,
                             std::vector<float>& contactAgeByPlatePair,
                             float dtMy) {
    if (plates.size() < 2) return;
    const std::size_t N = plates.size();
    if (N > DOCKING_MAX_PLATES) return; // Defensive: tracker fixed-size.

    // Accumulate per-pair contact area (count of cells) where both
    // sides are continental. We resize-or-grow the tracker if plate
    // count grew since last call; new pairs start at 0 contact age.
    const std::size_t pairCount = DOCKING_MAX_PLATES * DOCKING_MAX_PLATES;
    if (contactAgeByPlatePair.size() < pairCount) {
        contactAgeByPlatePair.assign(pairCount, 0.0f);
    }

    std::vector<int32_t> contactCells(pairCount, 0);
    std::vector<float>   sumA(N, 0.0f); // per-plate continental cell count

    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
        for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
            const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
            const int16_t selfId = field.plateId[idx];
            if (selfId < 0) continue;
            const float selfFrac = field.continentalFraction[idx];
            if (selfFrac > 0.5f) {
                sumA[static_cast<std::size_t>(selfId)] += 1.0f;
            }
            const float rate = field.convergenceRateRadPerMy[idx];
            if (rate <= 0.0f) continue;
            // Identify a differing neighbour, prefer N4.
            const int32_t lonE = (lonIdx == LON - 1) ? 0 : lonIdx + 1;
            const int32_t latN = (latIdx == LAT - 1) ? LAT - 1 : latIdx + 1;
            const std::size_t idxE = SphereField::cellIndex(lonE, latIdx);
            const std::size_t idxN = SphereField::cellIndex(lonIdx, latN);
            int16_t otherId = -1;
            std::size_t otherIdx = idx;
            const int16_t nE = field.plateId[idxE];
            const int16_t nN = field.plateId[idxN];
            if (nE != selfId && nE >= 0)      { otherId = nE; otherIdx = idxE; }
            else if (nN != selfId && nN >= 0) { otherId = nN; otherIdx = idxN; }
            if (otherId < 0) continue;
            const float otherFrac = field.continentalFraction[otherIdx];
            if (selfFrac < 0.5f || otherFrac < 0.5f) continue;
            // Both sides continental: this is a docking-eligible contact.
            int16_t a = std::min(selfId, otherId);
            int16_t b = std::max(selfId, otherId);
            const std::size_t key =
                static_cast<std::size_t>(a) * DOCKING_MAX_PLATES
                + static_cast<std::size_t>(b);
            ++contactCells[key];
        }
    }

    // For each plate pair that had any contact this epoch, advance
    // its contact age. Pairs without contact decay (slow forgetting:
    // half-life ~ DOCKING_MY_THRESHOLD so re-collision triggers fast
    // but old contact doesn't linger forever).
    const float decay = std::exp(-dtMy / DOCKING_MY_THRESHOLD);
    for (std::size_t key = 0; key < pairCount; ++key) {
        if (contactCells[key] > 0) {
            contactAgeByPlatePair[key] += dtMy;
        } else if (contactAgeByPlatePair[key] > 0.0f) {
            contactAgeByPlatePair[key] *= decay;
        }
    }

    // Pick mergers: any pair whose contact age exceeded the threshold
    // and which has an actual continental contact this epoch. Merge
    // the smaller plate into the larger.
    for (std::size_t key = 0; key < pairCount; ++key) {
        if (contactCells[key] == 0) continue;
        if (contactAgeByPlatePair[key] < DOCKING_MY_THRESHOLD) continue;
        const std::size_t a = key / DOCKING_MAX_PLATES;
        const std::size_t b = key % DOCKING_MAX_PLATES;
        if (a >= N || b >= N) continue;
        // Merge smaller continental area into the larger one.
        const std::size_t loser  = (sumA[a] < sumA[b]) ? a : b;
        const std::size_t winner = (sumA[a] < sumA[b]) ? b : a;
        if (sumA[loser] <= 0.0f) continue;
        const int16_t loserId  = static_cast<int16_t>(loser);
        const int16_t winnerId = static_cast<int16_t>(winner);
        for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
            if (field.plateId[i] == loserId) {
                field.plateId[i] = winnerId;
            }
        }
        // Reset trackers for both merged keys so we do not re-merge.
        contactAgeByPlatePair[key] = 0.0f;
        // sumA used only this call — no per-call mutation needed beyond
        // the merge itself (cells are reassigned in-field).
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

        // Spawn fresh plate inheriting parent's properties + perturbed
        // Euler pole (Müller 2022: rifted children diverge by ~10-20°
        // pole offset).
        Plate child = plates[i];
        const float poleOffsetDeg = (xorshift01(rngState) * 30.0f) - 15.0f;
        child.eulerPoleLatDeg = std::clamp(
            child.eulerPoleLatDeg + poleOffsetDeg, -89.0f, 89.0f);
        child.eulerPoleLonDeg += poleOffsetDeg;
        child.angularVelDeg *= (xorshift01(rngState) > 0.5f ? -1.0f : 1.0f);
        plates.push_back(child);
        const int16_t childId = static_cast<int16_t>(plates.size() - 1);

        // Reassign cells on the (negative-side) of the rift plane.
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
    // Exact integration of dz/dt = -K*z over interval dtMy:
    //   z(t+dt) = z(t) * exp(-K*dt)  =>  dz_fraction = 1 - exp(-K*dt)
    // Forward-Euler (`dz = K*z*dt`) is unstable when K*dt >= 1 — at
    // dtMy=25 My with K=0.06/My (K*dt=1.5) the explicit step over-
    // shoots and wipes out all elevation in a single epoch. The
    // exponential form is the closed-form solution of the linear
    // decay ODE, unconditionally stable, and approaches the explicit
    // form K*z*dt for small K*dt. No fudge — textbook fix.
    const float erosionFraction = 1.0f - std::exp(-K_EROSION_PER_MY * dtMy);
#if defined(AOC_HAS_OPENMP)
    #pragma omp parallel for schedule(static)
#endif
    for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
        const float z = field.surfaceElevationM[i];
        if (z <= 0.0f) continue;
        const float dz = z * erosionFraction; // metres surface lowering
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
                            std::vector<float>& contactAgeByPlatePair,
                            uint32_t& rngState,
                            float dtMy) {
    // Per-epoch passes in physical order:
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
    markBoundaryCells(field, boundaryScratch);
    accumulateClosingRate(field, plates, boundaryScratch);
    thickenFromClosingRate(field, dtMy);
    applySubduction(field, plates, dtMy);
    accreteAtDivergentBoundary(field, dtMy);
    applyContinentalDocking(field, plates, contactAgeByPlatePair, dtMy);
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
        float maxRate = 0.0f, maxCrust = 0.0f, maxZ = -1e9f;
        std::size_t mountainCells = 0;
        for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
            const float r = field.convergenceRateRadPerMy[i];
            const float h = field.crustThicknessKm[i];
            const float z = field.surfaceElevationM[i];
            if (r > maxRate)  maxRate  = r;
            if (h > maxCrust) maxCrust = h;
            if (z > maxZ)     maxZ     = z;
            if (z > 4000.0f) ++mountainCells;
        }
        std::fprintf(stderr,
            "[sphere] dt=%.1fMy maxRate=%.5frad/My maxCrust=%5.1fkm "
            "maxZ=%6.0fm mtn>4km=%zu plates=%zu\n",
            static_cast<double>(dtMy),
            static_cast<double>(maxRate),
            static_cast<double>(maxCrust),
            static_cast<double>(maxZ),
            mountainCells,
            plates.size());
    }
}

} // namespace aoc::map::gen

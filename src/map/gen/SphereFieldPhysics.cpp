#include "aoc/map/gen/SphereFieldPhysics.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/map/gen/PlatePhysics.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

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
        LOG_WARN("SphereFieldPhysics: %s called with empty plates -- skipping",
                 __func__);
        std::fill(field.plateId.begin(), field.plateId.end(),
                  static_cast<int16_t>(-1));
        return;
    }
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    const std::size_t totalCells = SphereField::CELL_COUNT;
    const std::size_t P = plates.size();

    std::fill(field.plateId.begin(), field.plateId.end(),
              static_cast<int16_t>(-1));

    // Deterministic per-call PRNG (SplitMix64) for both the
    // per-round shuffle and the per-claim random frontier pick.
    uint64_t rngState = seed ^ 0x9E3779B97F4A7C15ULL;
    if (rngState == 0) rngState = 0xDEADBEEFCAFEBABEULL;
    auto next64 = [&]() -> uint64_t {
        uint64_t x = (rngState += 0x9E3779B97F4A7C15ULL);
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        return x ^ (x >> 31);
    };
    auto nextRange = [&](std::size_t n) -> std::size_t {
        if (n <= 1) return 0;
        return static_cast<std::size_t>(next64() % static_cast<uint64_t>(n));
    };

    // Per-plate frontier vectors. Area-balanced region growing: each
    // round, the plate with the SMALLEST accumulated surface area
    // (sum of cos(lat) over its claimed cells) pops one random cell
    // from its frontier. This balances plates by SURFACE AREA on the
    // sphere, not by raw cell count. Without the area weighting,
    // polar-seeded plates sprawl east-west across the entire pole
    // because each cell at lat 80° is geographically tiny (~10 km
    // E-W) but counts the same as a mid-lat cell (~55 km E-W) in a
    // pure round-robin scheme — producing latitudinal-band plate
    // shapes (audit on seed 42 initial-cut grid showed clearly
    // banded plates spanning all longitudes at high lat). With
    // area-balancing, polar plates accumulate small per-cell area,
    // so they pop more often (catching up by cell count) until
    // their TOTAL surface area matches mid-lat plates — naturally
    // pushing them out of the polar cap into mid-latitudes once the
    // cap is filled.
    std::vector<std::vector<std::size_t>> frontiers(P);
    std::vector<double> claimedArea(P, 0.0);

    constexpr double DEG2RAD_D = 0.01745329252;
    auto cellAreaWeight = [](int32_t latIdx) -> double {
        const double latDeg =
            (static_cast<double>(latIdx) + 0.5)
            * static_cast<double>(SphereField::CELL_DEG) - 90.0;
        return std::cos(latDeg * DEG2RAD_D);
    };

    auto pushNeighbours = [&](std::size_t cellIdx, std::size_t plateIdx) {
        const int32_t latIdx = static_cast<int32_t>(
            cellIdx / static_cast<std::size_t>(LON));
        const int32_t lonIdx = static_cast<int32_t>(
            cellIdx % static_cast<std::size_t>(LON));
        const int32_t lonW = (lonIdx == 0)       ? LON - 1 : lonIdx - 1;
        const int32_t lonE = (lonIdx == LON - 1) ? 0       : lonIdx + 1;
        const int32_t latS = std::max(0, latIdx - 1);
        const int32_t latN = std::min(LAT - 1, latIdx + 1);
        const std::size_t nbrs[4] = {
            SphereField::cellIndex(lonW, latIdx),
            SphereField::cellIndex(lonE, latIdx),
            SphereField::cellIndex(lonIdx, latS),
            SphereField::cellIndex(lonIdx, latN),
        };
        for (int32_t k = 0; k < 4; ++k) {
            if (field.plateId[nbrs[k]] < 0) {
                frontiers[plateIdx].push_back(nbrs[k]);
            }
        }
    };

    // Seed each plate at its (latDeg, lonDeg) cratonic centroid.
    std::size_t claimed = 0;
    for (std::size_t i = 0; i < P; ++i) {
        const SphereField::CellCoord c =
            SphereField::locate(plates[i].latDeg, plates[i].lonDeg);
        const std::size_t idx = SphereField::cellIndex(c.lonIdx, c.latIdx);
        if (field.plateId[idx] >= 0) continue; // seed collided with prior
        field.plateId[idx] = static_cast<int16_t>(i);
        claimedArea[i] = cellAreaWeight(c.latIdx);
        pushNeighbours(idx, i);
        ++claimed;
    }

    while (claimed < totalCells) {
        // Pick plate with smallest claimedArea AND non-empty frontier.
        // Linear scan is O(P) — fine for P ~ 10-20 plates and
        // amortised across 259200 cell claims (~2.5M scan ops total).
        std::size_t pick = P;
        double minArea = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < P; ++i) {
            if (frontiers[i].empty()) continue;
            if (claimedArea[i] < minArea) {
                minArea = claimedArea[i];
                pick = i;
            }
        }
        if (pick == P) break; // every frontier empty
        // Pop random cell from picked plate's frontier; skip stale
        // entries (cells already claimed by a neighbour) until claim
        // or frontier empties.
        std::vector<std::size_t>& f = frontiers[pick];
        while (!f.empty()) {
            const std::size_t pickPos = nextRange(f.size());
            const std::size_t cellIdx = f[pickPos];
            f[pickPos] = f.back();
            f.pop_back();
            if (field.plateId[cellIdx] >= 0) continue;
            field.plateId[cellIdx] = static_cast<int16_t>(pick);
            const int32_t latIdx = static_cast<int32_t>(
                cellIdx / static_cast<std::size_t>(LON));
            claimedArea[pick] += cellAreaWeight(latIdx);
            pushNeighbours(cellIdx, pick);
            ++claimed;
            break;
        }
    }
}

void recomputePlateCentroidsFromCells(SphereField& field,
                                      std::vector<Plate>& plates) {
    if (plates.empty()) {
        LOG_WARN("SphereFieldPhysics: %s called with empty plates -- skipping",
                 __func__);
        return;
    }
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
    constexpr double RAD2DEG = 57.29577951;
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
// At every boundary cell whose instantaneous closing rate is below
// (more negative than) the slow-spreading-ridge threshold AND which
// is already oceanic in character, reset the cell state to fresh
// oceanic crust: thickness 7 km (Turcotte & Schubert 2014 ch. 2
// oceanic mean), continental fraction 0, age 0.
//
// `accumulateClosingRate` writes the SIGNED closing rate (positive
// convergent, negative divergent), so the divergent test is a
// straight `closing < threshold`.
//
// The continental-fraction gate prevents continental rift zones
// (East African Rift, Red Sea pre-spreading) from being instantly
// converted to oceanic crust the moment their closing rate goes
// divergent. Continental rifting is handled by `applyWilsonRifting`
// on its own ~150-200 My thermal-blanketing timescale; ridge
// accretion is the COMPLEMENT — it operates only on already-oceanic
// crust whose plates are pulling apart, modelling the ongoing
// extrusion of basalt at a true mid-ocean ridge.
void accreteAtDivergentBoundary(SphereField& field, float dtMy) {
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    (void)dtMy; // Reserved for time-dependent accretion-rate models.
    // Stein & Stein 1992 give a slow-spreading-ridge minimum of
    // ~1 cm/yr full-rate ≈ 0.0008 rad/My on the sphere; below this
    // the boundary is effectively quiescent / transform.
    constexpr float DIVERGENT_RATE_THRESHOLD = -0.0008f;
    // Cells with continentalFraction below this are predominantly
    // oceanic basalt; above it the crust carries continental block
    // material and Wilson-cycle rifting (separate pass) governs.
    constexpr float OCEANIC_CRUST_GATE = 0.30f;
    for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
        for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
            const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
            const int16_t selfId = field.plateId[idx];
            if (selfId < 0) continue;
            const float closing = field.convergenceRateRadPerMy[idx];
            if (closing >= DIVERGENT_RATE_THRESHOLD) continue;
            if (field.continentalFraction[idx] >= OCEANIC_CRUST_GATE) continue;
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
    if (plates.empty()) {
        LOG_WARN("SphereFieldPhysics: %s called with empty plates -- skipping",
                 __func__);
        return;
    }
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

    // Δω/ω per epoch capped to the LONG-TERM plate-motion stability
    // envelope from Tetley et al. 2019 ("Constraining absolute plate
    // motions since the Triassic", JGR Solid Earth 124) and Mueller
    // et al. 2008. On geological timescales (>50 Myr) plate-motion
    // vectors are stable to within ~2-4 % per Myr, NOT the 10 %/Myr
    // Quaternary short-term-variability figure that mostly captures
    // mantle micro-reorganisations on sub-Myr scales. Using 10 % per
    // 50-Myr epoch compounds to ~300x growth over a 60-epoch / 3-Gy
    // simulation, which drives every plate with active subduction
    // straight to the omega cap and produces ~8 full revolutions
    // (visible as latitudinal banding on the rendered globe) -- one
    // order of magnitude faster than real plates have moved over
    // Phanerozoic time. 0.03 per 50-Myr epoch compounds to ~5.5x
    // growth at most, in line with Tetley's long-term envelope.
    constexpr float MAX_FRAC_PER_EPOCH = 0.03f;
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
    // ~1.0 deg/My. Cap held at the MEDIAN, not the peak. Pacific's
    // 1.0 deg/My is a short-term modern figure that a plate cannot
    // sustain across geological time (Phanerozoic plate tracks
    // average 1-3 cm/yr over 100+ Myr = 0.05-0.15 deg/My). Sustained
    // motion at the modern peak compounds over the 3-Gy default sim
    // to ~8 full revolutions, far in excess of Earth's observed
    // ~0.25-0.75 revolutions per 0.5 Gy. The cap also bounds
    // backward-sample stretching artefacts on the raster: edge cells
    // freeze (incumbent's R^-1 walks off plate footprint) while
    // interior cells advect, so cumulative rotation determines how
    // much state can stretch into latitudinal bands. 0.15 deg/My x
    // 60 epochs x 50 My = 450 deg ~ 1.25 revolutions, matching the
    // long-term continental-track envelope.
    constexpr float MAX_ABS_OMEGA_DEG_PER_MY = 0.15f;

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

// Areal-fraction threshold for "supercontinent" classification
// (continental cells / global cells). Anderson 2007 "New Theory
// of the Earth" Table 15.1 lists Pangaean-class assemblies at
// ~25-30 % of total continental crust massed together — about
// 19-22 % of the global surface area (continental crust covers
// ~29 % of Earth's surface). 0.20 is the Pangaean-onset floor.
inline constexpr float SUPERCONTINENT_FRACTION = 0.20f;
inline constexpr float RIFT_THRESHOLD_MY       = 150.0f;
// Probability ramp width above RIFT_THRESHOLD_MY: rift probability
// reaches 1.0 at thermal age = threshold + RIFT_RAMP_MY. Vérard
// et al. 2015 ("Geodynamics of the 3 Ga old lithosphere") report
// ~80-150 My from rift initiation to full mantle-driven breakup
// for an Archean-class supercontinent; 100 My sits at the centre
// of that envelope.
inline constexpr float RIFT_RAMP_MY            = 100.0f;
// Hard mechanical cap: no tectonic plate can physically span > 40 % of
// a sphere — Earth's largest plate (Pacific) is ~20 %. Once a plate
// exceeds this fraction of total sphere cells, rift is forced regardless
// of thermal age. Models flexural / gravitational instability that
// prevented any second Pangaea from ever forming.
// Cite: Gurnis 1988 ("Large-scale mantle convection and the aggregation
// and dispersal of supercontinents"); Jordan 1981 (mechanical
// constraints on lithospheric plate sizes).
inline constexpr float FORCE_RIFT_TOTAL_FRACTION = 0.40f;

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
    if (plates.empty()) {
        LOG_WARN("SphereFieldPhysics: %s called with empty plates -- skipping",
                 __func__);
        return 0;
    }
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

    // Thermal-age update + per-plate mean. Decay factor is constant
    // across all cells in this epoch; precompute outside the hot loop.
    const float thermalDecayFactor = std::exp(-dtMy / RIFT_THRESHOLD_MY);
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
            field.thermalAgeMy[i] *= thermalDecayFactor;
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
        const float totalFrac = static_cast<float>(totalCells[i]) / globeCells;
        const bool forceRift = (totalFrac >= FORCE_RIFT_TOTAL_FRACTION);
        if (forceRift) {
            // Forced split for over-large plates — bypass thermal age.
            // Minimum cell count still needed to form a meaningful child.
            if (totalCells[i] < 4) continue;
        } else {
            if (thermalCount[i] < 4) continue; // Plate too small to rift.
            const float meanThermal = static_cast<float>(
                thermalSum[i] / static_cast<double>(thermalCount[i]));
            if (meanThermal < RIFT_THRESHOLD_MY) continue;
            const float over = meanThermal - RIFT_THRESHOLD_MY;
            const float prob = std::min(1.0f, over / RIFT_RAMP_MY);
            if (xorshift01(rngState) > prob) continue;
        }

        // PCA on cell sphere positions to find longest axis. Single
        // pass over the whole field collects the indices + unit
        // vectors of plate i's cells; subsequent PCA covariance and
        // reassign passes operate on the pre-collected vector only.
        constexpr double DEG2RAD = 0.01745329252;
        struct CellVec {
            std::size_t cellIdx;
            double cx, cy, cz;
        };
        std::vector<CellVec> plateCells;
        plateCells.reserve(static_cast<std::size_t>(totalCells[i]));
        double mx = 0.0, my = 0.0, mz = 0.0;
        for (std::size_t cell = 0; cell < SphereField::CELL_COUNT; ++cell) {
            if (field.plateId[cell] != static_cast<int16_t>(i)) continue;
            const int32_t latIdx = static_cast<int32_t>(cell / SphereField::LON_CELLS);
            const int32_t lonIdx = static_cast<int32_t>(cell % SphereField::LON_CELLS);
            const LatLon p = SphereField::cellCenter(lonIdx, latIdx);
            const double latR = static_cast<double>(p.latDeg) * DEG2RAD;
            const double lonR = static_cast<double>(p.lonDeg) * DEG2RAD;
            const double cosLat = std::cos(latR);
            const double cxv = cosLat * std::cos(lonR);
            const double cyv = cosLat * std::sin(lonR);
            const double czv = std::sin(latR);
            plateCells.push_back({cell, cxv, cyv, czv});
            mx += cxv; my += cyv; mz += czv;
        }
        if (plateCells.empty()) continue;
        const double inv = 1.0 / static_cast<double>(plateCells.size());
        mx *= inv; my *= inv; mz *= inv;

        // Compute covariance to extract principal axis. With ~hundreds
        // of cells the 3x3 power-iteration converges in <10 steps.
        double cxx=0, cyy=0, czz=0, cxy=0, cxz=0, cyz=0;
        for (const CellVec& v : plateCells) {
            const double dx = v.cx - mx;
            const double dy = v.cy - my;
            const double dz = v.cz - mz;
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
        // The int16_t plateId storage caps total plates at INT16_MAX
        // (32767). Plate count is bounded much lower in practice
        // (MapGenerator MAX_PLATE_CAP < 255, see static_assert in
        // MapGenerator.cpp) but the assert here documents the
        // contract at the SphereField boundary.
        assert(plates.size() <= 32767u
               && "plate count exceeds int16_t capacity");
        const int32_t childIdWide = static_cast<int32_t>(plates.size() - 1);
        const int16_t childId     = static_cast<int16_t>(childIdWide);

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
        const double nrmMag = std::sqrt(
            nrm_x * nrm_x + nrm_y * nrm_y + nrm_z * nrm_z);
        const double invNrmMag = (nrmMag > 1e-9) ? 1.0 / nrmMag : 0.0;
        for (const CellVec& v : plateCells) {
            const std::size_t cell = v.cellIdx;
            const double dot = (v.cx-mx)*nrm_x + (v.cy-my)*nrm_y + (v.cz-mz)*nrm_z;
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
            if (invNrmMag > 0.0) {
                const double axisProj =
                    (v.cx * nrm_x + v.cy * nrm_y + v.cz * nrm_z) * invNrmMag;
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


void mergePlatesBatch(SphereField& field,
                      std::vector<Plate>& plates,
                      const std::vector<std::pair<std::size_t,
                                                  std::size_t>>& pairs) {
    if (pairs.empty() || plates.empty()) return;
    const std::size_t N = plates.size();

    // Union-find over plate indices. Each request (survivor, absorbed)
    // unions the two sets, with the survivor's chosen as the root so
    // the caller's intent (which plate's metadata is authoritative) is
    // respected when chains form.
    std::vector<std::size_t> parent(N);
    for (std::size_t i = 0; i < N; ++i) parent[i] = i;
    auto findRoot = [&](std::size_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]]; // path compression
            x = parent[x];
        }
        return x;
    };
    for (const auto& pr : pairs) {
        if (pr.first >= N || pr.second >= N || pr.first == pr.second) continue;
        const std::size_t rs = findRoot(pr.first);
        const std::size_t ra = findRoot(pr.second);
        if (rs == ra) continue;
        // Survivor (pr.first)'s root wins. If the absorbed root differs
        // we point its parent at the survivor root.
        parent[ra] = rs;
    }

    // Pass 1: zero closing rate on every pre-merge plate-plate suture
    // that ends up inside a unioned plate. After remap those cells are
    // interior — the convergent rate left over from this epoch's
    // accumulateClosingRate is no longer physical.
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
        for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
            const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
            const int16_t selfId = field.plateId[idx];
            if (selfId < 0 || static_cast<std::size_t>(selfId) >= N) continue;
            const std::size_t selfRoot = findRoot(static_cast<std::size_t>(selfId));
            const int32_t lonW = (lonIdx == 0)       ? LON - 1 : lonIdx - 1;
            const int32_t lonE = (lonIdx == LON - 1) ? 0       : lonIdx + 1;
            const int32_t latS = std::max(0, latIdx - 1);
            const int32_t latN = std::min(LAT - 1, latIdx + 1);
            const std::size_t neigh[4] = {
                SphereField::cellIndex(lonW, latIdx),
                SphereField::cellIndex(lonE, latIdx),
                SphereField::cellIndex(lonIdx, latS),
                SphereField::cellIndex(lonIdx, latN),
            };
            for (int32_t k = 0; k < 4; ++k) {
                const int16_t nPid = field.plateId[neigh[k]];
                if (nPid < 0 || static_cast<std::size_t>(nPid) >= N) continue;
                if (nPid == selfId) continue;
                const std::size_t nRoot = findRoot(static_cast<std::size_t>(nPid));
                if (nRoot == selfRoot) {
                    field.convergenceRateRadPerMy[idx] = 0.0f;
                    field.boundaryType[idx] = 0;
                    break;
                }
            }
        }
    }

    // Fold absorbed plates' authoritative metadata into root survivor.
    // Use a unit-vector running mean on the sphere (averaging lat/lon
    // directly fails across antimeridian / poles).
    constexpr double DEG2RAD = 0.01745329252;
    constexpr double RAD2DEG = 57.29577951;
    std::vector<double> rootSx(N, 0.0), rootSy(N, 0.0), rootSz(N, 0.0);
    std::vector<int32_t> rootCount(N, 0);
    for (std::size_t i = 0; i < N; ++i) {
        const std::size_t r = findRoot(i);
        const double latR = static_cast<double>(plates[i].latDeg) * DEG2RAD;
        const double lonR = static_cast<double>(plates[i].lonDeg) * DEG2RAD;
        const double cosLat = std::cos(latR);
        rootSx[r] += cosLat * std::cos(lonR);
        rootSy[r] += cosLat * std::sin(lonR);
        rootSz[r] += std::sin(latR);
        ++rootCount[r];
    }
    for (std::size_t r = 0; r < N; ++r) {
        if (rootCount[r] <= 1) continue; // Not a merged root.
        const double mag = std::sqrt(rootSx[r] * rootSx[r]
                                   + rootSy[r] * rootSy[r]
                                   + rootSz[r] * rootSz[r]);
        if (mag < 1e-9) continue; // antipodal — keep prior centroid.
        const double mx = rootSx[r] / mag;
        const double my = rootSy[r] / mag;
        const double mz = rootSz[r] / mag;
        plates[r].latDeg = static_cast<float>(
            std::asin(std::clamp(mz, -1.0, 1.0)) * RAD2DEG);
        plates[r].lonDeg = static_cast<float>(std::atan2(my, mx) * RAD2DEG);
    }
    for (std::size_t i = 0; i < N; ++i) {
        const std::size_t r = findRoot(i);
        if (r == i) continue;
        plates[r].landFraction = std::max(plates[r].landFraction,
                                          plates[i].landFraction);
    }

    // Pass 2: build remap from old -> new index, doing the compaction
    // (erase non-roots) implicitly. Roots keep their original order
    // among survivors.
    std::vector<int16_t> remap(N, -1);
    {
        std::size_t newIdx = 0;
        for (std::size_t i = 0; i < N; ++i) {
            if (findRoot(i) == i) {
                remap[i] = static_cast<int16_t>(newIdx++);
            }
        }
        for (std::size_t i = 0; i < N; ++i) {
            if (remap[i] < 0) {
                remap[i] = remap[findRoot(i)];
            }
        }
    }

    // Single-sweep remap of every cell.
    for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
        const int16_t pid = field.plateId[i];
        if (pid < 0) continue;
        if (static_cast<std::size_t>(pid) >= N) {
            field.plateId[i] = -1;
            continue;
        }
        field.plateId[i] = remap[static_cast<std::size_t>(pid)];
    }

    // Compact plates vector. Roots are visited in original order so
    // surviving plates retain their relative order.
    std::vector<Plate> survivors;
    survivors.reserve(N);
    for (std::size_t i = 0; i < N; ++i) {
        if (findRoot(i) == i) survivors.push_back(plates[i]);
    }
    plates = std::move(survivors);
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
    if (plates.empty()) {
        LOG_WARN("SphereFieldPhysics: %s called with empty plates -- skipping",
                 __func__);
        return;
    }
    if (dtMy <= 0.0f) return;
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
    auto buildRot = [&](double poleLatR, double poleLonR, double thetaR) -> PlateRot {
        PlateRot r;
        const double cosLat = std::cos(poleLatR);
        r.axX   = cosLat * std::cos(poleLonR);
        r.axY   = cosLat * std::sin(poleLonR);
        r.axZ   = std::sin(poleLatR);
        r.cosT  = std::cos(thetaR);
        r.sinT  = std::sin(thetaR);
        r.oneMc = 1.0 - r.cosT;
        return r;
    };
    const std::size_t P = plates.size();
    std::vector<PlateRot> rotBack(P), rotFwd(P);
    for (std::size_t i = 0; i < P; ++i) {
        const double poleLatR = static_cast<double>(plates[i].eulerPoleLatDeg) * DEG2RAD;
        const double poleLonR = static_cast<double>(plates[i].eulerPoleLonDeg) * DEG2RAD;
        const double absTheta = static_cast<double>(plates[i].angularVelDeg)
                              * DEG2RAD * static_cast<double>(dtMy);
        rotBack[i] = buildRot(poleLatR, poleLonR, -absTheta);
        rotFwd [i] = buildRot(poleLatR, poleLonR, +absTheta);
    }

    // Sentinel: cell vacated by pass 1 (incumbent's backward sample
    // walked off its plate's footprint). Pass 2 tries to claim from
    // a 4-neighbour whose forward rotation lands here; pass 3 fills
    // any remaining cells with fresh oceanic crust (mid-ocean ridge).
    constexpr int16_t VACATED = -2;
    // DEBT(perf, WP-11): these six N-sized buffers (~5.4 MB total) are
    // allocated every substep, then std::move'd into the field at the end --
    // i.e. they BECOME the field's storage, and the field's old storage is
    // freed with the moved-from locals. Promoting them to persistent
    // SphereField members for reuse is not behaviour-preserving as a drop-in:
    // because of the swap-by-move, the "scratch" and the "live field" are the
    // same allocations rotated each call, so reuse needs an explicit
    // double-buffer (ping/pong) on SphereField plus a guaranteed full
    // overwrite (pass 1 writes incumbents, passes 2/3 cover vacated/orphan
    // cells, final fallback covers the rest -- every cell is written, so a
    // ping-pong is provably safe IF wired correctly). Deferred: touches the
    // SphereField struct layout and all call sites; out of scope for a
    // minimal correctness-focused WP. Reuse would save the per-substep alloc.
    std::vector<int16_t> newOwner(N, static_cast<int16_t>(-1));
    std::vector<float>   newCrust(N, 0.0f);
    std::vector<float>   newContFrac(N, 0.0f);
    std::vector<float>   newAge(N, 0.0f);
    std::vector<float>   newSurface(N, 0.0f);
    std::vector<float>   newThermal(N, 0.0f);
    std::size_t pass1Orphan = 0;
    std::size_t pass2Claim  = 0;
    std::size_t pass3Wake   = 0;

    auto rotateRodrigues = [](const PlateRot& R,
                              double cx, double cy, double cz)
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

    // PASS 1: incumbent backward-sample claim.
    // For each dest cell, the incumbent plate (current owner) may keep
    // the cell if its backward rotation lands inside the plate's
    // footprint -- that means the plate's source rotates forward to
    // exactly this cell. Standard semi-Lagrangian transport.
    //
    // Cells where the incumbent's backward rotation walks OFF the
    // footprint are TRAILING-EDGE cells: the plate has rotated forward
    // past them. Mark VACATED for pass 2 to resolve.
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

            if (incumbent >= 0 && static_cast<std::size_t>(incumbent) < P) {
                const std::size_t depIdx = rotateRodrigues(
                    rotBack[static_cast<std::size_t>(incumbent)], cx, cy, cz);
                if (field.plateId[depIdx] == incumbent) {
                    // Incumbent claim: copy state from departure cell.
                    newOwner[destIdx]    = incumbent;
                    newCrust[destIdx]    = field.crustThicknessKm[depIdx];
                    newContFrac[destIdx] = field.continentalFraction[depIdx];
                    newAge[destIdx]      = field.crustAgeMy[depIdx];
                    newSurface[destIdx]  = field.surfaceElevationM[depIdx];
                    newThermal[destIdx]  = field.thermalAgeMy[depIdx];
                    continue;
                }
            }

            // Vacated: incumbent's backward rotation walked off plate
            // footprint. Pass 2 below tries to find a claimant.
            newOwner[destIdx] = VACATED;
            ++pass1Orphan;
        }
    }

    // PASS 2: vacated cells claimed by 4-neighbours moving INTO them.
    // For each vacated cell we ask: does any 4-neighbour plate's
    // forward rotation of the neighbour cell land EXACTLY on this
    // vacated cell? If yes, that neighbour is geometrically advancing
    // into this cell -- a leading-edge claim. The neighbour's source
    // state is copied here.
    //
    // The geometric exact-match test (forward-rotated dest == this
    // vacated cell idx) is the key safeguard against the older
    // "smear" bug. The earlier rule allowed any plate whose backward
    // rotation happened to fall inside its own footprint to claim;
    // that condition was easy to satisfy for slow cratons, so they
    // annexed neighbour territory each substep. Here, the claimant
    // must be moving into THIS specific cell -- divergent neighbours
    // (whose forward rotation goes the other way) do not qualify.
    //
    // Multiple neighbours may claim. Tie-breaks:
    //   1. Continental overrides oceanic (Andean / Cascadian style).
    //   2. Slowest plate wins among same-class claimants (cratonic
    //      preservation; Gripp & Gordon 2002).
    for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
        for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
            const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
            if (newOwner[idx] != VACATED) continue;

            const int32_t lonW = (lonIdx == 0)       ? LON - 1 : lonIdx - 1;
            const int32_t lonE = (lonIdx == LON - 1) ? 0       : lonIdx + 1;
            const int32_t latS = std::max(0, latIdx - 1);
            const int32_t latN = std::min(LAT - 1, latIdx + 1);
            const std::size_t nIdx[4] = {
                SphereField::cellIndex(lonW, latIdx),
                SphereField::cellIndex(lonE, latIdx),
                SphereField::cellIndex(lonIdx, latS),
                SphereField::cellIndex(lonIdx, latN),
            };

            int16_t  bestPid    = -1;
            std::size_t bestSrc = 0;
            float    bestFrac   = -1.0f;
            float    bestOmega  = 1e9f;

            for (int32_t k = 0; k < 4; ++k) {
                const std::size_t nIdxK = nIdx[k];
                const int16_t nPid = field.plateId[nIdxK];
                if (nPid < 0 || static_cast<std::size_t>(nPid) >= P) continue;

                // Forward-rotate the neighbour's centre. If the result
                // lands in our vacated cell, the neighbour plate is
                // converging into this cell.
                const int32_t nLon = static_cast<int32_t>(nIdxK % LON);
                const int32_t nLat = static_cast<int32_t>(nIdxK / LON);
                const LatLon nP = SphereField::cellCenter(nLon, nLat);
                const double nLatR = static_cast<double>(nP.latDeg) * DEG2RAD;
                const double nLonR = static_cast<double>(nP.lonDeg) * DEG2RAD;
                const double nCosLat = std::cos(nLatR);
                const double ncx = nCosLat * std::cos(nLonR);
                const double ncy = nCosLat * std::sin(nLonR);
                const double ncz = std::sin(nLatR);
                const std::size_t fwdDest = rotateRodrigues(
                    rotFwd[static_cast<std::size_t>(nPid)], ncx, ncy, ncz);
                if (fwdDest != idx) continue;

                const float frac = field.continentalFraction[nIdxK];
                const float omega = std::fabs(
                    plates[static_cast<std::size_t>(nPid)].angularVelDeg);
                bool replace = false;
                if (bestPid < 0) {
                    replace = true;
                } else if (frac > 0.5f && bestFrac <= 0.5f) {
                    replace = true; // continental overrides oceanic
                } else if ((frac > 0.5f) == (bestFrac > 0.5f)
                           && omega < bestOmega) {
                    replace = true; // same class, slowest wins
                }
                if (replace) {
                    bestPid   = nPid;
                    bestSrc   = nIdxK;
                    bestFrac  = frac;
                    bestOmega = omega;
                }
            }

            if (bestPid >= 0) {
                newOwner[idx]    = bestPid;
                newCrust[idx]    = field.crustThicknessKm[bestSrc];
                newContFrac[idx] = field.continentalFraction[bestSrc];
                newAge[idx]      = field.crustAgeMy[bestSrc];
                newSurface[idx]  = field.surfaceElevationM[bestSrc];
                newThermal[idx]  = field.thermalAgeMy[bestSrc];
                ++pass2Claim;
            }
            // else: still VACATED -> pass 3 wake fill
        }
    }

    // PASS 3: wake fill. Cells still VACATED after pass 2 had no
    // converging neighbour -- they sit at a true divergent boundary.
    // Real Earth fills such cells with fresh basaltic oceanic crust
    // extruded at the mid-ocean ridge (Turcotte & Schubert 2014 ch. 2:
    // 7 km basalt at age 0). Ownership goes to the slowest 4-neighbour
    // so the fresh crust attaches to the cratonic side of the rift,
    // matching Atlantic-style passive-margin geometry.
    for (int32_t pass = 0; pass < 4; ++pass) {
        bool anyFilled = false;
        for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
            for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
                const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
                if (newOwner[idx] != VACATED) continue;
                const int32_t lonW = (lonIdx == 0)       ? LON - 1 : lonIdx - 1;
                const int32_t lonE = (lonIdx == LON - 1) ? 0       : lonIdx + 1;
                const int32_t latS = std::max(0, latIdx - 1);
                const int32_t latN = std::min(LAT - 1, latIdx + 1);
                const std::size_t nIdx[4] = {
                    SphereField::cellIndex(lonW, latIdx),
                    SphereField::cellIndex(lonE, latIdx),
                    SphereField::cellIndex(lonIdx, latS),
                    SphereField::cellIndex(lonIdx, latN),
                };
                int16_t pickPid    = -1;
                float   pickOmega  = 1e9f;
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
                // Continental crust is buoyant and does not subduct
                // or sink to mid-ocean-ridge depths even when its host
                // plate's boundary sweeps past (Cogley 1984 "Continental
                // margins and the extent and number of the continents",
                // Rev Geophys 22). When the trailing edge of a moving
                // plate vacates a continental cell, the cell remains
                // continental but transfers ownership to the converging
                // / cratonic neighbour plate -- exactly the passive-
                // margin transfer that gave us North Atlantic continental
                // shelves on both the Eurasian and North American
                // plates. Only previously-oceanic cells become fresh
                // mid-ocean-ridge basalt at the spreading centre.
                if (field.continentalFraction[idx] > 0.5f) {
                    newCrust[idx]    = field.crustThicknessKm[idx];
                    newContFrac[idx] = field.continentalFraction[idx];
                    newAge[idx]      = field.crustAgeMy[idx];
                    newSurface[idx]  = field.surfaceElevationM[idx];
                    newThermal[idx]  = field.thermalAgeMy[idx];
                } else {
                    newCrust[idx]    = PhysicsConstants::initialOceanicThicknessKm;
                    newContFrac[idx] = 0.0f;
                    newAge[idx]      = 0.0f;
                    newSurface[idx]  = 0.0f;
                    newThermal[idx]  = 0.0f;
                }
                ++pass3Wake;
                anyFilled = true;
            }
        }
        if (!anyFilled) break;
    }

    // Final fallback: a patch of vacated cells with no claimed neighbour
    // anywhere is implausible at CFL <=1 sub-step but preserves the
    // invariant that every cell ends with a valid plate id.
    for (std::size_t i = 0; i < N; ++i) {
        if (newOwner[i] < 0) {
            newOwner[i]    = 0;
            newCrust[i]    = PhysicsConstants::initialOceanicThicknessKm;
            newContFrac[i] = 0.0f;
            newAge[i]      = 0.0f;
            newSurface[i]  = 0.0f;
            newThermal[i]  = 0.0f;
        }
    }

    // Env var read once (it cannot change mid-run); this function runs
    // every advection epoch, so a per-call getenv() walked the environment
    // each epoch for a diagnostic that is off by default.
    static const bool kAdvectTrace = std::getenv("AOC_ADVECT_TRACE") != nullptr;
    if (kAdvectTrace) {
        std::fprintf(stderr,
            "[advect] dt=%.3fMy pass1Orphan=%zu pass2Claim=%zu pass3Wake=%zu\n",
            static_cast<double>(dtMy),
            pass1Orphan, pass2Claim, pass3Wake);
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
    if (plates.empty()) {
        LOG_WARN("SphereFieldPhysics: %s called with empty plates -- skipping",
                 __func__);
        return;
    }
#if defined(AOC_HAS_OPENMP)
    #pragma omp parallel for schedule(static)
#endif
    for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
        for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
            const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
            if (!isBoundary[idx]) continue;
            const int16_t selfId = field.plateId[idx];
            if (selfId < 0) continue;
            // Stale plate ids in the raster can outrun plates.size()
            // (microplate merges shrink the vector). Guard before
            // indexing, matching applySlabPullFeedback's bounds check.
            if (static_cast<std::size_t>(selfId) >= plates.size()) continue;

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
            if (static_cast<std::size_t>(otherId) >= plates.size()) continue;

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

void growContinentalFractionAtArcs(SphereField& field, float dtMy) {
    // Hawkesworth et al. 2010 net continental-crust generation ~0.5
    // km³/yr at modern arcs (1 km³/yr generated × ~50 % preserved
    // in stable continental crust); Phanerozoic mean is comparable
    // (Cawood et al. 2013). Per-cell continental-fraction increase
    // per unit closing rate per unit time, anchored to Andean rate:
    //   cell area ~3000 km² × 35 km thick = ~105 000 km³ at full
    //   continentality; modern Andes ~50 km³/My/cell at closing
    //   rate ~9e-3 rad/My gives K_ARC ≈ 0.05 (rad/My)^-1 My^-1.
    //
    // The arc zone is OFFSET from the trench by 100-200 km on the
    // overrider side (Tatsumi 1986: slab dehydration → melting at
    // ~100 km slab depth, ~100 km horizontal offset for a 30°
    // slab dip). On the 0.5° raster (~55 km/cell) this is ~2-3
    // cells inboard. Targeting the arc cell (not the trench cell)
    // keeps the gain applied to a stable interior cell that
    // applySubduction never resets, so growth compounds.
    constexpr float K_ARC_FRAC_PER_RADMY = 0.5f;
    // Continental thickness gain follows the same closing-rate
    // proportionality but at a reduced K — andesitic arc thickens
    // to ~30 km (DeCelles 2002 modern Andean active-arc), not the
    // 70 km Tibet steady-state that calibrates K_THICKEN.
    constexpr float K_ARC_KM_PER_RADMY = 100.0f;
    // Inboard offset in cells from the trench cell to the arc cell.
    // Tatsumi 1986 ~100 km horizontal offset; on 0.5° pitch (55 km
    // at equator, ~28 km at 60° lat) this maps to 2 cells equatorial
    // / up to 4 polar. Use 2 as a global mean — refining to a
    // latitude-aware step is an optimisation, not a physics fix.
    constexpr int32_t ARC_OFFSET_CELLS = 2;
    const float maxCrust = PhysicsConstants::maxCrustThicknessKm;
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
        for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
            const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
            if (field.boundaryType[idx] != 1u) continue;
            const float rate = field.convergenceRateRadPerMy[idx];
            if (rate <= 0.0f) continue;
            const int16_t selfId = field.plateId[idx];
            if (selfId < 0) continue;
            const int32_t lonW = (lonIdx == 0)       ? LON - 1 : lonIdx - 1;
            const int32_t lonE = (lonIdx == LON - 1) ? 0       : lonIdx + 1;
            const int32_t latS = (latIdx == 0)       ? 0       : latIdx - 1;
            const int32_t latN = (latIdx == LAT - 1) ? LAT - 1 : latIdx + 1;
            const std::size_t idxW = SphereField::cellIndex(lonW, latIdx);
            const std::size_t idxE = SphereField::cellIndex(lonE, latIdx);
            const std::size_t idxS = SphereField::cellIndex(lonIdx, latS);
            const std::size_t idxN = SphereField::cellIndex(lonIdx, latN);
            int32_t otherLon = lonIdx;
            int32_t otherLat = latIdx;
            const int16_t nW = field.plateId[idxW];
            const int16_t nE = field.plateId[idxE];
            const int16_t nS = field.plateId[idxS];
            const int16_t nN = field.plateId[idxN];
            std::size_t otherIdx = idx;
            if      (nW != selfId && nW >= 0) { otherIdx = idxW; otherLon = lonW; }
            else if (nE != selfId && nE >= 0) { otherIdx = idxE; otherLon = lonE; }
            else if (nS != selfId && nS >= 0) { otherIdx = idxS; otherLat = latS; }
            else if (nN != selfId && nN >= 0) { otherIdx = idxN; otherLat = latN; }
            else continue;
            // Identify overrider (higher continentalFraction side)
            // and step ARC_OFFSET_CELLS cells INBOARD into its
            // interior, away from the trench cell. The arc zone
            // sits in the overrider plate, not at the contact.
            const float selfFrac  = field.continentalFraction[idx];
            const float otherFrac = field.continentalFraction[otherIdx];
            int32_t arcLon, arcLat;
            int16_t arcOwnerId;
            if (selfFrac >= otherFrac) {
                // self is overrider; step AWAY from other (i.e.
                // negate the trench->other direction).
                const int32_t dLon = otherLon - lonIdx;
                const int32_t dLat = otherLat - latIdx;
                int32_t stepLon = (dLon > 0) ? -1 : (dLon < 0) ? 1 : 0;
                int32_t stepLat = (dLat > 0) ? -1 : (dLat < 0) ? 1 : 0;
                if (std::abs(dLon) > 1) stepLon = -stepLon; // wrap fix
                arcLon = lonIdx + stepLon * ARC_OFFSET_CELLS;
                arcLat = latIdx + stepLat * ARC_OFFSET_CELLS;
                arcOwnerId = selfId;
            } else {
                // other is overrider; step from other AWAY from self.
                const int32_t dLon = lonIdx - otherLon;
                const int32_t dLat = latIdx - otherLat;
                int32_t stepLon = (dLon > 0) ? -1 : (dLon < 0) ? 1 : 0;
                int32_t stepLat = (dLat > 0) ? -1 : (dLat < 0) ? 1 : 0;
                if (std::abs(dLon) > 1) stepLon = -stepLon;
                arcLon = otherLon + stepLon * ARC_OFFSET_CELLS;
                arcLat = otherLat + stepLat * ARC_OFFSET_CELLS;
                arcOwnerId = field.plateId[otherIdx];
            }
            // Latitude clamp; longitude wrap.
            if (arcLat < 0)        arcLat = 0;
            if (arcLat >= LAT)     arcLat = LAT - 1;
            arcLon = ((arcLon % LON) + LON) % LON;
            const std::size_t arcIdx = SphereField::cellIndex(arcLon, arcLat);
            // Stop if the inboard cell is no longer the overrider
            // (e.g. another plate sits in the way) — arc volcanism
            // does not punch across plate boundaries.
            if (field.plateId[arcIdx] != arcOwnerId) continue;
            const float dFrac = K_ARC_FRAC_PER_RADMY * rate * dtMy;
            float frac = field.continentalFraction[arcIdx] + dFrac;
            if (frac > 1.0f) frac = 1.0f;
            field.continentalFraction[arcIdx] = frac;
            const float dCrustKm = K_ARC_KM_PER_RADMY * rate * dtMy;
            float h = field.crustThicknessKm[arcIdx] + dCrustKm;
            if (h > maxCrust) h = maxCrust;
            field.crustThicknessKm[arcIdx] = h;
        }
    }
}

void accreteToCardinalNeighbours(SphereField& field, float dtMy) {
    // Cawood et al. 2013: accretionary orogens account for ~30 % of
    // present continental area, added predominantly during Phanerozoic
    // (~540 My). That equates to a normalised area growth rate of roughly
    // 30 % / 540 My ≈ 5.6e-4 / My. For a single donor cell radiating
    // to 4 neighbours, the per-cell rate is ~1.4e-4 / My. Observed
    // per-cell donation dFrac = K * dtMy = 0.002 * 50 = 0.1 / epoch.
    // A fresh oceanic cell (cf = 0) adjacent to a saturated arc cell
    // (cf > 0.95) therefore crosses the continental threshold (cf = 0.5)
    // in ~5 epochs (~250 My), and diffusion reaches ~12 cells deep over
    // the full 3 Gy run -- consistent with continent sizes enlarging
    // from cratonic nuclei (< 5 %) to modern extent (29 %) over 3 Gy.
    // The rate is set higher than Cawood's Phanerozoic figure because
    // the early Archean/Proterozoic accretion was even more rapid and
    // the sim integrates that era as well.
    //
    // SPREAD_FROM_THRESHOLD = 0.95 ensures only fully-mature
    // continental cells donate. Below that, cf is still growing via
    // arc volcanism and should not be diluted into neighbours.
    constexpr float K_SPREAD_PER_MY        = 0.0025f;
    constexpr float SPREAD_FROM_THRESHOLD  = 0.95f;
    // Crust donated per unit cf increase so that diffused cells emerge
    // above sea level as their cf crosses the continental threshold.
    // Derivation via Airy isostasy (PhysicsConstants):
    //   sea-level emergence requires h > datumM / (1 - rho_cf / rhoM)
    //   at cf=0.5: rho = 2800 kg/m3, rhoM=3300, datum=3549m
    //   h_min = 3549 / (1 - 2800/3300) = 23400 m = 23.4 km
    //   starting from oceanic h=7 km: gap = 16.4 km for 0.5 cf change
    //   K_CRUST = 16.4 / 0.5 = 32.8 km per unit cf
    constexpr float K_CRUST_PER_DIFF_FRAC  = 32.8f;
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    const float dFracMax = K_SPREAD_PER_MY * dtMy;
    if (dFracMax <= 0.0f) { return; }
    // Two-pass split so donors observe a stable cf snapshot. Writing
    // back into `continentalFraction` while iterating would let the
    // diffusion front spread an entire epoch's worth of growth in a
    // single sweep, breaking the per-My rate calibration above.
    std::vector<float> nextFrac(field.continentalFraction);
    // Crust thickening must use the same snapshot discipline as cf:
    // a cell can be a neighbour of several donors in one sweep, and
    // reading+writing the LIVE crustThicknessKm would let earlier
    // donations feed later ones, breaking the per-My rate calibration
    // and making the result order-dependent.
    std::vector<float> nextCrust(field.crustThicknessKm);
    for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
        for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
            const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
            const float cf = field.continentalFraction[idx];
            if (cf < SPREAD_FROM_THRESHOLD) { continue; }
            const int16_t donorId = field.plateId[idx];
            if (donorId < 0) { continue; }
            const int32_t lonW = (lonIdx == 0)       ? LON - 1 : lonIdx - 1;
            const int32_t lonE = (lonIdx == LON - 1) ? 0       : lonIdx + 1;
            const int32_t latS = (latIdx == 0)       ? 0       : latIdx - 1;
            const int32_t latN = (latIdx == LAT - 1) ? LAT - 1 : latIdx + 1;
            const std::size_t neighbours[4] = {
                SphereField::cellIndex(lonW, latIdx),
                SphereField::cellIndex(lonE, latIdx),
                SphereField::cellIndex(lonIdx, latS),
                SphereField::cellIndex(lonIdx, latN),
            };
            for (std::size_t n : neighbours) {
                // Same-plate gate -- cross-plate diffusion would break
                // Wilson-cycle assembly (continents cannot leak across
                // an open ocean basin without colliding first).
                if (field.plateId[n] != donorId) { continue; }
                if (nextFrac[n] >= 1.0f) { continue; }
                const float actual = std::min(dFracMax, 1.0f - nextFrac[n]);
                nextFrac[n] += actual;
                // Thicken crust proportionally so diffused cells emerge
                // above sea level as cf crosses the continental threshold.
                const float dCrust = actual * K_CRUST_PER_DIFF_FRAC;
                float h = nextCrust[n] + dCrust;
                if (h > PhysicsConstants::maxCrustThicknessKm) {
                    h = PhysicsConstants::maxCrustThicknessKm;
                }
                nextCrust[n] = h;
            }
        }
    }
    field.continentalFraction.swap(nextFrac);
    field.crustThicknessKm.swap(nextCrust);
}

void applySubduction(SphereField& field,
                     const std::vector<Plate>& plates,
                     float dtMy) {
    if (plates.empty()) {
        LOG_WARN("SphereFieldPhysics: %s called with empty plates -- skipping",
                 __func__);
        return;
    }
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

            // Pick consumed side. Primary rule: lower continental-
            // fraction side subducts (denser oceanic basalt sinks
            // beneath buoyant continental crust). Tie-break for
            // ocean-ocean boundaries by crust AGE — older oceanic
            // crust is colder and denser (Stein & Stein 1992 t^1/2
            // cooling-subsidence relation), so the older side
            // subducts. Without the age tie-break, two oceanic
            // plates with cf~0 had subduction direction decided by
            // raster iteration order, producing a deterministic
            // iteration-order cascade where one plate progressively
            // ate every neighbouring oceanic plate over the 3 Gy
            // run (audit: plate 6 grew from 1085 → 5681 cells over
            // seed 42's 60-epoch run).
            const float selfFrac  = field.continentalFraction[idx];
            const float otherFrac = field.continentalFraction[otherIdx];
            // Pure-continental collisions do not subduct -- both
            // sides thicken instead (thickenFromClosingRate).
            const float lowFrac = std::min(selfFrac, otherFrac);
            if (lowFrac >= 0.5f) continue;
            // Subduction requires a meaningful density contrast.
            // Ocean-ocean boundaries with near-equal cf have no
            // physical preferred consumption direction (real Earth:
            // such boundaries usually evolve into transform faults
            // with shear-dominant motion, not subduction). Without
            // this gate the iteration-order resolves the tie
            // deterministically, producing a runaway cascade where
            // one plate progressively eats every adjacent oceanic
            // plate — audit on seed 42 showed a single plate
            // growing from 1085 to >5000 cells over 60 epochs.
            //
            // CF_SUBDUCTION_THRESHOLD = 0.05 corresponds to ~5
            // percentage points of continental fraction — roughly
            // the difference between a true mid-ocean basin
            // (cf ≈ 0) and an oceanic plateau or accreted seamount
            // chain (cf ~ 0.05-0.10). Below that, the contrast is
            // subgrid noise.
            constexpr float CF_SUBDUCTION_THRESHOLD = 0.05f;
            const float fracDelta = selfFrac - otherFrac;
            if (std::fabs(fracDelta) < CF_SUBDUCTION_THRESHOLD) continue;
            int16_t     overriderId;
            int16_t     consumedSideId;
            int32_t     consumedLon, consumedLat;
            int32_t     overrideLon, overrideLat;
            const bool selfConsumed = (fracDelta < 0.0f);
            if (selfConsumed) {
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
        // CORRECTNESS FIX (changes generated maps): the longitudinal cell
        // pitch shrinks as cos(lat), so the zonal slope (zE - zW) / (2*pitch)
        // is amplified up to ~229x in the pole-most row (lat 89.75 deg,
        // cos ~= 0.0044). That spurious "slope" erodes genuine polar
        // highlands to the peneplain floor every epoch. Skip cells poleward
        // of 87 deg, where the amplification (>=19x) is no longer physical;
        // polar relief is preserved instead of being flattened each step.
        constexpr float POLAR_EROSION_CUTOFF_DEG = 87.0f;
        if (std::fabs(latDeg) > POLAR_EROSION_CUTOFF_DEG) continue;
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
    //   0. plate-cell advection — Lagrangian transport: each owned cell
    //      rotates about its plate's Euler pole by omega*dt (Rodrigues
    //      rotation, backward semi-Lagrangian sample with a vacated-cell
    //      wake-fill pass for divergent boundaries). Replaces the legacy
    //      centroid-Voronoi reassignment from the OLD pipeline.
    //   1. boundary detection.
    //   2. instantaneous closing rate from Euler-pole velocities.
    //   3. continental crust thickening at convergent boundary cells.
    //   4. oceanic-margin subduction (lower-density side flips).
    //   5. continental docking (cont-cont contact > 30 My fuses).
    //   6. Wilson-cycle rifting (supercontinent thermal-age trigger).
    //   7. Airy isostasy → surface elevation.
    //   8. stream-power surface erosion.
    //   9. compact + recompute plate centroids for next epoch.
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
    // Arc volcanism converts oceanic margins into andesitic
    // continental crust at convergent boundaries — the mechanism
    // that grows the global continental fraction over the 3 Gy run
    // from the ~5 % Archean baseline to the ~29 % modern figure.
    // Must run before applySubduction so cells about to be consumed
    // can still partially convert (the arc is just inboard of the
    // trench in real geology), and so that the next thicken pass
    // sees the elevated continentalFraction.
    growContinentalFractionAtArcs(field, dtMy);
    // Phase 1.4b: terrane-accretion diffusion. Saturated continental
    // cells donate cf to same-plate neighbours, modelling collisional
    // welding / interior accretion. Without this, cont(>0.5) plateaus
    // at the arc-band footprint (~16% sphere across all seeds) and
    // cf_mean never reaches the Cawood et al. 2013 modern 0.25-0.35
    // band. Runs BEFORE subduction so the diffusion frontier is
    // exposed to subduction's consumption pass in the same epoch and
    // cf stays in dynamic equilibrium between growth and destruction.
    accreteToCardinalNeighbours(field, dtMy);
    applySubduction(field, plates, dtMy);
    // Ridge accretion: extrude fresh oceanic basalt on already-oceanic
    // cells whose plates are pulling apart. The continental-fraction
    // gate inside the function protects continental rift zones; those
    // are handled by `applyWilsonRifting` on its own thermal-age
    // timescale. Without ridge accretion, divergent boundaries leave
    // stretched stale crust behind drifting plates with no mid-ocean-
    // ridge spreading record (Atlantic age gradient never appears).
    accreteAtDivergentBoundary(field, dtMy);
    // No full plate-pair fusion ("continental docking") at the
    // raster level here — `mergePlates` (called from MapGenerator on
    // 2D centroid contact) does that atomically.
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
    static const bool kSpherePhysTrace =
        std::getenv("AOC_SPHEREPHYS_TRACE") != nullptr;
    if (kSpherePhysTrace) {
        float maxRate = 0.0f, minRate = 0.0f, maxCrust = 0.0f, maxZ = -1e9f;
        std::size_t mountainCells = 0;
        std::size_t boundaryCount = 0;
        std::size_t continentalCells = 0;
        double sumContFrac = 0.0;
        for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
            const float r = field.convergenceRateRadPerMy[i];
            const float h = field.crustThicknessKm[i];
            const float z = field.surfaceElevationM[i];
            const float cf = field.continentalFraction[i];
            if (r > maxRate)  maxRate  = r;
            if (r < minRate)  minRate  = r;
            if (boundaryScratch[i]) ++boundaryCount;
            if (h > maxCrust) maxCrust = h;
            if (z > maxZ)     maxZ     = z;
            if (z > 4000.0f) ++mountainCells;
            if (cf > 0.5f)   ++continentalCells;
            sumContFrac += static_cast<double>(cf);
        }
        const double meanContFrac =
            sumContFrac / static_cast<double>(SphereField::CELL_COUNT);
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
            "z=%.0fm mtn=%zu cont(>0.5)=%zu cf_mean=%.3f "
            "plates=%zu boundary=%zu "
            "maxOmega=%.3fdeg/My cflCells=%.1f\n",
            static_cast<double>(dtMy),
            static_cast<double>(minRate),
            static_cast<double>(maxRate),
            static_cast<double>(maxCrust),
            static_cast<double>(maxZ),
            mountainCells,
            continentalCells,
            meanContFrac,
            plates.size(),
            boundaryCount,
            static_cast<double>(traceMaxOmegaDeg),
            static_cast<double>(cflCells));
    }
}

} // namespace aoc::map::gen

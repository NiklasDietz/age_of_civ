#include "aoc/map/gen/SphereFieldPhysics.hpp"

#include "aoc/map/gen/Terrane.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/map/gen/Noise.hpp"
#include "aoc/map/gen/PlatePhysics.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <queue>
#include <limits>
#include <map>
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
// Hard plate-speed ceiling (slab-pull clamp and the kinematic
// consumption cap in applySubduction both derive from it).
inline constexpr float MAX_ABS_OMEGA_DEG_PER_MY = 0.15f;

void generateInitialPlateOwnership(SphereField& field, const std::vector<Plate>& plates,
                                   uint64_t seed) {
    if (plates.empty()) {
        LOG_WARN("SphereFieldPhysics: %s called with empty plates -- skipping", __func__);
        std::fill(field.plateId.begin(), field.plateId.end(), static_cast<int16_t>(-1));
        return;
    }
    constexpr int32_t LON        = SphereField::LON_CELLS;
    constexpr int32_t LAT        = SphereField::LAT_CELLS;
    const std::size_t totalCells = SphereField::CELL_COUNT;
    const std::size_t P          = plates.size();

    std::fill(field.plateId.begin(), field.plateId.end(), static_cast<int16_t>(-1));

    // Deterministic per-call PRNG (SplitMix64) for both the
    // per-round shuffle and the per-claim random frontier pick.
    uint64_t rngState = seed ^ 0x9E3779B97F4A7C15ULL;
    if (rngState == 0) rngState = 0xDEADBEEFCAFEBABEULL;
    auto next64 = [&]() -> uint64_t {
        uint64_t x = (rngState += 0x9E3779B97F4A7C15ULL);
        x          = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x          = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
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
    auto cellAreaWeight        = [](int32_t latIdx) -> double {
        const double latDeg =
            (static_cast<double>(latIdx) + 0.5) * static_cast<double>(SphereField::CELL_DEG) - 90.0;
        return std::cos(latDeg * DEG2RAD_D);
    };

    auto pushNeighbours = [&](std::size_t cellIdx, std::size_t plateIdx) {
        const int32_t latIdx      = static_cast<int32_t>(cellIdx / static_cast<std::size_t>(LON));
        const int32_t lonIdx      = static_cast<int32_t>(cellIdx % static_cast<std::size_t>(LON));
        const int32_t lonW        = (lonIdx == 0) ? LON - 1 : lonIdx - 1;
        const int32_t lonE        = (lonIdx == LON - 1) ? 0 : lonIdx + 1;
        const int32_t latS        = std::max(0, latIdx - 1);
        const int32_t latN        = std::min(LAT - 1, latIdx + 1);
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
        const SphereField::CellCoord c = SphereField::locate(plates[i].latDeg, plates[i].lonDeg);
        const std::size_t idx          = SphereField::cellIndex(c.lonIdx, c.latIdx);
        if (field.plateId[idx] >= 0) continue; // seed collided with prior
        field.plateId[idx] = static_cast<int16_t>(i);
        claimedArea[i]     = cellAreaWeight(c.latIdx);
        pushNeighbours(idx, i);
        ++claimed;
    }

    while (claimed < totalCells) {
        // Pick plate with smallest claimedArea AND non-empty frontier.
        // Linear scan is O(P) — fine for P ~ 10-20 plates and
        // amortised across 259200 cell claims (~2.5M scan ops total).
        std::size_t pick = P;
        double minArea   = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < P; ++i) {
            if (frontiers[i].empty()) continue;
            if (claimedArea[i] < minArea) {
                minArea = claimedArea[i];
                pick    = i;
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
            f[pickPos]                = f.back();
            f.pop_back();
            if (field.plateId[cellIdx] >= 0) continue;
            field.plateId[cellIdx] = static_cast<int16_t>(pick);
            const int32_t latIdx   = static_cast<int32_t>(cellIdx / static_cast<std::size_t>(LON));
            claimedArea[pick] += cellAreaWeight(latIdx);
            pushNeighbours(cellIdx, pick);
            ++claimed;
            break;
        }
    }
}

void recomputePlateCentroidsFromCells(SphereField& field, std::vector<Plate>& plates) {
    if (plates.empty()) {
        LOG_WARN("SphereFieldPhysics: %s called with empty plates -- skipping", __func__);
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
            const int16_t pid     = field.plateId[idx];
            if (pid < 0 || static_cast<std::size_t>(pid) >= N) continue;
            const LatLon p      = SphereField::cellCenter(lonIdx, latIdx);
            const double latR   = static_cast<double>(p.latDeg) * DEG2RAD;
            const double lonR   = static_cast<double>(p.lonDeg) * DEG2RAD;
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
        const double mx  = sx[i] * inv;
        const double my  = sy[i] * inv;
        const double mz  = sz[i] * inv;
        const double r   = std::sqrt(mx * mx + my * my + mz * mz);
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
            const int16_t selfId  = field.plateId[idx];
            if (selfId < 0) continue;
            const float closing = field.convergenceRateRadPerMy[idx];
            if (closing >= DIVERGENT_RATE_THRESHOLD) continue;
            if (field.continentalFraction[idx] >= OCEANIC_CRUST_GATE) continue;
            field.crustThicknessKm[idx]    = PhysicsConstants::initialOceanicThicknessKm;
            field.continentalFraction[idx] = 0.0f;
            field.crustAgeMy[idx]          = 0.0f;
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
void applySlabPullFeedback(SphereField& field, std::vector<Plate>& plates, float dtMy) {
    if (plates.empty()) {
        LOG_WARN("SphereFieldPhysics: %s called with empty plates -- skipping", __func__);
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
    // long-term continental-track envelope. (Constant is file-scope:
    // the subduction consumption cap derives from it too.)

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
        float deltaFrac = static_cast<float>(slabPull[i]) * SLAB_PULL_GAIN *
                          (dtMy / 50.0f); // normalise to the 50-Myr base.
        if (deltaFrac > MAX_FRAC_PER_EPOCH) deltaFrac = MAX_FRAC_PER_EPOCH;
        if (deltaFrac < -MAX_FRAC_PER_EPOCH) deltaFrac = -MAX_FRAC_PER_EPOCH;
        float w = plates[i].angularVelDeg * (1.0f + deltaFrac);
        if (w > MAX_ABS_OMEGA_DEG_PER_MY) w = MAX_ABS_OMEGA_DEG_PER_MY;
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

// Threshold for "supercontinent" classification, as a share of the
// planet's OWN continental crust gathered into one plate. Anderson 2007
// "New Theory of the Earth" Table 15.1 lists Pangaean-class assemblies at
// ~25-30 % of total continental crust massed together; 0.25 is the
// Pangaean-onset floor.
//
// 2026-08-10: this used to be a share of the GLOBAL SURFACE (0.20, then
// 0.12), which is the same citation restated through Earth's particular
// 29 % continental coverage -- and that restatement does not survive on a
// planet whose continental coverage is not Earth's, or whose crust is
// distributed over a different number of plates. Measured on seed 42 at
// the 0.12 surface-share form: the largest plate's continental share of
// the globe sat at 0.07-0.10 for essentially the whole 3 Gy run, just
// under the bar, so thermalAgeMy was reset to zero almost every epoch and
// never approached RIFT_THRESHOLD_MY. The entire run produced TWO rifts,
// both of them forced by FORCE_RIFT_TOTAL_FRACTION rather than thermal.
// With the Wilson cycle switched off in all but name, continents could
// only ever assemble -- which is why every seed ended with one landmass
// holding 60-86 % of all land, and why no passive margins (hence no
// continental shelves) existed to be measured.
//
// Expressed as a share of the planet's own crust it is scale-free: it
// asks the question the citation actually answers, and it is immune to
// the land-fraction spread across seeds.
inline constexpr float SUPERCONTINENT_CRUST_SHARE = 0.25f;
inline constexpr float RIFT_THRESHOLD_MY          = 150.0f;
// Probability ramp width above RIFT_THRESHOLD_MY: rift probability
// reaches 1.0 at thermal age = threshold + RIFT_RAMP_MY. Vérard
// et al. 2015 ("Geodynamics of the 3 Ga old lithosphere") report
// ~80-150 My from rift initiation to full mantle-driven breakup
// for an Archean-class supercontinent; 100 My sits at the centre
// of that envelope.
inline constexpr float RIFT_RAMP_MY = 100.0f;
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

// Area-weighted continental-crust volume in RELATIVE units (km x cos-lat).
// Absolute m^2 never appears -- this matches solveSeaLevelFixedVolume's
// convention, and every figure derived from it is reported as a ratio, so the
// unit cancels.
//
// Counts a cell when continentalFraction >= 0.5: that is the same threshold the
// [hypso] diagnostic and the gate metrics use, so the numbers are comparable
// with them rather than being a fourth definition of "continental".
//
// SERIAL, fixed-order, row-then-weight -- the same discipline
// solveSeaLevelFixedVolume documents for its own volume sum. An OpenMP
// reduction here would make the float summation order thread-count dependent
// and break test_determinism and the portable golden preset.
[[nodiscard]] double continentalCrustVolume(const SphereField& field) {
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    double vol            = 0.0;
    for (int32_t j = 0; j < LAT; ++j) {
        const float latDeg = -90.0f + (static_cast<float>(j) + 0.5f) * SphereField::CELL_DEG;
        const double w     = static_cast<double>(std::max(0.0f, std::cos(latDeg * 0.01745329252f)));
        double rowSum      = 0.0;
        const std::size_t rowBase = static_cast<std::size_t>(j) * static_cast<std::size_t>(LON);
        for (int32_t i = 0; i < LON; ++i) {
            const std::size_t idx = rowBase + static_cast<std::size_t>(i);
            if (field.continentalFraction[idx] >= 0.5f) {
                rowSum += static_cast<double>(field.crustThicknessKm[idx]);
            }
        }
        vol += rowSum * w;
    }
    return vol;
}
} // namespace

int32_t applyWilsonRifting(SphereField& field, std::vector<Plate>& plates, uint32_t& rngState,
                           float dtMy) {
    if (plates.empty()) {
        LOG_WARN("SphereFieldPhysics: %s called with empty plates -- skipping", __func__);
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
    // Total continental crust on the planet, which is what a plate's share is
    // measured against. Guarded: a world with no continental crust has no
    // supercontinent rather than a division by zero.
    int32_t totalContCells = 0;
    for (const int32_t c : contCells) {
        totalContCells += c;
    }
    const float contCellsTotal = static_cast<float>(std::max(1, totalContCells));

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
        // Cell's plate qualifies as supercontinent? Share of the planet's own
        // continental crust, not of its surface -- see SUPERCONTINENT_CRUST_SHARE.
        const float share =
            static_cast<float>(contCells[static_cast<std::size_t>(pid)]) / contCellsTotal;
        if (share >= SUPERCONTINENT_CRUST_SHARE) {
            field.thermalAgeMy[i] += dtMy;
        } else {
            // Reset slowly — once a plate is no longer supercontinent
            // its thermal blanketing relaxes over ~RIFT_THRESHOLD_MY.
            field.thermalAgeMy[i] *= thermalDecayFactor;
        }
        thermalSum[static_cast<std::size_t>(pid)] += static_cast<double>(field.thermalAgeMy[i]);
        ++thermalCount[static_cast<std::size_t>(pid)];
    }

    if (std::getenv("AOC_SPHEREPHYS_TRACE") != nullptr) {
        // The rift trigger is the hardest pass in the file to reason about from
        // the outside, because "no rift happened" and "rift is disabled" look
        // identical downstream. Report the quantities the two gates actually
        // test, so a stalled Wilson cycle is visible as a number rather than as
        // an absence.
        float maxContFrac  = 0.0f;
        float maxTotalFrac = 0.0f;
        float maxThermal   = 0.0f;
        for (std::size_t i = 0; i < N; ++i) {
            maxContFrac  = std::max(maxContFrac, static_cast<float>(contCells[i]) / contCellsTotal);
            maxTotalFrac = std::max(maxTotalFrac, static_cast<float>(totalCells[i]) / globeCells);
            if (thermalCount[i] > 0) {
                maxThermal =
                    std::max(maxThermal, static_cast<float>(thermalSum[i] /
                                                            static_cast<double>(thermalCount[i])));
            }
        }
        std::fprintf(
            stderr,
            "[wilson] max plate crust share=%.3f (needs %.2f) total=%.3f (forces %.2f) "
            "meanThermal=%.0f (needs %.0f)\n",
            static_cast<double>(maxContFrac), static_cast<double>(SUPERCONTINENT_CRUST_SHARE),
            static_cast<double>(maxTotalFrac), static_cast<double>(FORCE_RIFT_TOTAL_FRACTION),
            static_cast<double>(maxThermal), static_cast<double>(RIFT_THRESHOLD_MY));
    }

    // Decide which plates rift this epoch. Single-rift-per-epoch cap
    // matches the empirical observation that rift bursts are clustered
    // in time: 290 plate births in 5 Myr at the Pangaea breakup
    // (Müller 2022 birth histogram).
    int32_t newPlates = 0;
    for (std::size_t i = 0; i < N; ++i) {
        const float totalFrac = static_cast<float>(totalCells[i]) / globeCells;
        const bool forceRift  = (totalFrac >= FORCE_RIFT_TOTAL_FRACTION);
        if (forceRift) {
            // Forced split for over-large plates — bypass thermal age.
            // Minimum cell count still needed to form a meaningful child.
            if (totalCells[i] < 4) continue;
        } else {
            if (thermalCount[i] < 4) continue; // Plate too small to rift.
            const float meanThermal =
                static_cast<float>(thermalSum[i] / static_cast<double>(thermalCount[i]));
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
            const LatLon p       = SphereField::cellCenter(lonIdx, latIdx);
            const double latR    = static_cast<double>(p.latDeg) * DEG2RAD;
            const double lonR    = static_cast<double>(p.lonDeg) * DEG2RAD;
            const double cosLat  = std::cos(latR);
            const double cxv     = cosLat * std::cos(lonR);
            const double cyv     = cosLat * std::sin(lonR);
            const double czv     = std::sin(latR);
            plateCells.push_back({cell, cxv, cyv, czv});
            mx += cxv;
            my += cyv;
            mz += czv;
        }
        if (plateCells.empty()) continue;
        const double inv = 1.0 / static_cast<double>(plateCells.size());
        mx *= inv;
        my *= inv;
        mz *= inv;

        // Compute covariance to extract principal axis. With ~hundreds
        // of cells the 3x3 power-iteration converges in <10 steps.
        double cxx = 0, cyy = 0, czz = 0, cxy = 0, cxz = 0, cyz = 0;
        for (const CellVec& v : plateCells) {
            const double dx = v.cx - mx;
            const double dy = v.cy - my;
            const double dz = v.cz - mz;
            cxx += dx * dx;
            cyy += dy * dy;
            czz += dz * dz;
            cxy += dx * dy;
            cxz += dx * dz;
            cyz += dy * dz;
        }
        // Power iteration on covariance for top eigenvector.
        double vx = 1.0, vy = 0.0, vz = 0.0;
        for (int iter = 0; iter < 12; ++iter) {
            const double nx  = cxx * vx + cxy * vy + cxz * vz;
            const double ny  = cxy * vx + cyy * vy + cyz * vz;
            const double nz  = cxz * vx + cyz * vy + czz * vz;
            const double mag = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (mag < 1e-12) break;
            vx = nx / mag;
            vy = ny / mag;
            vz = nz / mag;
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
        Plate child               = plates[i];
        const float poleOffsetDeg = 60.0f * (xorshift01(rngState) - 0.5f) * 2.0f;
        child.eulerPoleLatDeg = std::clamp(child.eulerPoleLatDeg + poleOffsetDeg, -89.0f, 89.0f);
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
        assert(plates.size() <= 32767u && "plate count exceeds int16_t capacity");
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
        const double nrmMag    = std::sqrt(nrm_x * nrm_x + nrm_y * nrm_y + nrm_z * nrm_z);
        const double invNrmMag = (nrmMag > 1e-9) ? 1.0 / nrmMag : 0.0;
        if (invNrmMag == 0.0) {
            ++newPlates;
            break;
        }
        // 2026-07-05 rift-seam geometry (defect D6b). The previous
        // split was the PCA-median great circle: every rifted
        // coastline was born DEAD STRAIGHT and every fragment pair
        // near-equal -- exactly the two artifact classes (straight
        // coasts, same-size fragments) the program removes elsewhere.
        // Real conjugate margins are sinuous (the Atlantic S-curve)
        // and supercontinents shed UNEQUAL fragments.
        //
        // (a) Asymmetric split: the plane offset is drawn as a
        //     fragment-share quantile q ~ 0.5 * exp(N(0, 0.5)),
        //     clamped to [0.18, 0.82] -- a log-normal size hierarchy
        //     instead of a fixed halving.
        // (b) Sinuous trace: the split threshold wiggles along the
        //     principal axis with smoothHashNoise (+-0.05 in
        //     sin-projection units ~ +-3 deg), so the seam -- and the
        //     two future passive coastlines it becomes -- meanders.
        //     Pure hash noise: deterministic, no RNG-stream coupling
        //     beyond the two explicit draws below.
        const float qGauss =
            (xorshift01(rngState) + xorshift01(rngState) + xorshift01(rngState)) * 2.0f - 3.0f;
        const double splitShare =
            std::clamp(0.5 * std::exp(0.5 * static_cast<double>(qGauss)), 0.18, 0.82);
        const uint64_t seamSeed = mixSeed((static_cast<uint64_t>(rngState) << 20) ^ 0x52494654ULL);
        // Per-cell projection across the seam (sin of angular distance
        // to the great circle) and along the principal axis.
        std::vector<double> projAcross(plateCells.size());
        std::vector<double> projAlong(plateCells.size());
        std::vector<double> sortedAcross;
        sortedAcross.reserve(plateCells.size());
        for (std::size_t k = 0; k < plateCells.size(); ++k) {
            const CellVec& v = plateCells[k];
            projAcross[k]    = (v.cx * nrm_x + v.cy * nrm_y + v.cz * nrm_z) * invNrmMag;
            projAlong[k]     = v.cx * vx + v.cy * vy + v.cz * vz;
            sortedAcross.push_back(projAcross[k]);
        }
        std::sort(sortedAcross.begin(), sortedAcross.end());
        const std::size_t qIdx = std::min(
            sortedAcross.size() - 1,
            static_cast<std::size_t>(splitShare * static_cast<double>(sortedAcross.size())));
        const double splitAt = sortedAcross[qIdx];
        // Seam sinuosity. A rift splits the plate along a great circle, and a
        // great circle is a DEAD STRAIGHT line in any cylindrical projection --
        // so every rifted coastline is born as a ruler edge, and since the seam
        // becomes two conjugate passive margins it stays one for the rest of
        // the run. Measured: a single seam produced an unbroken meridional
        // coast spanning 58 of 90 rows, and drove coastline axis_aligned_frac
        // to 0.65 against a 0.50 isotropic null.
        //
        // The previous +-0.05 (~3 deg) wiggle was too small to break that up by
        // roughly an order of magnitude. Real rifted margins meander at
        // continental scale and are segmented by transform offsets of hundreds
        // of kilometres -- the Atlantic S-curve and its fracture-zone
        // staircase. Three octaves: a ~1000 km meander, a ~400 km one, and a
        // short-wavelength roughness that keeps the trace from reading as a
        // smooth arc either.
        //
        // Units: `projAcross` is the sine of the angular distance from the
        // split plane, so 0.18 is ~10 deg ~ 1150 km of lateral excursion.
        constexpr double SEAM_WIGGLE_SIN        = 0.18;
        constexpr float SEAM_WIGGLE_FREQ        = 2.5f;
        constexpr int32_t SEAM_WIGGLE_OCTAVES   = 3;
        constexpr float SEAM_WIGGLE_LACUNARITY  = 2.7f;
        constexpr float SEAM_WIGGLE_PERSISTENCE = 0.5f;
        // Conjugate passive margins by McKenzie (1978) stretching.
        //
        // Before this the seam was a STEP: fresh 7 km oceanic crust inside
        // RIFT_AXIS_OCEAN_SIN_HALF, untouched 41 km craton immediately
        // outside. Nothing else in the simulation thins continental crust, so
        // -- combined with an erosion base level defined as `seaLevel + 600 m`,
        // i.e. above sea level by construction -- NO continental cell could
        // ever be submerged. Measured before this change: the thinnest crust
        // anywhere was 30.1 km while this planet's sea level sat at 29.7 km of
        // crust. Land area was therefore continental-crust area exactly, every
        // coastline was the continentalFraction = 0.5 contour, and a
        // continental shelf was impossible rather than rare.
        //
        // Stretching factor decays exponentially inboard from the
        // ocean-continent transition, which is the standard necking geometry
        // (sharp thinning at the OCT, a broad gently-stretched inner margin --
        // Iberia-Newfoundland, the Atlantic conjugates): crust goes to ~10 km
        // at the OCT and recovers to its unstretched thickness over ~4 necking
        // lengths. With the re-anchored elevation law that profile puts the
        // shoreline ~200 km inboard of the OCT and drowns everything seaward of
        // it, which is what a passive margin IS.
        //
        // Both parameters are modulated along-strike by the same hash noise
        // family as the seam wiggle, so conjugate margins vary in width the way
        // real ones do instead of being a constant-width ribbon.
        //
        // Known non-conservation, to revisit with the sediment work: real
        // stretching spreads a fixed crustal volume over a WIDER area, but the
        // raster's cell area is fixed, so thinning here removes mass rather
        // than redistributing it.
        constexpr float RIFT_BETA_MAX_MEAN     = 3.5f;
        constexpr float RIFT_NECK_LENGTH_KM    = 70.0f;
        constexpr float RIFT_MARGIN_NECK_SPANS = 4.0f;
        const float axialHalfKm = static_cast<float>(std::asin(RIFT_AXIS_OCEAN_SIN_HALF)) *
                                  PhysicsConstants::earthRadiusKm;
        // Crustal-mass ledger for this rift, in the same relative units as
        // continentalCrustVolume (km x cos-lat). Diagnostic only -- it measures
        // the non-conservation documented above rather than correcting it.
        // Accumulated in plateCells order, which is a deterministic row-major
        // scan filtered by plateId, and this loop is serial.
        // Read once: this function runs every epoch, and getenv walks the
        // environment (the same reason stepSpherePhysicsEpoch caches its flag).
        static const bool kRiftMassTrace = std::getenv("AOC_SPHEREPHYS_TRACE") != nullptr;
        const double contVolBefore       = kRiftMassTrace ? continentalCrustVolume(field) : 0.0;
        double removedByThinning         = 0.0;
        double convertedToOcean          = 0.0;
        for (std::size_t k = 0; k < plateCells.size(); ++k) {
            const std::size_t cell = plateCells[k].cellIdx;
            double wiggle          = 0.0;
            double wAmp            = 1.0;
            double wNorm           = 0.0;
            float wFreq            = SEAM_WIGGLE_FREQ;
            for (int32_t o = 0; o < SEAM_WIGGLE_OCTAVES; ++o) {
                wiggle +=
                    wAmp *
                    (static_cast<double>(smoothHashNoise(static_cast<float>(projAlong[k]) * wFreq,
                                                         static_cast<float>(o) * 0.37f, seamSeed)) -
                     0.5) *
                    2.0;
                wNorm += wAmp;
                wAmp *= SEAM_WIGGLE_PERSISTENCE;
                wFreq *= SEAM_WIGGLE_LACUNARITY;
            }
            wiggle           = (wiggle / wNorm) * SEAM_WIGGLE_SIN;
            const double eff = projAcross[k] - splitAt - wiggle;
            if (eff < 0.0) {
                field.plateId[cell] = childId;
            }
            field.thermalAgeMy[cell] = 0.0f;
            // Cells within the seam band become fresh oceanic crust:
            // the proto-ocean basin whose two flanks are conjugate
            // passive margins (Atlantic-style opening).
            if (std::fabs(eff) < RIFT_AXIS_OCEAN_SIN_HALF) {
                if (kRiftMassTrace && field.continentalFraction[cell] >= 0.5f) {
                    // cos(lat) without recomputing geometry: cz is sin(lat).
                    const double cz = plateCells[k].cz;
                    convertedToOcean += static_cast<double>(field.crustThicknessKm[cell]) *
                                        std::sqrt(std::max(0.0, 1.0 - cz * cz));
                }
                field.crustThicknessKm[cell]    = PhysicsConstants::initialOceanicThicknessKm;
                field.continentalFraction[cell] = 0.0f;
                field.crustAgeMy[cell]          = 0.0f;
                field.sutureContactMy[cell]     = 0.0f;
                continue;
            }
            // Continental flanks only: the axial band above is already ocean,
            // and stretching an oceanic column is not what this models.
            if (field.continentalFraction[cell] <= 0.5f) {
                continue;
            }
            // `eff` is the sine of the angular offset from the (wiggled) seam
            // plane; small-angle, so asin recovers the arc directly.
            const float offsetKm  = static_cast<float>(std::asin(std::min(1.0, std::fabs(eff)))) *
                                    PhysicsConstants::earthRadiusKm;
            const float inboardKm = offsetKm - axialHalfKm;
            // Along-strike modulation, +-35 % on both the peak stretching and
            // the necking length. Two decorrelated hash channels so width and
            // depth of thinning do not co-vary into a uniform ribbon.
            const float alongCoord = static_cast<float>(projAlong[k]);
            const float betaJitter =
                1.0f + 0.35f * (2.0f * smoothHashNoise(alongCoord * 5.0f, 0.25f, seamSeed) - 1.0f);
            const float neckJitter =
                1.0f + 0.35f * (2.0f * smoothHashNoise(alongCoord * 3.0f, 0.75f, seamSeed) - 1.0f);
            const float neckKm   = RIFT_NECK_LENGTH_KM * neckJitter;
            const float marginKm = neckKm * RIFT_MARGIN_NECK_SPANS;
            if (inboardKm >= marginKm) {
                continue;
            }
            const float betaMax = RIFT_BETA_MAX_MEAN * betaJitter;
            const float beta    = 1.0f + (betaMax - 1.0f) * std::exp(-inboardKm / neckKm);
            if (beta <= 1.0f) {
                continue;
            }
            if (kRiftMassTrace) {
                // Mass this divide is about to delete, before it happens.
                const double cz = plateCells[k].cz;
                removedByThinning += static_cast<double>(field.crustThicknessKm[cell]) *
                                     (1.0 - 1.0 / static_cast<double>(beta)) *
                                     std::sqrt(std::max(0.0, 1.0 - cz * cz));
            }
            // Thins only, never thickens: beta > 1 is guaranteed above, so a
            // cell already thinned by an earlier rift thins further rather than
            // being reset to a stretched-from-pristine value.
            field.crustThicknessKm[cell] /= beta;
            field.stretchFactor[cell] = std::max(field.stretchFactor[cell], beta);
        }
        if (std::getenv("AOC_SPHEREPHYS_TRACE") != nullptr) {
            std::size_t seamCells = 0;
            std::size_t thinned   = 0;
            for (const CellVec& v : plateCells) {
                if (field.continentalFraction[v.cellIdx] <= 0.5f) {
                    ++seamCells;
                } else if (field.crustThicknessKm[v.cellIdx] <
                           PhysicsConstants::refContinentalThicknessKm * 0.95f) {
                    ++thinned;
                }
            }
            std::fprintf(stderr,
                         "[rift] plate %zu split (%zu cells); oceanic-composition %zu, "
                         "thinned continental %zu\n",
                         i, plateCells.size(), seamCells, thinned);
            // Crustal-mass ledger. `thinned` mass is the genuinely unaccounted
            // part -- it is deleted outright. `toOcean` is the axial band being
            // converted to fresh 7 km basalt, which is what rifting physically
            // does, so it is reported separately rather than lumped in as loss.
            const double pct = (contVolBefore > 0.0) ? 100.0 / contVolBefore : 0.0;
            std::fprintf(stderr,
                         "[riftmass] plate %zu contVolBefore=%.4g thinned=%.4g (%.3f%%) "
                         "toOcean=%.4g (%.3f%%) total=(%.3f%%)\n",
                         i, contVolBefore, removedByThinning, removedByThinning * pct,
                         convertedToOcean, convertedToOcean * pct,
                         (removedByThinning + convertedToOcean) * pct);
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
    plates                = std::move(survivors);
    return removed;
}

void mergePlatesBatch(SphereField& field, std::vector<Plate>& plates,
                      const std::vector<std::pair<std::size_t, std::size_t>>& pairs) {
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
            x         = parent[x];
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
            const int16_t selfId  = field.plateId[idx];
            if (selfId < 0 || static_cast<std::size_t>(selfId) >= N) continue;
            const std::size_t selfRoot = findRoot(static_cast<std::size_t>(selfId));
            const int32_t lonW         = (lonIdx == 0) ? LON - 1 : lonIdx - 1;
            const int32_t lonE         = (lonIdx == LON - 1) ? 0 : lonIdx + 1;
            const int32_t latS         = std::max(0, latIdx - 1);
            const int32_t latN         = std::min(LAT - 1, latIdx + 1);
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
                    field.boundaryType[idx]            = 0;
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
        const double latR   = static_cast<double>(plates[i].latDeg) * DEG2RAD;
        const double lonR   = static_cast<double>(plates[i].lonDeg) * DEG2RAD;
        const double cosLat = std::cos(latR);
        rootSx[r] += cosLat * std::cos(lonR);
        rootSy[r] += cosLat * std::sin(lonR);
        rootSz[r] += std::sin(latR);
        ++rootCount[r];
    }
    for (std::size_t r = 0; r < N; ++r) {
        if (rootCount[r] <= 1) continue; // Not a merged root.
        const double mag =
            std::sqrt(rootSx[r] * rootSx[r] + rootSy[r] * rootSy[r] + rootSz[r] * rootSz[r]);
        if (mag < 1e-9) continue; // antipodal — keep prior centroid.
        const double mx  = rootSx[r] / mag;
        const double my  = rootSy[r] / mag;
        const double mz  = rootSz[r] / mag;
        plates[r].latDeg = static_cast<float>(std::asin(std::clamp(mz, -1.0, 1.0)) * RAD2DEG);
        plates[r].lonDeg = static_cast<float>(std::atan2(my, mx) * RAD2DEG);
    }
    for (std::size_t i = 0; i < N; ++i) {
        const std::size_t r = findRoot(i);
        if (r == i) continue;
        plates[r].landFraction = std::max(plates[r].landFraction, plates[i].landFraction);
        plates[r].mergesAbsorbed += 1 + plates[i].mergesAbsorbed;
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
void advectPlateOwnership(SphereField& field, const std::vector<Plate>& plates, float dtMy) {
    if (plates.empty()) {
        LOG_WARN("SphereFieldPhysics: %s called with empty plates -- skipping", __func__);
        return;
    }
    if (dtMy <= 0.0f) return;
    constexpr int32_t LON    = SphereField::LON_CELLS;
    constexpr int32_t LAT    = SphereField::LAT_CELLS;
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
        r.axX               = cosLat * std::cos(poleLonR);
        r.axY               = cosLat * std::sin(poleLonR);
        r.axZ               = std::sin(poleLatR);
        r.cosT              = std::cos(thetaR);
        r.sinT              = std::sin(thetaR);
        r.oneMc             = 1.0 - r.cosT;
        return r;
    };
    const std::size_t P = plates.size();
    std::vector<PlateRot> rotBack(P), rotFwd(P);
    for (std::size_t i = 0; i < P; ++i) {
        const double poleLatR = static_cast<double>(plates[i].eulerPoleLatDeg) * DEG2RAD;
        const double poleLonR = static_cast<double>(plates[i].eulerPoleLonDeg) * DEG2RAD;
        const double absTheta =
            static_cast<double>(plates[i].angularVelDeg) * DEG2RAD * static_cast<double>(dtMy);
        rotBack[i] = buildRot(poleLatR, poleLonR, -absTheta);
        rotFwd[i]  = buildRot(poleLatR, poleLonR, +absTheta);
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
    std::vector<float> newCrust(N, 0.0f);
    std::vector<float> newContFrac(N, 0.0f);
    std::vector<float> newAge(N, 0.0f);
    std::vector<float> newSurface(N, 0.0f);
    std::vector<float> newThermal(N, 0.0f);
    std::vector<float> newSuture(N, 0.0f);
    std::vector<float> newStretch(N, 1.0f);
    // Aliasing ledger: which sources have already been consumed this
    // substep. A rigid rotation is area-preserving, so wherever the
    // rounded backward map sends TWO destinations to one source
    // (collision) there is an adjacent source NO destination samples
    // (orphan) -- and under near-zonal rotation these pair up in full
    // latitude rows. Echo destinations therefore re-target the
    // adjacent orphan instead of duplicating (old behaviour, crust
    // fabrication) or turning to wake (intermediate behaviour, which
    // punched full-width ocean stripes through zonally-moving
    // continents).
    std::vector<uint8_t> claimed(N, 0);
    std::size_t pass1Orphan = 0;
    std::size_t pass2Claim  = 0;
    std::size_t pass3Wake   = 0;

    // Fixed sub-cell dither applied to the departure point before it is
    // rounded to a cell. Deterministic in the cell index alone -- NOT in the
    // substep -- so it is a stationary spatial jitter, not a random walk that
    // would diffuse the fields over the ~900 substeps of a run.
    //
    // Why it is needed. Plate Euler poles cluster at high latitude (Gripp &
    // Gordon 2002), so plate motion is dominantly ZONAL: every cell in a
    // latitude row displaces by the same amount, and the backward map rounds
    // that to the same integer cell shift for the whole row. Adjacent rows
    // round to DIFFERENT integers, so any feature spanning rows is sheared
    // into whole-cell steps with dead-straight row-aligned edges -- the long
    // horizontal 1-cell stripes visible in the rendered plate raster, and the
    // thin horizontal land ribbons they carve out of a continent.
    //
    // Offsetting each departure point by a fixed fraction of a cell turns that
    // shared rounding boundary into a stationary irregular curve: the
    // quantisation is still there (it must be, the raster is discrete) but it
    // no longer lines up along parallels. Amplitude is +-0.5 cell, i.e. the
    // rounding interval itself, which is the largest offset that cannot move a
    // sample more than one cell from where it belongs.
    auto cellDither = [](std::size_t cellIdx, float& dLatDeg, float& dLonDeg) {
        uint64_t h = static_cast<uint64_t>(cellIdx) * 0x9E3779B97F4A7C15ULL;
        h ^= h >> 29;
        h *= 0xBF58476D1CE4E5B9ULL;
        h ^= h >> 32;
        const float u = static_cast<float>((h >> 11) & 0xFFFFu) / 65535.0f;
        const float v = static_cast<float>((h >> 33) & 0xFFFFu) / 65535.0f;
        dLatDeg       = (u - 0.5f) * SphereField::CELL_DEG;
        dLonDeg       = (v - 0.5f) * SphereField::CELL_DEG;
    };

    auto rotateRodrigues = [&](const PlateRot& R, double cx, double cy, double cz,
                               std::size_t destIdx) -> std::size_t {
        const double dot       = R.axX * cx + R.axY * cy + R.axZ * cz;
        const double crossX    = R.axY * cz - R.axZ * cy;
        const double crossY    = R.axZ * cx - R.axX * cz;
        const double crossZ    = R.axX * cy - R.axY * cx;
        const double nX        = cx * R.cosT + crossX * R.sinT + R.axX * dot * R.oneMc;
        const double nY        = cy * R.cosT + crossY * R.sinT + R.axY * dot * R.oneMc;
        const double nZ        = cz * R.cosT + crossZ * R.sinT + R.axZ * dot * R.oneMc;
        const double clampedZ  = std::clamp(nZ, -1.0, 1.0);
        const double depLatDeg = std::asin(clampedZ) * RAD2DEG;
        const double depLonDeg = std::atan2(nY, nX) * RAD2DEG;
        float dLat             = 0.0f;
        float dLon             = 0.0f;
        cellDither(destIdx, dLat, dLon);
        const SphereField::CellCoord dep = SphereField::locate(
            static_cast<float>(depLatDeg) + dLat, static_cast<float>(depLonDeg) + dLon);
        return SphereField::cellIndex(dep.lonIdx, dep.latIdx);
    };

    // The cell's 8 raster neighbours, ordered by TRUE GROUND DISTANCE,
    // nearest first. Returns the count written (self-references at the polar
    // row clamp are dropped).
    //
    // 2026-08-12. Both the echo re-target in pass 1 and the leading-edge claim
    // in pass 2 previously used the 4 CARDINAL neighbours in a fixed
    // N/S/W/E order. Two axis artifacts follow, and they compound over the ~15
    // CFL substeps per epoch x 60 epochs this function runs:
    //   - a leading edge can only advance along the lon/lat axes, never
    //     diagonally, so a moving plate outline is progressively squared off
    //     into an L-infinity ball;
    //   - a fixed probe order always prefers a latitude neighbour, so echo
    //     re-targeting accumulates row structure.
    // Rendering the plate raster at epochs 1/5/15/30/60 shows exactly that
    // progression: organic lens-shaped plates at epoch 1 degrading into
    // rectangles and horizontal bands by epoch 30, with the final coastline
    // running dead straight along those boundaries. The identical 4-cardinal
    // defect was already found and fixed in accreteToNeighbours, whose comment
    // records that it "flattened every growth front into an axis-aligned wall
    // ... shows up as coastline axis_aligned_frac well above the 0.50
    // isotropic null"; this pass never received the same treatment.
    //
    // Ordering by ground distance rather than by index also removes the
    // latitude bias for free: toward the poles a longitude step spans far less
    // ground than a latitude step, so the east/west neighbours genuinely ARE
    // nearer and should be preferred. Ties break on cell index, so the order
    // is total and thread-count independent.
    struct RankedNeighbour {
        std::size_t idx;
        float dist2;
    };
    auto orderedNeighbours = [&](int32_t lonIdx, int32_t latIdx, RankedNeighbour* out) -> int32_t {
        const LatLon p         = SphereField::cellCenter(lonIdx, latIdx);
        const float cosLat     = std::cos(static_cast<float>(p.latDeg) * 0.01745329252f);
        const float cosLat2    = cosLat * cosLat;
        const std::size_t self = SphereField::cellIndex(lonIdx, latIdx);
        int32_t n              = 0;
        for (int32_t dLat = -1; dLat <= 1; ++dLat) {
            for (int32_t dLon = -1; dLon <= 1; ++dLon) {
                if (dLon == 0 && dLat == 0) continue;
                const int32_t nLat = latIdx + dLat;
                if (nLat < 0 || nLat >= LAT) continue; // poles: no wrap in lat
                int32_t nLon = lonIdx + dLon;
                if (nLon < 0) nLon += LON;
                if (nLon >= LON) nLon -= LON;
                const std::size_t nIdx = SphereField::cellIndex(nLon, nLat);
                if (nIdx == self) continue;
                out[n].idx = nIdx;
                out[n].dist2 =
                    static_cast<float>(dLon * dLon) * cosLat2 + static_cast<float>(dLat * dLat);
                ++n;
            }
        }
        // Insertion sort: n <= 8, and it keeps the comparison explicit so the
        // tie-break on index is visibly part of the ordering.
        for (int32_t i = 1; i < n; ++i) {
            const RankedNeighbour key = out[i];
            int32_t j                 = i - 1;
            while (j >= 0 && (out[j].dist2 > key.dist2 ||
                              (out[j].dist2 == key.dist2 && out[j].idx > key.idx))) {
                out[j + 1] = out[j];
                --j;
            }
            out[j + 1] = key;
        }
        return n;
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
            const int16_t incumbent   = field.plateId[destIdx];

            const LatLon p      = SphereField::cellCenter(lonIdx, latIdx);
            const double latR   = static_cast<double>(p.latDeg) * DEG2RAD;
            const double lonR   = static_cast<double>(p.lonDeg) * DEG2RAD;
            const double cosLat = std::cos(latR);
            const double cx     = cosLat * std::cos(lonR);
            const double cy     = cosLat * std::sin(lonR);
            const double cz     = std::sin(latR);

            if (incumbent >= 0 && static_cast<std::size_t>(incumbent) < P) {
                std::size_t depIdx = rotateRodrigues(rotBack[static_cast<std::size_t>(incumbent)],
                                                     cx, cy, cz, destIdx);
                if (field.plateId[depIdx] == incumbent) {
                    if (claimed[depIdx]) {
                        // Echo: this source already moved to another
                        // destination. Pair with the NEAREST unclaimed orphan
                        // source of the same plate, measured in ground
                        // distance over all 8 neighbours -- not the first hit
                        // of a fixed N/S-then-E/W scan over 4, which biased
                        // every re-target toward the latitude axis.
                        const int32_t dLon =
                            static_cast<int32_t>(depIdx % static_cast<std::size_t>(LON));
                        const int32_t dLat =
                            static_cast<int32_t>(depIdx / static_cast<std::size_t>(LON));
                        RankedNeighbour probe[8];
                        const int32_t probeCount = orderedNeighbours(dLon, dLat, probe);
                        std::size_t alt          = SIZE_MAX;
                        for (int32_t q = 0; q < probeCount; ++q) {
                            const std::size_t cand = probe[q].idx;
                            if (claimed[cand]) continue;
                            if (field.plateId[cand] != incumbent) continue;
                            alt = cand;
                            break;
                        }
                        if (alt == SIZE_MAX) {
                            // No orphan nearby: fall back to this
                            // cell's own (unmoved) column if free.
                            if (!claimed[destIdx] && field.plateId[destIdx] == incumbent) {
                                alt = destIdx;
                            }
                        }
                        if (alt == SIZE_MAX) {
                            newOwner[destIdx] = VACATED;
                            ++pass1Orphan;
                            continue;
                        }
                        depIdx = alt;
                    }
                    claimed[depIdx] = 1u;
                    // Incumbent claim: copy state from departure cell.
                    newOwner[destIdx]    = incumbent;
                    newCrust[destIdx]    = field.crustThicknessKm[depIdx];
                    newContFrac[destIdx] = field.continentalFraction[depIdx];
                    newAge[destIdx]      = field.crustAgeMy[depIdx];
                    newSurface[destIdx]  = field.surfaceElevationM[depIdx];
                    newThermal[destIdx]  = field.thermalAgeMy[depIdx];
                    newSuture[destIdx]   = field.sutureContactMy[depIdx];
                    newStretch[destIdx]  = field.stretchFactor[depIdx];
                    continue;
                }
            }

            // Vacated: incumbent's backward rotation walked off plate
            // footprint. Pass 2 below tries to find a claimant.
            newOwner[destIdx] = VACATED;
            ++pass1Orphan;
        }
    }

    // PASS 2: vacated cells claimed by neighbours moving INTO them.
    // For each vacated cell we ask: does any neighbour plate's
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
    // AOC_NO_ADVECT_REPAIR skips passes 2 and 3 -- the orphan-claim and
    // wake-fill repairs. Pass 1 is an exact rigid backward rotation and cannot
    // smear; these two are what copy crust from an arbitrary nearby source into
    // a cell that had no valid departure point, ~900 times per world, on the
    // very field whose 0.5 contour is the coastline. Measured: with advection
    // entirely off, crust perimeter/equal-area-disc falls 3.34 -> 1.48 (seed
    // 42) and the largest crust component 0.92 -> 0.42. This gate isolates how
    // much of that is the repair passes specifically.
    static const bool kNoRepair = std::getenv("AOC_NO_ADVECT_REPAIR") != nullptr;
    for (int32_t latIdx = 0; !kNoRepair && latIdx < LAT; ++latIdx) {
        for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
            const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
            if (newOwner[idx] != VACATED) continue;

            // All 8 neighbours, so a leading edge can advance diagonally.
            // Restricting the claim to the 4 cardinals meant a moving plate
            // front could only ever step along the raster axes, which squares
            // off plate outlines a little more on every one of the ~900
            // substeps in a run.
            RankedNeighbour nbr[8];
            const int32_t nbrCount = orderedNeighbours(lonIdx, latIdx, nbr);

            int16_t bestPid     = -1;
            std::size_t bestSrc = 0;
            float bestFrac      = -1.0f;
            float bestOmega     = 1e9f;

            for (int32_t k = 0; k < nbrCount; ++k) {
                const std::size_t nIdxK = nbr[k].idx;
                const int16_t nPid      = field.plateId[nIdxK];
                if (nPid < 0 || static_cast<std::size_t>(nPid) >= P) continue;

                // Forward-rotate the neighbour's centre. If the result
                // lands in our vacated cell, the neighbour plate is
                // converging into this cell.
                const int32_t nLon   = static_cast<int32_t>(nIdxK % LON);
                const int32_t nLat   = static_cast<int32_t>(nIdxK / LON);
                const LatLon nP      = SphereField::cellCenter(nLon, nLat);
                const double nLatR   = static_cast<double>(nP.latDeg) * DEG2RAD;
                const double nLonR   = static_cast<double>(nP.lonDeg) * DEG2RAD;
                const double nCosLat = std::cos(nLatR);
                const double ncx     = nCosLat * std::cos(nLonR);
                const double ncy     = nCosLat * std::sin(nLonR);
                const double ncz     = std::sin(nLatR);
                const std::size_t fwdDest =
                    rotateRodrigues(rotFwd[static_cast<std::size_t>(nPid)], ncx, ncy, ncz, nIdxK);
                if (fwdDest != idx) continue;

                const float frac  = field.continentalFraction[nIdxK];
                const float omega = std::fabs(plates[static_cast<std::size_t>(nPid)].angularVelDeg);
                bool replace      = false;
                if (bestPid < 0) {
                    replace = true;
                } else if (frac > 0.5f && bestFrac <= 0.5f) {
                    replace = true; // continental overrides oceanic
                } else if ((frac > 0.5f) == (bestFrac > 0.5f) && omega < bestOmega) {
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
                newSuture[idx]   = field.sutureContactMy[bestSrc];
                newStretch[idx]  = field.stretchFactor[bestSrc];
                ++pass2Claim;
            }
            // else: still VACATED -> pass 3 wake fill
        }
    }

    // PASS 3: wake fill. Cells still VACATED after pass 2 had no
    // converging neighbour -- they sit behind a receding trailing
    // edge. 2026-07-06: ownership goes to the PRIOR INCUMBENT (the
    // plate whose trailing edge vacated the cell) -- the half-
    // spreading picture: each ridge flank rides the plate that pulled
    // away from it. The previous slowest-4-neighbour rule handed wake
    // to whatever cratonic plate happened to touch it, and read
    // same-sweep newOwner values through a 4-iteration expansion loop
    // -- growing raster-order-dependent ribbons of DISCONNECTED
    // slow-plate territory up to 4 cells per substep (the primary
    // plate-archipelago generator, measured 8-12 fragmented plates
    // with up to 121 components each).
    //
    // Continental crust is buoyant (Cogley 1984) and keeps its column;
    // it stays with its own plate. The keep is REGISTERED in the
    // conservation ledger: pass 1 may have also copied this column
    // forward, and an unregistered keep bypassed the divide-by-k
    // thinning (silent crust duplication at every trailing
    // continental edge). Previously-oceanic cells become fresh
    // mid-ocean-ridge basalt (Turcotte & Schubert 2014 ch. 2).
    for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
        for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
            const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
            if (newOwner[idx] != VACATED) continue;
            const int16_t prior = field.plateId[idx];
            if (prior < 0 || static_cast<std::size_t>(prior) >= P) {
                continue; // fallback sweep below assigns these
            }
            newOwner[idx] = prior;
            // Continental trailing cells KEEP their column (with the
            // PRIOR owner -- the contiguity fix is the ownership, not
            // the crust). This keep is not literal "crust stays
            // behind": it is the discrete counterweight to the raster
            // transport's leading-edge sink -- at a blocked leading
            // boundary the front row of columns is deleted every
            // substep (incumbent wins; the column has nowhere to go),
            // which is the shortening/deformation a rigid-translation
            // scheme cannot express. Dropping the keep while the sink
            // remained bled continental area 16 % -> 4 % over one run.
            // The two discretisation artifacts cancel in aggregate;
            // neither is registered in the aliasing ledger.
            // Previously-oceanic cells become fresh mid-ocean-ridge
            // basalt (Turcotte & Schubert 2014 ch. 2).
            if (field.continentalFraction[idx] > 0.5f) {
                newCrust[idx]    = field.crustThicknessKm[idx];
                newContFrac[idx] = field.continentalFraction[idx];
                newAge[idx]      = field.crustAgeMy[idx];
                newSurface[idx]  = field.surfaceElevationM[idx];
                newThermal[idx]  = field.thermalAgeMy[idx];
                newSuture[idx]   = field.sutureContactMy[idx];
                newStretch[idx]  = field.stretchFactor[idx];
            } else {
                newCrust[idx]    = PhysicsConstants::initialOceanicThicknessKm;
                newContFrac[idx] = 0.0f;
                newAge[idx]      = 0.0f;
                newSurface[idx]  = 0.0f;
                newThermal[idx]  = 0.0f;
                newStretch[idx]  = 1.0f;
            }
            ++pass3Wake;
        }
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
            newStretch[i]  = 1.0f;
        }
    }

    // Env var read once (it cannot change mid-run); this function runs
    // every advection epoch, so a per-call getenv() walked the environment
    // each epoch for a diagnostic that is off by default.
    static const bool kAdvectTrace = std::getenv("AOC_ADVECT_TRACE") != nullptr;
    if (kAdvectTrace) {
        std::fprintf(stderr, "[advect] dt=%.3fMy pass1Orphan=%zu pass2Claim=%zu pass3Wake=%zu\n",
                     static_cast<double>(dtMy), pass1Orphan, pass2Claim, pass3Wake);
    }

    field.plateId             = std::move(newOwner);
    field.crustThicknessKm    = std::move(newCrust);
    field.continentalFraction = std::move(newContFrac);
    field.crustAgeMy          = std::move(newAge);
    field.surfaceElevationM   = std::move(newSurface);
    field.thermalAgeMy        = std::move(newThermal);
    field.sutureContactMy     = std::move(newSuture);
    field.stretchFactor       = std::move(newStretch);
}

void markBoundaryCells(const SphereField& field, std::vector<uint8_t>& isBoundary) {
    isBoundary.assign(SphereField::CELL_COUNT, 0u);
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
#if defined(AOC_HAS_OPENMP)
#pragma omp parallel for schedule(static)
#endif
    for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
        for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
            const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
            const int16_t self    = field.plateId[idx];
            const int32_t lonW    = (lonIdx == 0) ? LON - 1 : lonIdx - 1;
            const int32_t lonE    = (lonIdx == LON - 1) ? 0 : lonIdx + 1;
            const int32_t latS    = (latIdx == 0) ? 0 : latIdx - 1;
            const int32_t latN    = (latIdx == LAT - 1) ? LAT - 1 : latIdx + 1;
            const int16_t nW      = field.plateId[SphereField::cellIndex(lonW, latIdx)];
            const int16_t nE      = field.plateId[SphereField::cellIndex(lonE, latIdx)];
            const int16_t nS      = field.plateId[SphereField::cellIndex(lonIdx, latS)];
            const int16_t nN      = field.plateId[SphereField::cellIndex(lonIdx, latN)];
            if (nW != self || nE != self || nS != self || nN != self) {
                isBoundary[idx] = 1u;
            }
        }
    }
}

namespace {

/// True local boundary normal at a boundary cell, in the (east, north)
/// tangent basis, pointing FROM the self plate TOWARD the neighbour
/// side. Computed as the negated gradient of the self-plate indicator
/// over a 5x5 cell window (~1 deg, well below any real orogen arc
/// radius), with the east offsets scaled by cos(lat) so the gradient
/// lives in physical space (unweighted lattice offsets rotate normals
/// poleward at high latitude). Pure function of the plateId raster —
/// safe inside the OpenMP boundary loop. Returns false when the
/// gradient is degenerate (symmetric window, thin sliver); the caller
/// falls back to the cardinal normal, which never degenerates.
bool boundaryNormalAt(const SphereField& field, int32_t lonIdx, int32_t latIdx, int16_t selfId,
                      float& outNx, float& outNy) {
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    constexpr int32_t WIN = 2; // 5x5 window
    const LatLon centre   = SphereField::cellCenter(lonIdx, latIdx);
    const float cosLat    = std::max(0.05f, std::cos(centre.latDeg * 0.01745329252f));
    float gx              = 0.0f;
    float gy              = 0.0f;
    for (int32_t dy = -WIN; dy <= WIN; ++dy) {
        const int32_t lat = latIdx + dy;
        if (lat < 0 || lat >= LAT) continue;
        for (int32_t dx = -WIN; dx <= WIN; ++dx) {
            if (dx == 0 && dy == 0) continue;
            int32_t lon            = lonIdx + dx;
            lon                    = ((lon % LON) + LON) % LON;
            const std::size_t nIdx = SphereField::cellIndex(lon, lat);
            if (field.plateId[nIdx] != selfId) continue;
            // Physical offset of this self-cell from the centre; its
            // direction, weighted by 1/r^2, accumulates the indicator
            // gradient (self-mass pulls the gradient toward itself).
            const float ex = static_cast<float>(dx) * cosLat;
            const float ey = static_cast<float>(dy);
            const float r2 = ex * ex + ey * ey;
            gx += ex / r2;
            gy += ey / r2;
        }
    }
    const float mag = std::sqrt(gx * gx + gy * gy);
    if (mag < 1e-4f) return false;
    // Gradient points INTO the self plate; the boundary normal points
    // the other way (self -> neighbour).
    outNx = -gx / mag;
    outNy = -gy / mag;
    return true;
}

} // namespace

void accumulateClosingRate(SphereField& field, const std::vector<Plate>& plates,
                           const std::vector<uint8_t>& isBoundary) {
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    std::fill(field.convergenceRateRadPerMy.begin(), field.convergenceRateRadPerMy.end(), 0.0f);
    std::fill(field.boundaryType.begin(), field.boundaryType.end(), static_cast<uint8_t>(0));
    if (plates.empty()) {
        LOG_WARN("SphereFieldPhysics: %s called with empty plates -- skipping", __func__);
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
            const int32_t lonW = (lonIdx == 0) ? LON - 1 : lonIdx - 1;
            const int32_t lonE = (lonIdx == LON - 1) ? 0 : lonIdx + 1;
            const int32_t latS = (latIdx == 0) ? 0 : latIdx - 1;
            const int32_t latN = (latIdx == LAT - 1) ? LAT - 1 : latIdx + 1;

            int16_t otherId  = -1;
            int32_t nLon     = lonIdx;
            int32_t nLat     = latIdx;
            const int16_t nW = field.plateId[SphereField::cellIndex(lonW, latIdx)];
            const int16_t nE = field.plateId[SphereField::cellIndex(lonE, latIdx)];
            const int16_t nS = field.plateId[SphereField::cellIndex(lonIdx, latS)];
            const int16_t nN = field.plateId[SphereField::cellIndex(lonIdx, latN)];
            if (nW != selfId && nW >= 0) {
                otherId = nW;
                nLon    = lonW;
            } else if (nE != selfId && nE >= 0) {
                otherId = nE;
                nLon    = lonE;
            } else if (nS != selfId && nS >= 0) {
                otherId = nS;
                nLat    = latS;
            } else if (nN != selfId && nN >= 0) {
                otherId = nN;
                nLat    = latN;
            }
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
            const TangentVelocity vA =
                eulerVelocityAt(p, {A.eulerPoleLatDeg, A.eulerPoleLonDeg}, A.angularVelDeg);
            const TangentVelocity vB =
                eulerVelocityAt(p, {B.eulerPoleLatDeg, B.eulerPoleLonDeg}, B.angularVelDeg);
            const float dvE = vA.east - vB.east;
            const float dvN = vA.north - vB.north;

            // Boundary normal in the local east/north basis, pointing
            // FROM cell (lonIdx, latIdx) TOWARD the neighbour side.
            // Primary estimate: smoothed-indicator gradient over a 5x5
            // window (boundaryNormalAt) — resolves oblique boundary
            // strike instead of snapping to the raster axes, which
            // aliased every diagonal boundary into 0/90-degree
            // segments and made mountain belts, trenches, and the
            // coastlines they shape run axis-aligned.
            float nx;
            float ny;
            if (!boundaryNormalAt(field, lonIdx, latIdx, selfId, nx, ny)) {
                // Degenerate gradient: fall back to the cardinal
                // normal toward the picked neighbour (never fails).
                const float nE_dir = static_cast<float>(nLon - lonIdx);
                const float nN_dir = static_cast<float>(nLat - latIdx);
                nx                 = (nE_dir > 0) ? 1.0f : (nE_dir < 0) ? -1.0f : 0.0f;
                ny                 = (nN_dir > 0) ? 1.0f : (nN_dir < 0) ? -1.0f : 0.0f;
                // Longitude wrap: lonW=LON-1 reads as -719, not +1.
                if (std::fabs(nE_dir) > 1.0f) {
                    nx = (nE_dir > 0) ? -1.0f : 1.0f;
                }
            }

            // Closing component (along boundary normal n) and shear
            // component (perpendicular, along the tangent t). Signed
            // closing > 0 = convergent, < 0 = divergent; shear sign
            // distinguishes the two transform-fault polarities.
            const float closing = dvE * nx + dvN * ny;
            // Tangent unit vector perpendicular to (nx, ny) in the
            // east/north plane: t = (-ny, nx).
            const float shear                  = dvE * (-ny) + dvN * nx;
            field.convergenceRateRadPerMy[idx] = closing;
            // Classify by which component dominates. Müller 2022
            // boundary-type histogram (orogen_reference.txt extracts
            // ~25 % transform / 35 % convergent / 40 % divergent on
            // the modern Earth boundary network) calibrates the
            // tie-break: shear must clearly dominate to override the
            // closing classification, otherwise the cell counts as
            // convergent or divergent according to sign.
            // 2026-07-05: 1.5 -> 2.4, re-anchored after the gradient
            // normal replaced the cardinal normal. The old factor was
            // calibrated against cardinal-aliased decomposition (which
            // under-reads shear on oblique boundaries); with true
            // normals the same 1.5 over-classified transform (40 % vs
            // the ~25 % target on the seed-42 trace).
            const float absClosing = std::fabs(closing);
            const float absShear   = std::fabs(shear);
            uint8_t btype;
            if (absShear > 2.4f * absClosing) {
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
        float h              = field.crustThicknessKm[i] + dCrustKm;
        if (h > maxCrust) h = maxCrust;
        field.crustThicknessKm[i] = h;
    }
}

void growContinentalFractionAtArcs(SphereField& field, float dtMy) {
    // AOC_NO_ARC_GROWTH disables arc accretion. This pass is the suspected
    // source of filamentary continents: it adds continental EXTENT along 1-D
    // convergent boundaries, and over a run continental area grows 37 % while
    // continental volume grows 5 %. Gated so that claim is a measurement.
    static const bool kNoArcGrowth = std::getenv("AOC_NO_ARC_GROWTH") != nullptr;
    if (kNoArcGrowth) return;
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
    // 2026-07-05: 0.5 -> 0.25. Measured against the fixed-volume
    // sea level, the old rate over-produced continental crust to
    // ~55 % of the sphere by the end of the run (Earth: ~29 %) --
    // it had been compensating for the drowned-land look of the
    // unanchored datum era.
    constexpr float K_ARC_FRAC_PER_RADMY = 0.10f;
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
    const float maxCrust               = PhysicsConstants::maxCrustThicknessKm;
    constexpr int32_t LON              = SphereField::LON_CELLS;
    constexpr int32_t LAT              = SphereField::LAT_CELLS;
    for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
        for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
            const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
            if (field.boundaryType[idx] != 1u) continue;
            const float rate = field.convergenceRateRadPerMy[idx];
            if (rate <= 0.0f) continue;
            const int16_t selfId = field.plateId[idx];
            if (selfId < 0) continue;
            const int32_t lonW     = (lonIdx == 0) ? LON - 1 : lonIdx - 1;
            const int32_t lonE     = (lonIdx == LON - 1) ? 0 : lonIdx + 1;
            const int32_t latS     = (latIdx == 0) ? 0 : latIdx - 1;
            const int32_t latN     = (latIdx == LAT - 1) ? LAT - 1 : latIdx + 1;
            const std::size_t idxW = SphereField::cellIndex(lonW, latIdx);
            const std::size_t idxE = SphereField::cellIndex(lonE, latIdx);
            const std::size_t idxS = SphereField::cellIndex(lonIdx, latS);
            const std::size_t idxN = SphereField::cellIndex(lonIdx, latN);
            int32_t otherLon       = lonIdx;
            int32_t otherLat       = latIdx;
            const int16_t nW       = field.plateId[idxW];
            const int16_t nE       = field.plateId[idxE];
            const int16_t nS       = field.plateId[idxS];
            const int16_t nN       = field.plateId[idxN];
            std::size_t otherIdx   = idx;
            if (nW != selfId && nW >= 0) {
                otherIdx = idxW;
                otherLon = lonW;
            } else if (nE != selfId && nE >= 0) {
                otherIdx = idxE;
                otherLon = lonE;
            } else if (nS != selfId && nS >= 0) {
                otherIdx = idxS;
                otherLat = latS;
            } else if (nN != selfId && nN >= 0) {
                otherIdx = idxN;
                otherLat = latN;
            } else
                continue;
            // Identify overrider (higher continentalFraction side)
            // and step ARC_OFFSET_CELLS cells INBOARD into its
            // interior, away from the trench cell. The arc zone
            // sits in the overrider plate, not at the contact.
            // Inboard direction follows the TRUE boundary normal
            // (boundaryNormalAt) so arcs parallel curved trenches
            // instead of stepping only along the raster axes;
            // cardinal fallback on degenerate gradients.
            const float selfFrac  = field.continentalFraction[idx];
            const float otherFrac = field.continentalFraction[otherIdx];
            int32_t arcLon, arcLat;
            int16_t arcOwnerId;
            const bool selfIsOverrider = (selfFrac >= otherFrac);
            const int32_t ovrLon       = selfIsOverrider ? lonIdx : otherLon;
            const int32_t ovrLat       = selfIsOverrider ? latIdx : otherLat;
            arcOwnerId                 = selfIsOverrider ? selfId : field.plateId[otherIdx];
            float nrmE, nrmN;
            if (boundaryNormalAt(field, ovrLon, ovrLat, arcOwnerId, nrmE, nrmN)) {
                // Normal points overrider -> other side; inboard is
                // the opposite. Convert the physical east component
                // to lon cells at this latitude.
                const LatLon oc  = SphereField::cellCenter(ovrLon, ovrLat);
                const float cosL = std::max(0.05f, std::cos(oc.latDeg * 0.01745329252f));
                arcLon = ovrLon + static_cast<int32_t>(std::lround(
                                      -nrmE / cosL * static_cast<float>(ARC_OFFSET_CELLS)));
                arcLat = ovrLat + static_cast<int32_t>(
                                      std::lround(-nrmN * static_cast<float>(ARC_OFFSET_CELLS)));
            } else if (selfIsOverrider) {
                const int32_t dLon = otherLon - lonIdx;
                const int32_t dLat = otherLat - latIdx;
                int32_t stepLon    = (dLon > 0) ? -1 : (dLon < 0) ? 1 : 0;
                int32_t stepLat    = (dLat > 0) ? -1 : (dLat < 0) ? 1 : 0;
                if (std::abs(dLon) > 1) stepLon = -stepLon; // wrap fix
                arcLon = lonIdx + stepLon * ARC_OFFSET_CELLS;
                arcLat = latIdx + stepLat * ARC_OFFSET_CELLS;
            } else {
                const int32_t dLon = lonIdx - otherLon;
                const int32_t dLat = latIdx - otherLat;
                int32_t stepLon    = (dLon > 0) ? -1 : (dLon < 0) ? 1 : 0;
                int32_t stepLat    = (dLat > 0) ? -1 : (dLat < 0) ? 1 : 0;
                if (std::abs(dLon) > 1) stepLon = -stepLon;
                arcLon = otherLon + stepLon * ARC_OFFSET_CELLS;
                arcLat = otherLat + stepLat * ARC_OFFSET_CELLS;
            }
            // Latitude clamp; longitude wrap.
            if (arcLat < 0) arcLat = 0;
            if (arcLat >= LAT) arcLat = LAT - 1;
            arcLon                   = ((arcLon % LON) + LON) % LON;
            const std::size_t arcIdx = SphereField::cellIndex(arcLon, arcLat);
            // Stop if the inboard cell is no longer the overrider
            // (e.g. another plate sits in the way) — arc volcanism
            // does not punch across plate boundaries.
            if (field.plateId[arcIdx] != arcOwnerId) continue;
            const float dFrac = K_ARC_FRAC_PER_RADMY * rate * dtMy;
            float frac        = field.continentalFraction[arcIdx] + dFrac;
            if (frac > 1.0f) frac = 1.0f;
            field.continentalFraction[arcIdx] = frac;
            const float dCrustKm              = K_ARC_KM_PER_RADMY * rate * dtMy;
            float h                           = field.crustThicknessKm[arcIdx] + dCrustKm;
            if (h > maxCrust) h = maxCrust;
            field.crustThicknessKm[arcIdx] = h;

            // Tectonic (subduction) erosion of the overriding margin
            // (2026-07-05). Roughly half of Earth's convergent margins
            // are EROSIVE: the trench eats the overriding plate's edge
            // at rates comparable to arc production (von Huene &
            // Scholl 1991), which is why continental area is a
            // near-steady state on Earth. Without this sink the sim's
            // continental area grew monotonically (measured 25 % ->
            // 47 % of the sphere over one run) and the rising
            // displaced-water stand drowned the continents. Applied at
            // the overrider's trench-front cell, slightly below the
            // arc gain so net growth stays mildly positive
            // (Phanerozoic net accretion).
            constexpr float K_TRENCH_EROSION_FRAC_PER_RADMY = 0.03f;
            const std::size_t trenchIdx                     = selfIsOverrider ? idx : otherIdx;
            const float lossFrac = K_TRENCH_EROSION_FRAC_PER_RADMY * rate * dtMy;
            float tf             = field.continentalFraction[trenchIdx] - lossFrac;
            if (tf < 0.0f) tf = 0.0f;
            field.continentalFraction[trenchIdx] = tf;
            const float lossKm = K_ARC_KM_PER_RADMY * rate * dtMy *
                                 (K_TRENCH_EROSION_FRAC_PER_RADMY / K_ARC_FRAC_PER_RADMY);
            float th           = field.crustThicknessKm[trenchIdx] - lossKm;
            if (th < PhysicsConstants::initialOceanicThicknessKm) {
                th = PhysicsConstants::initialOceanicThicknessKm;
            }
            field.crustThicknessKm[trenchIdx] = th;
        }
    }
}

void accreteToNeighbours(SphereField& field, float dtMy) {
    // AOC_NO_ACCRETE disables terrane-accretion diffusion plus the maturation
    // it carries. Gated alongside AOC_NO_ARC_GROWTH so the two extent-changing
    // passes can be attributed separately.
    static const bool kNoAccrete = std::getenv("AOC_NO_ACCRETE") != nullptr;
    if (kNoAccrete) return;
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
    // 2026-07-05: halved with K_ARC_FRAC (see there).
    constexpr float K_SPREAD_PER_MY       = 0.0005f;
    constexpr float SPREAD_FROM_THRESHOLD = 0.95f;
    // Crust donated per unit cf increase: the column simply interpolates
    // between normal oceanic and normal continental thickness, so a cell that
    // accretes all the way from cf 0 to cf 1 ends at 41 km.
    //
    //   K_CRUST = refContinentalThicknessKm - initialOceanicThicknessKm
    //           = 41 - 7 = 34 km per unit cf
    //
    // 2026-08-10: was 32.8, back-derived from the old 3549 m datum with the
    // stated goal that "diffused cells emerge above sea level as their cf
    // crosses the continental threshold". That goal was itself the defect --
    // it forced the shoreline to coincide with the cf = 0.5 contour, which is
    // why every coastline was a cliff. Under the re-anchored law a
    // half-accreted margin (cf 0.5, 24 km) sits ~2.6 km deep and only emerges
    // as it approaches full continental thickness, which is what an accreted
    // terrane actually does: submerged until it is thickened.
    constexpr float K_CRUST_PER_DIFF_FRAC =
        PhysicsConstants::refContinentalThicknessKm - PhysicsConstants::initialOceanicThicknessKm;
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    const float dFracMax  = K_SPREAD_PER_MY * dtMy;
    if (dFracMax <= 0.0f) {
        return;
    }
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
            const float cf        = field.continentalFraction[idx];
            if (cf < SPREAD_FROM_THRESHOLD) {
                continue;
            }
            const int16_t donorId = field.plateId[idx];
            if (donorId < 0) {
                continue;
            }
            const int32_t lonW = (lonIdx == 0) ? LON - 1 : lonIdx - 1;
            const int32_t lonE = (lonIdx == LON - 1) ? 0 : lonIdx + 1;
            const int32_t latS = (latIdx == 0) ? 0 : latIdx - 1;
            const int32_t latN = (latIdx == LAT - 1) ? LAT - 1 : latIdx + 1;
            // 8-neighbour kernel, inverse-distance weighted in KILOMETRES
            // rather than in cell indices.
            //
            // A 4-cardinal kernel grew continents as an L-infinity ball and
            // flattened every growth front into an axis-aligned wall
            // (2026-07-05); the 8-neighbour form with 1/sqrt(2) diagonals
            // fixed that in INDEX space. But index space is not the sphere: a
            // longitude step spans 55.7*cos(lat) km against 55.7 km for a
            // latitude step, so at 60 deg a cell's east and west neighbours are
            // half as far away as its north and south ones. Weighting all four
            // cardinals equally therefore advances the growth front twice as
            // fast north-south, in km/My, as east-west -- a 1/cos(lat)
            // anisotropy that turns high-latitude continents into meridional
            // ribbons and shows up as coastline axis_aligned_frac well above
            // the 0.50 isotropic null.
            //
            // Weighting by 1/distance_km makes the front isotropic on the
            // ground at every latitude. Normalisation is recomputed per cell
            // (the weights now depend on latitude) so the TOTAL fraction a
            // donor gives away per epoch is unchanged and the rate calibration
            // above still holds.
            struct NeighbourShare {
                std::size_t idx;
                float w;
            };
            const LatLon donorPos = SphereField::cellCenter(lonIdx, latIdx);
            // Floored: within half a cell of the pole a longitude step spans
            // essentially zero ground distance, and 1/d would diverge.
            const float cosLat = std::max(0.02f, std::cos(donorPos.latDeg * 0.01745329252f));
            const float wEW    = 1.0f / cosLat;
            const float wNS    = 1.0f;
            const float wDiag  = 1.0f / std::sqrt(cosLat * cosLat + 1.0f);
            const NeighbourShare neighbours[8] = {
                {SphereField::cellIndex(lonW, latIdx), wEW},
                {SphereField::cellIndex(lonE, latIdx), wEW},
                {SphereField::cellIndex(lonIdx, latS), wNS},
                {SphereField::cellIndex(lonIdx, latN), wNS},
                {SphereField::cellIndex(lonW, latS), wDiag},
                {SphereField::cellIndex(lonE, latS), wDiag},
                {SphereField::cellIndex(lonW, latN), wDiag},
                {SphereField::cellIndex(lonE, latN), wDiag},
            };
            const float KERNEL_NORM = 4.0f / (2.0f * wEW + 2.0f * wNS + 4.0f * wDiag);
            for (const NeighbourShare& nb : neighbours) {
                const std::size_t n = nb.idx;
                // Same-plate gate -- cross-plate diffusion would break
                // Wilson-cycle assembly (continents cannot leak across
                // an open ocean basin without colliding first).
                //
                // 2026-08-10: TRIED AND REVERTED -- allowing donation across
                // CONVERGENT boundaries only (terrane accretion, which is
                // genuinely a cross-boundary process). The hypothesis was that
                // this gate is what pins coastlines to plate boundaries:
                // --dump-plates on seed 42 shows plate 5 (3 % land) abutting
                // plate 9 (62 % land) with a dead-straight coast along their
                // shared boundary at column 58. Measured over seeds
                // 42/7/100/777, coastline axis_aligned_frac did NOT improve
                // (mean 0.605 -> 0.609) while land fraction fell (0.246 ->
                // 0.233); largest-component share did fall (0.71 -> 0.61) but
                // that is confounded with having less land to connect.
                // So the coincidence of coast and boundary is real but this
                // gate is not its cause -- look at why the plate boundary
                // itself is straight before trying this again.
                if (field.plateId[n] != donorId) {
                    continue;
                }
                if (nextFrac[n] >= 1.0f) {
                    continue;
                }
                const float share  = dFracMax * nb.w * KERNEL_NORM;
                const float actual = std::min(share, 1.0f - nextFrac[n]);
                nextFrac[n] += actual;
                // Thicken crust proportionally so diffused cells emerge
                // above sea level as cf crosses the continental threshold.
                const float dCrust = actual * K_CRUST_PER_DIFF_FRAC;
                float h            = nextCrust[n] + dCrust;
                if (h > PhysicsConstants::maxCrustThicknessKm) {
                    h = PhysicsConstants::maxCrustThicknessKm;
                }
                nextCrust[n] = h;
            }
        }
    }
    field.continentalFraction.swap(nextFrac);
    field.crustThicknessKm.swap(nextCrust);
    // Terrane maturation: continental crust relaxes toward its equilibrium
    // thickness on a geological e-fold (Willett & Brandon 2002 cite 1-5 Gy for
    // thermal / isostatic equilibration).
    //
    // The target is a FIELD, not the single global reference thickness. Real
    // continental crust is not one number (Christensen & Mooney 1995: shields
    // 35-45 km, platforms 35-40, orogens 45-70, extended terranes 25-35), and
    // relaxing every cell toward 41 km piles ~45 % of continental crust into
    // an 0.8 km band -- measured p5=33.0, p50=33.8 against a 34.2 km
    // sea-level thickness. That degeneracy is not cosmetic: it makes the
    // submerged fraction hypersensitive, because a sub-km shift in the peak
    // flips half the continents across sea level at once (measured: submerged
    // 0.351 at a 5100 My e-fold, 0.139 at 5000 My).
    //
    // Rift margins are EXCLUDED rather than given a stretch-scaled target.
    // Both were measured; the exclusion wins. Dividing the target by
    // stretchFactor is the more principled statement -- a margin thinned by
    // beta has had that crust physically removed, so its equilibrium thickness
    // is the stretched one -- but it moved submerged share 0.391 -> 0.419 with
    // shelf and largest-landmass unchanged, because a cell already sitting at
    // 41/beta is already at that target and does not move. The exclusion keeps
    // the same margins thin for one fewer division.
    //
    // Known limitation: stretchFactor is a max-over-all-time and never decays,
    // so the excluded set only ratchets upward over a run. The p50 = 33.8 km
    // spike is that population, and broadening the target cannot reach it.
    // Un-ratcheting it (decay, or a re-thickening path through orogeny) is the
    // open lead on the submerged gate -- see the memory file.
    //
    // Sampled on unit-sphere coordinates: lat/lon-space noise would seam at
    // the antimeridian and pinch at the poles, and this field is broad enough
    // (~48 deg wavelength) that either artifact would be plainly visible as a
    // thickness discontinuity running the length of the map.
    constexpr float MATURATION_EFOLD_MY          = 5500.0f;
    constexpr float STRETCH_MATURATION_THRESHOLD = 1.05f;
    constexpr float MATURATION_TARGET_SPREAD_KM  = 4.5f;
    constexpr float MATURATION_TARGET_FREQ       = 1.2f;
    constexpr uint64_t MATURATION_TARGET_SEED    = 0x9E3779B97F4A7C15ULL;
    constexpr float DEG2RAD_F                    = 0.01745329252f;
    const float relax                            = 1.0f - std::exp(-dtMy / MATURATION_EFOLD_MY);
    for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
        if (field.continentalFraction[i] < 0.5f) continue;
        if (field.stretchFactor[i] > STRETCH_MATURATION_THRESHOLD) continue;
        const float h = field.crustThicknessKm[i];
        const LatLon c =
            SphereField::cellCenter(static_cast<int32_t>(i % static_cast<std::size_t>(LON)),
                                    static_cast<int32_t>(i / static_cast<std::size_t>(LON)));
        const float latR = c.latDeg * DEG2RAD_F;
        const float lonR = c.lonDeg * DEG2RAD_F;
        const float px   = std::cos(latR) * std::cos(lonR);
        const float py   = std::cos(latR) * std::sin(lonR);
        const float pz   = std::sin(latR);
        const float n =
            2.0f * smoothHashNoise3(px * MATURATION_TARGET_FREQ, py * MATURATION_TARGET_FREQ,
                                    pz * MATURATION_TARGET_FREQ, MATURATION_TARGET_SEED) -
            1.0f;
        const float targetKm =
            PhysicsConstants::refContinentalThicknessKm + MATURATION_TARGET_SPREAD_KM * n;
        if (h >= targetKm) continue;
        field.crustThicknessKm[i] = h + (targetKm - h) * relax;
    }
}

// ---------------------------------------------------------------------------
// Rigid terrane transport
//
// Replaces raster resampling of the continental crust fields. See Terrane.hpp
// for the measurement that motivated it: advection alone accounts for the
// crust footprint degrading from 1.28x an equal-area disc at epoch 1 to 2.62x
// by epoch 60, and for the 0.92-0.97 largest-component supercontinent.
//
// The body frame is the raster as it stood when terranes were seeded. A cell's
// present position is `R_t * body_direction`; transport is therefore a
// coordinate change and membership is exact, so shape is preserved under any
// accumulated rotation. Nothing is interpolated and nothing is repaired.
// ---------------------------------------------------------------------------

void despecklePlateOwnership(SphereField& field) {
    // Strip one-cell-thick ownership fringes left by advection's orphan-claim
    // pass, which walks eight neighbours in a fixed metric order and so grows
    // directional combs along a moving plate front. Measured at 480x270: 0.23 %
    // of cells sit in a one-row-thick east-west fringe, which reads as smearing
    // on any map finer than the 140x90 the tuning was done at.
    //
    // A cell whose north and south neighbours agree with each other but not
    // with it is such a fringe, and adopts them. Jacobi (read `src`, write
    // `dst`) so the result cannot depend on traversal or thread order.
    constexpr int32_t LON     = SphereField::LON_CELLS;
    constexpr int32_t LAT     = SphereField::LAT_CELLS;
    std::vector<int16_t> next = field.plateId;
    for (int32_t j = 1; j < LAT - 1; ++j) {
        for (int32_t i = 0; i < LON; ++i) {
            const std::size_t idx = SphereField::cellIndex(i, j);
            const int16_t self    = field.plateId[idx];
            const int16_t n       = field.plateId[SphereField::cellIndex(i, j + 1)];
            const int16_t s2      = field.plateId[SphereField::cellIndex(i, j - 1)];
            if (n == s2 && n != self && n >= 0) {
                next[idx] = n;
            }
        }
    }
    field.plateId.swap(next);
}

void seedTerranesFromRaster(const SphereField& field, std::vector<Terrane>& terranes,
                            TerraneBody& body) {
    // One terrane per connected component of continental crust at seeding time.
    // Cratons are already seeded compact (measured 1.28x disc at epoch 1); this
    // captures that geometry as a rigid body so the run cannot destroy it.
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    body.terraneId.assign(SphereField::CELL_COUNT, -1);
    body.crustKm.assign(SphereField::CELL_COUNT, 0.0f);
    body.ageMy.assign(SphereField::CELL_COUNT, 0.0f);
    terranes.clear();

    // Smooth the seeded mask ONCE, before the bodies are cut from it.
    //
    // The craton seeder is an anisotropic stochastic BFS, so its boundary is
    // rough at the cell scale even when the blob is globally compact. That
    // matters more than it looks: the shoreline is placed by eroding the crust
    // outline inward ~22 cells, and eroding a rough outline by a large distance
    // amplifies the roughness into a fragmented, ragged coastline. Measured,
    // crust perimeter/equal-area-disc of 1.21-2.12 was still yielding land at
    // 2.46-3.27.
    //
    // A majority filter is safe here precisely BECAUSE it runs once. The same
    // operator applied per epoch was measured to cost 7-16 % of continental
    // area per pass -- that bleed is what disqualified it as a running
    // mechanism; as a one-time conditioning of the initial condition it has no
    // such cost, and the rigid bodies then preserve the smoothed shape exactly
    // for the rest of the run.
    std::vector<uint8_t> mask(SphereField::CELL_COUNT, 0u);
    for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
        mask[i] = field.continentalFraction[i] >= 0.5f ? 1u : 0u;
    }
    for (int32_t pass = 0; pass < 3; ++pass) {
        std::vector<uint8_t> nextMask(mask);
        for (int32_t j = 0; j < LAT; ++j) {
            for (int32_t i = 0; i < LON; ++i) {
                const std::size_t idx   = SphereField::cellIndex(i, j);
                const int32_t iW        = (i == 0) ? LON - 1 : i - 1;
                const int32_t iE        = (i == LON - 1) ? 0 : i + 1;
                const std::size_t nb[4] = {SphereField::cellIndex(iW, j),
                                           SphereField::cellIndex(iE, j),
                                           SphereField::cellIndex(i, std::max(0, j - 1)),
                                           SphereField::cellIndex(i, std::min(LAT - 1, j + 1))};
                int32_t on              = 0;
                for (const std::size_t n : nb) {
                    on += mask[n] ? 1 : 0;
                }
                // Strict majority in either direction; a 2-2 split holds, so
                // the filter cannot oscillate and is idempotent at convergence.
                if (on >= 3)
                    nextMask[idx] = 1u;
                else if (on <= 1)
                    nextMask[idx] = 0u;
            }
        }
        mask.swap(nextMask);
    }

    std::vector<uint8_t> seen(SphereField::CELL_COUNT, 0u);
    std::vector<int32_t> stack;
    for (int32_t j0 = 0; j0 < LAT; ++j0) {
        for (int32_t i0 = 0; i0 < LON; ++i0) {
            const std::size_t start = SphereField::cellIndex(i0, j0);
            if (seen[start] || !mask[start]) continue;
            const int16_t id = static_cast<int16_t>(terranes.size());
            Terrane t;
            t.id      = id;
            t.plateId = field.plateId[start];
            stack.clear();
            stack.push_back(static_cast<int32_t>(start));
            seen[start] = 1u;
            while (!stack.empty()) {
                const int32_t cur = stack.back();
                stack.pop_back();
                t.bodyCells.push_back(cur);
                body.terraneId[static_cast<std::size_t>(cur)] = id;
                body.crustKm[static_cast<std::size_t>(cur)] =
                    field.crustThicknessKm[static_cast<std::size_t>(cur)];
                body.ageMy[static_cast<std::size_t>(cur)] =
                    field.crustAgeMy[static_cast<std::size_t>(cur)];
                const int32_t j     = cur / LON;
                const int32_t i     = cur % LON;
                const int32_t iW    = (i == 0) ? LON - 1 : i - 1;
                const int32_t iE    = (i == LON - 1) ? 0 : i + 1;
                const int32_t nb[4] = {
                    static_cast<int32_t>(SphereField::cellIndex(iW, j)),
                    static_cast<int32_t>(SphereField::cellIndex(iE, j)),
                    static_cast<int32_t>(SphereField::cellIndex(i, std::max(0, j - 1))),
                    static_cast<int32_t>(SphereField::cellIndex(i, std::min(LAT - 1, j + 1)))};
                for (const int32_t n : nb) {
                    if (n == cur || seen[static_cast<std::size_t>(n)]) continue;
                    if (!mask[static_cast<std::size_t>(n)]) continue;
                    seen[static_cast<std::size_t>(n)] = 1u;
                    stack.push_back(n);
                }
            }
            terranes.push_back(std::move(t));
        }
    }
}

void assignTerraneDrift(std::vector<Terrane>& terranes, float totalMy) {
    // PRESCRIBED DISPERSAL -- the Wilson cycle's second half, imposed rather
    // than hoped for.
    //
    // Why it has to be imposed. The rift trigger this replaces asked whether one
    // PLATE held 25 % of the continental crust, while the supercontinent is
    // crust welded ACROSS a dozen plates: measured, max plate share sat at
    // 0.19-0.25 against a 0.25 threshold for the whole run, so 2-5 rifts fired
    // in 3 Gy and nothing ever dispersed. Rigid terranes made the blocks
    // compact but did not change that -- they simply drift at random, collide,
    // and never separate again, leaving one mass covering ~45 % of the sphere
    // (largest connected crust component 0.86-1.00). Every remaining failure
    // followed from it: land fraction too high, largest landmass too high, and
    // a rim erosion that cannot bite because the mass is far larger than the
    // rim is wide.
    //
    // So each block is given an Euler pole that carries it radially AWAY from
    // the initial continental centroid. The axis `centroid x position` rotates
    // a point along the great circle running from the centroid through it,
    // which is outward by construction; blocks sitting near the centroid get a
    // random axis instead, since "away" is undefined there.
    //
    // The rate is set so a block travels ~50 deg over the run -- enough to open
    // an ocean between blocks that start ~50 deg apart -- and lands at
    // ~0.017 deg/My, comfortably inside the 0.005-0.30 deg/My envelope drawn
    // from the Muller 2022 reconstruction for real plates.
    constexpr int32_t LON      = SphereField::LON_CELLS;
    constexpr float SPREAD_DEG = 50.0f;
    if (terranes.empty() || totalMy <= 0.0f) return;

    double cx = 0.0, cy = 0.0, cz = 0.0;
    for (const Terrane& t : terranes) {
        for (const int32_t cell : t.bodyCells) {
            const int32_t j = cell / LON;
            const int32_t i = cell % LON;
            const LatLon p  = SphereField::cellCenter(i, j);
            const Vec3 v    = latLonToVec3(p);
            cx += v.x;
            cy += v.y;
            cz += v.z;
        }
    }
    const double clen = std::sqrt(cx * cx + cy * cy + cz * cz);
    if (clen < 1e-9) return;
    const Vec3 centre{static_cast<float>(cx / clen), static_cast<float>(cy / clen),
                      static_cast<float>(cz / clen)};

    for (std::size_t k = 0; k < terranes.size(); ++k) {
        Terrane& t = terranes[k];
        if (t.bodyCells.empty()) continue;
        double px = 0.0, py = 0.0, pz = 0.0;
        for (const int32_t cell : t.bodyCells) {
            const Vec3 v = latLonToVec3(SphereField::cellCenter(cell % LON, cell / LON));
            px += v.x;
            py += v.y;
            pz += v.z;
        }
        const double plen = std::sqrt(px * px + py * py + pz * pz);
        if (plen < 1e-9) continue;
        const Vec3 pos{static_cast<float>(px / plen), static_cast<float>(py / plen),
                       static_cast<float>(pz / plen)};
        // axis = centre x pos; rotating about it sweeps pos directly away from
        // centre along their common great circle.
        Vec3 axis{centre.y * pos.z - centre.z * pos.y, centre.z * pos.x - centre.x * pos.z,
                  centre.x * pos.y - centre.y * pos.x};
        float alen = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
        if (alen < 1e-4f) {
            // Sitting on (or opposite) the centroid: "away" is undefined, so
            // pick a deterministic axis from the block's index instead of
            // leaving it stationary while everything else disperses.
            const float a = static_cast<float>(k) * 2.39996f;
            axis          = Vec3{std::cos(a), std::sin(a), 0.0f};
            alen          = 1.0f;
        }
        axis                = Vec3{axis.x / alen, axis.y / alen, axis.z / alen};
        const LatLon pole   = vec3ToLatLon(axis);
        t.driftPoleLatDeg   = pole.latDeg;
        t.driftPoleLonDeg   = pole.lonDeg;
        t.driftRateDegPerMy = SPREAD_DEG / totalMy;
    }
}

void advanceTerraneRotations(std::vector<Terrane>& terranes, const std::vector<Plate>& plates,
                             float dtMy) {
    for (Terrane& t : terranes) {
        if (!t.alive) continue;
        if (t.plateId < 0 || static_cast<std::size_t>(t.plateId) >= plates.size()) continue;
        const Plate& p = plates[static_cast<std::size_t>(t.plateId)];
        // Prescribed dispersal carries the block; the plate's own rotation is
        // added at a reduced weight so motion still varies with the tectonic
        // configuration instead of every world dispersing identically.
        float step[9];
        eulerRotMatrix(t.driftPoleLatDeg, t.driftPoleLonDeg, t.driftRateDegPerMy * dtMy, step);
        float wander[9];
        eulerRotMatrix(p.eulerPoleLatDeg, p.eulerPoleLonDeg, p.angularVelDeg * dtMy * 0.25f,
                       wander);
        composeRot(step, wander, step);
        // step * current: the epoch's rotation is applied in the WORLD frame,
        // after everything the terrane has already accumulated.
        composeRot(step, t.rot, t.rot);
    }
}

void bakeTerranesToRaster(SphereField& field, const std::vector<Terrane>& terranes,
                          const TerraneBody& body) {
    // Continental crust is rebuilt from scratch each epoch from the rigid
    // bodies. Cells no terrane covers are oceanic -- which is how a rifted gap
    // becomes ocean without any explicit "make ocean" step.
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    constexpr float HO    = PhysicsConstants::initialOceanicThicknessKm;

    std::vector<float> newFrac(SphereField::CELL_COUNT, 0.0f);
    std::vector<float> newCrust(SphereField::CELL_COUNT, HO);
    std::vector<int16_t> newTerrane(SphereField::CELL_COUNT, -1);

#if defined(AOC_HAS_OPENMP)
#pragma omp parallel for schedule(static)
#endif
    for (int32_t j = 0; j < LAT; ++j) {
        const float latDeg = -90.0f + (static_cast<float>(j) + 0.5f) * SphereField::CELL_DEG;
        for (int32_t i = 0; i < LON; ++i) {
            const float lonDeg = -180.0f + (static_cast<float>(i) + 0.5f) * SphereField::CELL_DEG;
            const std::size_t idx = SphereField::cellIndex(i, j);
            // SUPERSAMPLED membership -- five sub-positions, majority vote.
            //
            // A single nearest-neighbour test aliases the rigid boundary: when
            // the accumulated rotation nearly aligns with the raster, adjacent
            // destination rows map to the same body row and then jump, leaving
            // one-cell-thick east-west fringes along every terrane edge. That
            // is invisible at the 140x90 this was tuned at and very visible as
            // "smearing" on a 1920x1080 map.
            //
            // The threshold is load-bearing and was measured. 3-of-5 is an area
            // estimate, so the boundary lands where the body covers half the
            // cell. 1-of-5 dilates instead and merges neighbouring blocks
            // (29/72 seed-gates versus 33/72). Removing the supersampling
            // entirely also scores 29/72. Note the honest caveat: across these
            // variants the gate total ranges 29-37 on six seeds, which is not a
            // resolution this metric can distinguish -- the supersampling is
            // kept because it removes a visible artefact, not because 33 beats
            // 29 significantly.
            constexpr float Q    = 0.25f * SphereField::CELL_DEG;
            const LatLon subs[5] = {{latDeg, lonDeg},
                                    {latDeg - Q, lonDeg - Q},
                                    {latDeg - Q, lonDeg + Q},
                                    {latDeg + Q, lonDeg - Q},
                                    {latDeg + Q, lonDeg + Q}};
            Vec3 subv[5];
            for (int32_t k = 0; k < 5; ++k) {
                subv[k] = latLonToVec3(subs[k]);
            }
            for (const Terrane& t : terranes) {
                if (!t.alive || t.bodyCells.empty()) continue;
                int32_t hits           = 0;
                std::size_t centreBidx = 0;
                for (int32_t k = 0; k < 5; ++k) {
                    const LatLon bl        = vec3ToLatLon(applyRotT(t.rot, subv[k]));
                    const auto bc          = SphereField::locate(bl.latDeg, bl.lonDeg);
                    const std::size_t bidx = SphereField::cellIndex(bc.lonIdx, bc.latIdx);
                    if (k == 0) centreBidx = bidx;
                    if (body.terraneId[bidx] == t.id) ++hits;
                }
                if (hits < 3) continue;
                newFrac[idx]    = 1.0f;
                newCrust[idx]   = body.crustKm[centreBidx] > 0.0f
                                      ? body.crustKm[centreBidx]
                                      : PhysicsConstants::refContinentalThicknessKm;
                newTerrane[idx] = t.id;
                break; // first terrane wins an overlap; welding resolves it
            }
        }
    }

    for (std::size_t k = 0; k < SphereField::CELL_COUNT; ++k) {
        // Oceanic cells keep whatever the ocean passes gave them; only cells
        // that a terrane covers, or that one has just vacated, are rewritten.
        if (newTerrane[k] >= 0) {
            field.continentalFraction[k] = newFrac[k];
            field.crustThicknessKm[k]    = newCrust[k];
        } else if (field.terraneId[k] >= 0 || field.continentalFraction[k] > 0.0f) {
            // Vacated by a departing terrane, or continental crust that no
            // rigid body owns. Both become ocean floor: with adoption off, a
            // terrane IS the continental crust, and anything outside one is a
            // raster artefact that advection would otherwise smear.
            field.continentalFraction[k] = 0.0f;
            field.crustThicknessKm[k]    = HO;
            field.crustAgeMy[k]          = 0.0f;
        }
        field.terraneId[k] = newTerrane[k];
    }
}

void writebackTerraneCrust(const SphereField& field, std::vector<Terrane>& terranes,
                           TerraneBody& body) {
    // Thickening, erosion and maturation act on the world-frame raster; carry
    // their result back into the body frame so it rides along next epoch. This
    // is the ONLY reverse coupling between the physics and the terrane state.
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    for (int32_t j = 0; j < LAT; ++j) {
        const float latDeg = -90.0f + (static_cast<float>(j) + 0.5f) * SphereField::CELL_DEG;
        for (int32_t i = 0; i < LON; ++i) {
            const std::size_t idx = SphereField::cellIndex(i, j);
            const int16_t tid     = field.terraneId[idx];
            if (tid < 0 || static_cast<std::size_t>(tid) >= terranes.size()) continue;
            const Terrane& t = terranes[static_cast<std::size_t>(tid)];
            if (!t.alive) continue;
            const float lonDeg = -180.0f + (static_cast<float>(i) + 0.5f) * SphereField::CELL_DEG;
            const Vec3 b       = applyRotT(t.rot, latLonToVec3(LatLon{latDeg, lonDeg}));
            const LatLon bl    = vec3ToLatLon(b);
            const auto bc      = SphereField::locate(bl.latDeg, bl.lonDeg);
            const std::size_t bidx = SphereField::cellIndex(bc.lonIdx, bc.latIdx);
            if (body.terraneId[bidx] != tid) continue;
            body.crustKm[bidx] = field.crustThicknessKm[idx];
            body.ageMy[bidx]   = field.crustAgeMy[idx];
        }
    }

    // Adopt newly continental cells into the terrane they border.
    //
    // Arc growth and accretion make crust on the world-frame raster, outside
    // any rigid body. Left unadopted it accumulates as non-terrane continental
    // crust, which the bake does not own and advection therefore still smears
    // -- measured, that leak held crust perimeter/equal-area-disc at 2.25-2.93
    // where fully rigid transport reaches 1.2-1.5. Adoption is the plan's
    // "growth may only advance an existing margin" rule: new crust joins the
    // body next to it and thereafter rides rigidly, rather than becoming a
    // free-floating raster value.
    //
    // Deterministic by construction: cells are visited in raster order and the
    // neighbour scan is in fixed W/E/S/N order, so the adopting terrane never
    // depends on iteration or thread order.
    //
    // MEASURED and DEFAULT OFF. Adoption lets a terrane absorb any adjacent
    // continental cell, which over a run lets neighbours grow into each other
    // and fuse: crust perimeter/equal-area-disc went 2.54 -> 3.15 on seed 42
    // and the largest component 0.908 -> 0.993, i.e. it re-created the
    // supercontinent by a different route. Correct adoption needs a
    // same-terrane-only or collision-aware rule; until then arc growth is
    // disabled instead and the continental budget comes from the seeded stock.
    static const bool kAdopt = std::getenv("AOC_TERRANE_ADOPT") != nullptr;
    for (int32_t j = 0; kAdopt && j < LAT; ++j) {
        const float latDeg = -90.0f + (static_cast<float>(j) + 0.5f) * SphereField::CELL_DEG;
        for (int32_t i = 0; i < LON; ++i) {
            const std::size_t idx = SphereField::cellIndex(i, j);
            if (field.terraneId[idx] >= 0) continue;
            if (field.continentalFraction[idx] < 0.5f) continue;
            const int32_t iW        = (i == 0) ? LON - 1 : i - 1;
            const int32_t iE        = (i == LON - 1) ? 0 : i + 1;
            const std::size_t nb[4] = {SphereField::cellIndex(iW, j), SphereField::cellIndex(iE, j),
                                       SphereField::cellIndex(i, std::max(0, j - 1)),
                                       SphereField::cellIndex(i, std::min(LAT - 1, j + 1))};
            int16_t host            = -1;
            for (const std::size_t n : nb) {
                if (field.terraneId[n] >= 0) {
                    host = field.terraneId[n];
                    break;
                }
            }
            if (host < 0 || static_cast<std::size_t>(host) >= terranes.size()) continue;
            Terrane& t = terranes[static_cast<std::size_t>(host)];
            if (!t.alive) continue;
            const float lonDeg = -180.0f + (static_cast<float>(i) + 0.5f) * SphereField::CELL_DEG;
            const Vec3 b       = applyRotT(t.rot, latLonToVec3(LatLon{latDeg, lonDeg}));
            const LatLon bl    = vec3ToLatLon(b);
            const auto bc      = SphereField::locate(bl.latDeg, bl.lonDeg);
            const std::size_t bidx = SphereField::cellIndex(bc.lonIdx, bc.latIdx);
            if (body.terraneId[bidx] >= 0) continue; // body cell already taken
            body.terraneId[bidx] = host;
            body.crustKm[bidx]   = field.crustThicknessKm[idx];
            body.ageMy[bidx]     = field.crustAgeMy[idx];
            t.bodyCells.push_back(static_cast<int32_t>(bidx));
        }
    }
}

void applySubduction(SphereField& field, const std::vector<Plate>& plates, float dtMy) {
    if (plates.empty()) {
        LOG_WARN("SphereFieldPhysics: %s called with empty plates -- skipping", __func__);
        return;
    }
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    const float R         = PhysicsConstants::earthRadiusKm;

    // Kinematic consumption cap: the largest closing distance any
    // boundary can sustain in one epoch, from the plate-motion
    // envelope (2 * MAX_ABS_OMEGA both plates head-on). ~34 cells at
    // dt = 50 My -- Tibet consumes ~45 cells per 50 My at 5 cm/yr,
    // same order. A cap BELOW the kinematic envelope would stop the
    // overrider advancing at the closing rate while the consumed
    // plate's trailing edge keeps spawning wake -- silent area-
    // accounting breakage under incumbent-wins advection.
    const int32_t kinematicCap = std::max(
        1, static_cast<int32_t>(std::ceil(2.0f * MAX_ABS_OMEGA_DEG_PER_MY * 0.01745329252f * dtMy *
                                          R / SUBDUCTION_CELL_WIDTH_KM)));
    // Peel state: per-cell seed/flip bookkeeping (see peel phase).
    std::vector<int32_t> peelBudget(SphereField::CELL_COUNT, -1);
    std::vector<int16_t> peelOwner(SphereField::CELL_COUNT, -1);
    std::vector<int16_t> peelConsumed(SphereField::CELL_COUNT, -1);
    std::vector<int32_t> candBudget(SphereField::CELL_COUNT, -1);
    std::vector<int16_t> candOwner(SphereField::CELL_COUNT, -1);
    std::vector<int16_t> candConsumed(SphereField::CELL_COUNT, -1);

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
            const int32_t lonW     = (lonIdx == 0) ? LON - 1 : lonIdx - 1;
            const int32_t lonE     = (lonIdx == LON - 1) ? 0 : lonIdx + 1;
            const int32_t latS     = (latIdx == 0) ? 0 : latIdx - 1;
            const int32_t latN     = (latIdx == LAT - 1) ? LAT - 1 : latIdx + 1;
            int16_t otherId        = -1;
            std::size_t otherIdx   = idx;
            const std::size_t idxW = SphereField::cellIndex(lonW, latIdx);
            const std::size_t idxE = SphereField::cellIndex(lonE, latIdx);
            const std::size_t idxS = SphereField::cellIndex(lonIdx, latS);
            const std::size_t idxN = SphereField::cellIndex(lonIdx, latN);
            const int16_t nW       = field.plateId[idxW];
            const int16_t nE       = field.plateId[idxE];
            const int16_t nS       = field.plateId[idxS];
            const int16_t nN       = field.plateId[idxN];
            if (nW != selfId && nW >= 0) {
                otherId  = nW;
                otherIdx = idxW;
            } else if (nE != selfId && nE >= 0) {
                otherId  = nE;
                otherIdx = idxE;
            } else if (nS != selfId && nS >= 0) {
                otherId  = nS;
                otherIdx = idxS;
            } else if (nN != selfId && nN >= 0) {
                otherId  = nN;
                otherIdx = idxN;
            }
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
            const float fracDelta                   = selfFrac - otherFrac;
            if (std::fabs(fracDelta) < CF_SUBDUCTION_THRESHOLD) continue;
            int16_t overriderId;
            int16_t consumedSideId;
            int32_t consumedLon, consumedLat;
            const bool selfConsumed = (fracDelta < 0.0f);
            if (selfConsumed) {
                overriderId    = otherId;
                consumedSideId = selfId;
                consumedLon    = lonIdx;
                consumedLat    = latIdx;
            } else {
                overriderId    = selfId;
                consumedSideId = otherId;
                consumedLon    = static_cast<int32_t>(otherIdx % LON);
                consumedLat    = static_cast<int32_t>(otherIdx / LON);
            }

            // Gate by closing distance: only consume when the closing
            // rate * dt would have advanced past one trench width.
            const float closingKm = rate * dtMy * R;
            if (closingKm < SUBDUCTION_CELL_WIDTH_KM) continue;

            // Register a PEEL SEED (2026-07-06). The previous
            // implementation walked an independent 1-cell-wide DDA
            // tongue up to 60 cells deep from every boundary cell;
            // adjacent tongues diverge/cross and comb the consumed
            // plate into stranded 1-wide teeth (measured: plates with
            // 100+ raster components). Consumption now happens as a
            // coherent FRONT: each seed carries a budget from its
            // LOCAL closing rate; the round-based peel below advances
            // the whole front one cell per round while budgets last.
            const int32_t maxCells    = static_cast<int32_t>(closingKm / SUBDUCTION_CELL_WIDTH_KM);
            const int32_t budgetHere  = std::min(maxCells, kinematicCap);
            const std::size_t seedIdx = SphereField::cellIndex(consumedLon, consumedLat);
            if (budgetHere > peelBudget[seedIdx] ||
                (budgetHere == peelBudget[seedIdx] && overriderId < peelOwner[seedIdx])) {
                peelBudget[seedIdx]   = budgetHere;
                peelOwner[seedIdx]    = overriderId;
                peelConsumed[seedIdx] = consumedSideId;
            }
        }
    }

    // PEEL PHASE: flip seeds, then expand round by round. Each round
    // marks candidates from the previous round's flips (4-adjacent
    // cells still owned by the flipped cell's consumed plate, oceanic
    // only -- continental cells arrest the front and can never be
    // tunnelled behind), then applies them as a batch: rounds are
    // order-independent, ties resolve to the lowest overrider id.
    std::vector<std::size_t> frontier;
    for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
        if (peelBudget[i] < 0) continue;
        // Seed flip (identical semantics to the old walk's first cell).
        field.plateId[i]             = peelOwner[i];
        field.crustThicknessKm[i]    = PhysicsConstants::initialOceanicThicknessKm;
        field.continentalFraction[i] = 0.0f;
        field.crustAgeMy[i]          = 0.0f;
        field.sutureContactMy[i]     = 0.0f;
        // L10 sediment sink: recycle sediment at subduction zones.
        if (!field.sedimentThicknessKm.empty()) field.sedimentThicknessKm[i] = 0.0f;
        frontier.push_back(i);
    }
    while (!frontier.empty()) {
        // Mark: candidate -> (budget, owner, consumed), best-of rules.
        std::vector<std::size_t> marked;
        for (const std::size_t c : frontier) {
            const int32_t b = peelBudget[c] - 1;
            if (b <= 0) continue;
            const int32_t lon         = static_cast<int32_t>(c % LON);
            const int32_t lat         = static_cast<int32_t>(c / LON);
            const int32_t lonW        = (lon == 0) ? LON - 1 : lon - 1;
            const int32_t lonE        = (lon == LON - 1) ? 0 : lon + 1;
            const std::size_t nbrs[4] = {
                SphereField::cellIndex(lonW, lat),
                SphereField::cellIndex(lonE, lat),
                (lat > 0) ? SphereField::cellIndex(lon, lat - 1) : c,
                (lat < LAT - 1) ? SphereField::cellIndex(lon, lat + 1) : c,
            };
            for (const std::size_t n : nbrs) {
                if (n == c) continue;
                if (field.plateId[n] != peelConsumed[c]) continue;
                if (field.continentalFraction[n] > 0.5f) continue;
                if (b > candBudget[n] || (b == candBudget[n] && peelOwner[c] < candOwner[n])) {
                    if (candBudget[n] < 0) marked.push_back(n);
                    candBudget[n]   = b;
                    candOwner[n]    = peelOwner[c];
                    candConsumed[n] = peelConsumed[c];
                }
            }
        }
        // Flip the batch; it becomes the next frontier.
        std::sort(marked.begin(), marked.end());
        frontier.clear();
        for (const std::size_t n : marked) {
            field.plateId[n]             = candOwner[n];
            field.crustThicknessKm[n]    = PhysicsConstants::initialOceanicThicknessKm;
            field.continentalFraction[n] = 0.0f;
            field.crustAgeMy[n]          = 0.0f;
            field.sutureContactMy[n]     = 0.0f;
            if (!field.sedimentThicknessKm.empty()) field.sedimentThicknessKm[n] = 0.0f;
            peelBudget[n]   = candBudget[n];
            peelOwner[n]    = candOwner[n];
            peelConsumed[n] = candConsumed[n];
            candBudget[n]   = -1;
            frontier.push_back(n);
        }
    }
}

int32_t enforcePlateContiguity(SphereField& field, const std::vector<Plate>& plates) {
    // Plates are contiguous by definition; fragments here are
    // mechanism artifacts (rift plane-splits striping large plates,
    // residual advection strandings), not geology. Each plate keeps
    // its largest connected component; every smaller fragment
    // transfers to the neighbouring plate sharing the longest border
    // with it -- physically a terrane transfer: ownership only, the
    // crust columns stay untouched. Serial, deterministic: decisions
    // are computed from an immutable snapshot and applied as one
    // batch; ties resolve to the lowest plate id. Repeats up to 3
    // rounds (a batch application can itself disconnect a shape in
    // rare comb geometries). Returns cells moved (trace diagnostic).
    //
    // SIZE CAP: only fragments <= min(25 % of the plate, 500 cells)
    // move. Re-keying a subcontinent to a neighbour's Euler pole in
    // one epoch would be a kinematic discontinuity, and a rift child
    // born as two lobes must not lose its larger lobe to a third
    // plate; over-cap fragments are left for the next epoch's
    // mechanisms.
    constexpr int32_t LON        = SphereField::LON_CELLS;
    constexpr int32_t LAT        = SphereField::LAT_CELLS;
    constexpr int32_t MAX_ROUNDS = 3;
    if (plates.empty()) return 0;
    int32_t totalMoved = 0;
    std::vector<int32_t> comp(SphereField::CELL_COUNT);
    std::vector<std::size_t> stack;
    for (int32_t round = 0; round < MAX_ROUNDS; ++round) {
        // Global CCL over 4-adjacency (lon wraps, no cross-pole join).
        std::fill(comp.begin(), comp.end(), -1);
        std::vector<int64_t> compSize;
        std::vector<int16_t> compPlate;
        for (std::size_t s = 0; s < SphereField::CELL_COUNT; ++s) {
            if (comp[s] >= 0) continue;
            const int16_t pid = field.plateId[s];
            if (pid < 0) continue;
            const int32_t cid = static_cast<int32_t>(compSize.size());
            comp[s]           = cid;
            int64_t size      = 0;
            stack.clear();
            stack.push_back(s);
            while (!stack.empty()) {
                const std::size_t c = stack.back();
                stack.pop_back();
                ++size;
                const int32_t lon         = static_cast<int32_t>(c % LON);
                const int32_t lat         = static_cast<int32_t>(c / LON);
                const int32_t lonW        = (lon == 0) ? LON - 1 : lon - 1;
                const int32_t lonE        = (lon == LON - 1) ? 0 : lon + 1;
                const std::size_t nbrs[4] = {
                    SphereField::cellIndex(lonW, lat),
                    SphereField::cellIndex(lonE, lat),
                    (lat > 0) ? SphereField::cellIndex(lon, lat - 1) : c,
                    (lat < LAT - 1) ? SphereField::cellIndex(lon, lat + 1) : c,
                };
                for (const std::size_t n : nbrs) {
                    if (n == c || comp[n] >= 0) continue;
                    if (field.plateId[n] != pid) continue;
                    comp[n] = cid;
                    stack.push_back(n);
                }
            }
            compSize.push_back(size);
            compPlate.push_back(pid);
        }
        // Largest component per plate (ties: lower component id, i.e.
        // lower seed index, wins -- CCL discovers in raster order).
        std::vector<int32_t> keepComp(plates.size(), -1);
        for (std::size_t cid = 0; cid < compSize.size(); ++cid) {
            const std::size_t p = static_cast<std::size_t>(compPlate[cid]);
            if (p >= plates.size()) continue;
            if (keepComp[p] < 0 ||
                compSize[cid] > compSize[static_cast<std::size_t>(keepComp[p])]) {
                keepComp[p] = static_cast<int32_t>(cid);
            }
        }
        std::vector<int64_t> plateCells(plates.size(), 0);
        for (std::size_t cid = 0; cid < compSize.size(); ++cid) {
            const std::size_t p = static_cast<std::size_t>(compPlate[cid]);
            if (p < plates.size()) plateCells[p] += compSize[cid];
        }
        // Border tally per fragment: longest shared border wins
        // (snapshot reads only; -1 neighbours do not count).
        std::vector<std::map<int16_t, int32_t>> borders(compSize.size());
        for (std::size_t s = 0; s < SphereField::CELL_COUNT; ++s) {
            const int32_t cid = comp[s];
            if (cid < 0) continue;
            const std::size_t p =
                static_cast<std::size_t>(compPlate[static_cast<std::size_t>(cid)]);
            if (p >= plates.size() || keepComp[p] == cid) continue;
            const int32_t lon         = static_cast<int32_t>(s % LON);
            const int32_t lat         = static_cast<int32_t>(s / LON);
            const int32_t lonW        = (lon == 0) ? LON - 1 : lon - 1;
            const int32_t lonE        = (lon == LON - 1) ? 0 : lon + 1;
            const std::size_t nbrs[4] = {
                SphereField::cellIndex(lonW, lat),
                SphereField::cellIndex(lonE, lat),
                (lat > 0) ? SphereField::cellIndex(lon, lat - 1) : s,
                (lat < LAT - 1) ? SphereField::cellIndex(lon, lat + 1) : s,
            };
            for (const std::size_t n : nbrs) {
                if (n == s) continue;
                const int16_t nPid = field.plateId[n];
                if (nPid < 0 || nPid == compPlate[static_cast<std::size_t>(cid)]) {
                    continue;
                }
                ++borders[static_cast<std::size_t>(cid)][nPid];
            }
        }
        // Batch transfer.
        std::vector<int16_t> target(compSize.size(), static_cast<int16_t>(-1));
        for (std::size_t cid = 0; cid < compSize.size(); ++cid) {
            const std::size_t p = static_cast<std::size_t>(compPlate[cid]);
            if (p >= plates.size() || keepComp[p] == static_cast<int32_t>(cid)) {
                continue;
            }
            const int64_t cap = std::min<int64_t>(plateCells[p] / 4, 500);
            if (compSize[cid] > cap) continue; // over-cap: leave for later
            int16_t best    = -1;
            int32_t bestLen = 0;
            for (const std::pair<const int16_t, int32_t>& e : borders[cid]) {
                if (e.second > bestLen) {
                    bestLen = e.second;
                    best    = e.first;
                }
            }
            if (best >= 0) target[cid] = best;
        }
        int32_t moved = 0;
        for (std::size_t s = 0; s < SphereField::CELL_COUNT; ++s) {
            const int32_t cid = comp[s];
            if (cid < 0) continue;
            const int16_t t = target[static_cast<std::size_t>(cid)];
            if (t < 0) continue;
            field.plateId[s] = t;
            ++moved;
        }
        totalMoved += moved;
        if (moved == 0) break;
    }
    return totalMoved;
}

void applyContinentalDocking(SphereField& field, std::vector<Plate>& plates, float dtMy) {
    // Continental docking on the raster (2026-07-05; replaces the
    // centroid-distance merge that gated on an init-time RANDOM
    // landFraction). Two plates weld only after SUSTAINED
    // continent-continent convergent contact along a real suture
    // (India-Asia: ~50 My of collision before effective fusion).
    //
    // Per-cell `sutureContactMy` accumulates while a continental cell
    // (cf > 0.5) sits at a convergent boundary against a different
    // plate's continental cell, and resets when the contact ends --
    // per-cell state survives plate-list compaction remaps by
    // construction (a per-pair map keyed on plate ids would be
    // invalidated every epoch). Fresh Wilson-rift children cannot
    // re-dock spuriously: their mutual boundary is the carved oceanic
    // seam (cf = 0), which never counts as suture contact.
    // Gates tuned against the failure mode where craton nuclei that
    // straddle initial plate boundaries read as instant sutures and
    // weld the world into 4-6 plates within three epochs: docking
    // needs an ACTIVE collision (finite closing rate ~ > 1.3 cm/yr),
    // sustained for ~250 My (India-Asia: ~50 My and still welding;
    // full cratonisation of a suture takes hundreds of My), along a
    // continental-collision-scale front (~800 km).
    constexpr float DOCKING_CONTACT_MY  = 250.0f;
    constexpr float DOCKING_MIN_RATE    = 0.002f; // rad/My
    constexpr int32_t DOCKING_MIN_CELLS = 16;
    constexpr int32_t LON               = SphereField::LON_CELLS;
    constexpr int32_t LAT               = SphereField::LAT_CELLS;
    if (plates.empty()) return;
    // Ordered pair map: deterministic iteration feeds mergePlatesBatch
    // in sorted key order (union-find root selection is pair-order
    // dependent -- commit 61384bb precedent).
    std::map<std::pair<int16_t, int16_t>, int32_t> ripeSuture;
    for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
        for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
            const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
            const int16_t selfId  = field.plateId[idx];
            bool inContact        = false;
            int16_t otherId       = -1;
            if (selfId >= 0 && field.boundaryType[idx] == 1u &&
                field.convergenceRateRadPerMy[idx] > DOCKING_MIN_RATE &&
                field.continentalFraction[idx] > 0.5f) {
                const int32_t lonW        = (lonIdx == 0) ? LON - 1 : lonIdx - 1;
                const int32_t lonE        = (lonIdx == LON - 1) ? 0 : lonIdx + 1;
                const int32_t latS        = (latIdx == 0) ? 0 : latIdx - 1;
                const int32_t latN        = (latIdx == LAT - 1) ? LAT - 1 : latIdx + 1;
                const std::size_t nbrs[4] = {
                    SphereField::cellIndex(lonW, latIdx),
                    SphereField::cellIndex(lonE, latIdx),
                    SphereField::cellIndex(lonIdx, latS),
                    SphereField::cellIndex(lonIdx, latN),
                };
                for (const std::size_t n : nbrs) {
                    const int16_t nPid = field.plateId[n];
                    if (nPid >= 0 && nPid != selfId && field.continentalFraction[n] > 0.5f) {
                        inContact = true;
                        otherId   = nPid;
                        break;
                    }
                }
            }
            if (!inContact) {
                field.sutureContactMy[idx] = 0.0f;
                continue;
            }
            field.sutureContactMy[idx] += dtMy;
            if (field.sutureContactMy[idx] >= DOCKING_CONTACT_MY) {
                const std::pair<int16_t, int16_t> key = {std::min(selfId, otherId),
                                                         std::max(selfId, otherId)};
                ++ripeSuture[key];
            }
        }
    }
    std::vector<std::pair<std::size_t, std::size_t>> mergePairs;
    for (const std::pair<const std::pair<int16_t, int16_t>, int32_t>& e : ripeSuture) {
        if (e.second >= DOCKING_MIN_CELLS) {
            mergePairs.emplace_back(static_cast<std::size_t>(e.first.first),
                                    static_cast<std::size_t>(e.first.second));
        }
    }
    if (!mergePairs.empty()) {
        mergePlatesBatch(field, plates, mergePairs);
    }
}

float continentalAreaShare(const SphereField& field) {
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    double cont           = 0.0;
    double total          = 0.0;
    for (int32_t j = 0; j < LAT; ++j) {
        const float latDeg = -90.0f + (static_cast<float>(j) + 0.5f) * SphereField::CELL_DEG;
        const double w     = static_cast<double>(std::max(0.0f, std::cos(latDeg * 0.01745329252f)));
        double rowCont     = 0.0;
        const std::size_t rowBase = static_cast<std::size_t>(j) * static_cast<std::size_t>(LON);
        for (int32_t i = 0; i < LON; ++i) {
            if (field.continentalFraction[rowBase + static_cast<std::size_t>(i)] >= 0.5f) {
                rowCont += 1.0;
            }
        }
        cont += rowCont * w;
        total += static_cast<double>(LON) * w;
    }
    return (total > 0.0) ? static_cast<float>(cont / total) : 0.0f;
}

void recomputeOceanicCrustAge(SphereField& field) {
    // Seafloor age from distance to the nearest spreading ridge.
    //
    // Why this is needed. Oceanic crust age is reset to 0 only where a cell is
    // AT a divergent boundary (accreteAtDivergentBoundary) or is subducted, and
    // plate ownership does not advect -- advectPlateOwnership's incumbent-wins
    // rule means boundaries move only through mechanism passes. So the ocean
    // floor is essentially static and simply accumulates `+= dtMy` for the
    // whole run: measured, oceanic age saturates at the 3 Gy run length.
    // Through GDH1 that returns the asymptotic 5651 m depth almost everywhere,
    // which is why this planet has 30.8 % of its surface below -5000 m against
    // Earth's ~15 %, and only 10.8 % in the -3000..-5000 band against Earth's
    // ~35 %. The abyssal-plain MODE -- the single largest feature of Earth's
    // hypsometric curve -- is absent.
    //
    // Real seafloor age is a function of distance from the ridge, because the
    // plate carries crust away from it at the half-spreading rate. That is
    // reconstructed here directly rather than waiting for transport to do it:
    //   age = geodesic distance to nearest divergent boundary / halfSpreadRate
    // clamped to MAX_SEAFLOOR_AGE_MY. Earth's oldest in-situ ocean floor is
    // ~180-200 My (western Pacific, eastern Mediterranean) because everything
    // older has been subducted; the clamp stands in for that recycling, which
    // this sim does not otherwise perform on interior ocean.
    //
    // Half-spreading rate 35 km/My is Earth's area-weighted mean (Muller et al.
    // 2008 age grid: full rates 20-150 mm/yr, mean ~70 mm/yr = 70 km/My full,
    // 35 km/My half). At that rate the clamp is reached 7000 km from a ridge,
    // which is about the half-width of the Pacific -- so basin interiors
    // saturate and basin flanks carry a real gradient, as on Earth.
    //
    // CONTINENTAL CRUST IS NOT TOUCHED. Its age is a basement age with entirely
    // different meaning (billions of years, and read by the resource geology),
    // and nothing here should overwrite it.
    constexpr int32_t LON               = SphereField::LON_CELLS;
    constexpr int32_t LAT               = SphereField::LAT_CELLS;
    constexpr float HALF_SPREAD_KM_MY   = 35.0f;
    constexpr float MAX_SEAFLOOR_AGE_MY = 200.0f;
    constexpr float OCEANIC_GATE        = 0.5f;
    constexpr float CELL_RAD            = SphereField::CELL_DEG * 0.01745329252f;
    const float cellHeightKm            = PhysicsConstants::earthRadiusKm * CELL_RAD;
    // Distance beyond which the age is clamped anyway; the search stops there
    // so an ocean with no ridge at all does not sweep the whole raster.
    const float maxDistKm = HALF_SPREAD_KM_MY * MAX_SEAFLOOR_AGE_MY;

    std::vector<float> dist(SphereField::CELL_COUNT, std::numeric_limits<float>::max());
    // Dijkstra over oceanic cells, sourced at every oceanic cell adjacent to a
    // divergent boundary. Tie-broken by cell index so the pop order -- and
    // therefore the result -- is independent of the heap's internal ordering:
    // the same determinism requirement that keeps solveContinentalFreeboard
    // serial.
    using Node = std::pair<float, std::size_t>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;
    for (int32_t j = 0; j < LAT; ++j) {
        for (int32_t i = 0; i < LON; ++i) {
            const std::size_t idx = SphereField::cellIndex(i, j);
            if (field.continentalFraction[idx] >= OCEANIC_GATE) continue;
            if (field.boundaryType[idx] != 2u) continue; // 2 = Divergent
            dist[idx] = 0.0f;
            pq.emplace(0.0f, idx);
        }
    }
    while (!pq.empty()) {
        const auto [d, idx] = pq.top();
        pq.pop();
        if (d > dist[idx]) continue;
        if (d > maxDistKm) continue;
        const int32_t j = static_cast<int32_t>(idx) / LON;
        const int32_t i = static_cast<int32_t>(idx) % LON;
        const float cellWidthKm =
            cellHeightKm *
            std::max(0.05f,
                     std::cos((-90.0f + (static_cast<float>(j) + 0.5f) * SphereField::CELL_DEG) *
                              0.01745329252f));
        const int32_t iW                            = (i == 0) ? LON - 1 : i - 1;
        const int32_t iE                            = (i == LON - 1) ? 0 : i + 1;
        const std::pair<std::size_t, float> nbrs[4] = {
            {SphereField::cellIndex(iW, j), cellWidthKm},
            {SphereField::cellIndex(iE, j), cellWidthKm},
            {SphereField::cellIndex(i, std::max(0, j - 1)), cellHeightKm},
            {SphereField::cellIndex(i, std::min(LAT - 1, j + 1)), cellHeightKm},
        };
        for (const auto& [n, step] : nbrs) {
            if (n == idx) continue;
            if (field.continentalFraction[n] >= OCEANIC_GATE) continue;
            const float nd = d + step;
            if (nd < dist[n]) {
                dist[n] = nd;
                pq.emplace(nd, n);
            }
        }
    }

    for (std::size_t idx = 0; idx < SphereField::CELL_COUNT; ++idx) {
        if (field.continentalFraction[idx] >= OCEANIC_GATE) continue;
        const float d = dist[idx];
        // Ocean with no reachable ridge is old crust, not new: clamp, do not
        // zero. Zeroing would make every ridgeless basin a shallow young sea.
        const float age       = (d == std::numeric_limits<float>::max())
                                    ? MAX_SEAFLOOR_AGE_MY
                                    : std::min(MAX_SEAFLOOR_AGE_MY, d / HALF_SPREAD_KM_MY);
        field.crustAgeMy[idx] = age;
    }
}

void recomputeIsostaticElevationOnRaster(SphereField& field) {
    // The law itself lives in PlatePhysics.hpp as a pure function so
    // tests/test_isostasy.cpp can pin it against the literature without
    // constructing a raster. See that header for the derivation and for what
    // the previous single-datum form got wrong; the short version is that its
    // 3549 m datum was calibrated from the oceanic branch alone, which left the
    // crust thickness that sits at sea level (14.51 km) BELOW the thinnest
    // crust the simulation ever produced (15.8 km) -- so no continental cell
    // could be submerged and a continental shelf was impossible by
    // construction, not merely rare.
#if defined(AOC_HAS_OPENMP)
#pragma omp parallel for schedule(static)
#endif
    for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
        // The freeboard offset rides the continental branch only, scaled by
        // continentalFraction so the continent-ocean blend stays continuous.
        // Solved by solveContinentalFreeboard, which runs immediately after.
        field.surfaceElevationM[i] =
            isostaticElevationM(field.crustThicknessKm[i], field.continentalFraction[i],
                                field.crustAgeMy[i]) +
            field.continentalFreeboardM * field.continentalFraction[i];
    }
}

void applyContinentalMarginProfile(SphereField& field) {
    // The Steep Shoreline invariant, applied as a landform template.
    //
    // Rigid terrane transport made the continental CRUST compact -- measured,
    // its perimeter/equal-area-disc fell from 3.34/4.56/3.29 to 1.78/1.22/1.20
    // on seeds 42/7/100. The emergent LAND stayed ragged (3.95/3.42/2.93),
    // because elevation still varies enough across a compact continent that the
    // sea-level contour wanders through its interior instead of running around
    // its edge. Earth's coastline is compact for the opposite reason: a
    // continental platform sits flat and high, and drops to the abyss over a
    // continental slope only ~30-80 km wide, so `|grad z|` at the shoreline
    // exceeds interior relief by two to three orders of magnitude.
    //
    // So the platform is supplied as a function of distance from the
    // continent-ocean boundary rather than left to emerge from crustal
    // thickness. Elevation becomes
    //
    //     z = (H - Href) * dz/dH        <- orogenic roots, untouched
    //       + PLATFORM_M * smoothstep(d / RAMP)
    //
    // where d is the distance in cells to the nearest non-continental cell.
    // At the crust edge the platform term is 0, so the shoreline lands ON the
    // crust boundary -- which is now compact -- and mountains still stand on
    // their roots because the first term is untouched. Freeboard is solved
    // afterwards and sets the absolute stand.
    //
    // RAMP is 4 cells ~ 220 km at 0.5 deg. Earth's shelf-plus-slope runs
    // 50-500 km (Shepard 1963 mean shelf width 78 km; slope 20-80 km), so this
    // is at the wide end -- deliberately, because the hex sampler strides ~286
    // km and a narrower ramp would fall entirely between two tiles.
    constexpr int32_t LON     = SphereField::LON_CELLS;
    constexpr int32_t LAT     = SphereField::LAT_CELLS;
    constexpr float CONT_GATE = 0.5f;
    // The shoreline sits at a FIXED DISTANCE inside the crust outline, and the
    // elevation ramps linearly through it. That makes land a morphological
    // erosion of the crust mask -- a shape operation -- rather than a threshold
    // on an elevation distribution. A compact crust mask therefore yields a
    // compact coastline, which is the whole point: rigid transport already took
    // crust perimeter/equal-area-disc to 1.2-1.8, and this is what transfers
    // that to the emergent land.
    //
    // Everything that made the old coastline degenerate is removed by
    // construction. There is no flat platform for a global stand to slice
    // through, and no solved scalar decides land area -- SHORE_CELLS and the
    // seeded crust stock do, and both are geometric.
    //
    // SHORE_CELLS is large because the cratons are large in cell
    // terms at 0.5 deg: a block holding ~8 % of the sphere has a radius near 80
    // cells, and turning a ~47 % crust budget into a ~29 % land fraction means
    // drowning a rim of about 0.2 R. That drowned rim IS the continental shelf,
    // and its width is then an output to compare against Earth rather than a
    // tuned band.
    // 24 -> 20. Narrowing the drowned rim strictly dominated on a paired
    // single-seed sweep: it raised the shelf share (7.7 % -> 8.1 % of water)
    // AND cut the submerged share of crust (49.2 % -> 42.8 %) at once, because
    // a narrower rim spends proportionally more of itself inside the terrace.
    constexpr float SHORE_CELLS = 20.0f;
    constexpr float RAMP_HALF   = 10.0f;
    constexpr float RELIEF_M    = 300.0f;
    // Seaward of the shoreline the profile is NOT the mirror of the landward
    // ramp. It was, and that is why the shelf gate read 0/24 at 0.015 against a
    // 0.05-0.08 band: a symmetric +-RELIEF_M ramp over RAMP_HALF cells falls at
    // 30 m/cell, so it crosses the 140 m shelf-break depth within 4.7 cells and
    // the other ~19 cells of drowned rim sit below the cut. The rim was wide in
    // distance and almost entirely absent from the depth band that defines a
    // shelf.
    //
    // Earth's margin is two segments, not one: a wide, nearly flat shelf out to
    // a break at ~140 m (Shepard 1963 mean width 78 km, gradient ~0.1 deg), then
    // a continental slope that is narrow and steep (20-80 km, 3-6 deg) down to
    // the rise. Reproducing that shape here means the drowned rim spends most of
    // its WIDTH above the break and most of its RELIEF below it.
    //
    // SHELF_CELLS is the width of the terrace, and it is the tuning knob for the
    // shelf gate. The slope then takes the remaining SHORE_CELLS - SHELF_CELLS
    // cells to fall from the break to SLOPE_FOOT_M, which is ~172 m/cell -- an
    // order of magnitude steeper than the terrace, which is the whole point.
    //
    // Both constants are set by what survives the projection to the hex grid,
    // which is where the gate is measured. At 140x90 against a 720x360 raster
    // one tile spans ~5 raster cells and its elevation is a 4x4 footprint
    // AVERAGE, so a terrace only 12 cells (~2.3 tiles) wide is eaten from both
    // sides: tiles straddling the shoreline average up into land, tiles
    // straddling the break average down into the slope. Measured at
    // SHELF_CELLS=12 / break 140: the raster carried shelf/planet 0.053, inside
    // the band, while the hex map read 0.024 -- the rim shrank from 27.8 % to
    // 20.7 % of water and the shallow share within it from 26.6 % to 16.3 %,
    // compounding to a 2.2x loss. The physics was right and the sampling ate it.
    //
    // So the terrace is 18 cells (~3.5 tiles), wide enough to have interior
    // tiles that straddle neither edge, and it grades to 90 m rather than to
    // the 140 m cut, leaving 50 m of headroom before an averaged tile falls out
    // of the band. That is also the more faithful shape: Earth's shelf averages
    // ~60 m deep and breaks at ~140 m, so a terrace using the entire depth
    // range to the break was already too steep.
    constexpr float SHELF_CELLS   = 18.0f;
    constexpr float SHELF_BREAK_M = 90.0f;
    constexpr float SLOPE_FOOT_M  = 2200.0f;
    // Fixed, NOT tied to SHELF_CELLS: the root fades over a set distance from
    // the shoreline, so widening the terrace does not drag isostatic relief
    // further out to sea and turn more of the shelf into land.
    constexpr float ROOT_TAPER_CELLS = 6.0f;
    // Distance is measured to the WORLD OCEAN, not to any non-continental cell.
    //
    // The distinction is not pedantic. Measuring to the nearest non-continental
    // cell means a one-cell gap between two adjacent blocks is a margin on both
    // sides, so the ramp drowns ~22 cells either way and a hairline gap becomes
    // a 40-cell strait carved through what should be continuous land. Those
    // were visible as thin channels running across the landmasses, and they
    // also inflate the coastline perimeter the whole exercise is trying to
    // reduce.
    //
    // So: find the connected components of non-continental cells, take the
    // largest as the world ocean, and seed the ramp only from continental cells
    // that touch it. Enclosed seas and narrow inter-block gaps then sit in the
    // continental interior at full platform height, which is what an
    // epicontinental sea or a suture actually is.
    std::vector<int32_t> oceanComp(SphereField::CELL_COUNT, -1);
    std::vector<int32_t> compSize;
    {
        std::vector<int32_t> stack;
        for (int32_t j = 0; j < LAT; ++j) {
            for (int32_t i = 0; i < LON; ++i) {
                const std::size_t start = SphereField::cellIndex(i, j);
                if (field.continentalFraction[start] >= CONT_GATE) continue;
                if (oceanComp[start] >= 0) continue;
                const int32_t id = static_cast<int32_t>(compSize.size());
                int32_t n        = 0;
                stack.clear();
                stack.push_back(static_cast<int32_t>(start));
                oceanComp[start] = id;
                while (!stack.empty()) {
                    const int32_t cur = stack.back();
                    stack.pop_back();
                    ++n;
                    const int32_t cj        = cur / LON;
                    const int32_t ci        = cur % LON;
                    const int32_t iW        = (ci == 0) ? LON - 1 : ci - 1;
                    const int32_t iE        = (ci == LON - 1) ? 0 : ci + 1;
                    const std::size_t nb[4] = {
                        SphereField::cellIndex(iW, cj), SphereField::cellIndex(iE, cj),
                        SphereField::cellIndex(ci, std::max(0, cj - 1)),
                        SphereField::cellIndex(ci, std::min(LAT - 1, cj + 1))};
                    for (const std::size_t nn : nb) {
                        if (field.continentalFraction[nn] >= CONT_GATE) continue;
                        if (oceanComp[nn] >= 0) continue;
                        oceanComp[nn] = id;
                        stack.push_back(static_cast<int32_t>(nn));
                    }
                }
                compSize.push_back(n);
            }
        }
    }
    int32_t worldOcean = -1;
    {
        int32_t best = -1;
        for (std::size_t k = 0; k < compSize.size(); ++k) {
            if (compSize[k] > best) {
                best       = compSize[k];
                worldOcean = static_cast<int32_t>(k);
            }
        }
    }

    std::vector<int16_t> dist(SphereField::CELL_COUNT, -1);
    std::vector<int32_t> queue;
    queue.reserve(SphereField::CELL_COUNT / 8);
    for (int32_t j = 0; j < LAT; ++j) {
        for (int32_t i = 0; i < LON; ++i) {
            const std::size_t idx = SphereField::cellIndex(i, j);
            if (field.continentalFraction[idx] < CONT_GATE) continue;
            const int32_t iW        = (i == 0) ? LON - 1 : i - 1;
            const int32_t iE        = (i == LON - 1) ? 0 : i + 1;
            const std::size_t nb[4] = {SphereField::cellIndex(iW, j), SphereField::cellIndex(iE, j),
                                       SphereField::cellIndex(i, std::max(0, j - 1)),
                                       SphereField::cellIndex(i, std::min(LAT - 1, j + 1))};
            for (const std::size_t n : nb) {
                if (field.continentalFraction[n] < CONT_GATE && oceanComp[n] == worldOcean) {
                    dist[idx] = 0;
                    queue.push_back(static_cast<int32_t>(idx));
                    break;
                }
            }
        }
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const int32_t cur       = queue[head];
        const int32_t j         = cur / LON;
        const int32_t i         = cur % LON;
        const int32_t iW        = (i == 0) ? LON - 1 : i - 1;
        const int32_t iE        = (i == LON - 1) ? 0 : i + 1;
        const std::size_t nb[4] = {SphereField::cellIndex(iW, j), SphereField::cellIndex(iE, j),
                                   SphereField::cellIndex(i, std::max(0, j - 1)),
                                   SphereField::cellIndex(i, std::min(LAT - 1, j + 1))};
        for (const std::size_t n : nb) {
            if (field.continentalFraction[n] < CONT_GATE) continue;
            if (dist[n] >= 0) continue;
            dist[n] = static_cast<int16_t>(dist[static_cast<std::size_t>(cur)] + 1);
            queue.push_back(static_cast<int32_t>(n));
        }
    }

#if defined(AOC_HAS_OPENMP)
#pragma omp parallel for schedule(static)
#endif
    for (std::size_t idx = 0; idx < SphereField::CELL_COUNT; ++idx) {
        const float cf = field.continentalFraction[idx];
        if (cf < CONT_GATE) continue;
        // dist < 0 means the BFS never reached this cell from the world ocean:
        // continental crust entirely enclosed by an inland sea. That is deep
        // interior, so it takes the full platform rather than being skipped
        // and left with whatever the isostatic law happened to give it.
        const float d = (dist[idx] < 0) ? (SHORE_CELLS + RAMP_HALF) : static_cast<float>(dist[idx]);
        // Signed distance from the shoreline: positive inland, negative drowned.
        const float s = d - SHORE_CELLS;
        float profile;
        if (s >= 0.0f) {
            profile = RELIEF_M * std::min(1.0f, s / RAMP_HALF);
        } else if (-s <= SHELF_CELLS) {
            profile = -SHELF_BREAK_M * (-s / SHELF_CELLS);
        } else {
            const float t =
                std::min(1.0f, (-s - SHELF_CELLS) / std::max(1.0f, SHORE_CELLS - SHELF_CELLS));
            profile = -SHELF_BREAK_M - (SLOPE_FOOT_M - SHELF_BREAK_M) * t;
        }
        // Orogenic roots ride on top, so mountain belts stand where their crust
        // is thick rather than where the distance field puts them. THICKENING
        // is passed through in full; THINNING is damped to a fifth.
        //
        // The asymmetry is deliberate and was measured. At 142.4 m per km of
        // crust, a cell thinned to 35 km carries a -854 m root, which swamps the
        // +300 m platform and drowns it even deep in a continental interior --
        // 198 fully-interior water tiles and 551 narrow channels on seed 42,
        // visible as straits cut across the landmasses. Real cratonic interiors
        // do not behave that way: they sit near +100-300 m in isostatic
        // equilibrium, planed flat, largely regardless of modest thickness
        // variation. Damped, a 35 km cell sits at +129 m and stays land while a
        // genuinely thin ~30 km cell still floods, which is what an
        // intracratonic basin is.
        const float rawRoot =
            (field.crustThicknessKm[idx] - PhysicsConstants::refContinentalThicknessKm) *
            continentalElevationPerKmM();
        const float root = (rawRoot > 0.0f) ? rawRoot : rawRoot * 0.2f;
        // The root FADES OUT across the shelf, and that is what makes a shelf
        // exist at all rather than merely be drawn.
        //
        // Measured with the terrace in but the root at full weight: the water
        // ring adjacent to land had median depth 467 m and a p10-p90 spread of
        // ~1800 m, against a terrace only 140 m tall. Rim crust is thinned, so
        // even damped to a fifth a cell 10 km under reference carries -285 m --
        // twice the shelf break on its own. Ring-to-ring profile was entirely
        // swamped by within-ring thickness variation, and the depth cut selected
        // 3.4 % of water instead of the ~20 % it should.
        //
        const float rootW = (s >= 0.0f) ? 1.0f : std::max(0.0f, 1.0f + s / ROOT_TAPER_CELLS);
        // Physically the root SHOULD vanish here. A continental shelf is a
        // planated surface -- wave-cut, sediment-draped, graded to sea level --
        // and its flatness comes from that levelling, not from uniform crust
        // beneath it. So isostatic relief is weighted out from the shoreline to
        // the shelf break, leaving the terrace to set the bathymetry.
        //
        // Tapering alone was not enough: it moved the adjacent ring only from
        // 467 m to 333 m median, still well past the break, because half a
        // -285 m root still doubles a 70 m terrace. The rule seaward is
        // therefore asymmetric -- NEGATIVE relief is filled, positive relief
        // stands. That is what sediment does: a shelf basin is buried by the
        // outbuilding wedge and grades to the same surface, while a bank or a
        // volcanic island keeps its height. The terrace becomes the FLOOR of
        // the bathymetry rather than merely its average.
        //
        // KNOWN COST, measured, not tuned away. This same relief is what makes
        // the shoreline ragged, so filling it smooths the coast:
        // coast_box_dimension fell 1.141 -> 1.068 on 6 of 6 seeds against a
        // 1.15 floor. Three attempts to separate the two failed, and each traced
        // the same frontier rather than escaping it:
        //   - fading over 6 / 12 / 18 cells: shelf 5/6, 0/6, 0/6 against box
        //     dimension 1.061, 1.126, 1.146;
        //   - starting the fill 3 / 6 / 10 cells offshore so the waterline keeps
        //     its relief: box dimension 1.067, 1.073, 1.068 -- no effect;
        //   - bounding the amplitude to +-25 / 40 / 70 m instead of fading it:
        //     shelf 1/6, 1/6, 0/6 against box dimension 1.083, 1.105, 1.126.
        // The shelf gate is sensitive specifically to NEGATIVE relief offshore
        // -- a -40 m root on a terrace grading to -90 m puts a cell past the
        // break once the hex sampler averages it -- so only the fill serves it.
        // One field is doing two jobs and no setting of it serves both; giving
        // the coastline its roughness back needs a separate source of
        // short-wavelength relief, not another setting of this one.
        const float relief           = (s >= 0.0f) ? root : std::max(0.0f, root * rootW);
        field.surfaceElevationM[idx] = relief + profile;
    }
}

void solveContinentalFreeboard(SphereField& field) {
    // Sea level is 0 by definition. The free scalar is continental freeboard:
    // how far the continental platform stands above the waterline. It is
    // bisected against LAND FRACTION, which is a property of the continental
    // branch -- unlike the fixed-water-volume solve this replaces, whose
    // derivative at the stand is the ocean area and which was therefore set by
    // the abyss and blind to the continents it positioned. See the comment on
    // SphereField::oceanVolumeEquivDepthM for the measured consequence.
    //
    // The target is Earth's 29.2 % (NOAA). Land fraction consequently stops
    // being an independent gate and becomes a convergence check; the honest
    // constraint moves to the REPORTED ocean volume, which this solve no longer
    // forces and which can therefore disagree with Earth and say so.
    //
    // SERIAL fixed-order summation, for the same reason the old solve was:
    // an OpenMP float reduction is thread-count dependent and breaks
    // test_determinism and the portable golden preset.
    constexpr int32_t LON  = SphereField::LON_CELLS;
    constexpr int32_t LAT  = SphereField::LAT_CELLS;
    constexpr float TARGET = 0.292f;
    // Physical bound on freeboard. Earth's is ~840 m of mean land elevation
    // over ~40 km of crust; +-1500 m brackets any plausible planet and stops a
    // crust-starved early epoch (continental area ~8 % at epoch 1, where 29.2 %
    // land is simply unreachable) from running the bisection off to a
    // nonsensical stand. Hitting the clamp early is expected and self-corrects
    // as arc growth and accretion build continental area.
    // Clamped tightly since the margin profile took over placing the
    // shoreline. Freeboard is now a small eustatic adjustment, not the thing
    // that decides land area: a range wide enough to move the stand through the
    // platform would slice it, which is the degeneracy the profile exists to
    // remove. Land fraction is consequently a geometric OUTPUT -- crust area
    // minus the drowned rim -- and is tuned by the seeded stock and the ramp,
    // not by this solve.
    constexpr float FB_MIN = -200.0f;
    constexpr float FB_MAX = 200.0f;

    field.seaLevelM = 0.0f;

    float latWeight[LAT];
    double totalWeight = 0.0;
    for (int32_t j = 0; j < LAT; ++j) {
        const float latDeg = -90.0f + (static_cast<float>(j) + 0.5f) * SphereField::CELL_DEG;
        latWeight[j]       = std::max(0.0f, std::cos(latDeg * 0.01745329252f));
        totalWeight += static_cast<double>(latWeight[j]) * static_cast<double>(LON);
    }

    // Elevation without any freeboard, so the bisection can add a trial offset
    // rather than re-running the whole isostatic law per iteration.
    const auto landFractionAt = [&](float fb) -> double {
        double land = 0.0;
        for (int32_t j = 0; j < LAT; ++j) {
            const double w = static_cast<double>(latWeight[j]);
            double row     = 0.0;
            for (int32_t i = 0; i < LON; ++i) {
                const std::size_t idx = SphereField::cellIndex(i, j);
                const float base = field.surfaceElevationM[idx] -
                                   field.continentalFreeboardM * field.continentalFraction[idx];
                if (base + fb * field.continentalFraction[idx] > 0.0f) row += 1.0;
            }
            land += row * w;
        }
        return land / totalWeight;
    };

    // Freeboard is retired as a solved quantity. applyContinentalMarginProfile
    // now places the shoreline geometrically, at a fixed distance inside the
    // crust outline, so there is nothing left for a global stand to solve
    // against -- and any stand wide enough to matter would slice the ramp and
    // re-create the degeneracy the profile removes. Kept at 0 so the field and
    // its readers stay valid.
    field.continentalFreeboardM = 0.0f;
    (void)FB_MIN;
    (void)FB_MAX;
    (void)TARGET;
    (void)landFractionAt;

    // Ocean volume is now an OUTPUT. Report it so a hypsometry that needs an
    // un-Earthlike amount of water to fill it is visible rather than silently
    // absorbed by the solve.
    double vol = 0.0;
    for (int32_t j = 0; j < LAT; ++j) {
        const double w = static_cast<double>(latWeight[j]);
        double row     = 0.0;
        for (int32_t i = 0; i < LON; ++i) {
            const float z = field.surfaceElevationM[SphereField::cellIndex(i, j)];
            if (z < 0.0f) row += static_cast<double>(-z);
        }
        vol += row * w;
    }
    field.oceanVolumeEquivDepthM = static_cast<float>(vol / totalWeight);
}

void solveSeaLevelFixedVolume(SphereField& field) {
    // Sea level is where the world's FIXED water volume fills the
    // hypsometric bowl (Earth's water inventory has been essentially
    // constant since the Archean; the level is a consequence of
    // hypsometry, not a tunable). Solve
    //   sum_i max(0, z_sea - elev_i) * cosLat_i
    //     = oceanVolumeEquivDepthM * sum_i cosLat_i
    // for z_sea by bisection. Approximation: no water-load isostasy
    // re-solve (mantleDatumM bakes modern ocean loading into the Airy
    // calibration), so a stand shift of +-100-200 m is slightly
    // over-stated -- acceptable at this fidelity.
    //
    // SERIAL fixed-order summation on purpose: an OpenMP reduction
    // makes the float sum thread-count-dependent, which breaks
    // test_determinism and the portable golden preset.
    constexpr int32_t LON = SphereField::LON_CELLS;
    constexpr int32_t LAT = SphereField::LAT_CELLS;
    constexpr float Z_MIN = -6000.0f;
    constexpr float Z_MAX = 6000.0f;

    // Per-latitude area weights (cell area scales with cos(lat)).
    float latWeight[LAT];
    double totalWeight = 0.0;
    for (int32_t j = 0; j < LAT; ++j) {
        const float latDeg = -90.0f + (static_cast<float>(j) + 0.5f) * SphereField::CELL_DEG;
        latWeight[j]       = std::max(0.0f, std::cos(latDeg * 0.01745329252f));
        totalWeight += static_cast<double>(latWeight[j]) * static_cast<double>(LON);
    }
    const double targetVolume = static_cast<double>(field.oceanVolumeEquivDepthM) * totalWeight;

    auto floodedVolume = [&](float zSea) -> double {
        double vol = 0.0;
        for (int32_t j = 0; j < LAT; ++j) {
            const double w            = static_cast<double>(latWeight[j]);
            double rowSum             = 0.0;
            const std::size_t rowBase = static_cast<std::size_t>(j) * static_cast<std::size_t>(LON);
            for (int32_t i = 0; i < LON; ++i) {
                const float d =
                    zSea - field.surfaceElevationM[rowBase + static_cast<std::size_t>(i)];
                if (d > 0.0f) rowSum += static_cast<double>(d);
            }
            vol += rowSum * w;
        }
        return vol;
    };

    // Warm-start bracket around the previous epoch's level (the
    // hypsometry moves slowly between epochs); fall back to the full
    // bracket when the warm one does not straddle the target.
    float lo = field.seaLevelM - 500.0f;
    float hi = field.seaLevelM + 500.0f;
    if (lo < Z_MIN) lo = Z_MIN;
    if (hi > Z_MAX) hi = Z_MAX;
    if (!(floodedVolume(lo) < targetVolume && floodedVolume(hi) > targetVolume)) {
        lo = Z_MIN;
        hi = Z_MAX;
        if (floodedVolume(hi) <= targetVolume) {
            // Degenerate hypsometry: even a +6 km stand cannot hold
            // the water budget (near-all-land world). Saturate.
            LOG_WARN("SphereFieldPhysics: sea-level bracket saturated "
                     "high (all-land hypsometry?)");
            field.seaLevelM = Z_MAX;
            return;
        }
        if (floodedVolume(lo) >= targetVolume) {
            LOG_WARN("SphereFieldPhysics: sea-level bracket saturated "
                     "low (all-ocean hypsometry?)");
            field.seaLevelM = Z_MIN;
            return;
        }
    }
    for (int32_t iter = 0; iter < 40; ++iter) {
        const float mid = 0.5f * (lo + hi);
        if (floodedVolume(mid) < targetVolume) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    field.seaLevelM = 0.5f * (lo + hi);
}

// Calibration gain on the stream-power law, set so total denudation matches
// the slope-only law it replaces. Measured with AOC_DUMP_EROSION: the point of
// L9b is to move erosion into channels, not to erode more.
inline constexpr float STREAM_GAIN = 1.20f;
static const bool kDumpErosion     = std::getenv("AOC_DUMP_EROSION") != nullptr;
static double gErodedRockM         = 0.0;
static std::size_t gErodedCells    = 0;

// L10 sediment routing constants.
// Airy ratio for sediment: km of surface elevation per km of sediment pile.
// rho_sed ≈ 2016 kg/m3 → (3300 - 2016) / 3300 = 0.389 ≈ 0.39.
// Matches the plan-calibrated value from the L10 second attempt.
inline constexpr float SEDIMENT_BUOYANCY = 0.39f;
// Maximum sediment pile in km. Earth's passive margins carry 3-10 km;
// 5 km caps pathological stacks while allowing a real shelf prism to develop.
inline constexpr float MAX_SEDIMENT_KM = 5.0f;
// Fraction of available water-column accommodation filled per epoch.
// 0.05 lets slope cells converge toward shelf depth over ~60 epochs
// (H_n = 0.95^n H_0; residual after 60 epochs ≈ 5 % of initial depth)
// while preventing single-step overfill at any starting depth.
inline constexpr float SEDIMENT_ACCOMMODATION_FRAC = 0.05f;
// Minimum water depth (m, negative) for deposition.  Cells shallower
// than this are the geometric shelf terrace produced by
// applyContinentalMarginProfile; depositing there would raise them
// above sea level and collapse the shelf gate.  Sediment builds the
// shelf by filling the continental SLOPE upward into the shelf band.
inline constexpr float SEDIMENT_MIN_DEPTH_M = -90.0f;
// Ring-fan half-radius (cells) for river-mouth spreading.
// 5 cells × 0.5 deg ≈ 280 km, continental-shelf scale.
inline constexpr int32_t SEDIMENT_FAN_RADIUS = 5;
// Hard cap on fan cells per mouth to bound the O(r²) ring scan.
inline constexpr int32_t SEDIMENT_FAN_MAX_CELLS = 96;

void reportErosionTotals() {
    if (!kDumpErosion) return;
    std::fprintf(stderr,
                 "[erosion] total rock removed %.6g m-cells over %zu cell-steps "
                 "(mean %.4g m per eroding cell-step)\n",
                 gErodedRockM, gErodedCells, gErodedRockM / std::max<std::size_t>(1, gErodedCells));
}

void computeDrainage(const SphereField& field, std::vector<int32_t>& receiver,
                     std::vector<int32_t>& order, std::vector<float>& drainageAreaKm2) {
    // Where a river goes, before anything is eroded. See the header for the
    // contract; this is the routing half of L9, deliberately separated from
    // incision so the network can be inspected on its own.
    constexpr int32_t LON    = SphereField::LON_CELLS;
    constexpr int32_t LAT    = SphereField::LAT_CELLS;
    constexpr float CELL_RAD = SphereField::CELL_DEG * 0.01745329252f;
    const float cellHeightKm = PhysicsConstants::earthRadiusKm * CELL_RAD;
    const std::size_t N      = SphereField::CELL_COUNT;

    receiver.assign(N, -1);
    drainageAreaKm2.assign(N, 0.0f);
    order.clear();
    order.reserve(N);

    // Per-row geometry. Longitude pitch shrinks as cos(lat); ignoring that
    // would make polar cells look enormously steep and capture every river.
    std::vector<float> widthKm(static_cast<std::size_t>(LAT));
    std::vector<float> areaKm2(static_cast<std::size_t>(LAT));
    for (int32_t j = 0; j < LAT; ++j) {
        const float latDeg = -90.0f + (static_cast<float>(j) + 0.5f) * SphereField::CELL_DEG;
        const float c      = std::max(0.02f, std::cos(latDeg * 0.01745329252f));
        widthKm[static_cast<std::size_t>(j)] = cellHeightKm * c;
        areaKm2[static_cast<std::size_t>(j)] = cellHeightKm * widthKm[static_cast<std::size_t>(j)];
    }

    // PRIORITY-FLOOD (Barnes, Lehman & Mulla 2014) from sea level.
    //
    // Depressions have to be dealt with or every closed basin swallows its
    // drainage and no discharge reaches the coast, which is exactly the signal
    // wanted. The fill is written to a SEPARATE surface used only for routing:
    // the real elevation must not move, because the margin profile owns it.
    //
    // Seeded from every cell below sea level, so the base level is the ocean
    // itself and an inland sea drains to it only if a path exists.
    constexpr float INF = std::numeric_limits<float>::max();
    std::vector<float> zf(N, INF);
    std::vector<uint8_t> done(N, 0u);
    // Keyed on (elevation, index): ties break on index, so the pop order is
    // fixed regardless of how the heap happens to arrange equal keys. Without
    // the index the fill is still correct but not reproducible.
    using Node = std::pair<float, int32_t>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;
    for (std::size_t i = 0; i < N; ++i) {
        if (field.surfaceElevationM[i] < 0.0f) {
            zf[i] = field.surfaceElevationM[i];
            pq.emplace(zf[i], static_cast<int32_t>(i));
        }
    }
    while (!pq.empty()) {
        const Node top = pq.top();
        pq.pop();
        const int32_t cur = top.second;
        if (done[static_cast<std::size_t>(cur)]) continue;
        done[static_cast<std::size_t>(cur)] = 1u;
        const int32_t j                     = cur / LON;
        const int32_t i                     = cur % LON;
        for (int32_t dj = -1; dj <= 1; ++dj) {
            const int32_t nj = j + dj;
            if (nj < 0 || nj >= LAT) continue;
            for (int32_t di = -1; di <= 1; ++di) {
                if (di == 0 && dj == 0) continue;
                const int32_t ni       = (i + di + LON) % LON;
                const std::size_t nIdx = SphereField::cellIndex(ni, nj);
                if (done[nIdx]) continue;
                // The filled height is the higher of the cell's own elevation
                // and the level of the spill point reached so far, plus an
                // EPSILON so the fill is strictly increasing away from the
                // outlet.
                //
                // Without the epsilon a filled depression is exactly flat, no
                // cell in it has a strictly lower neighbour, and every one
                // becomes a sink that swallows its own drainage. Measured
                // without it: 12363 sinks against 8278 river mouths, and the
                // largest basin reached 0.36 % of land where Earth's Amazon is
                // ~4 %. This is Barnes, Lehman & Mulla's (2014) epsilon variant
                // and it is what makes flats drain toward their spill point.
                //
                // 1e-4 m per step is far below any elevation the gates read,
                // so even a basin thousands of cells across is inflated by
                // centimetres.
                constexpr float FILL_EPS_M = 1e-4f;
                const float nz = std::max(field.surfaceElevationM[nIdx],
                                          zf[static_cast<std::size_t>(cur)] + FILL_EPS_M);
                if (nz < zf[nIdx]) {
                    zf[nIdx] = nz;
                    pq.emplace(nz, static_cast<int32_t>(nIdx));
                }
            }
        }
    }

    // D8 receiver on the filled surface: steepest descent by true metric
    // gradient, not by height difference alone.
    for (int32_t j = 0; j < LAT; ++j) {
        const float wKm    = widthKm[static_cast<std::size_t>(j)];
        const float diagKm = std::sqrt(wKm * wKm + cellHeightKm * cellHeightKm);
        for (int32_t i = 0; i < LON; ++i) {
            const std::size_t idx = SphereField::cellIndex(i, j);
            if (field.surfaceElevationM[idx] < 0.0f) continue; // ocean: an outlet
            if (zf[idx] == INF) continue;                      // never reached
            float bestSlope = 0.0f;
            int32_t best    = -1;
            for (int32_t dj = -1; dj <= 1; ++dj) {
                const int32_t nj = j + dj;
                if (nj < 0 || nj >= LAT) continue;
                for (int32_t di = -1; di <= 1; ++di) {
                    if (di == 0 && dj == 0) continue;
                    const int32_t ni       = (i + di + LON) % LON;
                    const std::size_t nIdx = SphereField::cellIndex(ni, nj);
                    if (zf[nIdx] == INF) continue;
                    const float drop = zf[idx] - zf[nIdx];
                    if (drop <= 0.0f) continue;
                    const float dist  = (di == 0) ? cellHeightKm : ((dj == 0) ? wKm : diagKm);
                    const float slope = drop / dist;
                    // Strict >, then lowest index, so equal slopes resolve the
                    // same way every run.
                    if (slope > bestSlope ||
                        (slope == bestSlope && best >= 0 && static_cast<int32_t>(nIdx) < best)) {
                        bestSlope = slope;
                        best      = static_cast<int32_t>(nIdx);
                    }
                }
            }
            receiver[idx]        = best;
            drainageAreaKm2[idx] = areaKm2[static_cast<std::size_t>(j)];
            order.push_back(static_cast<int32_t>(idx));
        }
    }

    // Accumulate downstream, highest filled elevation first, so a cell's own
    // catchment is complete before it donates. Sorting by (zf, index) keeps
    // the traversal reproducible where many cells share a filled level -- which
    // is common inside a filled depression.
    std::sort(order.begin(), order.end(), [&](int32_t a, int32_t b) {
        const float za = zf[static_cast<std::size_t>(a)];
        const float zb = zf[static_cast<std::size_t>(b)];
        if (za != zb) return za > zb;
        return a < b;
    });
    for (const int32_t idx : order) {
        const int32_t r = receiver[static_cast<std::size_t>(idx)];
        if (r < 0) continue;
        drainageAreaKm2[static_cast<std::size_t>(r)] +=
            drainageAreaKm2[static_cast<std::size_t>(idx)];
    }

    if (std::getenv("AOC_DUMP_DRAINAGE") != nullptr) {
        double landKm2 = 0.0, maxA = 0.0;
        std::size_t landCells = 0, mouths = 0, sinks = 0;
        double mouthMax = 0.0;
        for (std::size_t i = 0; i < N; ++i) {
            if (field.surfaceElevationM[i] < 0.0f) continue;
            ++landCells;
            landKm2 += areaKm2[static_cast<std::size_t>(i / LON)];
            maxA            = std::max(maxA, static_cast<double>(drainageAreaKm2[i]));
            const int32_t r = receiver[i];
            if (r < 0) {
                ++sinks;
            } else if (field.surfaceElevationM[static_cast<std::size_t>(r)] < 0.0f) {
                ++mouths;
                mouthMax = std::max(mouthMax, static_cast<double>(drainageAreaKm2[i]));
            }
        }
        std::vector<float> as;
        as.reserve(landCells);
        for (std::size_t i = 0; i < N; ++i) {
            if (field.surfaceElevationM[i] >= 0.0f) as.push_back(drainageAreaKm2[i]);
        }
        std::sort(as.begin(), as.end());
        const auto pct = [&](double p) {
            return as.empty()
                       ? 0.0f
                       : as[std::min(as.size() - 1, static_cast<std::size_t>(p * (as.size() - 1)))];
        };
        std::fprintf(stderr,
                     "[drainage] land=%zu cells (%.3g Mkm2) mouths=%zu sinks=%zu\n"
                     "[drainage] area km2: p50=%.4g p90=%.4g p99=%.4g max=%.4g "
                     "(largest basin %.2f%% of land)\n"
                     "[drainage] largest basin reaching the sea: %.4g km2\n",
                     landCells, landKm2 * 1e-6, mouths, sinks, static_cast<double>(pct(0.50)),
                     static_cast<double>(pct(0.90)), static_cast<double>(pct(0.99)), maxA,
                     100.0 * maxA / std::max(1.0, landKm2), mouthMax);
    }
}

void applySurfaceErosionOnRaster(SphereField& field, float dtMy,
                                 const std::vector<float>& drainageAreaKm2,
                                 std::vector<float>* erodedVolKm3) {
    // Metres of crust that must be removed per metre of surface lowering.
    // Derived as the INVERSE of the elevation law's own slope rather than
    // recomputed from the densities, so the two cannot drift: erosion and
    // isostasy are a matched pair, and if erosion converts at a different rate
    // than the law credits, mountains erode at the wrong speed and the
    // discrepancy is invisible in the output. 7.02 m crust per m of
    // continental relief, 8.25 m per m of oceanic.
    const float airyRatio_cont = 1000.0f / continentalElevationPerKmM();
    const float airyRatio_oce  = 1000.0f / oceanicElevationPerKmM();
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
    constexpr int32_t LON    = SphereField::LON_CELLS;
    constexpr int32_t LAT    = SphereField::LAT_CELLS;
    constexpr float CELL_RAD = SphereField::CELL_DEG * 0.01745329252f;
    const float earthRadiusM = PhysicsConstants::earthRadiusKm * 1000.0f;
    const float cellHeightM  = earthRadiusM * CELL_RAD;
    // L10 slope exclusion: when sediment exists, subtract its elevation
    // contribution from neighbour samples before computing gradients so the
    // sediment→slope→erosion feedback cannot run away (the sediment-modified
    // surface builds the shelf but does not steepen the river-incision rate).
    const bool hasSediment = !field.sedimentThicknessKm.empty();
    // L10 eroded-volume output: initialise to zero if the caller wants it.
    const std::size_t N = SphereField::CELL_COUNT;
    if (erodedVolKm3 != nullptr) erodedVolKm3->assign(N, 0.0f);
#if defined(AOC_HAS_OPENMP)
#pragma omp parallel for schedule(static)
#endif
    for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
        const float latDeg = -90.0f + (static_cast<float>(latIdx) + 0.5f) * SphereField::CELL_DEG;
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
        const int32_t latS     = std::max(0, latIdx - 1);
        const int32_t latN     = std::min(LAT - 1, latIdx + 1);
        for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
            const std::size_t idx = SphereField::cellIndex(lonIdx, latIdx);
            const float z         = field.surfaceElevationM[idx];
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
            // Base level follows the solved sea level (2026-07-05):
            // erosion grades toward the shoreline, wherever the fixed
            // water volume puts it -- this is the freeboard
            // self-regulation loop (high stand -> more of the land
            // column erodes -> isostatic adjustment -> equilibrium).
            //
            // 2026-08-10: +600 -> +150. The +600 was not chosen from
            // geomorphology; the comment it replaced says it was raised from
            // +100 because "+100 parked every eroded interior inside the
            // +-400 m coastal-detail band, which then speckled it into
            // land/water noise" -- a physics constant bent to work around a
            // rendering artefact. That artefact is fixed at its own site
            // (MapGenerator now bounds the coastal-detail amplitude by local
            // relief instead of using a flat +-400 m), so the floor can go back
            // to something defensible.
            //
            // It matters more than it looks. A floor 600 m above the stand
            // guarantees erosion can never bring land near sea level, so no
            // interior can ever be flooded: measured before this change, 90 %
            // of continental crust sat in a single 45-430 m band and 99 % of
            // land was ONE connected component. Earth drowns ~30 % of its
            // continental crust, and the drowned parts -- epicontinental seas,
            // Hudson Bay, the Baltic, the Sunda shelf -- are exactly what
            // separates one landmass from the next.
            //
            // +150 m keeps a real peneplain floor (Davis 1899, Hack 1960:
            // shields reach dynamic equilibrium against slow uplift rather
            // than eroding to nothing) without pre-emptively fencing the
            // shoreline out of the continental interior.
            // The peneplain floor applies to HILLSLOPES, not to channels.
            //
            // A river grades to sea level -- that is what base level means --
            // and a valley that cannot reach it cannot be drowned into a ria,
            // which is the whole mechanism L9 exists to produce. A single floor
            // at +150 m stops every channel 150 m short of the coast.
            //
            // It was also measurably binding: raising the stream-power gain 20 %
            // increased total denudation only 5.8 %, because the extra was
            // absorbed by cells clamping against this floor rather than cutting
            // deeper. Erosion here is cap-limited, not rate-limited.
            //
            // So the floor is relaxed in proportion to discharge, logarithmically
            // between a hillslope and a major river. Interfluves keep the full
            // +150 m that stops shoreline retreat from cannibalising continents
            // (the reason the floor exists, documented above); trunk streams
            // grade to the sea.
            constexpr float PENEPLAIN_M    = 150.0f;
            constexpr float CHANNEL_A0_KM2 = 1.0e4f; // hillslope
            constexpr float CHANNEL_A1_KM2 = 1.0e6f; // trunk river
            float channelW                 = 0.0f;
            if (!drainageAreaKm2.empty()) {
                const float a = std::max(1.0f, drainageAreaKm2[idx]);
                channelW = std::clamp((std::log10(a) - std::log10(CHANNEL_A0_KM2)) /
                                          (std::log10(CHANNEL_A1_KM2) - std::log10(CHANNEL_A0_KM2)),
                                      0.0f, 1.0f);
            }
            const float baseLevelM = field.seaLevelM + PENEPLAIN_M * (1.0f - channelW);
            if (z < baseLevelM) continue;
            // Neighbour elevations are clamped at sea level before the
            // gradient is taken. Rivers grade to BASE LEVEL, not to the sea
            // floor: the continental slope is a submarine feature and no
            // subaerial process sees it. Without the clamp a coastal cell
            // measures its gradient against the abyss -- (840 + 2600) m over
            // one 55 km cell, a slope of 0.06 -- and the stream-power law
            // returns 6 km of lowering in a single 50 My step, so the cell is
            // planated to the floor immediately and its inland neighbour
            // inherits the same cliff next epoch. That is a planation wave
            // that eats a continent from its edges, and it is why continental
            // crust measured a 2.4 km spread (p5-p90) sitting exactly at
            // whatever the base level happened to be: the clamp, not the
            // stream-power law, was setting continental elevation.
            //
            // gen/Relief.cpp already excludes water neighbours for the same
            // reason ("the continental slope to the abyss is not roughness,
            // and counting it would make every shoreline the steepest place on
            // the map"); erosion never got the same treatment.
            const float sea    = field.seaLevelM;
            const int32_t lonW = (lonIdx == 0) ? LON - 1 : lonIdx - 1;
            const int32_t lonE = (lonIdx == LON - 1) ? 0 : lonIdx + 1;
            // L10 slope exclusion: subtract sediment elevation from each
            // neighbour so the erosion law sees the BEDROCK surface. Without
            // this, sediment deposited on the shelf steepens coastal gradients
            // and drives a positive feedback (more sediment → steeper slope →
            // more erosion → more sediment) that caused the second L10 attempt
            // to run away (denudation 2.5x, piles 24-30 km). The sediment
            // still contributes to surfaceElevationM for geometry / routing,
            // just not to the slope that drives the incision rate.
            const auto bedrockZ = [&](std::size_t nk) -> float {
                const float elev = field.surfaceElevationM[nk];
                if (!hasSediment) return std::max(sea, elev);
                const float sedElev = field.sedimentThicknessKm[nk] * 1000.0f * SEDIMENT_BUOYANCY;
                return std::max(sea, elev - sedElev);
            };
            const float zW    = bedrockZ(SphereField::cellIndex(lonW, latIdx));
            const float zE    = bedrockZ(SphereField::cellIndex(lonE, latIdx));
            const float zS    = bedrockZ(SphereField::cellIndex(lonIdx, latS));
            const float zN    = bedrockZ(SphereField::cellIndex(lonIdx, latN));
            const float dzLon = (zE - zW) / (2.0f * cellWidthM);
            const float dzLat = (zN - zS) / (2.0f * cellHeightM);
            const float slope = std::sqrt(dzLon * dzLon + dzLat * dzLat);
            const float c     = field.continentalFraction[idx];
            const float airy  = c * airyRatio_cont + (1.0f - c) * airyRatio_oce;
            // K_EROSION * slope is a DENUDATION rate -- metres of ROCK removed
            // per My, which is the quantity this constant's own comment cites
            // (cratonic 5-15 m/My, Himalayan 200 m/My). It is not a
            // surface-lowering rate: an eroding column rebounds isostatically,
            // so removing rock lowers the surface by only
            // (1 - rho_c/rho_m) ~ 1/7 of the thickness removed.
            //
            // 2026-08-10: the previous code read K*slope*dt as the SURFACE
            // lowering and then multiplied by the Airy ratio to get the rock
            // removed, applying the 7x a second time -- so denudation ran at
            // ~7x the calibrated rate. At that rate any relief gentler than
            // ~170 m per 1000 km is erased inside the 3 Gy run, which is why
            // every continent arrived as a featureless plain pinned at the
            // base level (measured: continental crust p5-p90 spanning
            // 26.7-29.1 km, a 2.4 km spread mapping to ~340 m of elevation,
            // sitting exactly wherever the floor was). Earth's cratons survive
            // 3 Gy precisely because rebound makes net lowering a seventh of
            // denudation.
            // STREAM POWER when a drainage field is supplied (L9b), the
            // historical slope-only law otherwise.
            //
            //     dz/dt = -K (A/Aref)^m S
            //
            // Slope alone erodes a hillside and a river valley at the same rate
            // if they are equally steep, so it lowers terrain uniformly and
            // cannot cut a channel. Discharge is what distinguishes them, and
            // channels are what indent a coastline: a valley reaching the shore
            // is a ria. That is the whole reason for L9 -- four cheaper
            // mechanisms were measured against the coastline gates and failed.
            //
            // m = 0.5, n = 1 (Whipple & Tucker 1999). Aref normalises so the
            // TOTAL denudation is unchanged and only its DISTRIBUTION moves;
            // see the [erosion] dump, which exists to hold that invariant. A K
            // that changes the total would shift every hypsometry gate at once
            // and confound the measurement.
            constexpr float STREAM_M    = 0.5f;
            constexpr float STREAM_AREF = 1.0e4f; // km2
            float dRockM;
            if (drainageAreaKm2.empty()) {
                dRockM = K_EROSION_M_PER_MY_PER_SLOPE * slope * dtMy;
            } else {
                const float a = std::max(1.0f, drainageAreaKm2[idx]) / STREAM_AREF;
                dRockM        = K_EROSION_M_PER_MY_PER_SLOPE * STREAM_GAIN * std::pow(a, STREAM_M) *
                                slope * dtMy;
            }
            // Cap so one forward-Euler step cannot drive the SURFACE below the
            // peneplain floor; convert that surface allowance back into rock
            // thickness through the same ratio.
            const float maxRockM = std::max(0.0f, (z - baseLevelM) * airy);
            if (dRockM > maxRockM) dRockM = maxRockM;
            if (dRockM <= 0.0f) continue;
            float h = field.crustThicknessKm[idx] - dRockM * 1e-3f;
            if (h < 0.0f) h = 0.0f;
            field.crustThicknessKm[idx] = h;
            if (kDumpErosion) {
                gErodedRockM += static_cast<double>(dRockM);
                ++gErodedCells;
            }
            // L10: record eroded rock volume (km3) for sediment routing.
            if (erodedVolKm3 != nullptr) {
                const float cellAreaKm2 = (cellWidthM * 1e-3f) * (cellHeightM * 1e-3f);
                (*erodedVolKm3)[idx]    = dRockM * 1e-3f * cellAreaKm2;
            }
        }
    }
}

void routeSediment(SphereField& field, const std::vector<int32_t>& receiver,
                   const std::vector<int32_t>& order, const std::vector<float>& erodedVolKm3) {
    // Carry eroded rock volumes (km3 per cell) downstream to river mouths and
    // deposit as sediment on the continental shelf.
    //
    // Design:
    // - Volume transfer: each eroding cell contributes dRockKm × areaKm2 km3.
    //   Routing accumulates this so totals are area-conserving regardless of
    //   cos-lat size variation.
    // - order is sorted descending by filled elevation (headwaters first),
    //   same ordering as computeDrainage. Walking front-to-back ensures each
    //   tributary is summed before propagating onward.
    // - Deposition at ocean cells directly downstream of a land cell (river
    //   mouths). Flux is spread over a ring fan (SEDIMENT_FAN_RADIUS, max
    //   SEDIMENT_FAN_MAX_CELLS cells) in fixed (dj, di) order, shared equally.
    // - Accommodation cap per fan cell per epoch: at most
    //   SEDIMENT_ACCOMMODATION_FRAC × water-depth / SEDIMENT_BUOYANCY km of
    //   sediment, preventing single-step overfill.
    // - Equilibrium sink: applySubduction zeroes sedimentThicknessKm for
    //   consumed cells so the pile cannot grow without bound.
    constexpr int32_t LON    = SphereField::LON_CELLS;
    constexpr int32_t LAT    = SphereField::LAT_CELLS;
    constexpr float CELL_RAD = SphereField::CELL_DEG * 0.01745329252f;
    const float cellHeightKm = PhysicsConstants::earthRadiusKm * CELL_RAD;

    // Per-row cell area in km2.
    std::vector<float> areaKm2(static_cast<std::size_t>(LAT));
    for (int32_t j = 0; j < LAT; ++j) {
        const float latDeg = -90.0f + (static_cast<float>(j) + 0.5f) * SphereField::CELL_DEG;
        const float c      = std::max(0.02f, std::cos(latDeg * 0.01745329252f));
        areaKm2[static_cast<std::size_t>(j)] = cellHeightKm * cellHeightKm * c;
    }

    // Accumulate sediment flux downstream (km3). Start from eroded volumes.
    const std::size_t N     = SphereField::CELL_COUNT;
    std::vector<float> flux = erodedVolKm3;
    if (flux.size() != N) flux.assign(N, 0.0f);
    for (const int32_t k : order) {
        const int32_t r = receiver[static_cast<std::size_t>(k)];
        if (r < 0) continue;
        flux[static_cast<std::size_t>(r)] += flux[static_cast<std::size_t>(k)];
    }

    // Deposit at river mouths (land cell whose direct receiver is ocean).
    struct FanCell {
        std::size_t idx;
        float maxVolKm3;
    };
    std::vector<FanCell> fan;
    fan.reserve(static_cast<std::size_t>(SEDIMENT_FAN_MAX_CELLS));

    for (const int32_t k : order) {
        if (field.surfaceElevationM[static_cast<std::size_t>(k)] < 0.0f) continue;
        const int32_t r = receiver[static_cast<std::size_t>(k)];
        if (r < 0) continue;
        if (field.surfaceElevationM[static_cast<std::size_t>(r)] >= 0.0f) continue;
        const float vol = flux[static_cast<std::size_t>(k)];
        if (vol <= 0.0f) continue;

        // Scan ocean cells in the ring fan around the mouth cell.
        const int32_t mLon = static_cast<int32_t>(static_cast<std::size_t>(r) % LON);
        const int32_t mLat = static_cast<int32_t>(static_cast<std::size_t>(r) / LON);
        const int32_t R2   = SEDIMENT_FAN_RADIUS * SEDIMENT_FAN_RADIUS;
        fan.clear();
        for (int32_t dj = -SEDIMENT_FAN_RADIUS;
             dj <= SEDIMENT_FAN_RADIUS && static_cast<int32_t>(fan.size()) < SEDIMENT_FAN_MAX_CELLS;
             ++dj) {
            const int32_t nj = mLat + dj;
            if (nj < 0 || nj >= LAT) continue;
            for (int32_t di = -SEDIMENT_FAN_RADIUS;
                 di <= SEDIMENT_FAN_RADIUS &&
                 static_cast<int32_t>(fan.size()) < SEDIMENT_FAN_MAX_CELLS;
                 ++di) {
                if (di * di + dj * dj > R2) continue;
                const int32_t ni       = (mLon + di + LON) % LON;
                const std::size_t nIdx = SphereField::cellIndex(ni, nj);
                if (field.surfaceElevationM[nIdx] > SEDIMENT_MIN_DEPTH_M) continue;
                const float waterDepthKm = -field.surfaceElevationM[nIdx] * 1e-3f;
                const float maxAddKm =
                    SEDIMENT_ACCOMMODATION_FRAC * waterDepthKm / SEDIMENT_BUOYANCY;
                const float maxFromCap =
                    std::max(0.0f, MAX_SEDIMENT_KM - field.sedimentThicknessKm[nIdx]);
                const float avail     = std::min(maxAddKm, maxFromCap);
                const float maxVolKm3 = avail * areaKm2[static_cast<std::size_t>(nj)];
                if (maxVolKm3 <= 0.0f) continue;
                fan.push_back({nIdx, maxVolKm3});
            }
        }
        if (fan.empty()) continue;

        // Distribute flux equally; cap each cell by its accommodation.
        const float shareKm3 = vol / static_cast<float>(fan.size());
        for (const FanCell& fc : fan) {
            const float depositKm3 = std::min(shareKm3, fc.maxVolKm3);
            const int32_t fj       = static_cast<int32_t>(fc.idx / static_cast<std::size_t>(LON));
            const float addKm      = depositKm3 / areaKm2[static_cast<std::size_t>(fj)];
            field.sedimentThicknessKm[fc.idx] =
                std::min(MAX_SEDIMENT_KM, field.sedimentThicknessKm[fc.idx] + addKm);
        }
    }
}

void stepSpherePhysicsEpoch(SphereField& field, std::vector<Plate>& plates,
                            std::vector<uint8_t>& boundaryScratch, uint32_t& rngState, float dtMy,
                            std::vector<Terrane>* terranes, TerraneBody* terraneBody) {
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
    // AOC_NO_ADVECT freezes plate ownership and the fields it transports.
    // advectPlateOwnership resamples the cf raster ~900 times per world, and
    // its own incumbent-wins rule means it cannot move a plate boundary -- so
    // it is the prime suspect for degrading the crust footprint from 1.28x an
    // equal-area disc at epoch 1 to 2.62x by epoch 60. Gated so that is a
    // measurement rather than an inference.
    static const bool kNoAdvect = std::getenv("AOC_NO_ADVECT") != nullptr;
    // MEASURED 2026-08-31, and the result rules out the obvious suspicion.
    // The transport is nearest-neighbour, so interpolation is not what smears
    // the crust field; the suspect was the sheer number of resamples (~15 per
    // epoch, ~900 per world) and their ORPHAN-FILL repair path, which copies
    // from an arbitrary nearby source whenever a destination has no valid
    // departure point. Sweeping this bound at 1/2/4/16/1000 (i.e. down to one
    // substep per epoch) changed crust perimeter/equal-area-disc on seeds 42/7
    // from 3.34/4.56 to 4.16-5.27 / 3.35-4.65 -- no better, mostly worse, and
    // non-monotonic. Resample COUNT is not the damage; the mechanism is.
    // Disabling advection entirely (AOC_NO_ADVECT) takes those same numbers to
    // 1.48/1.23. Do not try to fix this by tuning the substep bound.
    constexpr float CFL_SAFETY = 1.0f;
    float maxOmegaDeg          = 0.0f;
    for (const Plate& p : plates) {
        const float a = std::fabs(p.angularVelDeg);
        if (a > maxOmegaDeg) maxOmegaDeg = a;
    }
    // Per-pass continental-volume attribution -- which pass is adding crust and
    // which is removing it, in the relative units continentalCrustVolume uses.
    // Off unless AOC_TRACE_BUDGET is set; each snapshot is a full raster sweep,
    // so this roughly doubles epoch cost when enabled and costs one branch when
    // not. This is the instrument that identified advection as a
    // non-conservative transport and erosion as the dominant sink -- see the
    // memory file for the numbers.
    static const bool kBudgetTrace = std::getenv("AOC_TRACE_BUDGET") != nullptr;
    double budgetPrev              = kBudgetTrace ? continentalCrustVolume(field) : 0.0;
    double dAdvect = 0.0, dThicken = 0.0, dArcs = 0.0, dAccrete = 0.0, dSubduct = 0.0;
    double dDiverge = 0.0, dDock = 0.0, dSlab = 0.0, dRift = 0.0, dContig = 0.0, dErode = 0.0;
    const auto budgetSnap = [&](double& acc) {
        if (!kBudgetTrace) return;
        const double v = continentalCrustVolume(field);
        acc            = v - budgetPrev;
        budgetPrev     = v;
    };

    float remainingDt = dtMy;
    if (kNoAdvect) {
        // fall through: no transport at all
    } else if (maxOmegaDeg > 0.0f) {
        const float maxStep = CFL_SAFETY * SphereField::CELL_DEG / maxOmegaDeg;
        while (remainingDt > 1e-6f) {
            const float subDt = std::min(maxStep, remainingDt);
            advectPlateOwnership(field, plates, subDt);
            remainingDt -= subDt;
        }
    } else {
        advectPlateOwnership(field, plates, dtMy);
    }
    budgetSnap(dAdvect);
    despecklePlateOwnership(field);
    // Rigid terrane transport supersedes advection's handling of the
    // continental crust fields. Advection still runs (it carries plate
    // ownership and the oceanic fields); the bake then overwrites continental
    // crust from the rigid bodies, so the resampling damage measured in
    // Terrane.hpp never reaches the field whose 0.5 contour is the coastline.
    static const bool kNoTerranes = std::getenv("AOC_NO_TERRANES") != nullptr;
    if (!kNoTerranes && terranes != nullptr && terraneBody != nullptr) {
        advanceTerraneRotations(*terranes, plates, dtMy);
        bakeTerranesToRaster(field, *terranes, *terraneBody);
    }
    markBoundaryCells(field, boundaryScratch);
    accumulateClosingRate(field, plates, boundaryScratch);
    thickenFromClosingRate(field, dtMy);
    budgetSnap(dThicken);
    // Arc volcanism converts oceanic margins into andesitic
    // continental crust at convergent boundaries — the mechanism
    // that grows the global continental fraction over the 3 Gy run
    // from the ~5 % Archean baseline to the ~29 % modern figure.
    // Must run before applySubduction so cells about to be consumed
    // can still partially convert (the arc is just inboard of the
    // trench in real geology), and so that the next thicken pass
    // sees the elevated continentalFraction.
    growContinentalFractionAtArcs(field, dtMy);
    budgetSnap(dArcs);
    // Phase 1.4b: terrane-accretion diffusion. Saturated continental
    // cells donate cf to same-plate neighbours, modelling collisional
    // welding / interior accretion. Without this, cont(>0.5) plateaus
    // at the arc-band footprint (~16% sphere across all seeds) and
    // cf_mean never reaches the Cawood et al. 2013 modern 0.25-0.35
    // band. Runs BEFORE subduction so the diffusion frontier is
    // exposed to subduction's consumption pass in the same epoch and
    // cf stays in dynamic equilibrium between growth and destruction.
    accreteToNeighbours(field, dtMy);
    budgetSnap(dAccrete);
    applySubduction(field, plates, dtMy);
    budgetSnap(dSubduct);
    // Ridge accretion: extrude fresh oceanic basalt on already-oceanic
    // cells whose plates are pulling apart. The continental-fraction
    // gate inside the function protects continental rift zones; those
    // are handled by `applyWilsonRifting` on its own thermal-age
    // timescale. Without ridge accretion, divergent boundaries leave
    // stretched stale crust behind drifting plates with no mid-ocean-
    // ridge spreading record (Atlantic age gradient never appears).
    accreteAtDivergentBoundary(field, dtMy);
    budgetSnap(dDiverge);
    applyContinentalDocking(field, plates, dtMy);
    budgetSnap(dDock);
    applySlabPullFeedback(field, plates, dtMy);
    budgetSnap(dSlab);
    applyWilsonRifting(field, plates, rngState, dtMy);
    budgetSnap(dRift);
    const int32_t contiguityMoved = enforcePlateContiguity(field, plates);
    budgetSnap(dContig);
    recomputeOceanicCrustAge(field);
    recomputeIsostaticElevationOnRaster(field);
    applyContinentalMarginProfile(field);
    // L10: add sediment elevation AFTER the margin profile overwrites
    // surfaceElevationM for continental cells. This must run every epoch
    // because applyContinentalMarginProfile resets the field; the sediment
    // pile itself persists in sedimentThicknessKm across epochs.
    // Disabled by AOC_NO_SEDIMENT (same gate as routeSediment below).
    static const bool kNoSediment = std::getenv("AOC_NO_SEDIMENT") != nullptr;
    if (!kNoSediment && !field.sedimentThicknessKm.empty()) {
        for (std::size_t k = 0; k < SphereField::CELL_COUNT; ++k) {
            field.surfaceElevationM[k] +=
                field.sedimentThicknessKm[k] * 1000.0f * SEDIMENT_BUOYANCY;
        }
    }
    solveContinentalFreeboard(field);
    // L9b+L10: route drainage, incise channels, deposit sediment at mouths.
    // AOC_NO_STREAM_POWER falls back to the slope-only law (L9b disabled).
    // AOC_NO_SEDIMENT disables routeSediment while keeping L9b active.
    static const bool kNoStreamPower = std::getenv("AOC_NO_STREAM_POWER") != nullptr;
    if (kNoStreamPower) {
        applySurfaceErosionOnRaster(field, dtMy);
    } else {
        std::vector<int32_t> rcv, ord;
        std::vector<float> area;
        computeDrainage(field, rcv, ord, area);
        std::vector<float> erodedVol;
        applySurfaceErosionOnRaster(field, dtMy, area, kNoSediment ? nullptr : &erodedVol);
        if (!kNoSediment && !erodedVol.empty() && !field.sedimentThicknessKm.empty()) {
            routeSediment(field, rcv, ord, erodedVol);
        }
    }
    budgetSnap(dErode);
    if (kBudgetTrace) {
        std::fprintf(stderr,
                     "[budget] advect=%+.4g thicken=%+.4g arcs=%+.4g accrete=%+.4g "
                     "subduct=%+.4g diverge=%+.4g dock=%+.4g slab=%+.4g rift=%+.4g "
                     "contig=%+.4g erode=%+.4g total=%.6g\n",
                     dAdvect, dThicken, dArcs, dAccrete, dSubduct, dDiverge, dDock, dSlab, dRift,
                     dContig, dErode, budgetPrev);
    }
    if (!kNoTerranes && terranes != nullptr && terraneBody != nullptr) {
        writebackTerraneCrust(field, *terranes, *terraneBody);
    }
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
    static const bool kSpherePhysTrace = std::getenv("AOC_SPHEREPHYS_TRACE") != nullptr;
    if (kSpherePhysTrace) {
        float maxRate = 0.0f, minRate = 0.0f, maxCrust = 0.0f, maxZ = -1e9f;
        std::size_t mountainCells    = 0;
        std::size_t boundaryCount    = 0;
        std::size_t continentalCells = 0;
        // Per-boundary-type counts for the Muller 2022 histogram check
        // (~35 % convergent / 40 % divergent / 25 % transform on the
        // modern Earth boundary network).
        std::size_t btConvergent = 0, btDivergent = 0, btTransform = 0;
        double sumContFrac = 0.0;
        for (std::size_t i = 0; i < SphereField::CELL_COUNT; ++i) {
            const float r  = field.convergenceRateRadPerMy[i];
            const float h  = field.crustThicknessKm[i];
            const float z  = field.surfaceElevationM[i];
            const float cf = field.continentalFraction[i];
            if (r > maxRate) maxRate = r;
            if (r < minRate) minRate = r;
            if (boundaryScratch[i]) ++boundaryCount;
            switch (field.boundaryType[i]) {
            case 1u:
                ++btConvergent;
                break;
            case 2u:
                ++btDivergent;
                break;
            case 3u:
                ++btTransform;
                break;
            default:
                break;
            }
            if (h > maxCrust) maxCrust = h;
            if (z > maxZ) maxZ = z;
            if (z > 4000.0f) ++mountainCells;
            if (cf > 0.5f) ++continentalCells;
            sumContFrac += static_cast<double>(cf);
        }
        const double meanContFrac = sumContFrac / static_cast<double>(SphereField::CELL_COUNT);
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
        // Plate-contiguity diagnostic: connected components per plate
        // on the raster (4-adjacency, lon wrap, no cross-pole joins).
        // Real plates are contiguous by definition; fragments here are
        // mechanism bugs, not geology.
        std::size_t fragmentedPlates = 0;
        std::size_t maxComponents    = 0;
        {
            constexpr int32_t LONC = SphereField::LON_CELLS;
            constexpr int32_t LATC = SphereField::LAT_CELLS;
            std::vector<int32_t> comp(SphereField::CELL_COUNT, -1);
            std::vector<std::size_t> stack;
            std::vector<int32_t> compsPerPlate(plates.size(), 0);
            int32_t nextComp = 0;
            for (std::size_t s = 0; s < SphereField::CELL_COUNT; ++s) {
                if (comp[s] >= 0) continue;
                const int16_t pid = field.plateId[s];
                if (pid < 0) continue;
                comp[s] = nextComp;
                stack.clear();
                stack.push_back(s);
                while (!stack.empty()) {
                    const std::size_t c = stack.back();
                    stack.pop_back();
                    const int32_t lon         = static_cast<int32_t>(c % LONC);
                    const int32_t lat         = static_cast<int32_t>(c / LONC);
                    const int32_t lonW        = (lon == 0) ? LONC - 1 : lon - 1;
                    const int32_t lonE        = (lon == LONC - 1) ? 0 : lon + 1;
                    const std::size_t nbrs[4] = {
                        SphereField::cellIndex(lonW, lat),
                        SphereField::cellIndex(lonE, lat),
                        (lat > 0) ? SphereField::cellIndex(lon, lat - 1) : c,
                        (lat < LATC - 1) ? SphereField::cellIndex(lon, lat + 1) : c,
                    };
                    for (const std::size_t n : nbrs) {
                        if (n == c) continue;
                        if (comp[n] >= 0) continue;
                        if (field.plateId[n] != pid) continue;
                        comp[n] = nextComp;
                        stack.push_back(n);
                    }
                }
                if (static_cast<std::size_t>(pid) < compsPerPlate.size()) {
                    ++compsPerPlate[static_cast<std::size_t>(pid)];
                }
                ++nextComp;
            }
            for (const int32_t k : compsPerPlate) {
                if (k > 1) ++fragmentedPlates;
                if (static_cast<std::size_t>(k) > maxComponents) {
                    maxComponents = static_cast<std::size_t>(k);
                }
            }
        }
        const float cflCells = (traceMaxOmegaDeg * dtMy) / SphereField::CELL_DEG;
        std::fprintf(stderr,
                     "[sphere] dt=%.1fMy rate[%.4f..%.4f] crust=%.1fkm "
                     "z=%.0fm freeboard=%.0fm mtn=%zu cont(>0.5)=%zu cf_mean=%.3f "
                     "plates=%zu boundary=%zu btype(c/d/t)=%zu/%zu/%zu "
                     "frag=%zu maxComp=%zu terrane=%d "
                     "maxOmega=%.3fdeg/My cflCells=%.1f contVol=%.6g\n",
                     static_cast<double>(dtMy), static_cast<double>(minRate),
                     static_cast<double>(maxRate), static_cast<double>(maxCrust),
                     static_cast<double>(maxZ), static_cast<double>(field.continentalFreeboardM),
                     mountainCells, continentalCells, meanContFrac, plates.size(), boundaryCount,
                     btConvergent, btDivergent, btTransform, fragmentedPlates, maxComponents,
                     contiguityMoved, static_cast<double>(traceMaxOmegaDeg),
                     static_cast<double>(cflCells), continentalCrustVolume(field));
    }
}

} // namespace aoc::map::gen

/**
 * @file MapGenerator.cpp
 * @brief Procedural map generation using layered value noise.
 */

#include "aoc/map/MapGenerator.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/gen/CliffCoast.hpp"
#include "aoc/map/gen/Biogeography.hpp"
#include "aoc/map/gen/ClimateBiome.hpp"
#include "aoc/map/gen/CoastalErosion.hpp"
#include "aoc/map/gen/EarthSystem.hpp"
#include "aoc/map/gen/HexNeighbors.hpp"
#include "aoc/map/gen/IceAndRock.hpp"
#include "aoc/map/gen/LakePurge.hpp"
#include "aoc/map/gen/MapGenContext.hpp"
#include "aoc/map/gen/Mappability.hpp"
#include "aoc/map/gen/PostSim.hpp"
#include "aoc/map/gen/Thresholds.hpp"
#include "aoc/map/gen/AtmosphereOcean.hpp"
#include "aoc/map/gen/BiomeSubtypes.hpp"
#include "aoc/map/gen/KoppenStructures.hpp"
#include "aoc/map/gen/Lithology.hpp"
#include "aoc/map/gen/CoastalLandforms.hpp"
#include "aoc/map/gen/InsolationSlope.hpp"
#include "aoc/map/gen/StreamRiparian.hpp"
#include "aoc/map/gen/NppCarryingCapacity.hpp"
#include "aoc/map/gen/DrainageLivestock.hpp"
#include "aoc/map/gen/EcoAnalytics.hpp"
#include "aoc/map/gen/Noise.hpp"
#include "aoc/map/gen/Plate.hpp"
#include "aoc/map/gen/SphereGeometry.hpp"
#include "aoc/map/gen/PlatePhysics.hpp"
#include "aoc/map/gen/SphereField.hpp"
#include "aoc/map/gen/SphereFieldPhysics.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/map/Chokepoint.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <vector>

// OpenMP-backed parallelization for independent per-tile post-passes.
// AOC_PARALLEL_FOR_ROWS expands to a row-parallel pragma when OpenMP is
// available, otherwise a no-op. Each post-pass that writes ONLY to its
// own tile (no neighbour scatter, no shared accumulators) can prefix
// the outer for-row loop with this macro for free CPU-core scaling.
#ifdef AOC_HAS_OPENMP
#  include <omp.h>
#  define AOC_PARALLEL_FOR_ROWS _Pragma("omp parallel for schedule(static)")
#else
#  define AOC_PARALLEL_FOR_ROWS
#endif

namespace aoc::map {

// Hard upper bound on the plate count carried by the simulation.
// HexGrid::setPlateId stores a uint8_t with 255 reserved as the
// "unowned" sentinel, so any pid >= 255 cannot be persisted and would
// be silently dropped. Plate growth is currently bounded only by the
// Wilson-rifting cadence (at most one split per epoch, 60 epochs,
// ~13 initial plates -> worst case well under 255); this constant
// documents the contract until raster docking wires it in as an
// explicit Wilson spawn gate.
inline constexpr std::size_t MAX_PLATE_CAP = 32;
static_assert(MAX_PLATE_CAP < 255,
              "plate count must fit in 0..254 with 255 as sentinel");

// Noise utilities live in src/map/gen/Noise.{hpp,cpp}. The MapGenerator
// member declarations stay in the header for callers that already use them
// via class scope; they delegate to the gen:: free functions. File-local
// hashNoise / smoothstep / lerp aliases reproduce the historical un-namespaced
// helpers used pervasively throughout the rest of this file.
using gen::hashNoise;
using gen::smoothstep;

float MapGenerator::noise2D(float x, float y, float frequency, aoc::Random& rng) {
    return gen::noise2D(x, y, frequency, rng);
}

float MapGenerator::fractalNoise(float x, float y, int octaves, float frequency,
                                  float persistence, aoc::Random& rng) {
    return gen::fractalNoise(x, y, octaves, frequency, persistence, rng);
}

// ============================================================================
// Generation steps
// ============================================================================

void MapGenerator::generate(const Config& config, HexGrid& outGrid) {
    outGrid.initialize(config.width, config.height, config.topology);

    aoc::Random rng(config.seed);

    // Coarse-grained per-stage timing for profiling. Logs at DEBUG.
    using PerfClock = std::chrono::steady_clock;
    const auto t0 = PerfClock::now();
    auto logStage = [&t0]([[maybe_unused]] const char* name) {
        [[maybe_unused]] const auto now = PerfClock::now();
        [[maybe_unused]] const auto ms = std::chrono::duration_cast<
            std::chrono::milliseconds>(now - t0).count();
        LOG_DEBUG("[mapgen] %lld ms total — stage: %s",
            static_cast<long long>(ms), name);
    };

    // 2026-05-03: LandWithSeas removed. Only Continents path remains; it
    // runs the standard tectonic-plate pipeline (assignTerrain → coastline
    // smoothing → features → rivers → wonders).
    assignTerrain(config, outGrid, rng);
    logStage("assign-terrain");
    smoothCoastlines(outGrid);
    logStage("smooth-coastlines");
    assignFeatures(config, outGrid, rng);
    logStage("assign-features");
    generateRivers(outGrid, rng);
    logStage("generate-rivers");
    placeNaturalWonders(outGrid, rng);
    logStage("natural-wonders");

    // SEDIMENT / ALLUVIAL PLAINS. Real rivers deposit sediment along
    // their course and at their mouths, creating flat plains in the
    // middle of otherwise hilly terrain (Mississippi delta, Po Plain,
    // Ganges Plain, Pampas). After rivers are generated, find any
    // land tile within 2 hexes of a river and convert any Hills
    // feature back to None — fluvial sediment buries the relief and
    // the land flattens. Mountain tiles unaffected (rivers cut
    // through, but mountains themselves persist).
    {
        const int32_t total = outGrid.tileCount();
        const int32_t width = outGrid.width();
        std::vector<int8_t> nearRiver(static_cast<std::size_t>(total), 0);
        for (int32_t i = 0; i < total; ++i) {
            if (outGrid.riverEdges(i) != 0) {
                nearRiver[static_cast<std::size_t>(i)] = 1;
            }
        }
        // Two-hop dilation.
        for (int32_t pass = 0; pass < 2; ++pass) {
            std::vector<int8_t> next = nearRiver;
            for (int32_t i = 0; i < total; ++i) {
                if (nearRiver[static_cast<std::size_t>(i)]) { continue; }
                const hex::AxialCoord ax =
                    hex::offsetToAxial({i % width, i / width});
                for (const hex::AxialCoord& n : hex::neighbors(ax)) {
                    if (!outGrid.isValid(n)) { continue; }
                    if (nearRiver[static_cast<std::size_t>(outGrid.toIndex(n))]) {
                        next[static_cast<std::size_t>(i)] = 1;
                        break;
                    }
                }
            }
            nearRiver.swap(next);
        }
        for (int32_t i = 0; i < total; ++i) {
            if (!nearRiver[static_cast<std::size_t>(i)]) { continue; }
            const TerrainType t = outGrid.terrain(i);
            if (t == TerrainType::Mountain) { continue; }
            if (isWater(t)) { continue; }
            // Sediment: smooth Hills to None, flatten elevation tier.
            if (outGrid.feature(i) == FeatureType::Hills) {
                outGrid.setFeature(i, FeatureType::None);
            }
            outGrid.setElevation(i, 0);
        }
    }

    // Resource placement policy is orthogonal to terrain style.  Realistic
    // uses geology/basic rules keyed off mapType.  Random overrides with a
    // uniform per-tile chance.  Fair runs the realistic pass then redistributes
    // surplus to quadrants that ended up resource-starved.
    // 2026-05-02: geology pass now runs for every map type, not just
    // LandWithSeas. Continent / Pangaea / Fractal / Realistic etc. were
    // falling back to placeBasicResources() which never seeded any of the
    // 16 Session-8 specialty goods (NICKEL, COBALT, HELIUM, PLATINUM,
    // SULFUR, GYPSUM, FLUORITE, DOLOMITE, BARITE, ALLUVIAL_GOLD,
    // BEACH_PLACER, PYRITE, PHOSPHATE, VMS_ORE, SKARN_ORE, MVT_ORE),
    // making the matching production recipes unreachable on those maps.
    switch (config.placement) {
        case ResourcePlacementMode::Random:
            placeRandomResources(config, outGrid, rng);
            break;
        case ResourcePlacementMode::Fair:
            placeGeologyResources(config, outGrid, rng);
            balanceResourcesFair(config, outGrid, rng);
            break;
        case ResourcePlacementMode::Realistic:
        default:
            placeGeologyResources(config, outGrid, rng);
            break;
    }
    logStage("resource-placement-DONE");

    // Natural fish spots: seed FISH on ShallowWater tiles.
    // Coast terrain is no longer generated; all shallow water is ShallowWater.
    {
        aoc::Random fishRng(config.seed ^ 0x46495348u);  // "FISH"
        const int32_t tiles = outGrid.tileCount();
        for (int32_t i = 0; i < tiles; ++i) {
            const TerrainType t = outGrid.terrain(i);
            if (t != TerrainType::ShallowWater) {
                continue;
            }
            if (outGrid.resource(i).isValid()) { continue; }
            constexpr float p = 0.04f;
            if (fishRng.chance(p)) {
                outGrid.setResource(i, ResourceId{aoc::sim::goods::FISH});
                outGrid.setReserves(i, aoc::sim::defaultReserves(aoc::sim::goods::FISH));
            }
        }
    }

    // Detect strategic chokepoints after all terrain is finalized
    aoc::sim::detectChokepoints(outGrid);
}

void MapGenerator::assignTerrain(const Config& config, HexGrid& grid, aoc::Random& rng) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    // Cylindrical X wrap toggles the seam-handling logic in the passes
    // that still reason in projected map space (hotspot drift, post-sim
    // neighbour walks) so the east/west edge doesn't appear as a
    // straight cut through plates.
    const bool cylSim = (config.topology == MapTopology::Cylindrical);

    // Generate elevation map using fractal noise
    std::vector<float> elevationMap(static_cast<std::size_t>(grid.tileCount()));

    // Use a copy of rng to generate consistent noise seeds per octave
    aoc::Random noiseRng(rng.next());

    // Compute continent centers for multi-center map types
    // Seed a local RNG for center placement so it's deterministic
    aoc::Random centerRng(noiseRng.next());

    struct LandCenter {
        float cx;
        float cy;
        float strength;
    };

    std::vector<LandCenter> landCenters;

    // Tectonic-plate seeds for the Continents map type. Each seed owns
    // a Voronoi cell and is flagged ocean or land. Plate-based generation
    // (vs the older additive falloff) produces sharp coastline gradients
    // and unambiguous deep-water gaps, so 4 continents at random positions
    // never re-merge into a single landmass via the falloff sum.
    // Plate struct moved to include/aoc/map/gen/Plate.hpp on 2026-05-03 so
    // extracted post-sim / elevation passes can construct it directly.
    using Plate = aoc::map::gen::Plate;
    std::vector<Plate> plates;
    struct Hotspot {
        float cx;
        float cy;
        float strength;
    };
    std::vector<Hotspot> hotspots;

    // Sea level is not a knob: it is solved per epoch from a conserved
    // ocean-water volume against the evolving hypsometry
    // (solveSeaLevelFixedVolume). The climate phase perturbs the WATER
    // BUDGET physically -- icehouse locks ~ -120 m of stand in ice
    // sheets, greenhouse melts them / thermally expands the column for
    // ~ +100 m (Cretaceous/Pleistocene envelope). Stand-to-volume via
    // the ~0.708 ocean-area fraction. The old effectiveWaterRatio
    // plumbing this replaces was computed and then discarded by
    // Thresholds -- the greenhouse/icehouse UI knob was a silent
    // no-op. config.seaLevelDelta remains the creative slider and
    // still shifts the final cut in Thresholds.
    float eustaticStandM = 0.0f;
    if (config.climatePhase == 1)      { eustaticStandM = +100.0f; }
    else if (config.climatePhase == 2) { eustaticStandM = -120.0f; }

    switch (config.mapType) {
        case MapType::Continents: {
            // Plate-tectonic continent layout. Plate seeds placed here
            // parameterise rigid motion (Euler pole + angular velocity)
            // only; raster ownership comes from stochastic region
            // growing (generateInitialPlateOwnership — NO Voronoi), and
            // continental crust from the independent craton BFS below.
            // Land/water emerges from 3 Gy of raster physics
            // (SphereFieldPhysics), not from these seeds' positions.
            //
            // Pass 1: 3-4 LAND plate seeds, rejection-sampled with a
            // large minimum gap (0.42) so the land cells can't share a
            // boundary directly.
            // Pass 2: 5-7 OCEAN plate seeds, fill in the gaps with a
            // smaller minimum gap (0.13) from ANY existing seed. These
            // sit between the land seeds and become the connective
            // ocean tissue separating continents.
            // Earth has ~7 major tectonic plates (Eurasian, African,
            // North/South American, Pacific, Antarctic, Indo-Australian)
            // plus ~8 minor ones. Default 7 gives realistic continent
            // sizing; user can push to 14 via setup screen.
            // 2026-05-04: ocean plate count dropped 7-10 -> 3-4 to match
            // Earth's actual distribution: ~7 major plates total of
            // which only ~2-3 are mostly oceanic (Pacific, Nazca,
            // Cocos), and ONE of those (Pacific) covers ~30 % of the
            // surface alone. Old 7-10 ocean plates fragmented the
            // ocean into many similar-sized cells; combined with rift
            // children + microplates the total plate count hit 20+
            // and visible plate territories were all small/uniform
            // rather than the giant-Pacific + a-few-medium pattern of
            // real Earth.
            // Initial plate population matches Müller 2022 modern-Earth
            // major-plate count (~7-15 active throughout Phanerozoic).
            // Spawning more than this floods the world with boundary
            // cells; ridge-accretion and subduction passes then erode
            // continental crust faster than docking can preserve it.
            // Wilson-cycle dynamics expand and contract the count
            // naturally over the 3-Gy default sim, so init does not
            // need to over-seed.
            const int32_t landCountTarget = (config.landPlateCount > 0)
                ? std::max(1, config.landPlateCount)
                : centerRng.nextInt(5, 8);
            const int32_t oceanCountTarget = centerRng.nextInt(4, 7);
            const float LAND_MIN_GAP  = std::max(0.18f,
                0.70f / static_cast<float>(landCountTarget + 1));
            constexpr float OCEAN_MIN_GAP = 0.09f;

            const auto pushPlate = [&](float cx, float cy, bool isLand) {
                Plate p;
                p.cx = cx;
                p.cy = cy;
                // 2026-05-05: SPHERE MIGRATION - derive lat/lon from
                // legacy (cx, cy) via Mollweide inverse. Calls that
                // pass (cx, cy) outside the Mollweide ellipse get
                // clamped: lat from y assuming valid ellipse interior,
                // lon scaled by ellipse fill ratio. Plates initialised
                // here will have authoritative lat/lon for sphere
                // motion in Phase 2; legacy cx/cy retained until full
                // Phase 6 cleanup.
                {
                    aoc::map::gen::MollweideInverseResult mw =
                        aoc::map::gen::mollweideInverse(cx, cy);
                    if (!mw.valid) {
                        // Out of ellipse: nearest valid (lat, lon) is
                        // along the ellipse boundary closest to (cx,cy).
                        // Approximate by clamping y -> sphere bounds.
                        const float yClip = std::clamp(cy, 0.02f, 0.98f);
                        const float xClip = std::clamp(cx, 0.02f, 0.98f);
                        mw = aoc::map::gen::mollweideInverse(xClip, yClip);
                        if (!mw.valid) {
                            mw.coord = {0.0f, 0.0f};
                        }
                    }
                    p.latDeg = mw.coord.latDeg;
                    p.lonDeg = mw.coord.lonDeg;
                }
                // Random Euler pole on sphere + log-normal angular
                // velocity. Distribution parameters derive from the
                // Müller 2022 1000-Ma reconstruction filtered to
                // major plates (README findings: median 0.1 deg/Ma,
                // p95 1.0 deg/Ma) which gives μ=-2.30, σ=1.40 in
                // ln(deg/Ma) — see `data/plate_statistics.csv` and
                // `tools/plate_data/extract_statistics.py`. Box-Muller
                // for Gaussian sampling so a single deterministic RNG
                // call drives the whole draw. Continental plates are
                // shifted slightly slower (Δμ = -0.4) per HS3-NUVEL-1A
                // observation that cratonic plates resist mantle drag
                // more than oceanic ones (Gripp & Gordon 2002 Pacific
                // 0.96 vs Eurasian 0.14).
                {
                    // Clamp Euler-pole latitude to |lat| <= 60 deg.
                    // Real Earth Euler poles cluster between -60 and
                    // +60 latitude (Gripp & Gordon 2002 plate-motion
                    // catalogue: only the Cocos pole at 36.8 N gets
                    // close to the limit; most are < 60). Poles
                    // closer to a sphere pole produce near-axial
                    // rotation that visually smears continental crust
                    // into concentric circles when rendered on a 3D
                    // globe, even though the per-cell physics is
                    // correct -- it is the lat/lon raster's polar
                    // singularity that visualises a normal rigid
                    // rotation as a swirl. Clamping eliminates the
                    // visual artefact at the cost of a tiny exclusion
                    // zone near the geographic poles.
                    const float poleLat = centerRng.nextFloat(-60.0f, 60.0f);
                    const float poleLon = centerRng.nextFloat(-180.0f, 180.0f);
                    p.eulerPoleLatDeg = poleLat;
                    p.eulerPoleLonDeg = poleLon;
                    constexpr float MAJOR_MOTION_LN_MU    = -2.30f;
                    constexpr float MAJOR_MOTION_LN_SIGMA =  1.40f;
                    const float u1 = std::max(1e-6f,
                        centerRng.nextFloat(0.0f, 1.0f));
                    const float u2 = centerRng.nextFloat(0.0f, 1.0f);
                    const float gaussian = std::sqrt(-2.0f * std::log(u1))
                                          * std::cos(6.28318530718f * u2);
                    const float continentalShift = isLand ? -0.40f : 0.0f;
                    const float lnSpeed = MAJOR_MOTION_LN_MU
                                        + continentalShift
                                        + MAJOR_MOTION_LN_SIGMA * gaussian;
                    // Clamp to Mueller 2022 modern plate-motion catalogue
                    // GEOLOGICAL-TIMESCALE envelope: median plate ~0.1
                    // deg/My with sustainable upper bound at ~0.30 deg/My
                    // (twice the median). Modern Pacific 1.0 deg/My is
                    // a short-term figure that a plate cannot sustain
                    // across the 3-Gy default sim without generating
                    // unrealistic ~8-revolution sweeps. Lower bound
                    // 0.005 deg/My matches the slowest cratonic plates
                    // (Eurasian / Antarctic). Init clamp must agree
                    // with the slab-pull cap (MAX_ABS_OMEGA_DEG_PER_MY
                    // in SphereFieldPhysics.cpp = 0.15) so a plate that
                    // spawns near the init upper limit decays under
                    // slab-pull damping toward 0.15.
                    const float angVelMag =
                        std::clamp(std::exp(lnSpeed), 0.005f, 0.30f);
                    p.angularVelDeg = (centerRng.nextFloat(0.0f, 1.0f) < 0.5f)
                        ? -angVelMag : angVelMag;
                }
                // landFraction: continental plates are mostly land but
                // not entirely (Eurasian has plenty of seas); oceanic
                // plates have small islands and seamount chains. Adds
                // realism: every plate has SOME land and SOME ocean
                // intrinsic to it.
                // CRATONIC INIT. Initial continental plates are SMALL
                // STABLE CRATONS (0.45-0.65 land coverage) — Archean-
                // shield-like nuclei representing early continental
                // crust. Over the sim they GROW via:
                //   • Subduction-arc volcanism along their boundaries
                //     pushing tiles above water (Andes, Cordillera)
                //   • Terrane accretion at mergers (Cordilleran terranes)
                //   • Hotspot tracks adding volcanic islands
                //   • Orogeny lifting margin tiles above water level
                // Net effect over 100+ epochs: cratons → full continents
                // (matches Earth's progression from Archean nuclei to
                // present continents through ~2.5 Ga of accretion).
                // 2026-05-04: dropped from 0.85-0.95 to 0.35-0.55. Real
                // tectonic plates are mostly OCEAN even when they carry a
                // continent: African plate is ~32% land, Indian ~36%,
                // Eurasian ~40%, North American ~45%, Pacific ~0%. The
                // old 0.85-0.95 range made every "land" plate read as a
                // near-continuous continent, so adjacent land plates
                // formed a Pangaea even when their centroids drifted
                // apart. With 0.35-0.55 each plate carries a continent-
                // sized landmass surrounded by intra-plate ocean, and
                // adjacent land plates produce two SEPARATE continents
                // separated by ocean rather than a fused supercontinent.
                // 2026-05-04: ocean-plate landFraction dropped 0.02-0.08
                // -> 0.005-0.02. With 5-7% land randomly scattered,
                // ocean plates produced isolated land tiles that
                // bordered neighbouring continental land tiles and
                // counted toward "internal plates inside one
                // continent" -- the largest visible continent showed
                // 8-12 plate ids contributing land. Real Pacific plate
                // is ~99 % oceanic; volcanic islands are rare and
                // distant. With 0.5-2 % crust mask above threshold,
                // ocean plates contribute essentially zero stray land
                // tiles to neighbouring continents.
                p.landFraction = isLand
                    ? centerRng.nextFloat(0.35f, 0.55f)
                    : centerRng.nextFloat(0.005f, 0.02f);
                // Initial crust age: continental plates start old (cratons
                // = Archean, billions of years), oceanic plates start
                // moderately aged (random 0-50 epochs equivalent).
                // Cratons get high age so their slab-pull contribution
                // is suppressed (they shouldn't subduct themselves).
                // Irregular-shape probability: 35 % of plates get one
                // extra Voronoi seed offset from the primary, giving
                // an L-shape / lobed / curved territory rather than
                // a clean Voronoi cell. Real Earth plates are highly
                // irregular due to accretion + rifting history.
                // 60 % of plates get 1 extra seed, 25 % of those get
                // a SECOND extra seed → multi-lobed territory.
                // Real Earth plates almost never have clean Voronoi
                // shapes; they have arms and bays from accretion +
                // partial mergers.
                // 2026-05-04: dropped extras 3-6 -> 0-2, offset 0.10-0.22
                // -> 0.04-0.10. With 3-6 extra seeds spread 0.10-0.22
                // away from primary, plate territories became multi-
                // lobed AND the lobes interleaved with neighbouring
                // plates' lobes -- one plate ended up surrounded by
                // (or surrounding) another, producing the "weirdly
                // shaped plates embedded inside a bigger plate" pattern
                plates.push_back(p);
            };

            // ---- Pass 1: land seeds, stratified by latitude band ----
            // Earth has continents distributed across latitudes
            // (Eurasia mid-N, Africa straddles equator, Australia mid-S,
            // Antarctica polar). Random uniform placement clustered all
            // land at one Y-band on small samples. Stratified sampling
            // forces at least one land plate per Y-band so the layout
            // reads as a globe with multiple latitudinal continents.
            // Up to landCountTarget bands — each band seeds one plate.
            // Cap at landCountTarget so extra plates fill via fallback.
            // Cylindrical maps wrap on X — placement uses the full [0,1)
            // range and proximity tests use the wrapped (shortest) dx.
            // Flat maps keep an interior buffer so seeds don't sit on
            // the rectangle edge.
            const bool cylPlace = (config.topology == MapTopology::Cylindrical);
            const float xLo = cylPlace ? 0.0f : 0.10f;
            const float xHi = cylPlace ? 1.0f : 0.90f;
            const float xLoOcn = cylPlace ? 0.0f : 0.04f;
            const float xHiOcn = cylPlace ? 1.0f : 0.96f;
            const auto wrapDx = [cylPlace](float a, float b) {
                float d = a - b;
                if (cylPlace) {
                    if (d >  0.5f) { d -= 1.0f; }
                    if (d < -0.5f) { d += 1.0f; }
                }
                return d;
            };

            int32_t landPlaced = 0;
            int32_t attempts = 0;
            // 2026-05-08: stratified-by-latitude placement removed.
            // Splitting the land-lat range into N bands and forcing
            // exactly one plate per band produced visible horizontal
            // plate-id stripes after region growing -- each plate's
            // BFS reach was already lat-skewed by its seed band, so
            // the final ownership map looked like a layer cake.
            // Uniform random sampling with rejection on minimum gap
            // produces clustered + isolated continents alike, matching
            // Earth's irregular plate distribution.
            constexpr float LAND_LAT_LO = 0.22f;
            constexpr float LAND_LAT_HI = 0.78f;
            while (landPlaced < landCountTarget && attempts < 800) {
                ++attempts;
                const float cx = centerRng.nextFloat(xLo, xHi);
                const float cy = centerRng.nextFloat(LAND_LAT_LO, LAND_LAT_HI);
                bool tooClose = false;
                for (const Plate& existing : plates) {
                    const float dx = wrapDx(cx, existing.cx);
                    const float dy = cy - existing.cy;
                    if (std::sqrt(dx * dx + dy * dy) < LAND_MIN_GAP) {
                        tooClose = true; break;
                    }
                }
                if (tooClose) { continue; }
                pushPlate(cx, cy, true);
                ++landPlaced;
            }

            // ---- Pass 2: ocean seeds, filling gaps ----
            // Force-place ocean seeds in the polar bands first
            // (above LAND_LAT_HI and below LAND_LAT_LO). Without
            // these, the only nearby plates to a top-band land plate
            // are also land → continent extends to the map edge
            // (no Australia-like isolated continents possible).
            // 3-4 forced polar ocean seeds per band caps land plates
            // and produces clear "northern ocean" and "southern ocean"
            // (Arctic / Southern Ocean style).
            attempts = 0;
            int32_t oceanPlaced = 0;
            // Two polar oceanic plates (Arctic + Antarctic analogues).
            // Routed through pushPlate so they get authoritative
            // (latDeg, lonDeg) from Mollweide inverse and an Euler
            // pole / angular velocity drawn from the standard
            // distribution. Without this, polar plates default to
            // (lat=0, lon=0) and collide at the prime-meridian
            // equator — both want the same seed cell, only one
            // claims, the other dies silently in
            // generateInitialPlateOwnership.
            pushPlate(0.5f, centerRng.nextFloat(0.03f, 0.10f), false);  // Arctic
            pushPlate(0.5f, centerRng.nextFloat(0.90f, 0.97f), false);  // Antarctic
            oceanPlaced += 2;
            // Then fill remaining ocean seeds across the map (interior
            // gaps between continents).
            while (oceanPlaced < oceanCountTarget && attempts < 800) {
                ++attempts;
                const float cx = centerRng.nextFloat(xLoOcn, xHiOcn);
                const float cy = centerRng.nextFloat(0.04f, 0.96f);
                bool tooClose = false;
                for (const Plate& existing : plates) {
                    const float dx = wrapDx(cx, existing.cx);
                    const float dy = cy - existing.cy;
                    if (std::sqrt(dx * dx + dy * dy) < OCEAN_MIN_GAP) {
                        tooClose = true;
                        break;
                    }
                }
                if (tooClose) { continue; }
                pushPlate(cx, cy, false);
                ++oceanPlaced;
            }
            // Hotspots: 5-8 mantle-plume volcanic island chains placed
            // INSIDE ocean territory (not near continents, where they
            // used to create ringed-crater artefacts). Small radius +
            // low strength so they produce Hawaii / Iceland scale
            // island chains, not full continents. Stored in a separate
            // vector so the overlay can render their positions in dark
            // red and the user can correlate volcanic islands to
            // mantle activity.
            {
                // 5-8 was 5-11 % of total land per audit; Earth has
                // ~10 currently-active mantle plumes but only 2-3 of
                // them produce sub-aerial islands (Hawaii, Iceland,
                // Galapagos). Drop count to 2-4 so hotspot land
                // budget falls under 2 % of total.
                const int32_t hotspotTarget = centerRng.nextInt(2, 4);
                int32_t placed = 0;
                int32_t htAttempts = 0;
                while (placed < hotspotTarget && htAttempts < 300) {
                    ++htAttempts;
                    const float hcx = centerRng.nextFloat(0.05f, 0.95f);
                    const float hcy = centerRng.nextFloat(0.10f, 0.90f);
                    // Reject if too close to a LAND plate centre — keep
                    // hotspots in the deep ocean where they belong.
                    bool nearLand = false;
                    for (const Plate& p : plates) {
                        if (p.landFraction <= 0.40f) { continue; }
                        const float dxh = hcx - p.cx;
                        const float dyh = hcy - p.cy;
                        if (std::sqrt(dxh * dxh + dyh * dyh) < 0.20f) {
                            nearLand = true; break;
                        }
                    }
                    if (nearLand) { continue; }
                    Hotspot h;
                    h.cx = hcx; h.cy = hcy;
                    h.strength = centerRng.nextFloat(0.08f, 0.16f);
                    hotspots.push_back(h);
                    ++placed;
                }
                LOG_INFO("Hotspots placed: %d (target %d)",
                         placed, hotspotTarget);
            }
            break;
        }
        // (continents tectonic-sim runs after the switch — see below)
    }

    // ========================================================================
    // Tectonic-sim phase (Continents only). Runs the plates through a
    // multi-epoch motion + collision + rifting cycle, accumulating an
    // orogeny field per tile that captures cumulative uplift from
    // convergent boundaries. The final elevation pass below adds this
    // field on top of the base Voronoi heights, giving mountain ranges
    // ONLY where actual subduction stress accumulated — passive
    // (divergent / no-stress) coasts stay flat. Light box-blur erosion
    // afterward smooths peaks into ranges instead of solitary spikes.
    // ========================================================================
    std::vector<float> orogeny(static_cast<std::size_t>(width * height), 0.0f);
    // Per-tile auxiliary fields populated during/after the tectonic sim.
    // Size matches the tile grid so they can be written by world-frame
    // post-passes (post-sim sediment, rock type, margin type, crust age).
    std::vector<float>   sediment(static_cast<std::size_t>(width * height), 0.0f);
    std::vector<uint8_t> rockTypeTile(static_cast<std::size_t>(width * height), 0); // 0=sed
    std::vector<uint8_t> marginTypeTile(static_cast<std::size_t>(width * height), 0); // 0=interior
    std::vector<uint8_t> ophioliteMask(static_cast<std::size_t>(width * height), 0); // suture marks
    std::vector<float>   crustAgeTile(static_cast<std::size_t>(width * height), 0.0f);
    if (config.mapType == MapType::Continents && !plates.empty()) {
        // Multi-cycle plate-tectonic sim. EPOCHS scales the simulated
        // geological age — more epochs = more cycles of drift, collide,
        // rift, drift-back. Earth's history has ~4 supercontinent cycles
        // (Rodinia → Pannotia → Pangaea + present + projected); we
        // approximate by triggering a global-rift event every CYCLE
        // epochs which scatters land plates outward. Erosion intensity
        // scales with EPOCHS so older simulated worlds have softer
        // mountains.
        // Resolve total simulated time in millions of years, then
        // derive the epoch count. Caller may set tectonicTotalMy
        // (preferred) or tectonicEpochs (legacy direct override).
        const int32_t totalMy = (config.tectonicTotalMy > 0)
            ? config.tectonicTotalMy
            : MapGenerator::DEFAULT_TECTONIC_TOTAL_MY;
        int32_t epochsFromTime = std::max(3, static_cast<int32_t>(
            (totalMy + MapGenerator::MY_PER_EPOCH_TARGET / 2)
            / MapGenerator::MY_PER_EPOCH_TARGET));
        const int32_t requestedEpochs = (config.tectonicEpochs > 0)
            ? std::max(3, config.tectonicEpochs)
            : epochsFromTime;
        // Stepper hook: scrubber callers pass runEpochsLimit to halt
        // the sim mid-flight at a specific epoch and view that state.
        const int32_t EPOCHS = (config.runEpochsLimit > 0)
            ? std::min(requestedEpochs, config.runEpochsLimit)
            : requestedEpochs;
        // SphereField allocation is unconditional; the world-frame elevation pass downstream
        // sources tile elevation from this raster via bilinearSample.
        // Procedural cratonic seeding (CLAUDE.md rule 2: never load
        // craton polygons from a dataset). Place 5-9 small Archean
        // continental nuclei at random sphere positions with a
        // minimum angular separation, expand each via stochastic BFS
        // to an absolute log-normal area drawn per nucleus. Total
        // initial continental fraction ~5 % of the sphere — matches
        // mid-Archean Earth (~3.5 Ga, Cawood et al. 2013, Belousova
        // et al. 2010 detrital-zircon record). The remaining ~25
        // percentage points needed to reach modern Earth's 29 %
        // continental area must EMERGE from per-epoch arc volcanism
        // (`thickenFromClosingRate`) over the 3-Gy run; quota-based
        // shapers (CLAUDE.md rule 3) are not allowed to fill the gap.
        // Stochastic BFS in random frontier order produces non-
        // convex shapes with embayments and peninsulas — the
        // Lautenschlager & Wraight 2013 cellular-automaton craton
        // seeding pattern.
        aoc::map::gen::SphereField sphereField;
        sphereField.resize();
        // Water budget: Earth-mean 2607 m equivalent depth (3682 m
        // mean ocean depth x 70.8 % coverage, NOAA) perturbed by the
        // climate-phase stand offset converted to volume through the
        // ocean-area fraction.
        sphereField.oceanVolumeEquivDepthM =
            2607.0f + eustaticStandM * 0.708f;
        std::vector<uint8_t> sphereBoundaryScratch;
        // Wilson-rifting RNG: deterministic per map seed so the same
        // seed always reproduces the same supercontinent breakup
        // sequence.
        uint32_t physicsRngState = static_cast<uint32_t>(config.seed) ^ 0xDEADBEEFu;
        if (physicsRngState == 0u) physicsRngState = 0x12345678u;
        {
            using SF = aoc::map::gen::SphereField;
            constexpr int32_t LON = SF::LON_CELLS;
            constexpr int32_t LAT = SF::LAT_CELLS;
            constexpr std::size_t N = SF::CELL_COUNT;
            // Independent RNG for cratonic seeding -- distinct from
            // physicsRngState so changes here do not perturb the
            // Wilson rifting cadence.
            aoc::Random cratonRng(config.seed ^ 0x43524154u); // "CRAT"

            // 2026-05-14: bumped from 5-8 -> 7-11 cratons. The lower bound
            // matters more than the upper for seeds with adverse plate
            // geometries: seeds 100 and 200 produced only 3-5 % land in the
            // 6-seed sweep at numCratons=5, because few cratons + small
            // log-normal samples + few convergent boundaries starved the
            // accreteToNeighbours diffusion pass of donor cells.
            // Real Earth has ~12 stable cratons (Pilbara, Yilgarn, Slave,
            // Kaapvaal, North Atlantic, Siberian, North China, Tarim,
            // Indian, São Francisco, Amazonian, West African; Cawood et
            // al. 2013, table 1). 7-11 brings the simulation in line.
            const int32_t numCratons = 7 + cratonRng.nextInt(0, 4);

            // Per-nucleus absolute area drawn from log-normal — no
            // global quota. 2026-07-05: median raised 0.7 % -> 1.8 %
            // of sphere. The old value targeted the ~5 % mid-Archean
            // (~3.5 Ga) baseline and hoped arc volcanism would grow
            // it to the modern 29 % — measured across every seed
            // sweep, that growth reliably undershot (final land
            // 13-29 %). The sim's own sources say most continental
            // crust already existed EARLY: ~60-70 % of today's volume
            // by ~2.5 Ga (Cawood et al. 2013; Belousova et al. 2010
            // detrital-zircon record), i.e. a ~15-20 % initial
            // areal stock is MORE faithful than 5 % + over-weighted
            // arc production. 7-11 nuclei x 1.8 % median ~ 15-22 %
            // initial coverage; arcs and accretion then add the
            // Phanerozoic tail.
            constexpr float NUCLEUS_MEDIAN_FRACTION = 0.018f;
            constexpr float NUCLEUS_LOG_SIGMA      = 0.6f;
            const float nucleusMedianCells =
                NUCLEUS_MEDIAN_FRACTION * static_cast<float>(N);
            std::vector<std::size_t> cratonTarget(
                static_cast<std::size_t>(numCratons), 0);
            for (int32_t i = 0; i < numCratons; ++i) {
                const float u1 = std::max(1e-6f,
                    cratonRng.nextFloat(0.0f, 1.0f));
                const float u2 = cratonRng.nextFloat(0.0f, 1.0f);
                const float gauss =
                    std::sqrt(-2.0f * std::log(u1)) *
                    std::cos(6.28318530718f * u2);
                const float scale = std::exp(NUCLEUS_LOG_SIGMA * gauss);
                const float cells = nucleusMedianCells * scale;
                // Clamp to [50, 9000] cells — protects against
                // pathological samples; upper bound ~3.5 % of sphere
                // (Eurasia-craton-cluster scale) so the raised median
                // is not truncated to a flat size distribution.
                const float clamped = std::clamp(cells, 50.0f, 9000.0f);
                cratonTarget[static_cast<std::size_t>(i)] =
                    static_cast<std::size_t>(clamped);
            }

            // Place craton seeds with minimum angular separation
            // (Lambertian uniform on sphere via cos-lat sampling so
            // tropical seeds are not over-represented).
            std::vector<int32_t> seedLon(static_cast<std::size_t>(numCratons));
            std::vector<int32_t> seedLat(static_cast<std::size_t>(numCratons));
            constexpr float MIN_SEP_RAD = 0.45f; // ~26 deg: prevents seeds clumping
            // Mid-latitude bias for craton seeds. Earth's continental
            // crust concentrates between roughly 20-70 degrees N and
            // 25-50 degrees S; only Antarctica sits over a geographic
            // pole. The visible-pole "swirl" smearing comes from
            // continental cells rotating about polar Euler axes
            // (plate Euler poles cluster at high latitudes, Gripp &
            // Gordon 2002), where many longitude lines converge to a
            // point and any continental rotation looks like a circle
            // around the pole. Restricting craton seeds to |lat| <= 65
            // makes the polar caps oceanic-by-default, matching real
            // Earth and removing the visual artefact entirely.
            constexpr float CRATON_LAT_LIMIT_SIN = 0.906f; // sin(65 deg)
            for (int32_t i = 0; i < numCratons; ++i) {
                // Two-tier rejection sampling: first 64 attempts use the
                // strict separation MIN_SEP_RAD. On failure (high craton
                // count + small sphere — the strict packing is
                // infeasible) emit a warning and retry once with a
                // relaxed 0.5x separation. The relaxed band still
                // prevents craton overlap while admitting denser
                // packings the original threshold would reject.
                bool ok = false;
                auto tryPlace = [&](float minSep) -> bool {
                    const float u = cratonRng.nextFloat(
                        -CRATON_LAT_LIMIT_SIN, CRATON_LAT_LIMIT_SIN);
                    const float latDeg = std::asin(u) * 57.29577951f;
                    const float lonDeg = cratonRng.nextFloat(-180.0f, 180.0f);
                    const SF::CellCoord c = SF::locate(latDeg, lonDeg);
                    for (int32_t j = 0; j < i; ++j) {
                        const aoc::map::gen::LatLon a = SF::cellCenter(
                            c.lonIdx, c.latIdx);
                        const aoc::map::gen::LatLon b = SF::cellCenter(
                            seedLon[static_cast<std::size_t>(j)],
                            seedLat[static_cast<std::size_t>(j)]);
                        const float d = aoc::map::gen::haversineRadians(a, b);
                        if (d < minSep) { return false; }
                    }
                    seedLon[static_cast<std::size_t>(i)] = c.lonIdx;
                    seedLat[static_cast<std::size_t>(i)] = c.latIdx;
                    return true;
                };
                for (int32_t attempt = 0; attempt < 64 && !ok; ++attempt) {
                    ok = tryPlace(MIN_SEP_RAD);
                }
                if (!ok) {
                    LOG_WARN("MapGenerator: craton seed %d failed strict "
                             "MIN_SEP_RAD=%.3f after 64 attempts -- "
                             "retrying with relaxed 0.5x separation",
                             i, static_cast<double>(MIN_SEP_RAD));
                    constexpr float RELAXED = 0.5f * MIN_SEP_RAD;
                    for (int32_t attempt = 0; attempt < 64 && !ok; ++attempt) {
                        ok = tryPlace(RELAXED);
                    }
                }
            }

            // Stochastic BFS expansion. Each craton grows by repeatedly
            // picking a random cell from its frontier and claiming it.
            // Random selection (vs FIFO) produces non-convex shapes with
            // peninsulas and bays.
            std::vector<int8_t> claimed(N, 0); // 1 = continental
            for (int32_t cidx = 0; cidx < numCratons; ++cidx) {
                const std::size_t target =
                    cratonTarget[static_cast<std::size_t>(cidx)];
                if (target == 0) continue;
                const std::size_t startIdx = SF::cellIndex(
                    seedLon[static_cast<std::size_t>(cidx)],
                    seedLat[static_cast<std::size_t>(cidx)]);
                if (claimed[startIdx]) continue; // overlap with prior craton
                claimed[startIdx] = 1;
                std::size_t grown = 1;
                std::vector<std::size_t> frontier;
                frontier.reserve(target * 2);
                auto pushNbrs = [&](int32_t lonI, int32_t latI) {
                    const int32_t lonW = (lonI == 0)       ? LON - 1 : lonI - 1;
                    const int32_t lonE = (lonI == LON - 1) ? 0       : lonI + 1;
                    const int32_t latS = std::max(0, latI - 1);
                    const int32_t latN = std::min(LAT - 1, latI + 1);
                    const std::size_t nbrs[4] = {
                        SF::cellIndex(lonW, latI),
                        SF::cellIndex(lonE, latI),
                        SF::cellIndex(lonI, latS),
                        SF::cellIndex(lonI, latN),
                    };
                    for (int32_t k = 0; k < 4; ++k) {
                        if (!claimed[nbrs[k]]) frontier.push_back(nbrs[k]);
                    }
                };
                pushNbrs(seedLon[static_cast<std::size_t>(cidx)],
                         seedLat[static_cast<std::size_t>(cidx)]);
                while (grown < target && !frontier.empty()) {
                    // Pick a random frontier cell. Swap-remove for O(1)
                    // deletion.
                    const std::size_t pick = static_cast<std::size_t>(
                        cratonRng.nextInt(0,
                            static_cast<int32_t>(frontier.size()) - 1));
                    const std::size_t cellIdx = frontier[pick];
                    frontier[pick] = frontier.back();
                    frontier.pop_back();
                    if (claimed[cellIdx]) continue;
                    claimed[cellIdx] = 1;
                    ++grown;
                    const int32_t cellLon =
                        static_cast<int32_t>(cellIdx % LON);
                    const int32_t cellLat =
                        static_cast<int32_t>(cellIdx / LON);
                    pushNbrs(cellLon, cellLat);
                }
            }

            // Claimed cells are continental crust; unclaimed are
            // oceanic. A 1-pass 4-neighbour smoothing softens the
            // 0->1 cliffs at coastlines so the SphereField bilinear
            // sampler does not produce single-cell shards downstream
            // (the same issue the Bird-falloff function was solving
            // with its 0.55 rad blend, recovered procedurally here).
            for (int32_t latIdx = 0; latIdx < LAT; ++latIdx) {
                for (int32_t lonIdx = 0; lonIdx < LON; ++lonIdx) {
                    const std::size_t idx = SF::cellIndex(lonIdx, latIdx);
                    const int32_t lonW = (lonIdx == 0)       ? LON - 1 : lonIdx - 1;
                    const int32_t lonE = (lonIdx == LON - 1) ? 0       : lonIdx + 1;
                    const int32_t latS = std::max(0, latIdx - 1);
                    const int32_t latN = std::min(LAT - 1, latIdx + 1);
                    const float self = static_cast<float>(claimed[idx]);
                    const float fW = static_cast<float>(claimed[
                        SF::cellIndex(lonW, latIdx)]);
                    const float fE = static_cast<float>(claimed[
                        SF::cellIndex(lonE, latIdx)]);
                    const float fS = static_cast<float>(claimed[
                        SF::cellIndex(lonIdx, latS)]);
                    const float fN = static_cast<float>(claimed[
                        SF::cellIndex(lonIdx, latN)]);
                    const float frac = (self * 2.0f + fW + fE + fS + fN) / 6.0f;
                    sphereField.continentalFraction[idx] = frac;
                    sphereField.crustThicknessKm[idx] =
                        frac  * aoc::map::gen::PhysicsConstants::initialContinentalThicknessKm
                      + (1.0f - frac) * aoc::map::gen::PhysicsConstants::initialOceanicThicknessKm;
                }
            }
        }
        // Procedural initial plate-ownership assignment via stochastic
        // region growing from cratonic seeds. NO Voronoi (per CLAUDE.md
        // "World-generation physics requirements"): cells claim plate
        // identity by path-dependent BFS expansion, producing
        // non-convex peninsulas + bays + lobed shapes. From this
        // initial cut onwards plateId persists; only mechanism passes
        // (subduction, ridge accretion, docking, rifting) rewrite it.
        aoc::map::gen::generateInitialPlateOwnership(
            sphereField, plates, static_cast<uint64_t>(config.seed));
        aoc::map::gen::recomputeIsostaticElevationOnRaster(sphereField);
        // Per-epoch substep duration in My. Derived from total simulated
        // time so the physics integrates at a fixed cadence regardless
        // of caller-requested epoch count.
        const float MY_PER_EPOCH_P1 = static_cast<float>(totalMy)
            / static_cast<float>(requestedEpochs);

        for (int32_t epoch = 0; epoch < EPOCHS; ++epoch) {
            // 2026-05-07 P6.10 recalibration: Stochastic Euler-pole jitter
            // for the 25-Myr-per-epoch tectonic timescale. Müller et al.
            // 2008 / Tetley et al. 2019 report ~10%/Myr fractional change
            // in plate-motion vectors at the Quaternary (sub-Myr) scale,
            // dominated by mantle micro-reorganisations; on the 25-Myr
            // tectonic-epoch scale relative to which our model integrates,
            // plate motions are far more stable (Pacific plate sustained
            // ~10 cm/yr across the 80-Myr Hawaiian-Emperor chain; Indian
            // plate sustained 5 cm/yr northward through the 50-Myr Tibet
            // collision). Use a long-term σ = 1%/Myr fractional change in
            // |ω| and 0.1°/Myr Euler-pole drift, scaled as √dt for
            // random-walk variance. Without this calibration mountains
            // never reach steady state because the rate at any boundary
            // resets faster than the K_EROSION decay constant
            // (1/K = 16.7 Myr).
            const float jitterScale = std::sqrt(std::max(1.0f, MY_PER_EPOCH_P1));
            const float velFracSigma = 0.01f * jitterScale;
            const float poleSigmaDeg = 0.10f * jitterScale;
            auto gaussianFromUniform = [&]() {
                // Sum of 3 U[-1,1] approximates N(0,1) (CLT, σ=1).
                return centerRng.nextFloat(-1.0f, 1.0f)
                     + centerRng.nextFloat(-1.0f, 1.0f)
                     + centerRng.nextFloat(-1.0f, 1.0f);
            };

            // P6.10 Euler-pole jitter: drift each plate's pole and
            // perturb |omega| before the raster physics integrates
            // motion this epoch. The legacy 2D centroid advance that
            // lived here was dead weight — plate lat/lon is overwritten
            // every epoch by recomputePlateCentroidsFromCells, and the
            // raster motion is integrated by advectPlateOwnership from
            // the Euler parameters this jitter perturbs.
            for (Plate& p : plates) {
                if (p.eulerPoleLatDeg != 0.0f
                    || p.eulerPoleLonDeg != 0.0f
                    || p.angularVelDeg != 0.0f) {
                    p.eulerPoleLatDeg = std::clamp(
                        p.eulerPoleLatDeg
                            + gaussianFromUniform() * poleSigmaDeg,
                        -89.0f, 89.0f);
                    p.eulerPoleLonDeg += gaussianFromUniform() * poleSigmaDeg;
                    while (p.eulerPoleLonDeg >  180.0f) p.eulerPoleLonDeg -= 360.0f;
                    while (p.eulerPoleLonDeg < -180.0f) p.eulerPoleLonDeg += 360.0f;
                    p.angularVelDeg *= 1.0f + gaussianFromUniform() * velFracSigma;
                }
            }

            // Plate-pair interactions for this epoch:
            //  - Very close pairs of land plates collide → fuse (drop
            //    the second, transfer its mass into the first).
            //  - Pairs of plates may rift apart at random with low
            //    probability per epoch, modelling a passive divergent
            //    boundary that opens a new ocean.
            // Stress integration handles the per-tile elevation effect
            // — these structural events only change which plates are
            // around in subsequent epochs.
            // Continental collision (fusion). Two land plates fuse when
            //   1. Centres are within MERGE_DIST_RAD on the sphere
            //      (haversine distance — replaces the legacy Euclidean
            //      Mollweide distance which was distorted at high
            //      latitude and across the antimeridian).
            //   2. Both plates carry > 40 % continental land fraction.
            //
            // CONTINENTAL COLLISION DYNAMICS (multi-stage):
            //   1. Approach — plates close from afar at full velocity.
            //   2. CONTACT: crust starts deforming. Mountain orogeny
            //      accumulates via the convergent-stress code below.
            //      Plates keep identities until SUTURING.
            //   3. SUTURING (d < MERGE_DIST): collision energy mostly
            //      dissipated, plates have welded along the boundary
            //      → fuse atomically via mergePlatesBatch.
            //
            // Real-world: India-Eurasia has been actively colliding for
            // ~50 My, building Himalayas. Plates are still moving
            // relative to each other (~5 cm/yr) — they aren't yet fully
            // merged. Our simulation mirrors this: long collision
            // phase, then merge.
            //
            // MERGE_DIST_RAD = 0.22 radians ≈ 12.6° (~1400 km on the
            // sphere). Calibrated to the legacy Mollweide threshold
            // (0.07 normalised units across a Mollweide span of
            // sqrt(2)/π = 0.45 width unit ≈ 12-13° at the equator
            // depending on latitude — the haversine equivalent at the
            // equator equals the Mollweide value times π/2 ≈ 1.57x
            // because Mollweide x compresses east-west by 2/π).
            constexpr float MERGE_DIST_RAD = 0.22f;
            std::vector<std::pair<std::size_t, std::size_t>> mergePairs;
            for (std::size_t a = 0; a < plates.size(); ++a) {
                if (plates[a].landFraction <= 0.40f) continue;
                for (std::size_t b = a + 1; b < plates.size(); ++b) {
                    if (plates[b].landFraction <= 0.40f) continue;
                    const float d = aoc::map::gen::haversineRadians(
                        aoc::map::gen::LatLon{plates[a].latDeg, plates[a].lonDeg},
                        aoc::map::gen::LatLon{plates[b].latDeg, plates[b].lonDeg});
                    if (d < MERGE_DIST_RAD) {
                        mergePairs.emplace_back(a, b);
                    }
                }
            }
            if (!mergePairs.empty()) {
                // Single deferred sweep across field.plateId — replaces
                // the previous N-pair sequential mergePlates loop where
                // each call performed its own full grid sweep.
                aoc::map::gen::mergePlatesBatch(
                    sphereField, plates, mergePairs);
                // Re-project Mollweide cache for survivors (physics
                // does not own the projection layer).
                for (Plate& p : plates) {
                    const aoc::map::gen::MollweidePoint mw =
                        aoc::map::gen::mollweideForward(
                            aoc::map::gen::LatLon{p.latDeg, p.lonDeg});
                    p.cx = mw.mapX;
                    p.cy = mw.mapY;
                }
            }
            // Hotspot drift. Real plumes drift ~1 cm/yr (Hawaiian-Emperor
            // bend at 47 Mya). Tiny rotation about map centre per epoch
            // curves trails subtly over long sims.
            for (Hotspot& h : hotspots) {
                constexpr float HS_DRIFT_RAD = 0.00040f;
                const float rx = h.cx - 0.5f;
                const float ry = h.cy - 0.5f;
                const float cw = std::cos(HS_DRIFT_RAD);
                const float sw = std::sin(HS_DRIFT_RAD);
                h.cx = 0.5f + rx * cw - ry * sw;
                h.cy = 0.5f + rx * sw + ry * cw;
                if (cylSim) {
                    if (h.cx < 0.0f) { h.cx += 1.0f; }
                    if (h.cx > 1.0f) { h.cx -= 1.0f; }
                } else {
                    h.cx = std::clamp(h.cx, 0.05f, 0.95f);
                }
                h.cy = std::clamp(h.cy, 0.05f, 0.95f);
            }

            // Raster physics epoch: advection, boundary classification,
            // thickening, arc growth, subduction, ridge accretion,
            // slab pull, Wilson rifting, isostasy, erosion.
            aoc::map::gen::stepSpherePhysicsEpoch(
                sphereField, plates, sphereBoundaryScratch,
                physicsRngState, MY_PER_EPOCH_P1);
        }
        // Hand the SphereField surface-elevation snapshot to the
        // HexGrid for the renderer/save path.
        grid.setSphereFieldElevationSnapshot(sphereField.surfaceElevationM);

        // Per-hex-tile orogeny lookup. NO Voronoi: tile (col, row) →
        // Mollweide-inverse → lat/lon → SphereField peakSample for
        // mountain detection (peak captures narrow Andean / Himalayan
        // belts that bilinear averaging would dilute below threshold)
        // and bilinearSample of continentalFraction for the
        // continental-only mountain gate. Tiles with ocean SphereField
        // mass are clamped to a low orogeny tier to prevent oceanic
        // cells with random elevation noise registering as mountains.
    // PERF: fused orogeny + elevation pass. Both passes iterate the same
    // (row, col) grid, are row-parallel, and recompute the identical
    // projectionInverse per cell; merging them computes that pure inverse
    // once and writes the two disjoint output arrays (orogeny, elevationMap)
    // in one sweep. orogeny is only written when the cell is in projection
    // range (matching the old `continue`); out-of-range cells keep their
    // pre-initialised 0.0f orogeny and get elevationMap = -1.0f exactly as
    // before. Results are bit-identical -- one of three projectionInverse
    // calls per cell is removed.
    //
    // World-frame elevation: tile (col, row) maps to lat/lon via the user-
    // selected projection; elevation comes from the SphereField
    // surfaceElevationM raster (authoritative state produced by 3 Gy of
    // mechanism physics — subduction trims, ridges accrete, continents dock,
    // Wilson cycles rift). Tiles outside the projection's valid range get a
    // deep ocean elevation so the rendering still draws them as water. Output
    // is a percentile-rank map: ClimateBiome.cpp picks ocean / shore / land
    // tiers via Thresholds.cpp on a sorted view of this array, so the
    // absolute scale only needs to be monotonic.
    // Seed for the sub-grid coastal detail field below. Fixed tag so
    // the field is stable per world seed and independent of every RNG
    // stream (pure hash noise -- no draws, no ordering sensitivity).
    const uint64_t coastSeed = aoc::map::gen::mixSeed(
        static_cast<uint64_t>(config.seed) ^ 0x434F4153ULL); // "COAS"
    AOC_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            // Half-cell offset: sample at the hex CENTRE, not its
            // north-west corner (col/width alone biases every sample
            // half a hex toward -x/-y).
            const float nx = (static_cast<float>(col) + 0.5f)
                / static_cast<float>(width);
            const float ny = (static_cast<float>(row) + 0.5f)
                / static_cast<float>(height);
            const aoc::map::gen::MollweideInverseResult mw =
                aoc::map::gen::projectionInverse(
                    config.projection, nx, ny);
            float elev = -1.0f; // Out of projection range → ocean.
            if (mw.valid) {
                // --- orogeny (mountain) sample ---
                // halfSearchCells calibrated to the hex tile's actual
                // footprint on the sphere. SphereField cell pitch is
                // 0.5 deg; hex tile width is 360/width deg. Half-window
                // = ceil(hex_half_width / cell_pitch) so peakSample
                // captures every SphereField cell whose centre falls
                // inside the hex. Without this, a 4 km+ peak that
                // straddles the hex edge is missed and the tile renders
                // as flat -- visible mountain ranges shrink to a few
                // scattered tiles even when the SphereField shows
                // hundreds of mountain cells. Default 80-wide map:
                // hex_half_width = 2.25 deg, halfSearchCells = 5.
                const int32_t hexHalfCells = std::max(3,
                    static_cast<int32_t>(std::ceil(
                        180.0f / static_cast<float>(width) / 0.5f)));
                const float zMpeak = sphereField.peakSample(
                    sphereField.surfaceElevationM,
                    mw.coord.latDeg, mw.coord.lonDeg,
                    hexHalfCells);
                // Mountain height is measured ABOVE SEA LEVEL, which
                // the physics solves per epoch from the fixed water
                // volume (SphereField::seaLevelM).
                float oroSampled = (zMpeak - sphereField.seaLevelM
                        > aoc::map::gen::MOUNTAIN_THRESHOLD_M)
                    ? 1.0f : 0.0f;
                const float fracPeak = sphereField.bilinearSample(
                    sphereField.continentalFraction,
                    mw.coord.latDeg, mw.coord.lonDeg);
                if (fracPeak < 0.5f && oroSampled > 0.10f) {
                    oroSampled = 0.10f;
                }
                orogeny[static_cast<std::size_t>(row * width + col)] = oroSampled;

                // --- elevation sample ---
                const float zM = sphereField.bilinearSample(
                    sphereField.surfaceElevationM,
                    mw.coord.latDeg, mw.coord.lonDeg);
                const float contFracHere = sphereField.bilinearSample(
                    sphereField.continentalFraction,
                    mw.coord.latDeg, mw.coord.lonDeg);
                // Map metres above mantle datum (zero = real sea level
                // by Airy isostasy + datum calibration in
                // PlatePhysics.hpp) to a unitless elevation. The
                // 5000 m scale puts oceanic basement at ~-0.54 and
                // continental highland at ~+0.56 -- the sign of the
                // unitless value matches the sign of the physical
                // value, so the binary water/land cut is simply
                // elev < 0. No contFrac lift, no percentile cutoff:
                // these were band-aids for a previous erosion law
                // that drove continental cells to z=0; the slope-
                // based stream-power erosion (SphereFieldPhysics
                // applySurfaceErosionOnRaster) preserves shields at
                // their cratonic +500-800 m steady state, so the
                // physical sea-level cut suffices.
                // Unitless elevation relative to the SOLVED sea level
                // (zero = shoreline by construction; the fixed-volume
                // solve anchors land fraction physically instead of
                // hoping the Airy datum lands at the right stand).
                const float zRelM = zM - sphereField.seaLevelM;
                elev = zRelM / 5000.0f;
                // Sub-grid coastal detail. The 0.5-deg raster cannot
                // represent coastline relief below ~55 km and bilinear
                // sampling to hex pitch low-passes even that, so coasts
                // come out as smooth arcs; real coastlines are fractal
                // down to metre scale (D ~ 1.1-1.3, Mandelbrot 1967).
                // Reconstruct the unresolved relief with deterministic
                // value noise confined to the raster's own vertical
                // ambiguity band (within 400 m of sea level) and to
                // crust carrying any continental signal. This is a
                // stationary seed-derived field -- it fits no ratio
                // target and decays to zero away from the coast band.
                // Sampled on the unit sphere: no antimeridian seam.
                const float coastBand = 1.0f
                    - std::min(1.0f, std::abs(zRelM) / 400.0f);
                if (coastBand > 0.0f && contFracHere >= 0.05f) {
                    const float latR = mw.coord.latDeg * 0.01745329252f;
                    const float lonR = mw.coord.lonDeg * 0.01745329252f;
                    const float px = std::cos(latR) * std::cos(lonR);
                    const float py = std::cos(latR) * std::sin(lonR);
                    const float pz = std::sin(latR);
                    // 4 octaves from ~10 deg base wavelength (above the
                    // ~2.6-deg hex pitch at Standard width, so detail
                    // reads as bays/headlands rather than speckle) down
                    // to ~1.25 deg, amplitude halving per octave.
                    float detail = 0.0f;
                    float amp = 1.0f;
                    float freq = 5.73f; // 1 / (10 deg in radians)
                    float norm = 0.0f;
                    for (int32_t o = 0; o < 4; ++o) {
                        detail += amp * (2.0f * aoc::map::gen::smoothHashNoise3(
                            px * freq, py * freq, pz * freq,
                            coastSeed + static_cast<uint64_t>(o)) - 1.0f);
                        norm += amp;
                        amp *= 0.5f;
                        freq *= 2.0f;
                    }
                    // +-0.08 unitless = +-400 m at full window: only
                    // tiles the raster itself puts near sea level can
                    // flip, i.e. a 1-2 hex coastal ribbon.
                    elev += (detail / norm) * 0.08f
                        * aoc::map::gen::smoothstep(coastBand);
                }
            }
            // Hotspot volcanic islands. Each hotspot is a mantle
            // plume that builds a Hawaiian/Icelandic-scale island
            // in deep ocean. Distance test runs in lat/lon space so
            // island size is independent of hex grid resolution.
            if (mw.valid) {
                for (const Hotspot& h : hotspots) {
                    const aoc::map::gen::MollweideInverseResult hmw =
                        aoc::map::gen::projectionInverse(
                            config.projection, h.cx, h.cy);
                    if (!hmw.valid) continue;
                    float dLat = mw.coord.latDeg - hmw.coord.latDeg;
                    float dLon = mw.coord.lonDeg - hmw.coord.lonDeg;
                    if (dLon >  180.0f) dLon -= 360.0f;
                    if (dLon < -180.0f) dLon += 360.0f;
                    const float cosLat = std::cos(
                        mw.coord.latDeg * 3.14159265f / 180.0f);
                    dLon *= cosLat;
                    const float r2deg = dLat * dLat + dLon * dLon;
                    constexpr float HOTSPOT_RADIUS_DEG = 1.5f;
                    constexpr float HOTSPOT_R2_LIMIT =
                        HOTSPOT_RADIUS_DEG * HOTSPOT_RADIUS_DEG;
                    if (r2deg < HOTSPOT_R2_LIMIT) {
                        const float sigma2 = HOTSPOT_R2_LIMIT * 0.16f;
                        const float falloff = std::exp(-r2deg / sigma2);
                        // Was 0.65 + strength*2.0 = up to 0.97, which
                        // lifted ~10 cells per hotspot above the
                        // percentile water threshold. Drop to 0.40 +
                        // strength*1.0 = up to 0.56 so only the centre
                        // cell crests; falloff puts the next ring at
                        // ~0.34, below threshold. Result: ~1-2 land
                        // cells per hotspot, Hawaii-Big-Island scale.
                        elev += (0.40f + h.strength * 1.0f) * falloff;
                    }
                }
            }
            elevationMap[static_cast<std::size_t>(row * width + col)] = elev;
        }
    }

    // Per-hex-tile plate id, projected from the SphereField raster
    // through the user-selected projection. `SphereField::locate`
    // returns the authoritative raster cell.
    //
    // DEBT(perf, WP-11): this third full-grid pass recomputes the same
    // projectionInverse per cell as the fused orogeny/elevation pass above.
    // It is NOT fused in because it is intentionally serial -- grid.setPlateId
    // lazy-allocates m_plateId on its first call (HexGrid.hpp), which is not
    // thread-safe, so folding it into the row-parallel loop above would race.
    // Fusing safely needs the m_plateId buffer pre-allocated before the loop;
    // deferred to keep this WP's diff minimal and behaviour-identical.
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const float nx = (static_cast<float>(col) + 0.5f)
                / static_cast<float>(width);
            const float ny = (static_cast<float>(row) + 0.5f)
                / static_cast<float>(height);
            const aoc::map::gen::MollweideInverseResult mw =
                aoc::map::gen::projectionInverse(
                    config.projection, nx, ny);
            if (!mw.valid) { continue; }
            const aoc::map::gen::SphereField::CellCoord c =
                aoc::map::gen::SphereField::locate(
                    mw.coord.latDeg, mw.coord.lonDeg);
            const std::size_t idx = aoc::map::gen::SphereField::cellIndex(
                c.lonIdx, c.latIdx);
            const int16_t pid = sphereField.plateId[idx];
            if (pid >= 0 && pid < 255) {
                grid.setPlateId(row * width + col,
                    static_cast<uint8_t>(pid));
            }
            // Boundary type over the hex footprint (same window logic
            // as the mountain peakSample): raster boundaries are
            // 1-cell lines, so a centre-point lookup would miss most
            // of them and margin classification would come out patchy.
            const int32_t btHalfCells = std::max(3,
                static_cast<int32_t>(std::ceil(
                    180.0f / static_cast<float>(width) / 0.5f)));
            const uint8_t bt = sphereField.boundaryTypeMode(
                mw.coord.latDeg, mw.coord.lonDeg, btHalfCells);
            if (bt != 0u) {
                grid.setBoundaryTypeTile(row * width + col, bt);
            }
        }
    }
    } // end if (config.mapType == MapType::Continents && !plates.empty())
    if (config.mapType == MapType::Continents && !plates.empty()) {
        // Persist hotspot positions for the overlay.
        std::vector<std::pair<float, float>> hsCopy;
        hsCopy.reserve(hotspots.size());
        for (const Hotspot& h : hotspots) {
            hsCopy.emplace_back(h.cx, h.cy);
        }
        grid.setHotspots(std::move(hsCopy));
        // 2026-05-06 cleanup: dead setter calls (LatLon/Weight/
        // EulerPole/AngularVelDeg/Rot/Polygons/PolygonEdgeTypes/
        // Setters consumed by IceAndRock/Biogeography/EarthSystem/
        // ClimateBiome via the getter API.
        std::vector<std::pair<float, float>> motions;
        std::vector<std::pair<float, float>> centers;
        std::vector<float>                   landFracs;
        std::vector<float>                   crustAges;
        std::vector<int32_t>                 mergesAbsorbed;
        std::vector<uint8_t>                 isPolar;
        motions.reserve(plates.size());
        centers.reserve(plates.size());
        landFracs.reserve(plates.size());
        crustAges.reserve(plates.size());
        mergesAbsorbed.reserve(plates.size());
        isPolar.reserve(plates.size());
        for (const Plate& p : plates) {
            motions.emplace_back(0.0f, 0.0f);
            centers.emplace_back(p.cx, p.cy);
            landFracs.push_back(p.landFraction);
            crustAges.push_back(0.0f);
            mergesAbsorbed.push_back(0);
            isPolar.push_back(0u);
        }
        grid.setPlateMotions(std::move(motions));
        grid.setPlateCenters(std::move(centers));
        grid.setPlateLandFrac(std::move(landFracs));
        grid.setPlateCrustAge(std::move(crustAges));
        grid.setPlateMergesAbsorbed(std::move(mergesAbsorbed));
        grid.setPlateIsPolar(std::move(isPolar));

        // 2026-05-03: POST-SIM GEOLOGICAL PASSES extracted to gen/PostSim.cpp.
        {
            aoc::map::gen::MapGenContext ctx{};
            ctx.grid           = &grid;
            ctx.width          = width;
            ctx.height         = height;
            ctx.cylindrical    = cylSim;
            ctx.plates         = &plates;
            ctx.elevationMap   = &elevationMap;
            ctx.orogeny        = &orogeny;
            ctx.sediment       = &sediment;
            ctx.crustAgeTile   = &crustAgeTile;
            ctx.rockTypeTile   = &rockTypeTile;
            ctx.marginTypeTile = &marginTypeTile;
            ctx.ophioliteMask  = &ophioliteMask;
            aoc::map::gen::runPostSimPasses(ctx);
        }
    }

    // 2026-05-03: water + mountain threshold computation extracted to
    // gen/Thresholds.cpp.
    aoc::map::gen::ThresholdResult thresholds;
    aoc::map::gen::runThresholdComputation(
        grid, config.mapType,
        config.seaLevelDelta, elevationMap, thresholds);
    std::vector<float>&   mountainElev      = thresholds.mountainElev;
    std::vector<int32_t>& distFromCoast     = thresholds.distFromCoast;
    const float           waterThreshold    = thresholds.waterThreshold;
    const float           mountainThreshold = thresholds.mountainThreshold;

    // 2026-05-03: 2-D climate model + biome assignment extracted to
    // gen/ClimateBiome.cpp.
    aoc::map::gen::runClimateBiomePass(grid, config, rng, elevationMap,
                                        mountainElev, distFromCoast,
                                        orogeny, thresholds.isWater,
                                        waterThreshold, mountainThreshold);

    // Rain-shadow / wind conversion is now integrated into the
    // moisture computation above (windMoist field walks upwind across
    // the same wind belts and subtracts moisture per mountain crossed).
    // No separate binary post-pass needed — biome assignment already
    // produces the correct Desert/Plains/Grassland mix from T × M.
    (void)config;

    // 2026-05-03: ice-sheet + rock-type extracted to gen/IceAndRock.cpp.
    if (config.mapType == MapType::Continents) {
        aoc::map::gen::runIceSheetExpansion(grid);
        aoc::map::gen::runRockTypeAssignment(grid, ophioliteMask, sediment,
                                             rockTypeTile);
    }

    // ============================================================
    // EARTH-SYSTEM POST-PASSES (lakes / volcanism / hazard /
    // permafrost / upwelling / deltas / salt flats / soil fertility /
    // mountain glaciers). All driven by the real tectonic + climate
    // data computed earlier; populate per-tile vectors that the game
    // mechanics + overlays can read.
    // ============================================================
    if (config.mapType == MapType::Continents) {
        const int32_t totalT = width * height;

        // 2026-05-03: EARTH-SYSTEM POST-PASSES (lakes through wetlands)
        // extracted to gen/EarthSystem.cpp. Biogeographic realms / land
        // bridges / refugia / metamorphic-core remain inline below because
        // they need direct Plate-struct access from the assignTerrain stack.
        aoc::map::gen::EarthSystemOutputs esOut;
        aoc::map::gen::runEarthSystemPasses(grid, cylSim, orogeny, sediment,
                                             esOut);
        std::vector<float>&   soilFert   = esOut.soilFert;
        std::vector<uint8_t>& volcanism  = esOut.volcanism;
        std::vector<uint8_t>& hazard     = esOut.hazard;
        std::vector<uint8_t>& permafrost = esOut.permafrost;
        std::vector<uint8_t>& lakeFlag   = esOut.lakeFlag;
        std::vector<uint8_t>& upwelling  = esOut.upwelling;

        // Trailing in-stack helper retained for biogeographic / land-bridge
        // / refugia / metamorphic blocks that still need it.
        auto nbHelper = [&](int32_t col, int32_t row,
                             int32_t dir, int32_t& outIdx) {
            const bool oddRow = (row & 1) != 0;
            static const int32_t DCOL_EVEN[6] = { +1,  0, -1, -1,  0, +1};
            static const int32_t DCOL_ODD[6]  = { +1, +1,  0, -1, -1,  0};
            static const int32_t DROW[6]      = {  0, -1, -1,  0, +1, +1};
            const int32_t dc = oddRow ? DCOL_ODD[dir] : DCOL_EVEN[dir];
            const int32_t dr = DROW[dir];
            int32_t nc = col + dc;
            int32_t nr = row + dr;
            if (cylSim) {
                if (nc < 0)        { nc += width; }
                if (nc >= width)   { nc -= width; }
            } else {
                if (nc < 0 || nc >= width) { return false; }
            }
            if (nr < 0 || nr >= height) { return false; }
            outIdx = nr * width + nc;
            return true;
        };

        // 2026-05-03: biogeographic-realms extracted to gen/Biogeography.cpp.
        aoc::map::gen::runBiogeographicRealms(grid);

        // 2026-05-03: land bridges / refugia / metamorphic core complex
        // extracted to gen/Biogeography.cpp.
        aoc::map::gen::runLandBridges(grid, cylSim, lakeFlag);
        aoc::map::gen::runRefugia(grid, cylSim, soilFert);
        aoc::map::gen::runMetamorphicCoreComplex(grid, cylSim);

        // 2026-05-03: SESSION 3 extracted to gen/Session3.cpp.
        aoc::map::gen::AtmosphereOceanOutputs s3out;
        aoc::map::gen::runAtmosphereOcean(grid, cylSim, sediment, lakeFlag, s3out);
        std::vector<uint8_t>& climateHazard = s3out.climateHazard;
        std::vector<uint8_t>& glacialFeat   = s3out.glacialFeat;
        std::vector<uint8_t>& oceanZone     = s3out.oceanZone;
        std::vector<float>&   cloudCover    = s3out.cloudCover;
        std::vector<uint8_t>& flowDir       = s3out.flowDir;

        // 2026-05-03: SESSION 4 extracted to gen/Session4.cpp.
        aoc::map::gen::BiomeSubtypesInputs s4in;
        s4in.cylindrical   = cylSim;
        s4in.seed          = config.seed;
        s4in.sediment      = &sediment;
        s4in.permafrost    = &permafrost;
        s4in.lakeFlag      = &lakeFlag;
        s4in.volcanism     = &volcanism;
        s4in.hazard        = &hazard;
        s4in.upwelling     = &upwelling;
        s4in.climateHazard = &climateHazard;
        s4in.oceanZone     = &oceanZone;
        s4in.cloudCover    = &cloudCover;
        aoc::map::gen::BiomeSubtypesOutputs s4out;
        aoc::map::gen::runBiomeSubtypes(grid, s4in, s4out);
        std::vector<uint16_t>& natHazard = s4out.natHazard;
        std::vector<uint8_t>&  bSub      = s4out.bSub;
        std::vector<uint8_t>&  marineD   = s4out.marineD;
        std::vector<uint8_t>&  wildlife  = s4out.wildlife;
        std::vector<uint8_t>&  disease   = s4out.disease;
        std::vector<uint8_t>&  windE     = s4out.windE;
        std::vector<uint8_t>&  solarE    = s4out.solarE;
        std::vector<uint8_t>&  hydroE    = s4out.hydroE;
        std::vector<uint8_t>&  geoE      = s4out.geoE;
        std::vector<uint8_t>&  tidalE    = s4out.tidalE;
        std::vector<uint8_t>&  waveE     = s4out.waveE;
        std::vector<uint8_t>&  atmExtras = s4out.atmExtras;
        std::vector<uint8_t>&  hydExtras = s4out.hydExtras;
        std::vector<uint8_t>&  eventMrk  = s4out.eventMrk;

        // 2026-05-03: post-SESSION-4 analysis blocks (mountain passes,
        // defensibility, domesticable, trade route, habitability, wetland,
        // coral reef) extracted to gen/Mappability.cpp.
        aoc::map::gen::runMountainPass(grid, cylSim);
        aoc::map::gen::runDefensibility(grid, cylSim);
        aoc::map::gen::runDomesticable(grid);
        aoc::map::gen::runTradeRoutePotential(grid, marineD);
        aoc::map::gen::runHabitability(grid, cylSim, soilFert, natHazard,
                                       disease, permafrost);
        aoc::map::gen::runWetlandSubtype(grid);
        aoc::map::gen::runCoralReef(grid, bSub);

        // 2026-05-03: SESSION 9 compute body extracted to gen/Session9.cpp.
        aoc::map::gen::KoppenStructuresOutputs s9out;
        aoc::map::gen::runKoppenStructures(grid, cylSim, orogeny, lakeFlag, sediment,
                                   eventMrk, s9out);
        std::vector<uint8_t>& kop    = s9out.kop;
        std::vector<uint8_t>& mtnS   = s9out.mtnS;
        std::vector<uint8_t>& oreG   = s9out.oreG;
        std::vector<uint8_t>& strait = s9out.strait;
        std::vector<uint8_t>& harbor = s9out.harbor;
        std::vector<uint8_t>& chanP  = s9out.chanP;
        std::vector<uint8_t>& vegD   = s9out.vegD;
        std::vector<uint8_t>& coast  = s9out.coast;
        std::vector<uint8_t>& subV   = s9out.subV;
        std::vector<uint8_t>& volP   = s9out.volP;
        std::vector<uint8_t>& karstS = s9out.karstS;
        std::vector<uint8_t>& desS   = s9out.desS;
        std::vector<uint8_t>& massW  = s9out.massW;
        std::vector<uint8_t>& namedW = s9out.namedW;
        std::vector<uint8_t>& forA   = s9out.forA;
        std::vector<uint8_t>& soilM  = s9out.soilM;

        // 2026-05-03: SESSION 10 compute body extracted to gen/Session10.cpp.
        // Outputs are passed back via LithologyOutputs struct; setter calls
        // remain inline below to preserve the original setter ORDER (other
        // sessions may read these via grid getters).
        aoc::map::gen::LithologyOutputs s10out;
        aoc::map::gen::runLithology(grid, cylSim, permafrost, s10out);
        std::vector<uint8_t>& litho    = s10out.litho;
        std::vector<uint8_t>& bedrock  = s10out.bedrock;
        std::vector<uint8_t>& sOrder   = s10out.sOrder;
        std::vector<uint8_t>& crustTh  = s10out.crustTh;
        std::vector<uint8_t>& geoGrad  = s10out.geoGrad;
        std::vector<uint8_t>& albedo   = s10out.albedo;
        std::vector<uint8_t>& vegType  = s10out.vegType;
        std::vector<uint8_t>& atmRiv   = s10out.atmRiv;
        std::vector<uint8_t>& cycBasin = s10out.cycBasin;
        std::vector<uint8_t>& sst      = s10out.sst;
        std::vector<uint8_t>& iceShelf = s10out.iceShelf;
        std::vector<uint8_t>& permaD   = s10out.permaD;

        // 2026-05-03: cliff-coast classification extracted to gen/CliffCoast.cpp.
        aoc::map::gen::runCliffCoast(grid, cylSim);

        // 2026-05-03: SESSION 12 extracted to gen/Session12.cpp.
        aoc::map::gen::runCoastalLandforms(grid, cylSim, lakeFlag, orogeny);

        // 2026-05-03: SESSION 13 extracted to gen/Session13.cpp.
        aoc::map::gen::runInsolationSlope(grid, cylSim, config.axialTilt);

        // 2026-05-03: SESSION 14 extracted to gen/Session14.cpp.
        aoc::map::gen::runStreamRiparian(grid, cylSim, soilFert, orogeny);

        // 2026-05-03: SESSION 15 extracted to gen/Session15.cpp.
        aoc::map::gen::runNppCarryingCapacity(grid, cylSim, soilFert);

        // 2026-05-03: SESSION 16 extracted to gen/Session16.cpp.
        aoc::map::gen::runDrainageLivestock(grid, cylSim, soilFert, sediment, lakeFlag);

        // 2026-05-03: SESSION 17 extracted to gen/Session17.cpp.
        aoc::map::gen::runEcoAnalytics(grid);

        grid.setLithology(std::move(litho));
        grid.setBedrockLithology(std::move(bedrock));
        grid.setSoilOrder(std::move(sOrder));
        grid.setCrustalThickness(std::move(crustTh));
        grid.setGeothermalGradient(std::move(geoGrad));
        grid.setAlbedo(std::move(albedo));
        grid.setVegetationType(std::move(vegType));
        grid.setAtmosphericRiver(std::move(atmRiv));
        grid.setCycloneBasin(std::move(cycBasin));
        grid.setSeaSurfaceTemp(std::move(sst));
        grid.setIceShelfZone(std::move(iceShelf));
        grid.setPermafrostDepth(std::move(permaD));

        grid.setKoppen(std::move(kop));
        grid.setMountainStructure(std::move(mtnS));
        grid.setOreGrade(std::move(oreG));
        grid.setStrait(std::move(strait));
        grid.setHarborScore(std::move(harbor));
        grid.setChannelPattern(std::move(chanP));
        grid.setVegetationDensity(std::move(vegD));
        grid.setCoastalFeature(std::move(coast));
        grid.setSubmarineVent(std::move(subV));
        grid.setVolcanicProfile(std::move(volP));
        grid.setKarstSubtype(std::move(karstS));
        grid.setDesertSubtype(std::move(desS));
        grid.setMassWasting(std::move(massW));
        grid.setNamedWind(std::move(namedW));
        grid.setForestAgeClass(std::move(forA));
        grid.setSoilMoistureRegime(std::move(soilM));

        // ---- CORAL REEF TIER CLASSIFICATION ----
        // Categorize each Reef-feature tile:
        //   1 fringing — adjacent to land
        //   2 barrier — within 3 hexes of land but not adjacent
        //   3 atoll — biome subtype 11 (hotspot ring)
        //   4 patch — open shelf, isolated
        std::vector<uint8_t> reefT(static_cast<std::size_t>(totalT), 0);
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                if (grid.feature(i) != aoc::map::FeatureType::Reef) {
                    continue;
                }
                if (bSub[static_cast<std::size_t>(i)] == 11) {
                    reefT[static_cast<std::size_t>(i)] = 3; // atoll
                    continue;
                }
                bool adjLand = false;
                bool nearLand = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    const aoc::map::TerrainType nt = grid.terrain(nIdx);
                    if (nt != aoc::map::TerrainType::Ocean
                        && nt != aoc::map::TerrainType::ShallowWater) {
                        adjLand = true; break;
                    }
                }
                if (adjLand) {
                    reefT[static_cast<std::size_t>(i)] = 1; // fringing
                    continue;
                }
                for (int32_t dr = -3; dr <= 3 && !nearLand; ++dr) {
                    const int32_t rr = row + dr;
                    if (rr < 0 || rr >= height) { continue; }
                    for (int32_t dc = -3; dc <= 3 && !nearLand; ++dc) {
                        int32_t cc = col + dc;
                        if (cylSim) {
                            if (cc < 0)        { cc += width; }
                            if (cc >= width)   { cc -= width; }
                        } else if (cc < 0 || cc >= width) { continue; }
                        const aoc::map::TerrainType nt =
                            grid.terrain(rr * width + cc);
                        if (nt != aoc::map::TerrainType::Ocean
                            && nt != aoc::map::TerrainType::ShallowWater) {
                            nearLand = true;
                        }
                    }
                }
                reefT[static_cast<std::size_t>(i)] = nearLand ? 2 : 4;
            }
        }
        grid.setReefTier(std::move(reefT));

        grid.setNaturalHazard(std::move(natHazard));
        grid.setBiomeSubtype(std::move(bSub));
        grid.setMarineDepth(std::move(marineD));
        grid.setWildlife(std::move(wildlife));
        grid.setDisease(std::move(disease));
        grid.setWindEnergy(std::move(windE));
        grid.setSolarEnergy(std::move(solarE));
        grid.setHydroEnergy(std::move(hydroE));
        grid.setGeothermalEnergy(std::move(geoE));
        grid.setTidalEnergy(std::move(tidalE));
        grid.setWaveEnergy(std::move(waveE));
        grid.setAtmosphericExtras(std::move(atmExtras));
        grid.setHydroExtras(std::move(hydExtras));
        grid.setEventMarker(std::move(eventMrk));

        grid.setClimateHazard(std::move(climateHazard));
        grid.setGlacialFeature(std::move(glacialFeat));
        grid.setOceanZone(std::move(oceanZone));
        grid.setCloudCover(std::move(cloudCover));
        grid.setFlowDir(std::move(flowDir));

        grid.setSoilFertility(soilFert);
        grid.setVolcanism(volcanism);
        grid.setSeismicHazard(hazard);
        grid.setPermafrost(permafrost);
        grid.setLakeFlag(lakeFlag);
        grid.setUpwelling(upwelling);
    }

    // Erosion pass: connected-component flood fill on land. Components
    // smaller than MIN_ISLAND_SIZE tiles get drowned (converted to
    // Ocean) — clears single-tile confetti along continental shelves.
    {
        // 2026-07-05: fixed at 4 tiles (was clamp(12 + totalMy/200,
        // 12, 50) = 27 at the 3 Gy default, ~1.4M km2 at Standard
        // scale — everything below Greenland drowned, no archipelagos
        // possible and every hotspot island purged). Earth's island
        // inventory is dominated by small features; 4 keeps 1-3-tile
        // flecks out while letting Japan/Indonesia-scale groups
        // survive. Capital placement is guarded separately by a
        // minimum-landmass check at start-position selection.
        constexpr int32_t MIN_ISLAND_SIZE = 4;
        std::vector<int32_t> compId(static_cast<std::size_t>(width * height), -1);
        std::vector<int32_t> bfs;
        bfs.reserve(static_cast<std::size_t>(width * height));
        int32_t nextId = 0;
        std::vector<int32_t> compSize;
        for (int32_t i = 0; i < width * height; ++i) {
            if (compId[static_cast<std::size_t>(i)] >= 0) { continue; }
            if (isWater(grid.terrain(i))) { continue; }
            compId[static_cast<std::size_t>(i)] = nextId;
            int32_t size = 0;
            bfs.clear();
            bfs.push_back(i);
            while (!bfs.empty()) {
                const int32_t idx = bfs.back();
                bfs.pop_back();
                ++size;
                const int32_t col = idx % width;
                const int32_t row = idx / width;
                const hex::AxialCoord ax = hex::offsetToAxial({col, row});
                for (const hex::AxialCoord& n : hex::neighbors(ax)) {
                    if (!grid.isValid(n)) { continue; }
                    const int32_t ni = grid.toIndex(n);
                    if (compId[static_cast<std::size_t>(ni)] >= 0) { continue; }
                    if (isWater(grid.terrain(ni))) { continue; }
                    compId[static_cast<std::size_t>(ni)] = nextId;
                    bfs.push_back(ni);
                }
            }
            compSize.push_back(size);
            ++nextId;
        }
        // Drown sub-threshold land components.
        for (int32_t i = 0; i < width * height; ++i) {
            const int32_t cid = compId[static_cast<std::size_t>(i)];
            if (cid < 0) { continue; }
            if (compSize[static_cast<std::size_t>(cid)] < MIN_ISLAND_SIZE) {
                grid.setTerrain(i, TerrainType::Ocean);
                grid.setElevation(i, -1);
                // Clear any feature (Hills/Forest/Jungle) — drowned
                // land mustn't carry land-only features into the ocean.
                grid.setFeature(i, FeatureType::None);
            }
        }
    }

    // 2026-05-03: lake-purge + coastal-arm erosion extracted to gen/.
    aoc::map::gen::runLakePurge(grid, cylSim);
    aoc::map::gen::runCoastalErosion(grid);
}

// smoothCoastlines, assignFeatures, placeNaturalWonders moved to src/map/gen/Features.cpp.
// generateRivers moved to src/map/gen/Rivers.cpp.


// ============================================================================

// Resource-placement passes moved to src/map/gen/Resources.cpp.


} // namespace aoc::map

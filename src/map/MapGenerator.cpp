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
    // smoothing → features → rivers → wonders). The previous LandWithSeas
    // early-return called generateRealisticTerrain() which is now dead code
    // but kept under `#if 0` further down for reference.
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
    // Cylindrical X wrap toggles the seam-handling logic in plate motion,
    // collision tests, and Voronoi distance lookups so the east/west edge
    // doesn't appear as a straight cut through plates.
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

    // Per-type waterRatio override. waterRatio drives the final ocean cutoff,
    // so LandOnly needs a tiny override and Islands a high one regardless of
    // the caller's default.
    float effectiveWaterRatio = config.waterRatio;
    // Apply eustatic + climate-phase sea-level shift. Greenhouse (1)
    // adds +0.04 (warm-period high), icehouse (2) subtracts -0.06
    // (Pleistocene low, exposed shelves). User-supplied seaLevelDelta
    // stacks on top.
    if (config.climatePhase == 1)      { effectiveWaterRatio += 0.04f; }
    else if (config.climatePhase == 2) { effectiveWaterRatio -= 0.06f; }
    // 2026-05-04: EUSTATIC SEA-LEVEL CYCLES. Real Earth sea level
    // varies cyclically over ~30-100 My periods driven by total
    // mid-ocean-ridge volume (more spreading = more displaced water
    // = higher seas). Cretaceous high stand was +200 m above modern;
    // Pleistocene glacials dropped seas -120 m. The seed-derived
    // sine offset gives each generated world a different point in
    // its cycle (ranges +/- 0.06 of waterRatio = roughly +/- 200 m).
    {
        aoc::Random eustaticRng(config.seed ^ 0x45555354u); // "EUST"
        const float cyclePhase =
            eustaticRng.nextFloat(0.0f, 6.2832f);
        effectiveWaterRatio += std::sin(cyclePhase) * 0.06f;
    }
    effectiveWaterRatio = std::clamp(
        effectiveWaterRatio + config.seaLevelDelta, 0.05f, 0.85f);

    switch (config.mapType) {
        case MapType::Continents: {
            // Plate-tectonic continent layout: pick ~14 plate seeds
            // randomly, flag ~55% as ocean, run a Voronoi assignment
            // per tile. Land plates produce land, ocean plates produce
            // deep ocean. Plate boundaries blur via the same domain-
            // warp used for tile elevation, so coastlines are irregular
            // rather than straight Voronoi edges.
            //
            // This is closer to real Earth generation (compare PlaTec /
            // Lautenschlager / Wraight & Carrillo 2013 plate sims) and
            // crucially avoids the "additive falloff" failure mode that
            // merged neighbouring continents into one landmass.
            // Two-pass plate placement so land plates are never Voronoi-
            // adjacent. Adjacent land plates merge their bases (both
            // 0.85) and read as one continuous landmass — the bug that
            // made every map collapse to "one big landmass".
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
                // Reserved RNG draw (legacy log-normal speed bucket
                // discriminator -- vx/vy deleted but stream stays
                // stable so seed-replay matches earlier audits).
                (void)centerRng.nextFloat(0.0f, 1.0f);
                p.rot        = centerRng.nextFloat(-3.14159f, 3.14159f);
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
            // 2026-05-04: bumped clamp 0.40-0.55 -> 0.60-0.72 to match
            // Earth's actual water coverage (~71%). Old 40-55% global
            // water meant midlatitudes ended up 70-80% land, which fused
            // every continent into a Pangaea-shaped megablob even when
            // tectonic-sim plates remained physically separate. With the
            // higher cutoff, landmass/total-land fraction realistically
            // peaks around 0.40-0.55 and adjacent plates produce
            // genuinely separated continents.
            effectiveWaterRatio = std::clamp(config.waterRatio, 0.60f, 0.72f);
            break;
        }
        // (continents tectonic-sim runs after the switch — see below)
#if 0   // 2026-05-03: Islands / ContinentsPlusIslands / LandOnly / Fractal /
        // LandWithSeas removed by user direction. Code retained under #if 0
        // for reference; the enum members are commented out in MapGenerator.hpp
        // so these case labels would not compile. Restoring requires
        // un-commenting the enum members AND this block.
        case MapType::Islands: {
            effectiveWaterRatio = std::max(config.waterRatio, 0.70f);
            constexpr int32_t ISLAND_COUNT = 14;
            for (int32_t i = 0; i < ISLAND_COUNT; ++i) {
                float cx = centerRng.nextFloat(0.08f, 0.92f);
                float cy = centerRng.nextFloat(0.08f, 0.92f);
                landCenters.push_back({cx, cy, centerRng.nextFloat(2.8f, 4.0f)});
            }
            break;
        }
        case MapType::ContinentsPlusIslands: {
            const int32_t contVariant = centerRng.nextInt(0, 1);
            if (contVariant == 0) {
                landCenters.push_back({0.22f, 0.40f, 1.9f});
                landCenters.push_back({0.78f, 0.60f, 1.9f});
            } else {
                landCenters.push_back({0.22f, 0.30f, 2.0f});
                landCenters.push_back({0.78f, 0.30f, 2.0f});
                landCenters.push_back({0.50f, 0.80f, 2.0f});
            }
            constexpr int32_t ISLAND_COUNT = 7;
            for (int32_t i = 0; i < ISLAND_COUNT; ++i) {
                float cx = centerRng.nextFloat(0.10f, 0.90f);
                float cy = centerRng.nextFloat(0.10f, 0.90f);
                landCenters.push_back({cx, cy, centerRng.nextFloat(3.0f, 4.5f)});
            }
            effectiveWaterRatio = std::clamp(config.waterRatio + 0.05f, 0.3f, 0.55f);
            break;
        }
        case MapType::LandOnly: {
            landCenters.push_back({0.5f, 0.5f, 0.9f});
            effectiveWaterRatio = std::min(config.waterRatio, 0.08f);
            break;
        }
        case MapType::Fractal: {
            break;
        }
        case MapType::LandWithSeas: {
            break;
        }
#endif
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
        // DT derived from EPOCHS and the user-configurable total drift
        // budget. Default drift = 0.6 map widths. Larger drift = bigger
        // plate motion = more dramatic continental shuffle. Smaller =
        // plates barely move, fine-grained evolution at small scale.
        const float driftFrac = (config.driftFraction > 0.0f)
            ? config.driftFraction : 0.6f;
        const float DT = std::clamp(
            (driftFrac / 0.7f) / static_cast<float>(EPOCHS),
            0.001f, 0.040f);

        const std::vector<Plate> startPlates = plates;

        // Rift-burst scheduling. Each burst spawns 2-3 rifts within
        // 1-2 epochs; ~12-25 epoch gap between bursts. Models Earth
        // Phanerozoic burst pattern (Pangaea breakup at 250 Ma + smaller
        // events at 100, 65, 50 Ma).
        std::vector<int32_t> riftBurstEpochs;
        {
            int32_t e = centerRng.nextInt(3, 8);
            while (e < EPOCHS) {
                riftBurstEpochs.push_back(e);
                e += centerRng.nextInt(12, 25);
            }
        }

        // SphereField allocation is unconditional; the world-frame elevation pass downstream
        // sources tile elevation from this raster via bilinearSample.
        // The flag AOC_PHYSICS_ON_SPHEREFIELD now only gates the per-
        // epoch step + snapshot setter (default ON since P2.1).
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

            const int32_t numCratons = 5 + cratonRng.nextInt(0, 4);

            // Per-nucleus absolute area drawn from log-normal — no
            // global quota. Median nucleus = 0.7 % of sphere
            // (~1810 cells at 720x360), σ = 0.6 in log space gives
            // a 4x spread between smallest and largest nucleus
            // (Slave craton ~0.5 M km² up to Yilgarn ~3 M km²;
            // Belousova et al. 2010 cratonic age-area survey).
            // Mean total nucleus coverage with 7 cratons ≈ 5 % of
            // sphere — the early-Archean continental fraction
            // baseline (Cawood et al. 2013). Subsequent epoch
            // thickening grows this to the modern ~29 % via arc
            // volcanism at convergent margins.
            constexpr float NUCLEUS_MEDIAN_FRACTION = 0.007f;
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
                // Clamp to [50, 5000] cells — protects against
                // pathological samples and stays within the
                // Belousova 2010 cratonic-area envelope.
                const float clamped = std::clamp(cells, 50.0f, 5000.0f);
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
                bool ok = false;
                for (int32_t attempt = 0; attempt < 64 && !ok; ++attempt) {
                    const float u = cratonRng.nextFloat(
                        -CRATON_LAT_LIMIT_SIN, CRATON_LAT_LIMIT_SIN);
                    const float latDeg = std::asin(u) * 57.29577951f;
                    const float lonDeg = cratonRng.nextFloat(-180.0f, 180.0f);
                    const SF::CellCoord c = SF::locate(latDeg, lonDeg);
                    ok = true;
                    for (int32_t j = 0; j < i; ++j) {
                        const aoc::map::gen::LatLon a = SF::cellCenter(
                            c.lonIdx, c.latIdx);
                        const aoc::map::gen::LatLon b = SF::cellCenter(
                            seedLon[static_cast<std::size_t>(j)],
                            seedLat[static_cast<std::size_t>(j)]);
                        const float d = aoc::map::gen::haversineRadians(a, b);
                        if (d < MIN_SEP_RAD) { ok = false; break; }
                    }
                    if (ok) {
                        seedLon[static_cast<std::size_t>(i)] = c.lonIdx;
                        seedLat[static_cast<std::size_t>(i)] = c.latIdx;
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

            // Advance every plate by (vx, vy) * DT. Cylindrical maps WRAP
            // around the X axis (no east/west edge — like a globe band);
            // flat maps BOUNCE so plates stay on the rectangle.
            // Y always bounces (poles aren't wrap-connected).
            for (Plate& p : plates) {
                // Plate rotates around (eulerPoleLatDeg, eulerPoleLonDeg)
                // by (angularVelDeg * DT) per epoch. Legacy (cx, cy)
                // re-projected via Mollweide forward each epoch for
                // 2D-only consumers (Voronoi, tile-id stash).
                constexpr float motionScale = 1.0f;
                if (p.eulerPoleLatDeg != 0.0f
                    || p.eulerPoleLonDeg != 0.0f
                    || p.angularVelDeg != 0.0f) {
                    // P6.10 jitter: drift Euler pole + perturb angular
                    // velocity before applying motion this epoch.
                    p.eulerPoleLatDeg = std::clamp(
                        p.eulerPoleLatDeg
                            + gaussianFromUniform() * poleSigmaDeg,
                        -89.0f, 89.0f);
                    p.eulerPoleLonDeg += gaussianFromUniform() * poleSigmaDeg;
                    while (p.eulerPoleLonDeg >  180.0f) p.eulerPoleLonDeg -= 360.0f;
                    while (p.eulerPoleLonDeg < -180.0f) p.eulerPoleLonDeg += 360.0f;
                    p.angularVelDeg *= 1.0f + gaussianFromUniform() * velFracSigma;

                    aoc::map::gen::LatLon current{p.latDeg, p.lonDeg};
                    aoc::map::gen::LatLon pole{
                        p.eulerPoleLatDeg, p.eulerPoleLonDeg};
                    const float stepDeg =
                        p.angularVelDeg * DT * motionScale;
                    aoc::map::gen::LatLon next =
                        aoc::map::gen::rotateAroundEulerPole(
                            current, pole, stepDeg);
                    p.latDeg = next.latDeg;
                    p.lonDeg = next.lonDeg;
                    aoc::map::gen::MollweidePoint mw =
                        aoc::map::gen::mollweideForward(next);
                    p.cx = mw.mapX;
                    p.cy = mw.mapY;
                    // Propagate Euler rotation's local-vertical component
                    // into p.rot. Projection = stepRad * cos(haversine to
                    // pole): plate at pole spins fully; plate 90° from
                    // pole only translates.
                    const float angDistRad =
                        aoc::map::gen::haversineRadians(next, pole);
                    p.rot += stepDeg * 0.01745329252f
                        * std::cos(angDistRad);
                }
                ++p.ageEpochs;
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
            for (std::size_t a = 0; a < plates.size(); ++a) {
                for (std::size_t b = a + 1; b < plates.size(); ++b) {
                    float dx = plates[a].cx - plates[b].cx;
                    float dy = plates[a].cy - plates[b].cy;
                    // Cylindrical wrap on X: shortest separation goes
                    // around the back of the world if that's closer.
                    if (cylSim) {
                        if (dx >  0.5f) { dx -= 1.0f; }
                        if (dx < -0.5f) { dx += 1.0f; }
                    }
                    const float d = std::sqrt(dx * dx + dy * dy);
                    // Continental collision (fusion). Two land plates fuse
                    // ONLY when both:
                    //   1. Centres are close enough — fixed 0.13 unit
                    //      threshold (about 1.5x the typical plate radius
                    //      after Voronoi packing). This is independent
                    //      of plate count — Earth's continents collide
                    //      regardless of how many plates exist globally.
                    //   2. Relative velocity points INWARD (closing).
                    //      Glancing-pass plates that happen to be near
                    //      but moving apart should not fuse — they're
                    //      diverging, not colliding. Closing rate is
                    //      v_rel · displacement / |displacement|;
                    //      negative value = closing.
                    // CONTINENTAL COLLISION DYNAMICS (multi-stage):
                    //   1. Approach — plates close from afar at full velocity.
                    //   2. CONTACT (d < CONTACT_DIST, closing): crust
                    //      starts deforming. Relative velocity along the
                    //      collision axis decays at COLLISION_DAMP per
                    //      epoch (energy converts to crustal deformation
                    //      → mountain orogeny accumulates separately via
                    //      the convergent-stress code below). Plates
                    //      keep their identities — no merge yet.
                    //   3. SUTURING (d < MERGE_DIST AND |relV| < SLOW_V):
                    //      collision energy mostly dissipated, plates
                    //      have welded along the boundary → fuse.
                    //
                    // Real-world: India-Eurasia has been actively
                    // colliding for ~50 My, building Himalayas. Plates
                    // are still moving relative to each other (~5 cm/yr)
                    // — they aren't yet fully merged. Our simulation
                    // mirrors this: long collision phase, then merge.
                    // Merge gate: center-distance only (vx/vy + polygon
                    // adjacency probes deleted with their fields). 0.07
                    // matches Earth Phanerozoic median plate lifetime
                    // 250 Ma plate-recycle rate.
                    constexpr float MERGE_DIST = 0.07f;
                    const bool readyToMerge = (plates[a].landFraction > 0.40f
                                                && plates[b].landFraction > 0.40f
                                                && d < MERGE_DIST);
                    if (readyToMerge) {
                        // Continental collision: fuse plates atomically.
                        // mergePlates rewrites every raster cell pid
                        // == b -> a, zeroes the closing rate on the
                        // welded suture (so ghost orogeny does not
                        // accrete across the new plate interior),
                        // then erases plates[b] and remaps every
                        // pid > b down by one. Survivor's authoritative
                        // (latDeg, lonDeg, landFraction, ageEpochs)
                        // is updated in the helper. Mollweide cache
                        // (cx, cy) is re-projected here because the
                        // physics layer does not own the projection.
                        aoc::map::gen::mergePlates(
                            sphereField, plates, a, b);
                        const aoc::map::gen::MollweidePoint mw =
                            aoc::map::gen::mollweideForward(
                                aoc::map::gen::LatLon{plates[a].latDeg,
                                                      plates[a].lonDeg});
                        plates[a].cx = mw.mapX;
                        plates[a].cy = mw.mapY;
                        --b;
                    }
                }
            }
            // Rift events fire on scheduled burst epochs (3 rifts per
            // burst). Each rift splits a land plate into 1-2 children,
            // sometimes producing a triple junction.
            const bool isRiftEpoch = std::find(
                riftBurstEpochs.begin(), riftBurstEpochs.end(), epoch)
                != riftBurstEpochs.end();
            const int32_t riftsPerBurst = isRiftEpoch ? 3 : 0;
            if (isRiftEpoch) {
                std::vector<std::size_t> landIdx;
                for (std::size_t i = 0; i < plates.size(); ++i) {
                    if (plates[i].landFraction > 0.40f) { landIdx.push_back(i); }
                }
                // Cap total plate count. Earth has ~15 plates (7 major +
                // 8 minor) — let the world grow up to ~2x the initial
                // count then stop adding new plates. Without this, every
                // rift adds 1-2 land plates and over many epochs the
                // world fills up with land plates → land-only map.
                // Real-world Pangaea cycle has CONSTANT net plate count:
                // mergers balance rifts.
                // 2026-05-04: cap dropped 18 -> 13 to match Earth's
                // ~15-plate total when accounting for ID-mod-26 letter
                // collisions. Old 18 + start ~13 + child/microplate
                // growth let plate count run to 20-22 distinct ids.
                // Earth has ~7 major + ~8 minor = 15 plates total;
                // capping at 13 leaves headroom for occasional
                // microplate spawns without runaway proliferation.
                const std::size_t maxPlates = std::max(
                    static_cast<std::size_t>(13),
                    startPlates.size() + static_cast<std::size_t>(2));
                const int32_t splitsThisEpoch =
                    (plates.size() >= maxPlates)
                        ? 0
                        : std::min(riftsPerBurst,
                                   static_cast<int32_t>(landIdx.size()));
                for (int32_t s = 0; s < splitsThisEpoch; ++s) {
                    if (landIdx.empty()) { break; }
                    // PLUME-INDUCED RIFTING. Prefer plates near a
                    // hotspot — major rifts on Earth correlate with
                    // mantle plumes (Afar plume → East African Rift +
                    // Red Sea opening; CAMP plume → Atlantic opening
                    // ~200 Mya; Iceland plume → North Atlantic
                    // separation). For each candidate plate compute
                    // its distance to the nearest hotspot; weight the
                    // pick toward closer plates.
                    std::size_t pickPos = 0;
                    if (!hotspots.empty()) {
                        float bestProx = -1.0f;
                        for (std::size_t k = 0; k < landIdx.size(); ++k) {
                            const Plate& cand = plates[landIdx[k]];
                            float minHd = 1e9f;
                            for (const Hotspot& h : hotspots) {
                                float hdx = cand.cx - h.cx;
                                float hdy = cand.cy - h.cy;
                                if (cylSim) {
                                    if (hdx > 0.5f) { hdx -= 1.0f; }
                                    if (hdx < -0.5f) { hdx += 1.0f; }
                                }
                                const float hd = std::sqrt(hdx * hdx + hdy * hdy);
                                if (hd < minHd) { minHd = hd; }
                            }
                            const float prox = 1.0f / (0.05f + minHd);
                            const float jitter = centerRng.nextFloat(0.0f, prox * 0.5f);
                            if (prox + jitter > bestProx) {
                                bestProx = prox + jitter;
                                pickPos = k;
                            }
                        }
                    } else {
                        pickPos = static_cast<std::size_t>(
                            centerRng.nextInt(0,
                                static_cast<int32_t>(landIdx.size()) - 1));
                    }
                    const std::size_t pi = landIdx[pickPos];
                    landIdx.erase(landIdx.begin()
                        + static_cast<std::ptrdiff_t>(pickPos));

                    // Real continental rifting (Wilson cycle):
                    //  1. A rift LINE develops along a weak zone in
                    //     the continent (often an old suture from a
                    //     previous collision).
                    //  2. The two halves are pushed APART perpendicular
                    //     to the rift line by upwelling mantle
                    //     (ridge-push force).
                    //  3. New oceanic crust forms BETWEEN the halves;
                    //     they drift apart at ~few cm/year with a
                    //     mid-ocean ridge marking the original rift.
                    // Triple junctions (Afar, where African+Arabian+
                    // Somalian plates meet) form when three rifts meet
                    // at one point at roughly 120° to each other.
                    // 2026-05-04: triple-junction rate dropped 30% -> 5%.
                    // Real triple-junction events (Afar-style RRR) are
                    // rare: only a handful in Earth's geologic record.
                    // Old 30%-per-rift firing produced 2 children per
                    // rift in nearly every cycle, doubling plate
                    // proliferation rate.
                    const bool tripleSplit = (centerRng.nextFloat(0.0f, 1.0f) < 0.05f);
                    const int32_t childCount = tripleSplit ? 2 : 1;

                    // The rift LINE is at angle `faultAxis`. Children
                    // are placed perpendicular to this line (i.e., at
                    // faultAxis ± π/2) so they really do sit on
                    // opposite sides of the rift.
                    const float faultAxis = centerRng.nextFloat(0.0f, 6.2832f);
                    // FAILED RIFT (aulacogen). Real Earth: ~30 % of
                    // continental rifts stall before opening to ocean
                    // crust. Examples: Mississippi Embayment + Reelfoot
                    // Rift (failed Iapetus margin), North Sea Graben
                    // (failed Atlantic), Benue Trough (failed S
                    // Atlantic). Leaves a linear basin trough across
                    // the parent continent — visible as a low-elevation
                    // rift scar that gets sediment-filled over time.
                    // We mark this by carving negative orogeny along a
                    // narrow band in the parent's local frame, then
                    // skipping child creation entirely.
                    const bool failedRift =
                        (centerRng.nextFloat(0.0f, 1.0f) < 0.30f);
                    if (failedRift) {
                        // 30% of rift events skip child spawn (failed
                        // rift / aulacogen). Plate-count effect only.
                        continue;
                    }
                    // Smaller initial separation (was 0.10–0.18) so a
                    // rift is a less abrupt visual event. Children
                    // gradually drift apart over subsequent epochs
                    // instead of teleporting far at the rift moment.
                    const float offsetMag = centerRng.nextFloat(0.05f, 0.10f);
                    // Push direction = perpendicular to the rift line.
                    const float pushAxis = faultAxis + 1.5708f; // +π/2

                    // Snapshot parent before mutating it (its velocity
                    // becomes one of the diverging directions).
                    const Plate parent = plates[pi];
                    // Parent stays on one side of the rift line.
                    plates[pi].cx = std::clamp(parent.cx
                        + std::cos(pushAxis) * offsetMag * 0.5f, 0.05f, 0.95f);
                    plates[pi].cy = std::clamp(parent.cy
                        + std::sin(pushAxis) * offsetMag * 0.5f, 0.05f, 0.95f);
                    // Parent retains its ORIGINAL crust seed and most
                    // of its landFraction; it IS the original continent
                    // continuing on. Velocity tilts toward +pushAxis
                    // (it's the side moving in that direction).
                    {
                        plates[pi].landFraction = std::clamp(
                            parent.landFraction * centerRng.nextFloat(0.85f, 0.97f),
                            0.30f, 0.85f);
                    }

                    for (int32_t c = 0; c < childCount; ++c) {
                        Plate child = parent;
                        // 2026-05-04: triple-junction arms are 0°/120°/240°
                        // apart (Afar-style RRR junction). Was 0°/180°/120°,
                        // which made the 0/180 pair collinear -- not a
                        // genuine triple junction. Parent keeps original
                        // pushAxis; child 0 rotated +120°, child 1 +240°.
                        const float childPushAngle = (c == 0)
                            ? (pushAxis + 2.0944f)         // +120°
                            : (pushAxis + 4.18879f);       // +240°
                        child.cx = std::clamp(parent.cx
                            + std::cos(childPushAngle) * offsetMag * 0.5f, 0.05f, 0.95f);
                        child.cy = std::clamp(parent.cy
                            + std::sin(childPushAngle) * offsetMag * 0.5f, 0.05f, 0.95f);
                        // Children get fresh rotation, aspect, AND crust
                        // seed. A child plate carries a different chunk
                        // of the original landmass — its own crust
                        // pattern, not a duplicate of the parent's.
                        // (When Pangaea broke apart, Africa and S America
                        // Rotate child frame slightly so the same noise
                        // is sampled differently away from the seam,
                        // producing a divergent interior but matching
                        // coastline (Gondwana-style).
                        child.rot          = parent.rot
                                           + centerRng.nextFloat(-0.30f, 0.30f);
                        // Rifted children are continental fragments
                        // (S. America breaking off Pangaea). Both halves
                        // carry continent + new oceanic crust accreted
                        // at the trailing edge facing the rift line. No
                        // new third plate for the ocean basin -- it is
                        // split
                        // between the two existing plates' trailing
                        // edges; child landFraction reduced slightly so
                        // the new ocean basin appears at the rift seam.
                        child.landFraction = std::clamp(
                            parent.landFraction *
                                centerRng.nextFloat(0.85f, 1.00f),
                            0.30f, 0.55f);
                        // Children = young rifted fragments (Atlantic-
                        // post-Pangea analog). Inherit parent's Euler
                        // pole with slight perturbation.
                        plates.push_back(child);
                    }
                }
                // MICROPLATES. Between major plates, smaller fragments
                // sometimes form (Caribbean, Cocos, Scotia, Adria,
                // Anatolian, Aegean). Random small chance per rift epoch
                // to spawn a tiny plate at a triple-junction-like spot
                // between two adjacent major plates. Gives world a more
                // realistic "messy" plate distribution rather than only
                // major plates.
                // 2026-05-04: spawn rate cut 5% -> 1.5% per rift epoch
                // and capped at MICROPLATE_CAP=3 active. Old 5%/epoch
                // Microplate spawn: gated by maxPlates + 4-plate floor
                // + 1.5%/epoch chance. Produces ~1-2 microplates per sim
                // matching Earth's ~10-over-4.5Gyr.
                if (plates.size() < maxPlates
                    && plates.size() >= 4
                    && centerRng.nextFloat(0.0f, 1.0f) < 0.015f) {
                    // Pick two random plates that are close to each other.
                    const std::size_t pa = static_cast<std::size_t>(
                        centerRng.nextInt(0, static_cast<int32_t>(plates.size()) - 1));
                    std::size_t pb = pa;
                    float bestD = 1e9f;
                    for (std::size_t k = 0; k < plates.size(); ++k) {
                        if (k == pa) { continue; }
                        float kdx = plates[k].cx - plates[pa].cx;
                        float kdy = plates[k].cy - plates[pa].cy;
                        if (cylSim) {
                            if (kdx > 0.5f) { kdx -= 1.0f; }
                            if (kdx < -0.5f) { kdx += 1.0f; }
                        }
                        const float kd = std::sqrt(kdx * kdx + kdy * kdy);
                        if (kd < bestD) { bestD = kd; pb = k; }
                    }
                    if (pb != pa && bestD < 0.30f) {
                        Plate micro;
                        micro.cx = (plates[pa].cx + plates[pb].cx) * 0.5f
                                 + centerRng.nextFloat(-0.04f, 0.04f);
                        micro.cy = (plates[pa].cy + plates[pb].cy) * 0.5f
                                 + centerRng.nextFloat(-0.04f, 0.04f);
                        if (cylSim) {
                            if (micro.cx < 0.0f) { micro.cx += 1.0f; }
                            if (micro.cx > 1.0f) { micro.cx -= 1.0f; }
                        }
                        micro.cx = std::clamp(micro.cx, 0.05f, 0.95f);
                        micro.cy = std::clamp(micro.cy, 0.10f, 0.90f);
                        // Reject if midpoint lands inside a THIRD plate's
                        // Voronoi territory. Real Earth: microplates form
                        // at junctions BETWEEN adjacent plates, never
                        // embedded inside one plate's interior.
                        // Verify nearest plate at midpoint is pa or pb.
                        {
                            int32_t nearest = -1;
                            float bestSq = 1e9f;
                            for (std::size_t k = 0; k < plates.size(); ++k) {
                                float ddx = micro.cx - plates[k].cx;
                                float ddy = micro.cy - plates[k].cy;
                                if (cylSim) {
                                    if (ddx >  0.5f) { ddx -= 1.0f; }
                                    if (ddx < -0.5f) { ddx += 1.0f; }
                                }
                                const float dsq = ddx * ddx + ddy * ddy;
                                if (dsq < bestSq) {
                                    bestSq = dsq;
                                    nearest = static_cast<int32_t>(k);
                                }
                            }
                            if (nearest != static_cast<int32_t>(pa)
                                && nearest != static_cast<int32_t>(pb)) {
                                // Midpoint inside third plate — skip.
                                continue;
                            }
                        }
                        // Small random velocity (microplates have erratic motion).
                        micro.rot    = centerRng.nextFloat(-3.14f, 3.14f);
                        // Mixed land/ocean character — typical for
                        // 2026-05-04: microplates are now always oceanic
                        // with near-zero landFraction. Real microplates
                        // (Cocos, Juan de Fuca, Scotia, Anatolian) are
                        // mostly oceanic. Land-microplate option
                        // produced 0.75-0.90 landFraction patches that
                        // packed into existing continents and inflated
                        // the per-continent plate count. Like rifted
                        // children, microplates now exist only as
                        // boundary-stress sources and small island arcs.
                        micro.landFraction =
                            centerRng.nextFloat(0.005f, 0.02f);
                        micro.ageEpochs = 0;
                        // Microplates spin fast (Anatolian, Adria rotate
                        // measurably faster than majors over Quaternary).
                        plates.push_back(micro);
                    }
                }
            }


            // ---- RIDGE SPAWNING: new oceanic plate at largest void ----
            // Wilson cycle balance: as oceanic plates subduct away, new
            // ones must form at spreading centres so the ocean basins
            // refresh. Otherwise the world drains of oceanic plates over
            // time and gridlocks. Each epoch, count oceanic plates; if
            // below the target floor, find the world point farthest
            // from any plate centre — that's the largest void where a
            // new ridge axis would naturally seed — and spawn a fresh
            // oceanic plate there with crustAge=0.
            {
                int32_t oceanicCount = 0;
                for (const Plate& p : plates) {
                    if (p.landFraction < 0.40f) {
                        ++oceanicCount;
                    }
                }
                constexpr int32_t OCEANIC_FLOOR = 5;
                if (oceanicCount < OCEANIC_FLOOR
                    && plates.size() < static_cast<std::size_t>(22)) {
                    // Scan a coarse grid for the void centre.
                    constexpr int32_t SCAN = 32;
                    float bestDist = -1.0f;
                    float bestSx = 0.5f, bestSy = 0.5f;
                    for (int32_t sy = 2; sy < SCAN - 2; ++sy) {
                        for (int32_t sx = 0; sx < SCAN; ++sx) {
                            const float nxs = (static_cast<float>(sx) + 0.5f)
                                / static_cast<float>(SCAN);
                            const float nys = (static_cast<float>(sy) + 0.5f)
                                / static_cast<float>(SCAN);
                            float minSq = 1e9f;
                            for (const Plate& p : plates) {
                                // Include primary + extra seeds. Multi-
                                // seed plates can claim territory far
                                // from their primary centre; void
                                // detection must use ALL seeds or new
                                // ridge plates can spawn inside an
                                // extra-seed lobe of an existing plate.
                                auto seedDsq = [&](float seedX, float seedY) {
                                    float ddx = nxs - seedX;
                                    float ddy = nys - seedY;
                                    if (cylSim) {
                                        if (ddx >  0.5f) { ddx -= 1.0f; }
                                        if (ddx < -0.5f) { ddx += 1.0f; }
                                    }
                                    return ddx * ddx + ddy * ddy;
                                };
                                const float dsq = seedDsq(p.cx, p.cy);
                                if (dsq < minSq) { minSq = dsq; }
                            }
                            if (minSq > bestDist) {
                                bestDist = minSq;
                                bestSx = nxs;
                                bestSy = nys;
                            }
                        }
                    }
                    if (bestDist > 0.020f) { // require meaningful void
                        Plate fresh;
                        fresh.cx = bestSx;
                        fresh.cy = bestSy;
                        // Inherit a slow drift in the direction AWAY
                        // from the nearest plate (ridge-push starting
                        // condition).
                        fresh.rot    = centerRng.nextFloat(-3.14f, 3.14f);
                        // 2026-05-04: matched ocean-plate landFraction
                        // reduction. Fresh ridge-spawn ocean plates are
                        // brand new oceanic crust -- effectively 100 %
                        // ocean.
                        fresh.landFraction = centerRng.nextFloat(0.005f, 0.02f);
                        fresh.ageEpochs = 0;
                        // Fresh ridge-spawn plate: random pole, oceanic
                        // rotation magnitude (faster than continental).
                        plates.push_back(fresh);
                    }
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

            // Per-epoch EROSION: decay positive orogeny by a small
            // fraction every epoch. Real Earth erosion rates wear
            // mountain ranges from Himalaya-tall (~9 km) to nothing
            // over ~500 My. With each epoch ≈ 10-25 My that's
            // ~2-5% per epoch. Active boundaries keep adding stress
            // faster than erosion removes, so they keep growing.
            // Inactive boundaries (plates separated) erode toward
            // hills then plains — the natural mountain lifecycle.
            // Negative orogeny (subduction trenches) also fills in
            // slowly via sediment deposition.
            // Erosion now lives in SphereFieldPhysics::applySurfaceErosionOnRaster.
#if defined(AOC_PHYSICS_ON_SPHEREFIELD)
            aoc::map::gen::stepSpherePhysicsEpoch(
                sphereField, plates, sphereBoundaryScratch,
                physicsRngState, MY_PER_EPOCH_P1);
#endif
        }
#if defined(AOC_PHYSICS_ON_SPHEREFIELD)
        // 2026-05-06: PHYSICS-FIRST P1. Hand the SphereField surface-
        // elevation snapshot to the HexGrid for the renderer/save path.
        // P1 only writes the snapshot; tile elevation is still consumed
        // from the legacy world-frame `orogeny[]` array. Phase 2 will
        // route tile elevation through bilinearSample on this snapshot.
        grid.setSphereFieldElevationSnapshot(sphereField.surfaceElevationM);
#endif

        // Per-hex-tile orogeny lookup. NO Voronoi: tile (col, row) →
        // Mollweide-inverse → lat/lon → SphereField peakSample for
        // mountain detection (peak captures narrow Andean / Himalayan
        // belts that bilinear averaging would dilute below threshold)
        // and bilinearSample of continentalFraction for the
        // continental-only mountain gate. Tiles with ocean SphereField
        // mass are clamped to a low orogeny tier to prevent oceanic
        // cells with random elevation noise registering as mountains.
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const float nx = static_cast<float>(col) / static_cast<float>(width);
                const float ny = static_cast<float>(row) / static_cast<float>(height);
                aoc::map::gen::MollweideInverseResult mw =
                    aoc::map::gen::projectionInverse(
                        config.projection, nx, ny);
                if (!mw.valid) { continue; }
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
                const float zM = sphereField.peakSample(
                    sphereField.surfaceElevationM,
                    mw.coord.latDeg, mw.coord.lonDeg,
                    hexHalfCells);
                float oroSampled = (zM > aoc::map::gen::MOUNTAIN_THRESHOLD_M)
                    ? 1.0f : 0.0f;
                const float frac = sphereField.bilinearSample(
                    sphereField.continentalFraction,
                    mw.coord.latDeg, mw.coord.lonDeg);
                if (frac < 0.5f && oroSampled > 0.10f) {
                    oroSampled = 0.10f;
                }
                orogeny[static_cast<std::size_t>(row * width + col)] = oroSampled;
            }
        }

    // World-frame elevation pass. NO Voronoi: tile (col, row) maps to
    // lat/lon via the user-selected projection; elevation comes from
    // the SphereField surfaceElevationM raster (authoritative state
    // produced by 3 Gy of mechanism physics — subduction trims,
    // ridges accrete, continents dock, Wilson cycles rift).
    //
    // Tiles outside the projection's valid range (Mollweide ellipse
    // corners, Mercator polar clip) get a deep ocean elevation so
    // the rendering still draws them as water. Output is a
    // percentile-rank map: ClimateBiome.cpp picks ocean / shore /
    // land tiers via Thresholds.cpp on a sorted view of this array,
    // so the absolute scale only needs to be monotonic.
    AOC_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const float nx = static_cast<float>(col) / static_cast<float>(width);
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const aoc::map::gen::MollweideInverseResult mw =
                aoc::map::gen::projectionInverse(
                    config.projection, nx, ny);
            float elev = -1.0f; // Out of projection range → ocean.
            if (mw.valid) {
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
                elev = zM / 5000.0f;
                (void)contFracHere;
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
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const float nx = static_cast<float>(col) / static_cast<float>(width);
            const float ny = static_cast<float>(row) / static_cast<float>(height);
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
        grid, config.mapType, effectiveWaterRatio,
        elevationMap, thresholds);
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
    // Ocean) — clears single-tile flecks and 2-3-tile crumbs along
    // continental shelves so coastlines read as proper edges instead
    // of confetti. Big continents and intentional island chains
    // (size ≥ threshold) survive.
    {
        // Island purge threshold scales with simulated age. Real ocean
        // plates carry hotspot island chains that get DRAGGED into
        // subduction trenches over geological time (Hawaii→Aleutian
        // recycle). Longer sims drown more small islands. Threshold
        // grows linearly with totalMy (12 at 1 Gy → 50 at 4 Gy).
        const int32_t totalMyForIslands = (config.tectonicTotalMy > 0)
            ? config.tectonicTotalMy
            : MapGenerator::DEFAULT_TECTONIC_TOTAL_MY;
        const int32_t MIN_ISLAND_SIZE = std::clamp(
            12 + totalMyForIslands / 200, 12, 50);
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
// 2026-05-03: generateRealisticTerrain (the LandWithSeas-only entry point)
// is dead code. LandWithSeas was removed; the Continents path runs through
// assignTerrain() above, which has its own tectonic-plate sim. Wrapped in
// `#if 0` rather than deleted in case the user wants to recover it later.
// The function uses Random& rng + HexGrid&; restoring requires un-commenting
// the case in MapType (see MapGenerator.hpp) and the call site in
// MapGenerator::generate (see line ~70).
// ============================================================================

// ============================================================================

// Resource-placement passes moved to src/map/gen/Resources.cpp.


} // namespace aoc::map

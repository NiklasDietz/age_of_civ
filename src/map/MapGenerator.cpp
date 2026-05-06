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
#include "aoc/map/gen/PlateBoundary.hpp"
#include "aoc/map/gen/PlatePhysics.hpp"
#include "aoc/map/gen/PlateIdStash.hpp"
#include "aoc/map/gen/SphereField.hpp"
#include "aoc/map/gen/SphereFieldPhysics.hpp"
#include "aoc/map/gen/PlateReference.hpp"
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
    auto logStage = [&t0](const char* name) {
        const auto now = PerfClock::now();
        const auto ms = std::chrono::duration_cast<
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
    // 2026-05-04: bumped 64 -> 96 (2.25x memory, ~1.5x finer per axis).
    // At 64 each plate-local cell covered ~1.3 hex tiles at 280-wide map
    // and bilinear render expanded that to 2-3 hex per cell -- single
    // boundary scatter became a 5+ hex mountain block. With 96 cells per
    // plate-local span, single scatter covers ~1 hex; mountain ranges
    // render as narrow chains rather than chunky blobs.
    const int32_t OROGENY_GRID = 96 * std::max(1, config.superSampleFactor);
    // 2026-05-06: PHYSICS-FIRST P2.3e -- OROGENY_HALF deleted (last
    // user was the scar/arc-walk stamp loops, now gone). OROGENY_GRID
    // kept only for the orogenyLocal.assign(GRID*GRID, 0) zero-inits
    // at plate construction; P4 deletes the field + this constant.
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
            const int32_t landCountTarget = (config.landPlateCount > 0)
                ? std::max(1, config.landPlateCount)
                : centerRng.nextInt(3, 5);
            const int32_t oceanCountTarget = centerRng.nextInt(3, 4);
            // Tighter land gap lets 7-14 seeds fit on the unit square.
            // 0.42 was fine for 3-4 plates but rejects half the targets
            // when landCountTarget ≥ 6.
            const float LAND_MIN_GAP  = std::max(0.18f,
                0.70f / static_cast<float>(landCountTarget + 1));
            constexpr float OCEAN_MIN_GAP = 0.09f;

            const auto pushPlate = [&](float cx, float cy, bool isLand) {
                Plate p;
                p.cx = cx;
                p.cy = cy;
                p.isLand = isLand;
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
                // Euler pole + angular velocity will be initialised
                // alongside the existing eulerPoleX/eulerPoleY block
                // below (Phase 2 wires them to actually drive motion).
                // 2026-05-04: log-normal speed buckets matching Earth's
                // real Phanerozoic distribution (Pacific 10 cm/yr,
                // Eurasian 2 cm/yr, Antarctic 0.1 cm/yr -- 100x range).
                // Old uniform [-0.70, 0.70] gave only ~10x speed
                // variance; analyze.py showed real Earth has 13700x
                // max/median ratio. Three buckets: 60% slow plates
                // (cratons + slow oceanic), 30% medium, 10% fast.
                {
                    const float speedRoll = centerRng.nextFloat(0.0f, 1.0f);
                    float vMax;
                    if (speedRoll < 0.60f)      { vMax = 0.10f; }
                    else if (speedRoll < 0.90f) { vMax = 0.30f; }
                    else                        { vMax = 0.80f; }
                    p.vx = centerRng.nextFloat(-vMax, vMax);
                    p.vy = centerRng.nextFloat(-vMax, vMax);
                }
                // 2026-05-04: removed radial outward bias hack (was
                // counteracting Wilson cycle assembly force which is
                // also being removed). With slab pull as primary
                // velocity driver and no centroid-pull, no need for
                // artificial outward bias.
                // Wide aspect range: real Earth plates are 1:1 (Pacific)
                // to 4:1 (Andean Plate). Old 0.95-1.10 → all near-circle
                // → all continents looked uniform. Now 0.50-2.20.
                p.aspect     = centerRng.nextFloat(0.50f, 2.20f);
                p.rot        = centerRng.nextFloat(-3.14159f, 3.14159f);
                p.baseRot    = p.rot;
                p.baseAspect = p.aspect;
                // Per-plate crust-mask seed (large random offsets so each
                // plate samples a unique noise neighborhood). Stays fixed
                // through the whole sim — pattern travels with the plate.
                p.seedX = centerRng.nextFloat(0.0f, 1000.0f);
                p.seedY = centerRng.nextFloat(0.0f, 1000.0f);
                // 2026-05-03: Euler-pole rotation. Pole is a random point
                // outside the unit square (radius 1.5..3.5 from origin so
                // the rotation arc across the map is gentle and roughly
                // straight, like real plates over geologic timescales).
                // angularRate magnitude ~0.012 rad/epoch -> ~0.7 deg per
                // epoch; combined with linear vx/vy this curves hotspot
                // trails and rotates plate boundaries instead of dragging
                // them as parallel lines. Sign random so plates rotate
                // both ways. Cratons (isLand) rotate slower since old
                // continental crust is more stable.
                {
                    const float poleAng    = centerRng.nextFloat(0.0f, 6.2832f);
                    const float poleRadius = centerRng.nextFloat(1.5f, 3.5f);
                    p.eulerPoleX  = 0.5f + std::cos(poleAng) * poleRadius;
                    p.eulerPoleY  = 0.5f + std::sin(poleAng) * poleRadius;
                    const float rateMag = isLand
                        ? centerRng.nextFloat(0.004f, 0.014f)
                        : centerRng.nextFloat(0.008f, 0.020f);
                    p.angularRate = (centerRng.nextFloat(0.0f, 1.0f) < 0.5f)
                        ? -rateMag : rateMag;
                }
                // 2026-05-05: SPHERE - random Euler pole on the unit
                // sphere + angular velocity in deg/epoch. Real Earth
                // plates: 0.05 to 1.0 deg/Ma; sim ~10 Ma per epoch
                // gives 0.5-10 deg/epoch. Continental plates rotate
                // slower (cratonic stability) -- 0.5-3 deg/epoch.
                // Oceanic 1-6 deg/epoch.
                {
                    const float poleLat = centerRng.nextFloat(-85.0f, 85.0f);
                    const float poleLon = centerRng.nextFloat(-180.0f, 180.0f);
                    p.eulerPoleLatDeg = poleLat;
                    p.eulerPoleLonDeg = poleLon;
                    // 2026-05-05 Phase 7 sub-step 7a: bump floors
                    // 0.5 -> 1.0 (land) and 1.0 -> 2.0 (ocean) so
                    // even slow-spawn plates have enough relative
                    // motion to produce measurable convergence.
                    // Stuck-zero-mountain seeds (99/256/31337) had
                    // active strain (0.16-0.20 rad) but apparently
                    // not enough sustained convergence in mountain-
                    // biome-eligible regions to surface in the
                    // world-frame orogeny[] field.
                    const float angVelMag = isLand
                        ? centerRng.nextFloat(1.0f, 3.0f)
                        : centerRng.nextFloat(2.0f, 6.0f);
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
                p.orogenyLocal.assign(
                    static_cast<std::size_t>(OROGENY_GRID * OROGENY_GRID), 0.0f);
                p.orogenyAgeLocal.assign(
                    static_cast<std::size_t>(OROGENY_GRID * OROGENY_GRID),
                    static_cast<int16_t>(0));
                // Earth plate-size distribution: a couple of giants
                // (Pacific 1.5, Eurasian 1.3, African 1.2 — relative
                // to the median). Most plates ~1.0. Some small minor
                // plates 0.6-0.8. We sample from a skewed range so
                // generated worlds get a similar size mix.
                const float wRoll = centerRng.nextFloat(0.0f, 1.0f);
                if (wRoll < 0.15f)      { p.weight = centerRng.nextFloat(1.30f, 1.55f); } // giant
                else if (wRoll < 0.55f) { p.weight = centerRng.nextFloat(0.95f, 1.20f); } // medium
                else                    { p.weight = centerRng.nextFloat(0.60f, 0.90f); } // small
                // Crust area proxy: weight² (Voronoi cell area scales
                // with weight). Stored both as initial reference and
                // current value; subduction debits the current value.
                p.crustArea = p.weight * p.weight;
                p.crustAreaInitial = p.crustArea;
                // Initial crust age: continental plates start old (cratons
                // = Archean, billions of years), oceanic plates start
                // moderately aged (random 0-50 epochs equivalent).
                // Cratons get high age so their slab-pull contribution
                // is suppressed (they shouldn't subduct themselves).
                p.crustAge = isLand
                    ? centerRng.nextFloat(60.0f, 120.0f)
                    : centerRng.nextFloat(5.0f, 40.0f);
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
                // the user reported. Real Earth plates ARE irregular
                // (Eurasian arms, African L) but each plate is a
                // CONTIGUOUS region. Few extras at small offset keeps
                // territory contiguous while preserving the irregular
                // outline. Plates that get 0 extras render as clean
                // single-blob Voronoi cells.
                const int32_t extras = centerRng.nextInt(0, 2);
                const bool cylC = (config.topology == MapTopology::Cylindrical);
                for (int32_t e = 0; e < extras; ++e) {
                    const float ang = centerRng.nextFloat(0.0f, 6.2832f);
                    const float off = centerRng.nextFloat(0.04f, 0.10f) * p.weight;
                    float sx = cx + std::cos(ang) * off;
                    float sy = cy + std::sin(ang) * off;
                    if (cylC) {
                        if (sx < 0.0f) { sx += 1.0f; }
                        if (sx > 1.0f) { sx -= 1.0f; }
                    } else {
                        sx = std::clamp(sx, 0.05f, 0.95f);
                    }
                    sy = std::clamp(sy, 0.05f, 0.95f);
                    p.extraSeeds.emplace_back(sx, sy);
                }
                plates.push_back(p);
                aoc::map::gen::initialisePlatePhysicsGrid(
                    plates.back(), config.seed);
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

            const int32_t bands = std::max(2, std::min(landCountTarget, 7));
            int32_t landPlaced = 0;
            int32_t attempts = 0;
            // Force land plate centres into the INTERIOR latitude band
            // (0.22 - 0.78). Combined with forced ocean plates at the
            // polar bands below, this lets continents form bounded
            // landmasses (Australia / Americas style) rather than
            // always extending all the way to the top or bottom edge.
            constexpr float LAND_LAT_LO = 0.22f;
            constexpr float LAND_LAT_HI = 0.78f;
            const float landLatRange = LAND_LAT_HI - LAND_LAT_LO;
            for (int32_t b = 0; b < bands && landPlaced < landCountTarget; ++b) {
                const float bandLo = LAND_LAT_LO
                    + (landLatRange / static_cast<float>(bands)) * static_cast<float>(b);
                const float bandHi = LAND_LAT_LO
                    + (landLatRange / static_cast<float>(bands)) * static_cast<float>(b + 1);
                int32_t bandAttempts = 0;
                while (bandAttempts < 200) {
                    ++bandAttempts;
                    ++attempts;
                    const float cx = centerRng.nextFloat(xLo, xHi);
                    const float cy = centerRng.nextFloat(bandLo, bandHi);
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
                    break;
                }
            }
            // If stratified pass left slots open, top up with general
            // rejection-sampled placements anywhere INSIDE the land
            // latitude band (no spillover into polar bands).
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
            // Two GIANT polar plates (Arctic + Antarctic analogues).
            // Each is a single high-weight plate spanning its polar
            // band rather than 3 small plates. With weight 1.6 + extra
            // seeds spaced across the map width, these dominate the
            // top/bottom strip → Antarctica-style continuous polar
            // ocean. Marked isPolar so they barely drift.
            const auto pushPolarPlate = [&](float cy0) {
                Plate p;
                p.cx = 0.5f;
                p.cy = cy0;
                p.isLand = false;
                p.vx = centerRng.nextFloat(-0.10f, 0.10f);
                p.vy = centerRng.nextFloat(-0.05f, 0.05f);
                p.aspect = 1.0f;
                p.rot    = 0.0f;
                p.baseRot = p.rot;
                p.baseAspect = p.aspect;
                p.seedX = centerRng.nextFloat(0.0f, 1000.0f);
                p.seedY = centerRng.nextFloat(0.0f, 1000.0f);
                // 2026-05-04: matched ocean-plate landFraction reduction
                // (0.02-0.08 -> 0.005-0.02). Polar-band oceanic plates
                // would otherwise leak land tiles into mid-latitude
                // continents.
                p.landFraction = centerRng.nextFloat(0.005f, 0.02f);
                p.orogenyLocal.assign(
                    static_cast<std::size_t>(OROGENY_GRID * OROGENY_GRID), 0.0f);
                p.orogenyAgeLocal.assign(
                    static_cast<std::size_t>(OROGENY_GRID * OROGENY_GRID),
                    static_cast<int16_t>(0));
                // Modest weight + many extra seeds = thin band coverage.
                // weight 1.15 lets normal mid-lat plates still claim
                // their territories, while extra seeds keep the polar
                // strip continuous across the map width.
                p.weight = 1.15f;
                p.crustArea = p.weight * p.weight;
                p.crustAreaInitial = p.crustArea;
                p.crustAge = centerRng.nextFloat(80.0f, 160.0f); // polar = old, stable
                p.isPolar = true;
                // Polar plates: tiny Euler rotation only (cap-like).
                {
                    const float poleAng    = centerRng.nextFloat(0.0f, 6.2832f);
                    const float poleRadius = centerRng.nextFloat(2.0f, 4.0f);
                    p.eulerPoleX  = 0.5f + std::cos(poleAng) * poleRadius;
                    p.eulerPoleY  = 0.5f + std::sin(poleAng) * poleRadius;
                    const float rateMag = centerRng.nextFloat(0.001f, 0.004f);
                    p.angularRate = (centerRng.nextFloat(0.0f, 1.0f) < 0.5f)
                        ? -rateMag : rateMag;
                }
                p.extraSeeds.emplace_back(0.15f, cy0);
                p.extraSeeds.emplace_back(0.35f, cy0);
                p.extraSeeds.emplace_back(0.65f, cy0);
                p.extraSeeds.emplace_back(0.85f, cy0);
                plates.push_back(p);
                aoc::map::gen::initialisePlatePhysicsGrid(
                    plates.back(), config.seed);
            };
            pushPolarPlate(centerRng.nextFloat(0.03f, 0.10f));      // Arctic
            pushPolarPlate(centerRng.nextFloat(0.90f, 0.97f));      // Antarctic
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
            // 2026-05-04: Pacific-analog giant ocean plate. Earth's
            // Pacific Plate covers ~30 % of the surface alone --
            // 2-3x larger than the next biggest plate (Antarctic) and
            // ~5x the smallest majors. Without an explicit giant the
            // sim's plates are all roughly similar in size, producing
            // a fragmented Voronoi map rather than the giant-Pacific
            // + few-medium-continents pattern of real Earth. Pick one
            // ocean plate (skipping forced polar) and triple its
            // weight; Voronoi cell area scales with weight^2 so this
            // makes its territory ~9x larger than nominal -- roughly
            // matching the Pacific's share.
            {
                std::vector<std::size_t> oceanIdx;
                for (std::size_t i = 0; i < plates.size(); ++i) {
                    if (!plates[i].isLand && !plates[i].isPolar) {
                        oceanIdx.push_back(i);
                    }
                }
                if (!oceanIdx.empty()) {
                    const std::size_t pick = oceanIdx[
                        static_cast<std::size_t>(centerRng.nextInt(
                            0, static_cast<int32_t>(oceanIdx.size()) - 1))];
                    plates[pick].weight = centerRng.nextFloat(2.5f, 3.5f);
                    plates[pick].crustArea = plates[pick].weight
                                           * plates[pick].weight;
                    plates[pick].crustAreaInitial = plates[pick].crustArea;
                }
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
                const int32_t hotspotTarget = centerRng.nextInt(5, 8);
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
                        if (!p.isLand) { continue; }
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
    // 2026-05-06: PHYSICS-FIRST P4.1 -- sutureSeams record + SutureSeam
    // alias deleted. Polygon merger events no longer fire (P3.3); record
    // would stay empty. Post-sim ophiolite pass also deleted (P4.1).
    // 2026-05-04: spatial-hash index over plate AABBs. Declared here so
    // both the in-loop orogeny-scatter pass and the post-loop elevation
    // pass can share it (latter lives outside the tectonic-sim block).
    // Rebuilt every time AABBs are refreshed (per-epoch Phase C, plus
    // the intermediate Phase B clipping recompute, plus the final
    // post-loop state).
    // 2026-05-06: PHYSICS-FIRST P3.2 -- polySpatialIndex object +
    // rebuildPolygonSpatialIndex lambda deleted. Last consumer was the
    // polygon-PIP ownership override in the side-correctness pass
    // (also deleted).
    if (config.mapType == MapType::Continents && !plates.empty()) {
        // Multi-cycle plate-tectonic sim. EPOCHS scales the simulated
        // geological age — more epochs = more cycles of drift, collide,
        // rift, drift-back. Earth's history has ~4 supercontinent cycles
        // (Rodinia → Pannotia → Pangaea + present + projected); we
        // approximate by triggering a global-rift event every CYCLE
        // epochs which scatters land plates outward. Erosion intensity
        // scales with EPOCHS so older simulated worlds have softer
        // mountains.
        const int32_t requestedEpochs = (config.tectonicEpochs > 0)
            ? std::max(3, config.tectonicEpochs)
            : std::max(6, static_cast<int32_t>(centerRng.nextInt(10, 18)));
        // Stepper hook: scrubber callers pass runEpochsLimit to halt
        // the sim mid-flight at a specific epoch and view that state.
        const int32_t EPOCHS = (config.runEpochsLimit > 0)
            ? std::min(requestedEpochs, config.runEpochsLimit)
            : requestedEpochs;
        // 2026-05-04: cycle bumped 6 -> 10. With CYCLE=6 and 40-epoch
        // game-default sims, ~6 rift events fired per sim; even though
        // children are now oceanic they still consume Voronoi cells
        // (fragmenting the map). Earth has roughly 1 rift event per
        // 100 My; over a 40-epoch (~400 My) sim that's ~4 events.
        // CYCLE=10 yields ~4 rifts -- closer to Earth-historical rate.
        const int32_t CYCLE = 10;
        // DT derived from EPOCHS and the user-configurable total drift
        // budget. Default drift = 0.6 map widths. Larger drift = bigger
        // plate motion = more dramatic continental shuffle. Smaller =
        // plates barely move, fine-grained evolution at small scale.
        const float driftFrac = (config.driftFraction > 0.0f)
            ? config.driftFraction : 0.6f;
        const float DT = std::clamp(
            (driftFrac / 0.7f) / static_cast<float>(EPOCHS),
            0.001f, 0.040f);
        // 2026-05-04: wilsonPhaseOffset removed along with Wilson cycle
        // assembly force. Cycle now emerges from slab pull + rifts.

        // 2026-05-04: polygon construction LIFTED to post-sim (after
        // epoch loop) so vertices reflect FINAL plate positions, not
        // initial. Vertices stored in plate-local frame so consumers
        // that apply (cx, cy, rot) get correct world coords.

        // Snapshot starting positions so we can restore the final ones
        // for the rendering pass below — stress accumulates across the
        // whole sim regardless of which positions render.
        const std::vector<Plate> startPlates = plates;

        // Bilinear sample of a plate's local orogeny grid. Lifted above
        // the epoch loop on 2026-05-04 so the slab-pull / Wilson-cycle
        // scan can read live per-plate stress (the world-frame `orogeny[]`
        // stays zero during the loop -- it's only rebuilt post-loop in
        // the elevation pass). Without this lift, slab pull and Wilson
        // crust accounting were dead code reading an always-zero array.
        // 2026-05-06: PHYSICS-FIRST P2.3d-1 -- samplePlateOrogeny lambda
        // deleted. Last reader (slab-pull / Wilson scan) removed
        // simultaneously. orogenyLocal field stays alive (P4 deletes
        // atomically with all 25 OLD Plate fields).

        // 2026-05-04: rift-burst scheduling. Earth's Phanerozoic shows
        // 4 major reorganization bursts (Pangaea breakup at 250 Ma:
        // 290 plate births in 5 Ma; smaller bursts at 100, 65, 50 Ma).
        // Old `epoch % CYCLE == 0` fired uniformly. We schedule
        // bursts: each burst spawns 2-3 rifts within 1-2 epochs, then
        // waits 12-25 epochs for the next. Plus a global plate
        // reorganization event randomly placed mid-sim.
        std::vector<int32_t> riftBurstEpochs;
        {
            int32_t e = centerRng.nextInt(3, 8);
            while (e < EPOCHS) {
                riftBurstEpochs.push_back(e);
                e += centerRng.nextInt(12, 25);
            }
        }
        const int32_t reorgEpoch = static_cast<int32_t>(
            static_cast<float>(EPOCHS) * centerRng.nextFloat(0.30f, 0.70f));

        // 2026-05-06: PHYSICS-FIRST P3.4 -- rebuildPolygonsFromVoronoi
        // + recomputePolygonAABBs lambdas + initial-setup callers
        // deleted (~184 LOC). Plate ownership is pure haversine via
        // PlateIdStash (P3.1); polygon construction has no consumer.
        // boundaryVertices/polygonMin/Max remain default-zero on Plate
        // until P4 deletes the fields atomically with other 24 OLD
        // Voronoi-era Plate fields.

        // 2026-05-06: PHYSICS-FIRST P2.2. SphereField allocation is now
        // unconditional -- the world-frame elevation pass downstream
        // sources tile elevation from this raster via bilinearSample.
        // The flag AOC_PHYSICS_ON_SPHEREFIELD now only gates the per-
        // epoch step + snapshot setter (default ON since P2.1).
        // Seeding: each cell inherits continental fraction from the
        // nearest Bird (2003) reference plate at the cell's lat/lon
        // (Continental=1, Mixed=0.5, Oceanic=0). Crust thickness is the
        // linear blend of initial continental/oceanic thickness.
        aoc::map::gen::SphereField sphereField;
        sphereField.resize();
        std::vector<uint8_t> sphereBoundaryScratch;
        for (int32_t latIdx = 0; latIdx < aoc::map::gen::SphereField::LAT_CELLS; ++latIdx) {
            for (int32_t lonIdx = 0; lonIdx < aoc::map::gen::SphereField::LON_CELLS; ++lonIdx) {
                const aoc::map::gen::LatLon p =
                    aoc::map::gen::SphereField::cellCenter(lonIdx, latIdx);
                const aoc::map::gen::PlateCompositionType t =
                    aoc::map::gen::classifyByNearestReference(p);
                float frac = 0.0f;
                switch (t) {
                    case aoc::map::gen::PlateCompositionType::Continental: frac = 1.0f; break;
                    case aoc::map::gen::PlateCompositionType::Mixed:       frac = 0.5f; break;
                    case aoc::map::gen::PlateCompositionType::Oceanic:     frac = 0.0f; break;
                }
                const std::size_t idx =
                    aoc::map::gen::SphereField::cellIndex(lonIdx, latIdx);
                sphereField.continentalFraction[idx] = frac;
                sphereField.crustThicknessKm[idx] =
                      frac  * aoc::map::gen::PhysicsConstants::initialContinentalThicknessKm
                    + (1.0f - frac) * aoc::map::gen::PhysicsConstants::initialOceanicThicknessKm;
            }
        }
        aoc::map::gen::recomputeIsostaticElevationOnRaster(sphereField);
        const float MY_PER_EPOCH_P1 = 250.0f / static_cast<float>(EPOCHS);

        for (int32_t epoch = 0; epoch < EPOCHS; ++epoch) {
            // 2026-05-04: plate-pair velocity coupling REMOVED. Old
            // code averaged each plate's velocity 3 % toward its two
            // nearest non-polar neighbours. This was an artificial
            // smoothing that homogenised plate motion and was an
            // attempt to fake mantle convection coupling. Real plates
            // interact at boundaries (collision dynamics, ridge push)
            // not via free-space velocity averaging.

            // 2026-05-04: mantle-flow bias field REMOVED. Old code
            // added a 5%/epoch sin/cos pattern to all non-polar plate
            // velocities -- abstract substitute for real mantle
            // convection. Convection effects now come from slab pull
            // (which biases plates toward subduction) and the natural
            // ridge-push from divergent boundaries.

            // Advance every plate by (vx, vy) * DT. Cylindrical maps WRAP
            // around the X axis (no east/west edge — like a globe band);
            // flat maps BOUNCE so plates stay on the rectangle.
            // Y always bounces (poles aren't wrap-connected).
            for (Plate& p : plates) {
                // Polar plates barely drift — Antarctica is essentially
                // stationary on geological timescales. Apply 0.3× DT
                // to keep them anchored at the poles.
                const float motionScale = p.isPolar ? 0.3f : 1.0f;
                // 2026-05-05: SPHERE MIGRATION - motion is now Euler-
                // pole rotation on the sphere. Each plate rotates
                // around (eulerPoleLatDeg, eulerPoleLonDeg) by
                // (angularVelDeg * DT * motionScale) per epoch.
                // Legacy (cx, cy) re-projected via Mollweide forward
                // each epoch so 2D-only consumer code (Voronoi,
                // tile-id stash etc) still works during migration.
                if (p.eulerPoleLatDeg != 0.0f
                    || p.eulerPoleLonDeg != 0.0f
                    || p.angularVelDeg != 0.0f) {
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
                    // 2026-05-05: PHASE 3 - propagate the Euler rotation's
                    // local-vertical component into p.rot so polygon
                    // vertices, extraSeeds, and hotspot trails (all stored
                    // in plate-local 2D frame and rotated by p.rot at
                    // consumer sites) ride with the plate's true on-sphere
                    // orientation. The local-vertical projection equals
                    // stepRad * cos(haversine_to_pole) -- the component of
                    // the Euler rotation vector aligned with the plate
                    // centre's local up-axis. Angular distance to pole
                    // close to 0 (rotating around itself) -> full rate;
                    // 90 deg away (orthogonal pole) -> zero spin
                    // contribution, only translation.
                    const float angDistRad =
                        aoc::map::gen::haversineRadians(next, pole);
                    p.rot += stepDeg * 0.01745329252f
                        * std::cos(angDistRad);
                    // 2026-05-05: FULL SPHERE SWITCH attempt - tried setting
                    // vx/vy = eulerVelocityAt(plate, pole, angVelDeg). Each
                    // epoch the legacy heuristic mutations (slab pull,
                    // collision bounce, rift impulse) that write to vx/vy
                    // were getting overwritten by the recomputed Euler
                    // velocity, removing all dynamic perturbation. Audit
                    // showed continent diversity collapsing (mean continents
                    // 8 -> 5.5, largest/total 0.575 -> 0.733, single-
                    // continent worst case). Reverted -- vx/vy stays as the
                    // perturbable dynamics field; Euler-pole rotation drives
                    // POSITION but velocity dynamics retain their heuristic
                    // mutations. To do a proper migration we would need to
                    // split vx/vy into base + perturbation and route every
                    // mutation through the perturbation channel; that is
                    // multi-day surgery across ~70 sites and is deferred.
                } else {
                    // Pre-Euler-pole-init plate (legacy linear motion).
                    // Will be migrated when initialization sets pole +
                    // angularVel from the existing log-normal speed
                    // bucket.
                    p.cx += p.vx * DT * motionScale;
                    p.cy += p.vy * DT * motionScale;
                }
                // 2026-05-03: Euler-pole rotation. After linear drift,
                // rotate the plate (cx,cy + extraSeeds + hotspot trails)
                // by `angularRate * DT * motionScale` around its pole.
                // Combined with linear translation this curves trajectories
                // -- straight (vx,vy) drift produces unrealistically
                // parallel margins and dead-straight hotspot trails. Real
                // plates rotate as well as translate; the Hawaii-Emperor
                // bend is the canonical signature of this on Earth.
                {
                    const float dTheta = p.angularRate * DT * motionScale;
                    if (std::fabs(dTheta) > 1e-7f) {
                        const float cs = std::cos(dTheta);
                        const float sn = std::sin(dTheta);
                        auto rotateAroundPole = [&](float& x, float& y) {
                            float dx = x - p.eulerPoleX;
                            float dy = y - p.eulerPoleY;
                            const float nx = dx * cs - dy * sn;
                            const float ny = dx * sn + dy * cs;
                            x = p.eulerPoleX + nx;
                            y = p.eulerPoleY + ny;
                        };
                        rotateAroundPole(p.cx, p.cy);
                        for (std::pair<float, float>& es : p.extraSeeds) {
                            rotateAroundPole(es.first, es.second);
                        }
                        for (std::pair<float, float>& hs : p.hotspotTrail) {
                            rotateAroundPole(hs.first, hs.second);
                        }
                        // Also rotate plate's local-frame orientation so
                        // crust-mask noise sample frame tracks the spin.
                        p.rot += dTheta;
                    }
                }
                if (cylSim) {
                    if (p.cx < 0.0f) { p.cx += 1.0f; }
                    if (p.cx > 1.0f) { p.cx -= 1.0f; }
                } else {
                    if (p.cx < 0.05f) { p.cx = 0.05f; p.vx = -p.vx; }
                    if (p.cx > 0.95f) { p.cx = 0.95f; p.vx = -p.vx; }
                }
                if (p.cy < 0.05f) { p.cy = 0.05f; p.vy = -p.vy; }
                if (p.cy > 0.95f) { p.cy = 0.95f; p.vy = -p.vy; }
                ++p.ageEpochs;
                // 2026-05-04: oceanic wedge accretion. Each epoch the
                // post-rift plate adds new oceanic crust at its trailing
                // edge facing the rift line. Earth Atlantic: widens
                // ~2 cm/yr -> over 100 My ridge accretes ~2000 km of
                // crust on each side. We grow the wedge proportionally
                // to plate motion magnitude so fast-drifting plates
                // open wider basins. Cap at 0.40 plate-local units to
                // prevent the wedge eating the whole plate.
                if (p.oceanWedgeWidth > 0.0f && p.oceanWedgeWidth < 0.40f) {
                    const float vMag = std::sqrt(p.vx * p.vx + p.vy * p.vy);
                    // 2026-05-04: WP4 - STAGED CONTINENTAL RIFTING.
                    // Real rifts evolve through 4 stages over ~50-100
                    // My: (1) thermal bulge before crust ruptures, (2)
                    // graben / narrow rift valley (East African Rift
                    // today), (3) narrow sea (Red Sea, Gulf of
                    // California), (4) wide open ocean (Atlantic).
                    // Wedge growth rate is now stage-dependent:
                    //   Stage 1 bulge (width < 0.04): SLOW growth
                    //     (0.05x normal) -- continent thinning, no
                    //     ocean yet.
                    //   Stage 2 graben (0.04..0.10): MODERATE (0.4x).
                    //   Stage 3 narrow sea (0.10..0.20): FAST (0.8x).
                    //   Stage 4 ocean (0.20+): FULL rate (1.0x).
                    // Total time-to-ocean ~ 30-40 epochs at typical
                    // plate motion -- matches real Atlantic timescale.
                    float stageMult = 1.0f;
                    if (p.oceanWedgeWidth < 0.04f) {
                        stageMult = 0.05f;
                    } else if (p.oceanWedgeWidth < 0.10f) {
                        stageMult = 0.40f;
                    } else if (p.oceanWedgeWidth < 0.20f) {
                        stageMult = 0.80f;
                    }
                    p.oceanWedgeWidth = std::min(0.40f,
                        p.oceanWedgeWidth + vMag * DT * 0.40f * stageMult);
                }
                // 2026-05-04: STICK-SLIP intra-plate stress release.
                // Plate accumulates stress proportional to its current
                // speed (proxy for boundary-friction loading). When
                // accumulator crosses threshold, plate releases stress
                // as a small velocity perturbation (sudden motion =
                // earthquake-cluster analog) and resets accumulator.
                p.stressAccum += std::sqrt(p.vx * p.vx + p.vy * p.vy)
                                * DT * 0.5f;
                if (p.stressAccum > 0.20f) {
                    const float dirAng = centerRng.nextFloat(0.0f, 6.2832f);
                    const float kick = p.stressAccum * 0.15f;
                    p.vx += std::cos(dirAng) * kick;
                    p.vy += std::sin(dirAng) * kick;
                    p.stressAccum = 0.0f;
                }
            }

            // 2026-05-04: GLOBAL PLATE REORGANIZATION event. Earth
            // experiences periodic mantle-driven reorganizations every
            // ~100 Ma where plate velocities globally reset (e.g. the
            // 50-Ma Hawaii-Emperor bend records one). Sim plates set
            // their initial velocity once and never re-randomize.
            // Fires once per sim at a random mid-sim epoch: each non-
            // polar plate gets a fresh velocity (drawn from the same
            // log-normal speed buckets as init) plus a 50 % magnitude
            // damping (post-reorg slowdown is observed in real plate
            // models).
            if (epoch == reorgEpoch) {
                for (Plate& p : plates) {
                    if (p.isPolar) { continue; }
                    const float speedRoll =
                        centerRng.nextFloat(0.0f, 1.0f);
                    float vMax;
                    if (speedRoll < 0.60f)      { vMax = 0.10f; }
                    else if (speedRoll < 0.90f) { vMax = 0.30f; }
                    else                        { vMax = 0.80f; }
                    p.vx = centerRng.nextFloat(-vMax, vMax) * 0.5f;
                    p.vy = centerRng.nextFloat(-vMax, vMax) * 0.5f;
                }
            }

            // 2026-05-04: WILSON CYCLE force REMOVED. Old code applied
            // a periodic centroid-attractive/repulsive pulse to all
            // land plates, simulating supercontinent assembly /
            // dispersal. This was an abstract hack; real Wilson cycles
            // emerge naturally from slab pull + ridge push + mantle
            // convection patterns -- they don't require an explicit
            // centroid force. Slab pull (computed below per-plate
            // from accumulated subduction stress) drives plates
            // toward subduction zones, producing convergence; ridge
            // push at divergent boundaries opens rifts. The two
            // forces alternate dominance over geological timescales,
            // producing the cycle organically.

            // (rotation/aspect drift moved further down — seeded by a
            // SEPARATE RNG independent of epoch count so scrubber
            // positions render the same per-plate state and don't
            // jump randomly between adjacent epochs.)

            // POLAR WANDERING. Real Earth: the entire plate-mantle
            // system slowly rotates relative to the rotational axis
            // (~10° over 500 My). Every plate's centre rotates by a
            // tiny angle per epoch about the map's central pole. Net
            // effect over a long sim: continents shift around the
            // map even without plate-relative motion, mimicking the
            // True Polar Wander of Earth's history.
            {
                constexpr float POLE_X = 0.5f;
                constexpr float POLE_Y = 0.5f;
                // ~0.05° per epoch — yields ~2° over 40 epochs,
                // ~10° over 200 epochs (matches Earth-scale TPW).
                constexpr float POLE_RAD = 0.00087f;
                const float cw = std::cos(POLE_RAD);
                const float sw = std::sin(POLE_RAD);
                for (Plate& p : plates) {
                    const float rx = p.cx - POLE_X;
                    const float ry = p.cy - POLE_Y;
                    p.cx = POLE_X + rx * cw - ry * sw;
                    p.cy = POLE_Y + rx * sw + ry * cw;
                    if (cylSim) {
                        if (p.cx < 0.0f) { p.cx += 1.0f; }
                        if (p.cx > 1.0f) { p.cx -= 1.0f; }
                    } else {
                        p.cx = std::clamp(p.cx, 0.05f, 0.95f);
                    }
                    p.cy = std::clamp(p.cy, 0.05f, 0.95f);
                }
            }

            // 2026-05-05 PHASE 2: convergence-strain accumulation +
            // crust thickening on each plate's PhysicsGrid. dtMy ~ 10
            // matches the legacy "sim ~10 Ma per epoch" cadence; will
            // become a derived dt = totalMy / EPOCHS once Phase 6
            // wires real Myr scaling. Mountains emerge from sustained
            // convergence: closingRate * dtMy accumulates strain;
            // strain * 90 km/rad converts to crust thickness; Airy
            // isostasy lifts the surface.
            {
                // 2026-05-05 Phase 2.5: dropped MY_PER_EPOCH 10 -> 1.
                // angularVelDeg is per-epoch (deg/epoch); eulerVelocityAt
                // returns rad/per-epoch matching that input. Multiplying
                // by dtMy=10 was treating epochs as 10 My each which
                // double-counts -- the velocities already are per-epoch
                // rates. dtMy=1 means strain += closingRate * 1 epoch
                // per epoch step. Net effect: ~10x lower strain
                // accumulation -> mountain rate target 6-7 %.
                constexpr float MY_PER_EPOCH = 1.0f;
                aoc::map::gen::accumulateConvergenceStrain(
                    plates, MY_PER_EPOCH);
                for (Plate& p : plates) {
                    aoc::map::gen::thickenCrustFromStrain(
                        p.grid, MY_PER_EPOCH);
                    aoc::map::gen::applySurfaceErosion(
                        p.grid, MY_PER_EPOCH);
                }

                // 2026-05-05 Path X: per-plate flexure + sediment
                // routing + compaction were ripped. Their
                // infrastructure produced no observable effect at
                // 140x90 game grid (max|w| 12-50 m; sediment cap
                // bandaid required for stability). Eroded mass is
                // now simply discarded -- mountain crust thins, no
                // routing or compaction. Pipeline reduced to:
                // accumulateConvergenceStrain -> thickenCrustFromStrain
                // -> applySurfaceErosion -> Airy isostasy.
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
                    // 2026-05-04: tightened merge gates to combat Pangaea
                    // bias. Old MERGE_DIST=0.10 + SLOW_V=0.08 fired
                    // mergers aggressively because Wilson + plate-pair
                    // coupling damped relative velocity below 0.08 within
                    // a few epochs of contact, fusing every adjacent
                    // pair. Halving both thresholds requires plates to be
                    // physically much closer AND moving very slowly
                    // relative to each other before fusing -- matches
                    // real Earth where India-Eurasia have been in contact
                    // 50 My without fully merging. Audit shows this is
                    // the dominant lever for landmass-count distribution.
                    // 2026-05-04: relaxed gates (0.05 -> 0.07, 0.04 -> 0.06).
                    // Earth Phanerozoic median plate lifetime is 250 Ma
                    // (~half of plates die per supercontinent cycle);
                    // sim's tightened 0.05/0.04 fired ~0 mergers per
                    // 40-epoch sim. Looser gate combined with the new
                    // log-normal slow-plate bucket lets converging slow
                    // plates fuse, matching Earth's plate-recycle rate.
                    constexpr float MERGE_DIST   = 0.07f;
                    constexpr float CONTACT_DIST = 0.16f;
                    constexpr float SLOW_V       = 0.06f;
                    constexpr float COLLISION_DAMP = 0.93f; // 7% energy loss/epoch in contact

                    bool inContact = false;
                    bool isClosing = false;
                    float relVx = 0.0f, relVy = 0.0f;
                    float closingRate = 0.0f;
                    // Collision dynamics fire for ALL plate-pair types
                    // (cont-cont, cont-ocean, ocean-ocean). Earlier code
                    // restricted this to cont-cont, leaving oceanic
                    // plates with no momentum exchange — they passed
                    // through each other unphysically. Now every pair
                    // closing at < CONTACT_DIST decelerates along the
                    // collision axis.
                    if (d < CONTACT_DIST) {
                        relVx = plates[a].vx - plates[b].vx;
                        relVy = plates[a].vy - plates[b].vy;
                        closingRate = (relVx * dx + relVy * dy)
                                    / std::max(0.0001f, d);
                        isClosing = (closingRate < -0.02f);
                        inContact = isClosing;
                    }

                    // RIDGE PUSH. At a divergent (spreading) boundary,
                    // the elevated ridge axis exerts a gravitational
                    // sliding force on adjacent oceanic crust → both
                    // plates accelerate AWAY from the ridge. Earth's
                    // ridge push contributes ~25 % of plate motion energy
                    // (slab pull dominates at ~50 %, drag ~25 %). Without
                    // this, divergent margins are passive — plates only
                    // separate via initial velocity. Apply when plates
                    // are within RIDGE_DIST and DIVERGING (positive
                    // closing rate = retreat).
                    constexpr float RIDGE_DIST = 0.18f;
                    constexpr float RIDGE_PUSH_GAIN = 0.0040f;
                    if (d < RIDGE_DIST && d > 0.001f) {
                        const float relVxR = plates[a].vx - plates[b].vx;
                        const float relVyR = plates[a].vy - plates[b].vy;
                        const float divRate = (relVxR * dx + relVyR * dy)
                                            / std::max(0.0001f, d);
                        if (divRate > 0.02f) {
                            const float dxn = dx / d;
                            const float dyn = dy / d;
                            // Distance falloff: stronger when closer to
                            // the ridge. Ridges with small separation
                            // (just-rifted) push harder than mature ones.
                            const float falloff = (1.0f - d / RIDGE_DIST);
                            const float k = RIDGE_PUSH_GAIN * falloff;
                            // a is on the +dxn side → push +; b on -dxn
                            // side → push -. Both plates accelerate
                            // AWAY from each other.
                            plates[a].vx += dxn * k;
                            plates[a].vy += dyn * k;
                            plates[b].vx -= dxn * k;
                            plates[b].vy -= dyn * k;
                        }
                    }

                    // STAGE 2: in-contact, decelerate plates along the
                    // collision axis. Apply equal+opposite impulse so
                    // total momentum conserved (wallop both plates).
                    if (inContact) {
                        const float dxn = dx / std::max(0.0001f, d);
                        const float dyn = dy / std::max(0.0001f, d);
                        const float relVn = relVx * dxn + relVy * dyn;
                        const float dampedRelVn = relVn * COLLISION_DAMP;
                        const float deltaVn = dampedRelVn - relVn;
                        plates[a].vx += deltaVn * dxn * 0.5f;
                        plates[a].vy += deltaVn * dyn * 0.5f;
                        plates[b].vx -= deltaVn * dxn * 0.5f;
                        plates[b].vy -= deltaVn * dyn * 0.5f;

                        // TECTONIC ESCAPE / LATERAL EXTRUSION. Real
                        // example: India crashing into Eurasia pushed
                        // Indochina sideways out of the way (~5 cm/yr
                        // SE motion of SE Asia). Apply a small lateral
                        // velocity nudge to nearby plates perpendicular
                        // to the collision axis, simulating crustal
                        // material being squeezed sideways.
                        const float perpX = -dyn;
                        const float perpY =  dxn;
                        for (std::size_t k = 0; k < plates.size(); ++k) {
                            if (k == a || k == b) { continue; }
                            float kdx = plates[k].cx
                                      - (plates[a].cx + plates[b].cx) * 0.5f;
                            float kdy = plates[k].cy
                                      - (plates[a].cy + plates[b].cy) * 0.5f;
                            if (cylSim) {
                                if (kdx > 0.5f) { kdx -= 1.0f; }
                                if (kdx < -0.5f) { kdx += 1.0f; }
                            }
                            const float kd = std::sqrt(kdx * kdx + kdy * kdy);
                            if (kd > 0.30f) { continue; }
                            // Sign chosen so plate is pushed away from
                            // collision center perpendicular to axis.
                            const float side = (perpX * kdx + perpY * kdy) > 0.0f
                                ? 1.0f : -1.0f;
                            const float strength = (1.0f - kd / 0.30f) * 0.004f;
                            plates[k].vx += perpX * side * strength;
                            plates[k].vy += perpY * side * strength;
                        }
                    }

                    const float relVMag = std::sqrt(relVx * relVx + relVy * relVy);
                    // 2026-05-04: WP2 - merger trigger via polygon-
                    // edge adjacency. In addition to center-distance,
                    // accept merger if plate A has a polygon edge
                    // with neighbor=B classified as type 4 (collision
                    // suture). Edge-adjacency is a more physically
                    // grounded merge condition than center-distance --
                    // plates fuse at SHARED EDGES, not at proximity
                    // of their centroids. Center-distance retained as
                    // a fallback for when polygons haven't formed
                    // (e.g. just-spawned plates) or are degenerate.
                    bool sharedSutureEdge = false;
                    if (a < plates.size() && b < plates.size()) {
                        const Plate& pa = plates[a];
                        const std::size_t ne =
                            std::min(pa.boundaryEdgeTypes.size(),
                                     pa.boundaryNeighborIds.size());
                        for (std::size_t ei = 0; ei < ne; ++ei) {
                            if (pa.boundaryNeighborIds[ei] == b
                                && pa.boundaryEdgeTypes[ei] == 4u) {
                                sharedSutureEdge = true;
                                break;
                            }
                        }
                    }
                    // STAGE 3: full merge. Distance close + collision
                    // velocity nearly zero → plates have welded.
                    const bool readyToMerge = (plates[a].isLand && plates[b].isLand
                                                && (d < MERGE_DIST
                                                    || sharedSutureEdge)
                                                && relVMag < SLOW_V);
                    if (readyToMerge) {
                        // Continental collision: fuse plates.
                        //
                        // TERRANE ACCRETION. When B docks against A,
                        // its mountain belts (orogeny field) become
                        // part of A's geology — like the Cordilleran
                        // terranes glued onto western N America, or
                        // Avalonia onto eastern N America. Transfer
                        // 2026-05-05 Phase 12a: terrane-stamp pass into
                        // A.orogenyLocal ripped (orogenyLocal dead
                        // post-rewrite; PhysicsGrid.surfaceElevationM is
                        // sole source of orogeny). Suture seam record
                        // below preserved -- ophiolite mask still uses
                        // it.
                        // 2026-05-06: PHYSICS-FIRST P4.1 -- suture seam
                        // record push deleted. SutureSeam was driving
                        // post-sim ophiolite-mask + rock-type stamp
                        // along plate-merger fossil seams (also deleted).
                        plates[a].cx = (plates[a].cx + plates[b].cx) * 0.5f;
                        plates[a].cy = (plates[a].cy + plates[b].cy) * 0.5f;
                        plates[a].vx = (plates[a].vx + plates[b].vx) * 0.5f;
                        plates[a].vy = (plates[a].vy + plates[b].vy) * 0.5f;
                        plates[a].ageEpochs = std::max(
                            plates[a].ageEpochs, plates[b].ageEpochs);
                        // Plate-ID consolidation: merged plate must
                        // claim the COMBINED Voronoi territory, else
                        // surrounding plates rush in to fill B's vacated
                        // cell and the "merged" continent stays split
                        // across many IDs. Combine weights via area-sum
                        // (sqrt of squares) and take max landFraction so
                        // the merged plate's crust mask stays
                        // continental.
                        plates[a].weight = std::sqrt(
                            plates[a].weight * plates[a].weight
                            + plates[b].weight * plates[b].weight);
                        plates[a].landFraction = std::max(
                            plates[a].landFraction,
                            plates[b].landFraction);
                        // Merge crust accounting: areas combine, age
                        // takes the older value (cratonic basement of
                        // the merged continent dominates the mean age).
                        plates[a].crustArea
                            += plates[b].crustArea;
                        plates[a].crustAreaInitial
                            += plates[b].crustAreaInitial;
                        plates[a].crustAge = std::max(
                            plates[a].crustAge, plates[b].crustAge);
                        // Track merge participation for biogeographic-
                        // realm classification. Plates that never merge
                        // are isolated continents (Australia analog).
                        plates[a].mergesAbsorbed += 1
                            + plates[b].mergesAbsorbed;
                        // Inherit B's extra Voronoi seeds (translated
                        // into A's local frame) so A's cell takes on
                        // B's lobed shape — preserves the merged
                        // territory's geometry instead of collapsing
                        // back to a clean Voronoi blob.
                        for (const auto& es : plates[b].extraSeeds) {
                            plates[a].extraSeeds.push_back(es);
                        }

                        // SLAB BREAK-OFF. After continental collision
                        // shuts off subduction, the trailing oceanic
                        // slab detaches from the buoyant continental
                        // crust and sinks into the mantle. Surface
                        // rebounds upward as the slab's downward pull
                        // is removed (~+1 km uplift across Tibet-
                        // Himalaya, post-Indian collision). Add a
                        // broad +0.05 uplift across a wide area of the
                        // merged plate centred on the seam.
                        // 2026-05-05 Phase 12a: slab-rebound stamp into
                        // orogenyLocal ripped along with the terrane
                        // pass above; the field is dead.
                        plates.erase(plates.begin()
                            + static_cast<std::ptrdiff_t>(b));
                        --b;
                    }
                }
            }
            // Per-epoch plate deformation. Use DETERMINISTIC per-plate-
            // per-epoch noise (not centerRng) so scrubbing back/forth
            // through epoch N always shows the same plate state. Each
            // plate's rotation = base + sin(plate_seed + epoch*freq)
            // for a smooth slow oscillation. Magnitude tiny so plate-
            // local crust mask samples don't shift much per epoch.
            for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                Plate& p = plates[pi];
                const float phase = static_cast<float>(epoch) * 0.04f
                                  + p.seedX * 0.001f;
                // ±0.04 rad oscillation (~2.3°) on top of fixed baseRot.
                float rotOffset = std::sin(phase) * 0.04f;
                // CONTINENTAL BLOCK ROTATION. Small continental
                // fragments rotate independently of larger neighbours
                // (Iberia rotated ~35° during the Atlantic opening,
                // Italian peninsula rotated CCW during Africa-Eurasia
                // closure). Small plates (weight < 0.85) AND continental
                // (landFraction > 0.40) get an extra steady drift on
                // top of the standard oscillation.
                if (p.weight < 0.85f && p.landFraction > 0.40f) {
                    // 2026-05-04: was `epoch * 0.0025` which grew
                    // linearly to ±17° at epoch 120 -- far above the
                    // ~5°/My realistic max for microplates. Bounded
                    // sinusoid stays within ±0.20 rad regardless of
                    // epoch count.
                    const float blockDrift =
                        std::sin(static_cast<float>(epoch) * 0.05f
                                 + p.seedX * 0.007f) * 0.20f;
                    rotOffset += blockDrift;
                }
                p.rot = p.baseRot + rotOffset;
                // Aspect oscillates ±0.08 around baseAspect.
                // 2026-05-04: clamp range follows baseAspect (was hard
                // [0.7, 1.4] which pinned every plate inside that window
                // regardless of initial value 0.50-2.20). The most elongated
                // plates lost their shape after epoch 1; now they keep it.
                const float aspectOsc = std::cos(phase * 0.7f + p.seedX * 0.002f) * 0.08f;
                p.aspect = std::clamp(p.baseAspect + aspectOsc,
                                      p.baseAspect * 0.9f,
                                      p.baseAspect * 1.1f);
            }

            // Periodic rift events. Picks 1-3 land plates per CYCLE epochs
            // and splits each one in 1-2 ways, sometimes producing a
            // triple junction (3 children from one parent). Each child
            // sits offset on a randomly oriented fault axis and inherits
            // a velocity rotated by a small angle from the parent —
            // models how Pangaea broke into Africa, S America, India, etc.
            // along irregular non-orthogonal fault systems.
            // 2026-05-04: rift bursts (Pangaea-style breakup events).
            // Earth: 290 plate births in 5 Ma at 250 Ma. Sim now fires
            // 2-3 rifts per burst event scheduled in riftBurstEpochs.
            const bool isRiftEpoch = std::find(
                riftBurstEpochs.begin(), riftBurstEpochs.end(), epoch)
                != riftBurstEpochs.end();
            const int32_t riftsPerBurst = isRiftEpoch ? 3 : 0;
            (void)CYCLE;
            if (isRiftEpoch) {
                std::vector<std::size_t> landIdx;
                for (std::size_t i = 0; i < plates.size(); ++i) {
                    if (plates[i].isLand) { landIdx.push_back(i); }
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
                // Only ONE rift per CYCLE epochs (was up to 3).
                // Multiple simultaneous rifts produced abrupt visual
                // jumps — real Wilson-cycle rifting happens piecemeal
                // over millions of years, not all at once.
                // 2026-05-04: rift bursts use riftsPerBurst (3 per
                // burst event) instead of 1-per-cycle.
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
                        // 2026-05-06: PHYSICS-FIRST P2.3e -- failed-rift
                        // (aulacogen) orogenyLocal scar loop deleted.
                        // Was stamping SCAR_DEPTH=-0.10 along a narrow
                        // band perpendicular to faultAxis. Tile elevation
                        // sources from SphereField; orogenyLocal writes
                        // have no consumer. Plate-count effect (skip
                        // child spawn 30% of rifts) preserved via the
                        // continue below.
                        continue; // skip child spawn for this rift event
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
                        const float ang = centerRng.nextFloat(-0.4f, 0.4f);
                        const float cs = std::cos(ang);
                        const float sn = std::sin(ang);
                        const float vx = parent.vx * cs - parent.vy * sn;
                        const float vy = parent.vx * sn + parent.vy * cs;
                        plates[pi].vx = vx + std::cos(pushAxis) * 0.30f;
                        plates[pi].vy = vy + std::sin(pushAxis) * 0.30f;
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
                        const float ang = centerRng.nextFloat(-0.4f, 0.4f);
                        const float cs = std::cos(ang);
                        const float sn = std::sin(ang);
                        const float vx = parent.vx * cs - parent.vy * sn;
                        const float vy = parent.vx * sn + parent.vy * cs;
                        // Strong push perpendicular to the rift line —
                        // ridge-push is the dominant force in early
                        // rifting (Atlantic opening at ~2 cm/yr).
                        child.vx = vx + std::cos(childPushAngle) * 0.30f;
                        child.vy = vy + std::sin(childPushAngle) * 0.30f;
                        // Children get fresh rotation, aspect, AND crust
                        // seed. A child plate carries a different chunk
                        // of the original landmass — its own crust
                        // pattern, not a duplicate of the parent's.
                        // (When Pangaea broke apart, Africa and S America
                        // each carried different territory; they don't
                        // look identical.)
                        // GONDWANA COAST MATCHING. When continents rift,
                        // their conjugate margins are mirror images
                        // (S America Atlantic margin fits Africa's like
                        // a jigsaw because they were once one piece).
                        // Child inherits parent's seedX/seedY so the
                        // crust mask noise pattern is identical → the
                        // rifted edges have matching shapes when pushed
                        // back together. We rotate the child's local
                        // frame so the same noise is sampled differently
                        // away from the seam, producing a divergent
                        // interior but a matching coastline.
                        child.rot          = parent.rot
                                           + centerRng.nextFloat(-0.30f, 0.30f);
                        child.aspect       = parent.aspect
                                           * centerRng.nextFloat(0.92f, 1.08f);
                        child.baseRot      = child.rot;
                        child.baseAspect   = child.aspect;
                        // Inherit parent crust seed → conjugate margin
                        // shape match. Tiny offset prevents identical
                        // copy across the rift.
                        child.seedX        = parent.seedX
                                           + centerRng.nextFloat(-2.0f, 2.0f);
                        child.seedY        = parent.seedY
                                           + centerRng.nextFloat(-2.0f, 2.0f);
                        // 2026-05-04 (rev 2): rifted children are CONTINENTAL
                        // fragments (chunks of original continent) like
                        // S. America breaking off Pangaea. Earth model:
                        // BOTH halves carry continent + new oceanic
                        // crust accreted at the trailing edge facing
                        // the rift line. There is NO new third plate
                        // for the ocean basin -- the basin is split
                        // between the two existing plates' trailing
                        // edges. We mark each plate's rift-facing side
                        // via oceanWedge fields; crust-mask sampling
                        // overrides those tiles to ocean, and the wedge
                        // widens with each subsequent epoch as the
                        // plates drift apart and accrete new oceanic
                        // crust at the spreading ridge.
                        child.isLand = true;
                        child.landFraction = std::clamp(
                            parent.landFraction *
                                centerRng.nextFloat(0.85f, 1.00f),
                            0.30f, 0.55f);
                        // Oceanic wedge points FROM child centroid TOWARD
                        // parent (rift line is between them). In plate-
                        // local frame: convert world-space (parent.cx -
                        // child.cx, parent.cy - child.cy) via child.rot.
                        {
                            float wnx = parent.cx - child.cx;
                            float wny = parent.cy - child.cy;
                            const float wnLen = std::sqrt(wnx*wnx + wny*wny);
                            if (wnLen > 0.0001f) {
                                wnx /= wnLen; wny /= wnLen;
                                const float csC = std::cos(child.rot);
                                const float snC = std::sin(child.rot);
                                child.oceanWedgeNx =
                                    (wnx * csC + wny * snC);
                                child.oceanWedgeNy =
                                    (-wnx * snC + wny * csC);
                                child.oceanWedgeWidth = 0.05f;
                                child.oceanWedgeBornEpoch = epoch;
                            }
                        }
                        // Mirror wedge on the parent (its trailing edge
                        // also accretes new ocean crust). Parent's
                        // wedge points FROM parent TOWARD child.
                        {
                            Plate& parentWritable =
                                plates[static_cast<std::size_t>(pi)];
                            float wnx = child.cx - parentWritable.cx;
                            float wny = child.cy - parentWritable.cy;
                            const float wnLen = std::sqrt(wnx*wnx + wny*wny);
                            if (wnLen > 0.0001f) {
                                wnx /= wnLen; wny /= wnLen;
                                const float csP = std::cos(parentWritable.rot);
                                const float snP = std::sin(parentWritable.rot);
                                parentWritable.oceanWedgeNx =
                                    (wnx * csP + wny * snP);
                                parentWritable.oceanWedgeNy =
                                    (-wnx * snP + wny * csP);
                                parentWritable.oceanWedgeWidth = 0.05f;
                                parentWritable.oceanWedgeBornEpoch = epoch;
                            }
                        }
                        // Children get a fresh empty orogeny grid —
                        // they're "new crust" formed at the rift, not
                        // Hotspot trails reset (each fragment has its
                        // own future trail). Orogeny grid also reset:
                        // child is "new crust" formed at the rift,
                        // not carrying parent's mountain memory.
                        child.hotspotTrail.clear();
                        child.orogenyLocal.assign(
                            static_cast<std::size_t>(OROGENY_GRID * OROGENY_GRID), 0.0f);
                        child.orogenyAgeLocal.assign(
                            static_cast<std::size_t>(OROGENY_GRID * OROGENY_GRID),
                            static_cast<int16_t>(0));
                        // Rift child = young plate. Crust age resets
                        // because crust formed at the rift axis is fresh
                        // (Atlantic is younger than Pacific because it's
                        // post-Pangea). Crust area scales with new
                        // weight squared.
                        child.crustArea = child.weight * child.weight;
                        child.crustAreaInitial = child.crustArea;
                        child.crustAge = 0.0f;
                        // Children inherit parent's Euler pole but with
                        // slight perturbation (rifted fragments diverge
                        // from a common kinematic origin -- South America
                        // and Africa once shared parent rotation, then
                        // drifted to nearby but distinct poles).
                        child.eulerPoleX = parent.eulerPoleX
                                         + centerRng.nextFloat(-0.4f, 0.4f);
                        child.eulerPoleY = parent.eulerPoleY
                                         + centerRng.nextFloat(-0.4f, 0.4f);
                        child.angularRate = parent.angularRate
                                          * centerRng.nextFloat(0.7f, 1.3f);
                        plates.push_back(child);
                        aoc::map::gen::initialisePlatePhysicsGrid(
                            plates.back(), config.seed);
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
                // produced 4-6 microplates per 16-epoch sim, packing
                // microplates inside visible continents and producing
                // mountain ranges at internal microplate boundaries.
                // Earth has ~10 microplates total accumulated over 4.5By,
                // so on a single-sim timescale 1-3 is realistic.
                // 2026-05-04: cap dropped 3 -> 2 to match Earth's
                // typical microplate count (Caribbean, Cocos, Scotia,
                // Anatolian etc -- few active per geological era).
                constexpr int32_t MICROPLATE_CAP = 2;
                int32_t microplateCount = 0;
                for (const Plate& mp : plates) {
                    if (mp.isMicroplate) { ++microplateCount; }
                }
                if (plates.size() < maxPlates
                    && plates.size() >= 4
                    && microplateCount < MICROPLATE_CAP
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
                        micro.isMicroplate = true;
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
                        micro.vx = centerRng.nextFloat(-0.5f, 0.5f);
                        micro.vy = centerRng.nextFloat(-0.5f, 0.5f);
                        micro.aspect = centerRng.nextFloat(0.85f, 1.20f);
                        micro.rot    = centerRng.nextFloat(-3.14f, 3.14f);
                        micro.baseRot    = micro.rot;
                        micro.baseAspect = micro.aspect;
                        micro.seedX  = centerRng.nextFloat(0.0f, 1000.0f);
                        micro.seedY  = centerRng.nextFloat(0.0f, 1000.0f);
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
                        micro.isLand = false;
                        micro.landFraction =
                            centerRng.nextFloat(0.005f, 0.02f);
                        micro.ageEpochs = 0;
                        micro.orogenyLocal.assign(
                            static_cast<std::size_t>(OROGENY_GRID * OROGENY_GRID), 0.0f);
                        micro.orogenyAgeLocal.assign(
                            static_cast<std::size_t>(OROGENY_GRID * OROGENY_GRID),
                            static_cast<int16_t>(0));
                        // Microplate weight (smaller than majors) — set
                        // crust area accordingly. Microplates are young
                        // by definition (formed at junction events).
                        micro.weight = std::max(0.35f, micro.weight);
                        micro.crustArea = micro.weight * micro.weight;
                        micro.crustAreaInitial = micro.crustArea;
                        micro.crustAge = 0.0f;
                        // Microplates spin fast (Anatolian, Adria rotate
                        // measurably faster than majors over Quaternary).
                        {
                            const float poleAng    = centerRng.nextFloat(0.0f, 6.2832f);
                            const float poleRadius = centerRng.nextFloat(1.0f, 2.5f);
                            micro.eulerPoleX = 0.5f + std::cos(poleAng) * poleRadius;
                            micro.eulerPoleY = 0.5f + std::sin(poleAng) * poleRadius;
                            const float rateMag = centerRng.nextFloat(0.018f, 0.040f);
                            micro.angularRate = (centerRng.nextFloat(0.0f, 1.0f) < 0.5f)
                                ? -rateMag : rateMag;
                        }
                        plates.push_back(micro);
                        aoc::map::gen::initialisePlatePhysicsGrid(
                            plates.back(), config.seed);
                    }
                }
            }


            // 2026-05-06: PHYSICS-FIRST P2.3d-4 -- Wilson crust accounting
            // + active slab tearing + Wilson erase pass deleted (~95 LOC).
            // All inputs (trenchTilesPerPlate / ridgeTilesPerPlate) were
            // already zero since P2.3d-1, so the area-balance and
            // crust-erase loops were no-ops. Active slab tearing gated
            // on trenchTiles<20 (always true) -> never fired.
            // Two side effects must persist for downstream readers:
            //   p.crustAge += 1.0f       (read at lines ~2406, 2528, 2618, 2668, 2984)
            //   p.slabTornThisEpoch = 0  (read elsewhere)
            for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                Plate& p = plates[pi];
                p.crustAge += 1.0f;
                p.slabTornThisEpoch = 0;
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
                    if (p.landFraction < 0.40f && !p.isPolar) {
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
                                auto seedDsq = [&](float sx, float sy) {
                                    float ddx = nxs - sx;
                                    float ddy = nys - sy;
                                    if (cylSim) {
                                        if (ddx >  0.5f) { ddx -= 1.0f; }
                                        if (ddx < -0.5f) { ddx += 1.0f; }
                                    }
                                    return ddx * ddx + ddy * ddy;
                                };
                                float dsq = seedDsq(p.cx, p.cy);
                                for (const auto& es : p.extraSeeds) {
                                    const float d2 = seedDsq(
                                        es.first, es.second);
                                    if (d2 < dsq) { dsq = d2; }
                                }
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
                        fresh.isLand = false;
                        // Inherit a slow drift in the direction AWAY
                        // from the nearest plate (ridge-push starting
                        // condition).
                        fresh.vx = centerRng.nextFloat(-0.30f, 0.30f);
                        fresh.vy = centerRng.nextFloat(-0.30f, 0.30f);
                        fresh.aspect = centerRng.nextFloat(0.85f, 1.20f);
                        fresh.rot    = centerRng.nextFloat(-3.14f, 3.14f);
                        fresh.baseRot    = fresh.rot;
                        fresh.baseAspect = fresh.aspect;
                        fresh.seedX = centerRng.nextFloat(0.0f, 1000.0f);
                        fresh.seedY = centerRng.nextFloat(0.0f, 1000.0f);
                        // 2026-05-04: matched ocean-plate landFraction
                        // reduction. Fresh ridge-spawn ocean plates are
                        // brand new oceanic crust -- effectively 100 %
                        // ocean.
                        fresh.landFraction = centerRng.nextFloat(0.005f, 0.02f);
                        fresh.weight = centerRng.nextFloat(0.7f, 1.05f);
                        fresh.crustArea = fresh.weight * fresh.weight;
                        fresh.crustAreaInitial = fresh.crustArea;
                        fresh.crustAge = 0.0f;  // ridge-fresh
                        fresh.ageEpochs = 0;
                        fresh.orogenyLocal.assign(
                            static_cast<std::size_t>(OROGENY_GRID * OROGENY_GRID),
                            0.0f);
                        fresh.orogenyAgeLocal.assign(
                            static_cast<std::size_t>(OROGENY_GRID * OROGENY_GRID),
                            static_cast<int16_t>(0));
                        // Fresh ridge-spawn plate: random pole, oceanic
                        // rotation magnitude (faster than continental).
                        {
                            const float poleAng    = centerRng.nextFloat(0.0f, 6.2832f);
                            const float poleRadius = centerRng.nextFloat(1.5f, 3.5f);
                            fresh.eulerPoleX = 0.5f + std::cos(poleAng) * poleRadius;
                            fresh.eulerPoleY = 0.5f + std::sin(poleAng) * poleRadius;
                            const float rateMag = centerRng.nextFloat(0.008f, 0.020f);
                            fresh.angularRate = (centerRng.nextFloat(0.0f, 1.0f) < 0.5f)
                                ? -rateMag : rateMag;
                        }
                        plates.push_back(fresh);
                        aoc::map::gen::initialisePlatePhysicsGrid(
                            plates.back(), config.seed);
                    }
                }
            }
            // 2026-05-04: AREA-PROPORTIONAL mantle drag + cratonic
            // stability damping. Real Earth: large plates feel more
            // viscous resistance from the underlying mantle (drag
            // proportional to base area = weight^2). Cratons (old
            // continental cores, crustAge > 100 Ma + landFraction
            // > 0.4) have thick rigid roots that resist motion --
            // African Plate moves slowly partly because of its
            // multiple cratons. Drag formula: base 0.997 * area^0.25
            // factor + extra craton damping. Ensures Pacific (large
            // oceanic, no craton) drifts faster than African (large
            // with cratons) -- naturally producing Earth's speed
            // distribution.
            for (Plate& p : plates) {
                // Base drag scales gently with sqrt of area. Weight^2 =
                // area; weight^0.5 (sqrt of weight) is plate "size"
                // factor. Larger plates lose 0.3-1% extra per epoch.
                const float areaFactor = std::clamp(
                    std::sqrt(p.weight), 0.7f, 1.8f);
                const float baseDrag =
                    1.0f - (1.0f - 0.997f) * areaFactor;
                float drag = baseDrag;
                // Cratonic damping: stable old continental cores resist
                // motion. Effect scales with craton age (older = more
                // root depth = more resistance).
                if (p.isLand && p.landFraction > 0.4f
                    && p.crustAge > 100.0f) {
                    const float cratonAge = std::clamp(
                        (p.crustAge - 100.0f) / 100.0f, 0.0f, 1.0f);
                    drag -= 0.005f * cratonAge;
                }
                p.vx *= drag;
                p.vy *= drag;
            }

            // Hotspot trails. For each hotspot, find the plate above
            // it RIGHT NOW and record the hotspot's position in that
            // plate's LOCAL frame. As the plate drifts, future epochs
            // record a different plate-local coord (since the plate
            // moved) → trail forms. At the elevation pass we sample
            // each plate's trail to add small volcanic island bumps.
            for (Hotspot& h : hotspots) {
                // HOTSPOT DRIFT. Real plumes aren't perfectly fixed —
                // they drift slowly (~1 cm/yr equivalent). The Hawaiian-
                // Emperor bend at 47 Mya records a plume drift event.
                // Apply a deterministic tiny rotation about the map
                // centre per epoch so trails curve subtly over long
                // sims rather than being perfectly straight.
                {
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
                float bestSq = 1e9f; int32_t bestPi = -1;
                for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                    float dx = h.cx - plates[pi].cx;
                    float dy = h.cy - plates[pi].cy;
                    if (cylSim) {
                        if (dx > 0.5f) { dx -= 1.0f; }
                        if (dx < -0.5f) { dx += 1.0f; }
                    }
                    const float dsq = dx * dx + dy * dy;
                    if (dsq < bestSq) { bestSq = dsq; bestPi = static_cast<int32_t>(pi); }
                }
                if (bestPi < 0) { continue; }
                Plate& owner = plates[static_cast<std::size_t>(bestPi)];
                float dx = h.cx - owner.cx;
                float dy = h.cy - owner.cy;
                if (cylSim) {
                    if (dx > 0.5f) { dx -= 1.0f; }
                    if (dx < -0.5f) { dx += 1.0f; }
                }
                const float cs = std::cos(owner.rot);
                const float sn = std::sin(owner.rot);
                const float lx = (dx * cs + dy * sn) / owner.aspect;
                const float ly = (-dx * sn + dy * cs) * owner.aspect;
                owner.hotspotTrail.emplace_back(lx, ly);
                // Cap trail length to prevent unbounded growth on long
                // sims. ~40 trail points = chain spanning ~1500 km of
                // plate motion at Earth scales (Hawaii-Emperor chain).
                if (owner.hotspotTrail.size() > 40) {
                    owner.hotspotTrail.erase(owner.hotspotTrail.begin());
                }
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
            // Tiered erosion. High peaks erode fast (more relief →
            // steeper slopes → faster mass wasting). Low remnants of
            // old mountains erode VERY slowly because the resistant
            // crystalline core rocks (granite, gneiss) survive long
            // after the surrounding sediment is gone. This is why the
            // Harz, Black Forest, Appalachians, and Urals — all 250-
            // 400 My old — still stand as Hills surrounded by flat
            // plains: their root rock outlasts the soft sediment.
            //
            //   orogeny > 0.20  : 1.5 %/epoch (active range, fast wear)
            //   0.10 ≤ o ≤ 0.20 : 0.7 %/epoch (mature, moderate wear)
            //   0.00 < o < 0.10 : 0.15 %/epoch (root-rock preservation)
            //   o < 0           : 0.6 %/epoch sediment fill
            // Erosion now operates on each plate's LOCAL grid so the
            // mountain memory travels with the plate.
            // 2026-05-05 Phase 12a: legacy age-dependent erosion + the
            // "kept zeroed" world-frame decay loop ripped. Both walked
            // orogenyLocal/orogeny[] but downstream rebuilds orogeny[]
            // from PhysicsGrid.surfaceElevationM via peakSample, so the
            // mutations were dead.
            // 2026-05-04: WP2 - polygon evolution per epoch.
            // Phase A: re-classify edge types from current relative
            // velocity (boundaries change behaviour as plates accel.
            // / decel. via slab pull, ridge push, reorganization
            // events). Phase B: apply per-edge vertex motion -- ridge
            // edges push outward (new oceanic crust accreted), trench
            // edges pull inward (subducting crust destroyed),
            // transform/suture edges hold static. Phase C: recompute
            // AABBs for next epoch's tile-ownership fast-reject.
            for (std::size_t targetIdx = 0;
                 targetIdx < plates.size(); ++targetIdx) {
                Plate& target = plates[targetIdx];
                const std::size_t N = target.boundaryVertices.size();
                if (N < 3) { continue; }
                if (target.boundaryNeighborIds.size() != N
                    || target.boundaryEdgeTypes.size() != N) { continue; }
                const float csT = std::cos(target.rot);
                const float snT = std::sin(target.rot);
                // Phase 13d-A3 step 2 WP2 instrumentation counters,
                // hoisted to enclose the entire per-edge body so skip
                // paths bump them correctly. Behaviour-neutral.
                static thread_local int64_t s_wp2Total          = 0;
                static thread_local int64_t s_wp2NoNbr          = 0;
                static thread_local int64_t s_wp2ZeroBn         = 0;
                static thread_local int64_t s_wp2Trans[5][5]    = {};
                for (std::size_t i = 0; i < N; ++i) {
                    ++s_wp2Total;
                    const uint8_t nbrId = target.boundaryNeighborIds[i];
                    if (nbrId == 0xFFu
                        || nbrId >= plates.size()) {
                        ++s_wp2NoNbr;
                        continue;
                    }
                    const Plate& nbr = plates[nbrId];
                    const std::size_t i1 = (i + 1) % N;
                    const float lmx = (target.boundaryVertices[i].first
                        + target.boundaryVertices[i1].first) * 0.5f;
                    const float lmy = (target.boundaryVertices[i].second
                        + target.boundaryVertices[i1].second) * 0.5f;
                    const float wmx = target.cx + lmx * csT - lmy * snT;
                    const float wmy = target.cy + lmx * snT + lmy * csT;
                    float bnx = nbr.cx - wmx;
                    float bny = nbr.cy - wmy;
                    if (cylSim) {
                        if (bnx >  0.5f) { bnx -= 1.0f; }
                        if (bnx < -0.5f) { bnx += 1.0f; }
                    }
                    const float bnLen = std::sqrt(bnx * bnx + bny * bny);
                    if (bnLen < 0.0001f) {
                        ++s_wp2ZeroBn;
                        continue;
                    }
                    bnx /= bnLen; bny /= bnLen;
                    const float relVx = target.vx - nbr.vx;
                    const float relVy = target.vy - nbr.vy;
                    const float dotN = relVx * bnx + relVy * bny;
                    // 2026-05-04: WP2 - edge-type persistence with
                    // hysteresis. Old code re-classified every epoch
                    // on instantaneous velocity, so small noise in
                    // relative motion flipped boundaries between
                    // ridge/transform/trench. Real plate boundaries
                    // persist 100-500 My with stable type. Hysteresis:
                    // current type only flips if |dotN| exceeds the
                    // OPPOSING-type threshold by 50% margin. Near-zero
                    // dotN preserves whatever type was assigned at
                    // initial polygon construction.
                    const uint8_t prevType = target.boundaryEdgeTypes[i];
                    uint8_t edgeType = prevType;
                    if (prevType == 1u) {
                        // Currently ridge (divergent); flip to
                        // convergent only if strongly closing.
                        if (dotN > 0.06f) {
                            edgeType = (target.isLand && nbr.isLand) ? 4u : 2u;
                        } else if (dotN > -0.02f) {
                            edgeType = 3u; // weak -> transform
                        }
                    } else if (prevType == 2u || prevType == 4u) {
                        // Currently convergent; flip to divergent only
                        // if strongly opening.
                        if (dotN < -0.06f) {
                            edgeType = 1u;
                        } else if (dotN < 0.02f) {
                            edgeType = 3u;
                        } else {
                            edgeType = (target.isLand && nbr.isLand) ? 4u : 2u;
                        }
                    } else {
                        // Currently transform/unknown; standard
                        // classification thresholds apply.
                        if (dotN > 0.04f) {
                            edgeType = (target.isLand && nbr.isLand) ? 4u : 2u;
                        } else if (dotN < -0.04f) {
                            edgeType = 1u;
                        } else {
                            edgeType = 3u;
                        }
                    }
                    // 2026-05-05 Phase 13d-A3 step 2: WP2 reclassifier
                    // instrumentation. Behaviour identical; counters
                    // dump under AOC_EDGE_CLASS_DEBUG=1. Counters are
                    // declared above the per-edge loop so skip paths
                    // bump them correctly; this block records the
                    // (prev, new) transition + emits a periodic dump.
                    if (prevType < 5u && edgeType < 5u) {
                        ++s_wp2Trans[prevType][edgeType];
                    }
                    if (std::getenv("AOC_EDGE_CLASS_DEBUG") != nullptr
                        && (s_wp2Total & 0x3FF) == 0) {
                        std::fprintf(stderr,
                            "[wp2] total=%ld no_nbr=%ld zero_bn=%ld\n",
                            static_cast<long>(s_wp2Total),
                            static_cast<long>(s_wp2NoNbr),
                            static_cast<long>(s_wp2ZeroBn));
                        std::fprintf(stderr,
                            "[wp2-trans] (rows=prev cols=new):\n");
                        for (int p = 0; p < 5; ++p) {
                            std::fprintf(stderr,
                                "  prev=%d ->", p);
                            for (int n = 0; n < 5; ++n) {
                                std::fprintf(stderr, " n%d=%-5ld",
                                    n,
                                    static_cast<long>(s_wp2Trans[p][n]));
                            }
                            std::fprintf(stderr, "\n");
                        }
                    }
                    target.boundaryEdgeTypes[i] = edgeType;
                }
            }
            // Phase B: per-edge vertex motion. Each edge contributes a
            // displacement vector (perpendicular outward for ridge,
            // inward for trench) to its two endpoint vertices.
            // Vertices accumulate from both adjacent edges, then are
            // displaced. Magnitudes scaled by DT so total displacement
            // over a sim ~ Earth-realistic (Atlantic opens ~2 cm/yr).
            for (Plate& p : plates) {
                const std::size_t N = p.boundaryVertices.size();
                if (N < 3) { continue; }
                if (p.boundaryEdgeTypes.size() != N) { continue; }
                std::vector<std::pair<float, float>> disp(
                    N, {0.0f, 0.0f});
                for (std::size_t i = 0; i < N; ++i) {
                    const std::size_t i1 = (i + 1) % N;
                    float ex = p.boundaryVertices[i1].first
                        - p.boundaryVertices[i].first;
                    float ey = p.boundaryVertices[i1].second
                        - p.boundaryVertices[i].second;
                    const float eLen = std::sqrt(ex * ex + ey * ey);
                    if (eLen < 0.0001f) { continue; }
                    ex /= eLen; ey /= eLen;
                    // Outward normal for CCW polygon: (ey, -ex). Note
                    // the polar-sweep build order is CCW (angle 0->2pi)
                    // when viewed in plate-local frame. Verify if
                    // polygon area becomes negative -> negate normal.
                    const float nx = ey;
                    const float ny = -ex;
                    float dispMag = 0.0f;
                    const uint8_t et = p.boundaryEdgeTypes[i];
                    if (et == 1u) {
                        dispMag = +0.020f;  // ridge: outward push
                    } else if (et == 2u) {
                        dispMag = -0.025f;  // trench: inward pull
                    }
                    // suture (4) and transform (3) and unknown (0): 0
                    const float ddx = nx * dispMag * DT;
                    const float ddy = ny * dispMag * DT;
                    disp[i].first  += ddx;
                    disp[i].second += ddy;
                    disp[i1].first  += ddx;
                    disp[i1].second += ddy;
                }
                // Each vertex collected from 2 adjacent edges -> halve.
                for (std::size_t i = 0; i < N; ++i) {
                    p.boundaryVertices[i].first  += disp[i].first  * 0.5f;
                    p.boundaryVertices[i].second += disp[i].second * 0.5f;
                }
            }
            // 2026-05-06: PHYSICS-FIRST P3.3 -- polygon construction
            // Phase A (intermediate AABB recompute), Phase B (subduction
            // clipping using PolygonRing/AABB/pointInPolygon to mutate
            // boundaryVertices/polygonMin/Max), and Phase C (simplify +
            // self-intersect repair + rebuildPolygonsFromVoronoi /
            // recomputePolygonAABBs) deleted (~166 LOC). Plate ownership
            // is now pure haversine via PlateIdStash (P3.1); polygon
            // mutation has no consumer. boundaryVertices/polygonMin/Max
            // remain populated by initial-setup rebuilds; P4 deletes the
            // fields atomically.
            // 2026-05-06: PHYSICS-FIRST P2.3c -- per-edge orogeny stress
            // edge-walk stamp deleted. Was a post-hoc shaper writing
            // edge-type-coded magnitudes (trench +0.004 / suture +0.006 /
            // ridge -0.001) along plate boundary polygons via
            // EDGE_WALK_STEPS=8 to orogenyLocal. Tile elevation now
            // sources from SphereField (P2.2); orogenyLocal writes have
            // no consumer.
            // 2026-05-06: PHYSICS-FIRST P2.3b -- anisotropic diffusion
            // smoother deleted. Was a post-hoc gradient-aligned blur of
            // orogenyLocal (alphaTangent=0.10 / alphaNormal=0.01) that
            // sculpted chains lengthwise. Tile elevation now sources
            // from SphereField (P2.2); orogenyLocal writes have no
            // consumer.
            // 2026-05-06: PHYSICS-FIRST P2.3a -- pull-apart basin stamp
            // deleted. Was a post-hoc shaper writing PULL_APART_DEPTH=-0.008
            // to orogenyLocal at transform-corner vertices (Dead Sea / Salton
            // / Death Valley analog). Tile elevation now sources from
            // SphereField (P2.2); orogenyLocal writes have no consumer.
#if defined(AOC_PHYSICS_ON_SPHEREFIELD)
            // 2026-05-06: PHYSICS-FIRST P1. Run the SphereField epoch
            // step alongside the legacy per-plate pipeline. Plate
            // positions have already been advanced this epoch above.
            aoc::map::gen::stepSpherePhysicsEpoch(
                sphereField, plates, sphereBoundaryScratch,
                MY_PER_EPOCH_P1);
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

        // Rebuild world-frame `orogeny[]` array by sampling each tile
        // from its owning plate's local grid. Existing elevation pass
        // and side-correctness consume the world-frame array unchanged.
        // Sampling is bilinear so transitions across the plate-local
        // grid are smooth.
        // Pixel-anisotropic Voronoi: scale dx by W/H so plate cells
        // appear roughly isotropic in pixel space. World map is W>H
        // (typically 2:1) so without this fix all cells are E-W
        // stretched and continents look horizontal.
        const float aspectFix = static_cast<float>(width)
            / static_cast<float>(height);
        // Parallel: world-frame elevation pass — each iteration writes
        // only elevationMap[i]. Hot path (heavy per-tile work: nearest-
        // plate Voronoi + samplePL bilinear + crust mask sample).
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const float nx = static_cast<float>(col) / static_cast<float>(width);
                const float ny = static_cast<float>(row) / static_cast<float>(height);
                float bestSq = 1e9f; int32_t bestPi = -1;
                float bestLx = 0.0f, bestLy = 0.0f;
                for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                    float dx = nx - plates[pi].cx;
                    float dy = ny - plates[pi].cy;
                    if (cylSim) {
                        if (dx >  0.5f) { dx -= 1.0f; }
                        if (dx < -0.5f) { dx += 1.0f; }
                    }
                    dx *= aspectFix;
                    const float cs = std::cos(plates[pi].rot);
                    const float sn = std::sin(plates[pi].rot);
                    float lx = (dx * cs + dy * sn) / plates[pi].aspect;
                    float ly = (-dx * sn + dy * cs) * plates[pi].aspect;
                    // Plate-local boundary jitter — perturb the sample
                    // point with continuous per-plate noise so the
                    // Voronoi boundary develops organic non-circular
                    // shape. Travels with plate (deterministic from
                    // seedX). 2026-05-04: switched from
                    // hashNoise(floor(lx*5), ...) (which produced
                    // 0.2-unit stair-step kinks) to bilinear
                    // smoothHashNoise. mixSeed() decorrelates plates
                    // with similar seedX values.
                    {
                        const uint64_t pseed = aoc::map::gen::mixSeed(
                            static_cast<uint64_t>(plates[pi].seedX * 1.0e6f));
                        const float n1 = aoc::map::gen::smoothHashNoise(
                            lx * 4.0f, ly * 4.0f, pseed);
                        const float n2 = aoc::map::gen::smoothHashNoise(
                            lx * 4.0f, ly * 4.0f, pseed ^ 0xA5A5ULL);
                        lx += (n1 - 0.5f) * 0.18f;
                        ly += (n2 - 0.5f) * 0.18f;
                    }
                    const float dsq = (lx * lx + ly * ly) / (plates[pi].weight * plates[pi].weight);
                    if (dsq < bestSq) {
                        bestSq = dsq; bestPi = static_cast<int32_t>(pi);
                        bestLx = lx; bestLy = ly;
                    }
                }
                if (bestPi < 0) { continue; }
                // 2026-05-05 PHASE-1B-2: source orogeny from per-plate
                // PhysicsGrid surfaceElevationM (Airy isostasy). Orogeny
                // semantics = elevation EXCESS above flat-continent
                // baseline (4000 m). Initial 35 km crust gives ~3460 m
                // -> oro = 0; Phase-2 crust thickening past 40 km lifts
                // oro into [0, 1] (mountain tier). Convert tile (nx, ny)
                // -> lat/lon via Mollweide, then tangent-plane (lxRad,
                // lyRad) around plate centroid.
                float oroSampled = 0.0f;
                {
                    aoc::map::gen::MollweideInverseResult mw =
                        aoc::map::gen::mollweideInverse(nx, ny);
                    if (mw.valid) {
                        // 2026-05-05 Phase 2.5: raised base 4000 -> 5500
                        // and scale 2000 -> 3500. With 8-seed audit at
                        // 140x90 producing 30-50 % mountain rate (Earth
                        // target ~6-7 %), the linear oro-from-elevation
                        // remap was too generous. New values: 5500 m
                        // baseline (Andes-tall), 3500 m to reach oro=1
                        // (Himalaya-tall).
                        // 2026-05-05 Phase 6 sub-step 6c: 5500 ->
                        // 4500, 3500 -> 3000. Drops biome threshold
                        // to Andean-foothills bar (4500 m = low-lat
                        // snowline). oro=1 still hits at 7500 m
                        // (4500 + 3000) ≈ Himalaya summit. Earth's
                        // 6-7 % "mountainous terrain" UN definition
                        // includes hills + alpine; our 5500 m bar
                        // captured only Tibet/Andes/Himalaya peaks
                        // (~0.5 % of land), too narrow.
                        constexpr float MOUNTAIN_BASE_M  = 4500.0f;
                        constexpr float MOUNTAIN_SCALE_M = 3000.0f;
                        // 2026-05-06: PHYSICS-FIRST P2.2. Tile elevation
                        // sourced from the global SphereField raster
                        // (720x360 lat/lon at 0.5 deg) via bilinearSample
                        // at the tile's Mollweide-inverse lat/lon. The
                        // per-plate PhysicsGrid peakSample loop was
                        // dependent on each plate's tangent-plane half-
                        // extent and the now-defunct legacy aspect/rot
                        // parameters; the raster covers the whole sphere
                        // unconditionally and removes that bookkeeping.
                        const float zM = sphereField.bilinearSample(
                            sphereField.surfaceElevationM,
                            mw.coord.latDeg, mw.coord.lonDeg);
                        oroSampled = std::max(0.0f,
                            (zM - MOUNTAIN_BASE_M) / MOUNTAIN_SCALE_M);
                    }
                }
                // Mask gate. Cap positive orogeny on ocean-tile-of-
                // ocean-plate (prevents "ocean-ocean merger turns into
                // land bridge" artefact). On a CONTINENTAL plate's
                // ocean-mask tile, allow full orogeny — this is the
                // mechanism by which subduction arcs CRATONICALLY
                // GROW continents, lifting marginal seafloor into
                // new land along the coast.
                {
                    const Plate& owner = plates[static_cast<std::size_t>(bestPi)];
                    if (owner.landFraction < 0.40f) {
                        // Ocean plate. Cap positive orogeny at Hills tier.
                        aoc::Random crustRng(0u);
                        const float crust = fractalNoise(
                            bestLx * 2.0f + owner.seedX,
                            bestLy * 2.0f + owner.seedY,
                            4, 2.0f, 0.55f, crustRng);
                        const bool ownerSaysLand = crust > (1.0f - owner.landFraction);
                        if (!ownerSaysLand && oroSampled > 0.10f) {
                            oroSampled = 0.10f;
                        }
                    }
                }
                orogeny[static_cast<std::size_t>(row * width + col)] = oroSampled;
            }
        }

        // 2026-05-05 PHASE 6 sub-step 6d: per-plate diagnostic dump
        // gated by AOC_PHYSICS_DEBUG env var. Off by default; flip
        // env var to investigate stuck-zero-mountain seeds.
        if (std::getenv("AOC_PHYSICS_DEBUG") != nullptr) {
            std::fprintf(stderr,
                "[phys-debug] seed=%u plate_count=%zu\n",
                static_cast<unsigned>(config.seed), plates.size());
            for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                const Plate& p = plates[pi];
                const aoc::map::gen::PhysicsGrid& g = p.grid;
                if (g.cellsX <= 0 || g.cellsY <= 0) { continue; }
                float maxCrustKm = 0.0f;
                float maxStrain  = 0.0f;
                float maxZ       = -1e9f;
                for (std::size_t i = 0; i < g.crustThicknessKm.size(); ++i) {
                    if (g.cellActive[i] == 0u) { continue; }
                    if (g.crustThicknessKm[i] > maxCrustKm) {
                        maxCrustKm = g.crustThicknessKm[i];
                    }
                    if (g.cumulativeStrain[i] > maxStrain) {
                        maxStrain = g.cumulativeStrain[i];
                    }
                    if (g.surfaceElevationM[i] > maxZ) {
                        maxZ = g.surfaceElevationM[i];
                    }
                }
                std::fprintf(stderr,
                    "  pi=%-3zu lat=%+6.1f lon=%+7.1f w=%.2f "
                    "maxCrustKm=%5.1f maxStrain=%6.3f maxZ=%6.0f\n",
                    pi,
                    static_cast<double>(p.latDeg),
                    static_cast<double>(p.lonDeg),
                    static_cast<double>(p.weight),
                    static_cast<double>(maxCrustKm),
                    static_cast<double>(maxStrain),
                    static_cast<double>(maxZ));
            }
            // 2026-05-05 PHASE 7 sub-step 7b: world-frame orogeny[]
            // stats. Probes whether stuck-zero seeds have empty
            // orogeny field (= world-frame elevation pass missed
            // the plate's high-z cells) or non-empty but small
            // (= ClimateBiome compCap rejection).
            float oroMax = 0.0f;
            int32_t countOro007 = 0;
            int32_t countOro080 = 0;
            for (std::size_t i = 0; i < orogeny.size(); ++i) {
                const float o = orogeny[i];
                if (o > oroMax) { oroMax = o; }
                if (o >= 0.07f) { ++countOro007; }
                if (o >= 0.80f) { ++countOro080; }
            }
            std::fprintf(stderr,
                "  orogeny: max=%.3f  ge_0.07=%d  ge_0.80=%d  total=%zu\n",
                static_cast<double>(oroMax),
                countOro007, countOro080, orogeny.size());
        }

        // 2026-05-05 Phase 13d-A1 step 4: per-plate PhysicsGrid CSV
        // dump for offline diagnostic scripts. Plates are local to
        // assignTerrain so this hook must run before they go out of
        // scope. Output path is `<base>.plate<id>.csv` -- one file per
        // plate keeps each manageable for spreadsheet inspection.
        if (!config.physicsCellDumpPath.empty()) {
            constexpr float DEG2RAD = 3.14159265f / 180.0f;
            constexpr float RAD2DEG = 180.0f / 3.14159265f;
            for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                const Plate& p = plates[pi];
                const aoc::map::gen::PhysicsGrid& g = p.grid;
                if (g.cellsX <= 0 || g.cellsY <= 0) { continue; }
                char pathBuf[1024];
                std::snprintf(pathBuf, sizeof(pathBuf),
                              "%s.plate%03zu.csv",
                              config.physicsCellDumpPath.c_str(), pi);
                std::FILE* fp = std::fopen(pathBuf, "w");
                if (fp == nullptr) {
                    std::fprintf(stderr,
                        "%s:%d error: cannot open '%s' for writing "
                        "(--dump-physics-cells)\n",
                        __FILE__, __LINE__, pathBuf);
                    continue;
                }
                std::fprintf(fp,
                    "plate_id,cell_ix,cell_iy,lat,lon,"
                    "crust_thickness_km,surface_elevation_m,"
                    "continental_fraction,cumulative_strain,active\n");
                const float cellSizeRadX = (g.cellsX > 0)
                    ? (2.0f * g.halfExtentRadX
                       / static_cast<float>(g.cellsX))
                    : 0.0f;
                const float cellSizeRadY = (g.cellsY > 0)
                    ? (2.0f * g.halfExtentRadY
                       / static_cast<float>(g.cellsY))
                    : 0.0f;
                const float cosLat = std::cos(p.latDeg * DEG2RAD);
                const float invCosLatDeg = (std::fabs(cosLat) > 1e-4f)
                    ? (RAD2DEG / cosLat) : 0.0f;
                for (int32_t iy = 0; iy < g.cellsY; ++iy) {
                    for (int32_t ix = 0; ix < g.cellsX; ++ix) {
                        const std::size_t idx = g.cellIndex(ix, iy);
                        const float lxRad = -g.halfExtentRadX
                            + (static_cast<float>(ix) + 0.5f)
                              * cellSizeRadX;
                        const float lyRad = -g.halfExtentRadY
                            + (static_cast<float>(iy) + 0.5f)
                              * cellSizeRadY;
                        const float dLatDeg = lyRad * RAD2DEG;
                        const float dLonDeg = lxRad * invCosLatDeg;
                        float cellLat = p.latDeg + dLatDeg;
                        float cellLon = p.lonDeg + dLonDeg;
                        if (cellLon >  180.0f) { cellLon -= 360.0f; }
                        if (cellLon < -180.0f) { cellLon += 360.0f; }
                        std::fprintf(fp,
                            "%zu,%d,%d,%.4f,%.4f,%.3f,%.1f,%.4f,%.5f,%d\n",
                            pi, ix, iy,
                            static_cast<double>(cellLat),
                            static_cast<double>(cellLon),
                            static_cast<double>(g.crustThicknessKm[idx]),
                            static_cast<double>(g.surfaceElevationM[idx]),
                            static_cast<double>(g.continentalFraction[idx]),
                            static_cast<double>(g.cumulativeStrain[idx]),
                            static_cast<int>(g.cellActive[idx]));
                    }
                }
                std::fclose(fp);
            }
        }

        // Restore initial plate positions only when the user prefers
        // fixed-start visuals; we KEEP the moved positions because
        // they ARE the present-day map. (startPlates retained for
        // potential debug visualisation.)
        (void)startPlates;
    }

    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            float nx = static_cast<float>(col) / static_cast<float>(width);
            float ny = static_cast<float>(row) / static_cast<float>(height);

            float elev = fractalNoise(nx, ny, 6, 3.0f, 0.5f, noiseRng);

            float edgeFalloff = 0.0f;

            // 2026-05-05 Phase 13d-1: mid-ocean ridge bathymetry.
            // Populated by the ridge-distance block below if the tile
            // is ocean and within RIDGE_RANGE of a type-1 (divergent)
            // boundary edge of its owning plate. Lifts seafloor toward
            // shallower bathymetry around the ridge axis (real Earth:
            // -2500 m vs -5500 m abyssal).
            float ridgeBonus = 0.0f;

            // 2026-05-03: Fractal removed; original branch retained under #if 0.
            {
                // Domain-warp the sample position with low-frequency noise so
                // the radial falloff becomes irregular (bays, peninsulas,
                // inland seas) instead of perfect discs.  Previous version
                // produced circular continents with mountains piled at the
                // geometric centre — no trace of actual continental drift.
                // Two-octave domain warp (large-scale displacement +
                // smaller-scale ripple) so coastlines fjord, peninsulate,
                // and curve in plausible ways instead of reading as a
                // distorted disc. Magnitudes inspired by Wang/Voronoi-
                // plate plus FBM warping techniques used in procedural
                // continent generators (Inigo Quilez "domain warping").
                // Domain warp tuned WAY down — earlier 0.85+0.20+0.06
                // amplitudes scrambled the Voronoi plate assignment so
                // adjacent tiles landed in different plates and the map
                // looked like noise. Total warp now ≤0.18 so plate
                // cells stay coherent while coastlines retain modest
                // irregularity from a single wide-band octave.
                // Three-octave domain warp. Wider amplitude on the
                // low-frequency band gives plate boundaries an organic
                // curve (estuaries, gulfs, peninsulas), the mid band
                // adds medium-scale wiggles, and the high band breaks
                // perfectly straight Voronoi seams into ragged edges
                // where adjacent tiles can flip plate ownership across
                // the warp.
                // Two-octave domain warp at LOW frequency only. Bends
                // plate cells into smooth S-curves and meanders without
                // fragmenting the seam into noisy tile-by-tile flips.
                // High-frequency warp octaves were producing tiny
                // arm-like protrusions (each pixel pushed to a different
                // plate) — removed. Result: smoothly curved boundaries
                // with gentle bays and peninsulas, no jaggy edge.
                aoc::Random warpRng(noiseRng);
                const float warpX1 =
                    (fractalNoise(nx * 1.3f, ny * 1.3f, 4, 2.0f, 0.55f, warpRng) - 0.5f) * 0.24f;
                const float warpY1 =
                    (fractalNoise(nx * 1.3f + 17.0f, ny * 1.3f + 31.0f, 4, 2.0f, 0.55f, warpRng) - 0.5f) * 0.24f;
                const float warpX2 =
                    (fractalNoise(nx * 3.5f, ny * 3.5f, 2, 2.0f, 0.5f, warpRng) - 0.5f) * 0.06f;
                const float warpY2 =
                    (fractalNoise(nx * 3.5f + 9.0f, ny * 3.5f + 21.0f, 2, 2.0f, 0.5f, warpRng) - 0.5f) * 0.06f;
                const float warpX = warpX1 + warpX2;
                const float warpY = warpY1 + warpY2;
                const float wx = nx + warpX;
                const float wy = ny + warpY;

                if (!plates.empty()) {
                    // Voronoi plate lookup. Track first and second
                    // closest seeds so we can softly blend across the
                    // plate boundary rather than producing knife-edge
                    // straight Voronoi lines.
                    float d1Sq = 1e9f, d2Sq = 1e9f;
                    int32_t nearest = -1;
                    int32_t second  = -1;
                    // Outer-scope so the post-stress hard-floor pass
                    // (below) can read whether the tile sits on a land
                    // plate and how close it is to the boundary.
                    bool  nearestIsLand = false;
                    float boundary      = 0.0f;
                    // Plate-local coords for the nearest plate. Used
                    // below to sample a per-plate crust mask that is
                    // FIXED in plate-local space and travels with the
                    // plate as it drifts (so coastlines don't reshape
                    // wildly between epochs).
                    float lxNearest = 0.0f;
                    float lyNearest = 0.0f;
                    // 2026-05-05: SPHERE MIGRATION - convert the warped
                    // unit-square position back to lat/lon via Mollweide
                    // inverse, then rank plates by haversine distance.
                    // Tiles whose warped position falls outside the
                    // ellipse (polar voids) skip the plate ranking and
                    // keep their default ocean elevation.
                    const aoc::map::gen::MollweideInverseResult wTileLL =
                        aoc::map::gen::mollweideInverse(wx, wy);
                    aoc::map::gen::LatLon tileLatLon{0.0f, 0.0f};
                    float tileLatRad = 0.0f;
                    if (wTileLL.valid) {
                        tileLatLon = wTileLL.coord;
                        tileLatRad = tileLatLon.latDeg * 0.01745329252f;
                        for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                            const aoc::map::gen::LatLon plateLL{
                                plates[pi].latDeg, plates[pi].lonDeg};
                            const float h = aoc::map::gen::haversineRadians(
                                tileLatLon, plateLL);
                            const float dsq = (h * h)
                                / (plates[pi].weight * plates[pi].weight);
                            const float plateLatRad =
                                plates[pi].latDeg * 0.01745329252f;
                            float dLonDeg =
                                tileLatLon.lonDeg - plates[pi].lonDeg;
                            if (dLonDeg >  180.0f) { dLonDeg -= 360.0f; }
                            if (dLonDeg < -180.0f) { dLonDeg += 360.0f; }
                            const float dLatDeg =
                                tileLatLon.latDeg - plates[pi].latDeg;
                            const float lx = (dLonDeg * 0.01745329252f)
                                * std::cos(0.5f * (tileLatRad + plateLatRad));
                            const float ly = dLatDeg * 0.01745329252f;
                            if (dsq < d1Sq) {
                                d2Sq    = d1Sq;
                                second  = nearest;
                                d1Sq    = dsq;
                                nearest = static_cast<int32_t>(pi);
                                lxNearest = lx;
                                lyNearest = ly;
                            } else if (dsq < d2Sq) {
                                d2Sq   = dsq;
                                second = static_cast<int32_t>(pi);
                            }
                        }
                    }
                    // 2026-05-04: WP4 - polygon-based ownership override.
                    // After Voronoi computes nearest/second/d1Sq/d2Sq,
                    // check if any plate's polygon CLAIMS the tile via
                    // point-in-polygon. If a polygon claims and it is
                    // not the Voronoi nearest, override nearest to the
                    // polygon owner (closest centroid among polygon
                    // 2026-05-06: PHYSICS-FIRST P3.2 -- polygon-PIP
                    // ownership override block deleted. Was using
                    // polySpatialIndex AABB candidates + per-plate
                    // boundaryVertices PIP test to override the Voronoi
                    // nearest-plate ownership in the side-correctness
                    // pass. Plate ownership is now pure haversine
                    // (PlateIdStash, P3.1); the polygon override is
                    // redundant + about to lose its data source (P4
                    // deletes boundaryVertices/polygonMin/Max).
                    if (nearest < 0) { edgeFalloff = 0.0f; }
                    else {
                        const float d1 = std::sqrt(d1Sq);
                        const float d2 = std::sqrt(d2Sq);
                        // Blend factor: 0 deep inside the nearest plate,
                        // ramping toward 1 right on the boundary. Thin
                        // band so plates look distinct but aren't razor-
                        // sharp.
                        // Narrow boundary window — only the outermost
                        // 8% of d1/d2 ratio blends across the seam.
                        // Wider windows (≥15%) bled coastal water into
                        // the land plate's interior; user reported
                        // "too many inland lakes". Keep boundaries
                        // sharp; the tile-level domain warp already
                        // makes coastlines irregular without needing
                        // a wide blend band.
                        boundary = (d2 > 0.0001f)
                            ? std::clamp((d1 / d2 - 0.92f) / 0.08f, 0.0f, 1.0f)
                            : 0.0f;
                        // Lower land base lets noise amplitude bite into
                        // the continent, carving bays, gulfs, and inland
                        // seas — Earth-like irregular coasts instead of
                        // convex blobs. The hard floor below prevents
                        // deep-interior swiss-cheese lakes while still
                        // letting edge tiles become ocean.
                        const float oceanBase = 0.10f;
                        const Plate& pNearest = plates[static_cast<std::size_t>(nearest)];

                        // PER-PLATE crust mask. Sample plate-local noise
                        // using the plate's own seed, so the same point
                        // on the plate's surface always returns the same
                        // crust value — even as the plate drifts to a
                        // new world position. This makes land/ocean
                        // patches travel WITH the plate (like India's
                        // landmass riding the Indian plate from Africa
                        // to its present collision with Eurasia).
                        aoc::Random crustRng(noiseRng);
                        const float crust = fractalNoise(
                            lxNearest * 2.0f + pNearest.seedX,
                            lyNearest * 2.0f + pNearest.seedY,
                            4, 2.0f, 0.55f, crustRng);
                        // Threshold: high landFraction → low threshold
                        // → most of the plate is land. landFraction
                        // [0..1] maps the plate from pure-ocean (Pacific)
                        // to pure-continental (Eurasian).
                        const float landThresh = 1.0f - pNearest.landFraction;
                        // Narrow smooth band (0.025) around the threshold
                        // — wide bands produce too many half-elevation
                        // tiles that fluctuate around the water cutoff
                        // and fragment continents into archipelagos.
                        float t = std::clamp(
                            (crust - landThresh + 0.025f) / 0.05f, 0.0f, 1.0f);
                        // 2026-05-04: oceanic-wedge override at trailing
                        // rift edge -- new oceanic crust accreted at
                        // mid-ocean ridge.
                        if (pNearest.oceanWedgeWidth > 0.0f) {
                            const float dot = lxNearest * pNearest.oceanWedgeNx
                                            + lyNearest * pNearest.oceanWedgeNy;
                            if (dot > 0.0f && dot < pNearest.oceanWedgeWidth) {
                                t = 0.0f;
                            }
                        }
                        const float localLandness = t; // 0 = ocean, 1 = land
                        nearestIsLand = (localLandness > 0.5f);

                        // Land elevation curve: shield-like Gaussian peak
                        // near plate center + active-margin uplift on
                        // leading edge. Combined with the crust mask so
                        // only "land" parts of the plate get the curve.
                        float nearestHeight;
                        if (localLandness > 0.0f) {
                            const float dcx = wx - pNearest.cx;
                            const float dcy = wy - pNearest.cy;
                            const float dist_from_center = std::sqrt(dcx * dcx + dcy * dcy);
                            const float vLen = std::sqrt(pNearest.vx * pNearest.vx
                                                         + pNearest.vy * pNearest.vy);
                            float leading_factor = 0.0f;
                            if (vLen > 1e-5f) {
                                leading_factor = std::clamp(
                                    (dcx * pNearest.vx + dcy * pNearest.vy) / vLen,
                                    -1.0f, 1.0f);
                            }
                            const float craton_core   = 0.78f * std::exp(
                                -dist_from_center * dist_from_center * 8.0f);
                            const float craton_margin = 0.58f + leading_factor * 0.08f;
                            const float landH = std::clamp(
                                std::max(craton_core, craton_margin), 0.48f, 0.82f);
                            // Lerp ocean → land based on smooth crust mask
                            // so coastlines follow the noise, not the cell edge.
                            nearestHeight = oceanBase
                                + (landH - oceanBase) * localLandness;
                        } else {
                            nearestHeight = oceanBase;
                        }

                        // 2026-05-04: second-nearest crust-mask sampling.
                        // Old code used a flat 0.55 elevation when the
                        // second plate's `isLand` flag was true, which
                        // BRIDGED every adjacent land-plate pair: the
                        // entire boundary stayed above sea level even
                        // when the second plate's local crust mask said
                        // "ocean here". Audit traced this as the dominant
                        // source of Pangaea bias -- two land plates with
                        // 90% landFraction each could never have an
                        // ocean lane between them. Now we sample the
                        // second plate's own crust mask at this world
                        // coord, so ocean-mask tiles produce ocean
                        // boundaries even when both plates are continental.
                        float secondHeight = oceanBase;
                        if (second >= 0) {
                            const Plate& pSecond =
                                plates[static_cast<std::size_t>(second)];
                            float dx2 = wx - pSecond.cx;
                            float dy2 = wy - pSecond.cy;
                            if (cylSim) {
                                if (dx2 >  0.5f) { dx2 -= 1.0f; }
                                if (dx2 < -0.5f) { dx2 += 1.0f; }
                            }
                            const float cs2 = std::cos(pSecond.rot);
                            const float sn2 = std::sin(pSecond.rot);
                            const float lx2 = (dx2 * cs2 + dy2 * sn2) / pSecond.aspect;
                            const float ly2 = (-dx2 * sn2 + dy2 * cs2) * pSecond.aspect;
                            aoc::Random crust2Rng(noiseRng);
                            const float crust2 = fractalNoise(
                                lx2 * 2.0f + pSecond.seedX,
                                ly2 * 2.0f + pSecond.seedY,
                                4, 2.0f, 0.55f, crust2Rng);
                            const float thresh2 = 1.0f - pSecond.landFraction;
                            float t2 = std::clamp(
                                (crust2 - thresh2 + 0.025f) / 0.05f,
                                0.0f, 1.0f);
                            // Wedge override on second plate too.
                            if (pSecond.oceanWedgeWidth > 0.0f) {
                                const float dot2 = lx2 * pSecond.oceanWedgeNx
                                                 + ly2 * pSecond.oceanWedgeNy;
                                if (dot2 > 0.0f
                                    && dot2 < pSecond.oceanWedgeWidth) {
                                    t2 = 0.0f;
                                }
                            }
                            if (pSecond.isLand && t2 > 0.5f) {
                                secondHeight = 0.55f;
                            }
                        }
                        edgeFalloff = nearestHeight * (1.0f - boundary)
                                    + secondHeight * boundary;

                        // Per-tile plate-motion stress block removed —
                        // the orogeny field computed in the multi-epoch
                        // tectonic-sim phase above already encodes
                        // accumulated subduction / collision uplift and
                        // is added below.
                    }

                    // Hotspot plumes: distance-falloff bump regardless
                    // of plate boundary. Stacks above plate elevation,
                    // so a plume in an ocean cell becomes a volcanic
                    // island; on land it forms an isolated highland.
                    // Smaller radius (0.07) and weaker falloff so
                    // plumes don't double-stack with subduction trench
                    // subtractions and create the "ring with water in
                    // the middle" artefact the user reported.
                    for (const Hotspot& h : hotspots) {
                        const float hdx = wx - h.cx;
                        const float hdy = wy - h.cy;
                        const float hdist = std::sqrt(hdx * hdx + hdy * hdy);
                        const float hRadius = 0.04f;
                        if (hdist < hRadius) {
                            const float t = 1.0f - hdist / hRadius;
                            // 2026-05-04: HOTSPOT-ON-RIDGE BOOST. Real
                            // Iceland is anomalously elevated because a
                            // mantle plume sits directly under the
                            // Mid-Atlantic Ridge -- two uplift
                            // mechanisms stack. Detect: hotspot tile
                            // close to a plate boundary (boundary > 0.7
                            // means within outermost 30% of d1/d2
                            // ratio). Double the contribution there.
                            const float ridgeBoost =
                                (boundary > 0.7f) ? 2.0f : 1.0f;
                            edgeFalloff += h.strength * t * t * ridgeBoost;
                        }
                    }
                    // Hotspot TRAIL: each owning plate carries a list
                    // of plate-local positions where a hotspot has sat
                    // in past epochs. Sample tile's plate-local coords
                    // against the trail; nearby trail points produce
                    // a small island bump. Older points produce smaller
                    // bumps (eroded). This creates the Hawaiian-Emperor
                    // chain pattern — a line of decreasing-elevation
                    // islands trailing back along the plate's path.
                    if (nearest >= 0) {
                        const Plate& tp = plates[static_cast<std::size_t>(nearest)];
                        const std::size_t trailLen = tp.hotspotTrail.size();
                        for (std::size_t k = 0; k < trailLen; ++k) {
                            const std::pair<float, float>& tp_pt = tp.hotspotTrail[k];
                            // tp_pt is in plate-local frame; lxNearest/
                            // lyNearest is too. Direct distance.
                            const float trdx = lxNearest - tp_pt.first;
                            const float trdy = lyNearest - tp_pt.second;
                            const float trdist = std::sqrt(trdx * trdx + trdy * trdy);
                            constexpr float TR_RADIUS = 0.025f;
                            if (trdist < TR_RADIUS) {
                                // Atoll → guyot lifecycle: oldest trail
                                // points (k=0) have eroded flat AND
                                // sunk below water as the plate cooled
                                // and subsided. They contribute no
                                // elevation but the underwater seamount
                                // remains. Quadratic age decay: only
                                // the most recent ~40 % of the trail
                                // breaks the surface. Real Hawaiian-
                                // Emperor: only the youngest 5-6
                                // islands are subaerial; the rest are
                                // guyots / drowned seamounts.
                                const float ageT = static_cast<float>(k + 1)
                                                 / static_cast<float>(trailLen);
                                const float t = 1.0f - trdist / TR_RADIUS;
                                const float ageBoost = ageT * ageT;
                                edgeFalloff += 0.18f * t * t * ageBoost;
                            }
                        }
                    }
                    // Narrow hard floor: only the deep interior
                    // (boundary < 0.15, i.e. >85% of the way from
                    // the plate edge to its centre) is protected from
                    // going below water. The wider 0.40 floor was
                    // blocking the noise from carving coastal features
                    // (bays, peninsulas, gulfs) into land plates.
                    if (nearest >= 0 && nearestIsLand && boundary < 0.15f) {
                        edgeFalloff = std::max(edgeFalloff, 0.55f);
                    }
                    // 2026-05-05 Phase 13d-1: mid-ocean ridge bathymetry.
                    // Real Earth divergent boundaries (Mid-Atlantic Ridge,
                    // East Pacific Rise) sit ~3000 m above the abyssal
                    // plain because newly-formed crust is hot and thermally
                    // buoyant. We don't track crust age in PhysicsGrid
                    // post-Phase-13a, so use distance-from-edge as a
                    // proxy: ridge axis = youngest = shallowest. Ocean-
                    // only -- continental rifts (e.g. East African Rift)
                    // are handled separately by oceanWedge logic.
                    if (nearest >= 0 && !nearestIsLand) {
                        const Plate& pr = plates[
                            static_cast<std::size_t>(nearest)];
                        const std::size_t Nb = pr.boundaryVertices.size();
                        if (Nb >= 3 && pr.boundaryEdgeTypes.size() == Nb) {
                            const float csP = std::cos(pr.rot);
                            const float snP = std::sin(pr.rot);
                            float minDsq = 1e9f;
                            for (std::size_t bi = 0; bi < Nb; ++bi) {
                                if (pr.boundaryEdgeTypes[bi] != 1u) {
                                    continue;
                                }
                                const std::size_t bj = (bi + 1) % Nb;
                                const float ax = pr.cx
                                    + pr.boundaryVertices[bi].first  * csP
                                    - pr.boundaryVertices[bi].second * snP;
                                const float ay = pr.cy
                                    + pr.boundaryVertices[bi].first  * snP
                                    + pr.boundaryVertices[bi].second * csP;
                                const float bx = pr.cx
                                    + pr.boundaryVertices[bj].first  * csP
                                    - pr.boundaryVertices[bj].second * snP;
                                const float by = pr.cy
                                    + pr.boundaryVertices[bj].first  * snP
                                    + pr.boundaryVertices[bj].second * csP;
                                float ex = bx - ax;
                                float ey = by - ay;
                                float px = wx - ax;
                                float py = wy - ay;
                                if (cylSim) {
                                    if (ex >  0.5f) { ex -= 1.0f; }
                                    if (ex < -0.5f) { ex += 1.0f; }
                                    if (px >  0.5f) { px -= 1.0f; }
                                    if (px < -0.5f) { px += 1.0f; }
                                }
                                const float eLenSq = ex * ex + ey * ey;
                                float t = 0.0f;
                                if (eLenSq > 1e-9f) {
                                    t = std::clamp(
                                        (px * ex + py * ey) / eLenSq,
                                        0.0f, 1.0f);
                                }
                                const float qx = ax + t * ex;
                                const float qy = ay + t * ey;
                                float dx = wx - qx;
                                float dy = wy - qy;
                                if (cylSim) {
                                    if (dx >  0.5f) { dx -= 1.0f; }
                                    if (dx < -0.5f) { dx += 1.0f; }
                                }
                                const float dsq = dx * dx + dy * dy;
                                if (dsq < minDsq) { minDsq = dsq; }
                            }
                            // RIDGE_RANGE 0.015 ~= 2 tiles at 140-col
                            // grid; a ~430 km flank matches Mid-Atlantic
                            // Ridge axial-band width. RIDGE_MAX_BONUS
                            // 0.04 in unit-elev space (oceanBase 0.10 ->
                            // ridge crest 0.14), still well below 0.50
                            // land threshold so ridges remain ocean.
                            constexpr float RIDGE_RANGE      = 0.015f;
                            constexpr float RIDGE_MAX_BONUS  = 0.04f;
                            if (minDsq < RIDGE_RANGE * RIDGE_RANGE) {
                                const float dist = std::sqrt(minDsq);
                                const float k = 1.0f - dist / RIDGE_RANGE;
                                ridgeBonus = RIDGE_MAX_BONUS * k * k;
                            }
                        }
                    }
                } else {
                    for (const LandCenter& center : landCenters) {
                        float dx = (wx - center.cx) * 2.0f;
                        float dy = (wy - center.cy) * 2.0f;
                        float distFromCenter = std::sqrt(dx * dx + dy * dy);
                        float falloff = 1.0f - std::clamp(distFromCenter * center.strength, 0.0f, 1.0f);
                        falloff = smoothstep(falloff);
                        edgeFalloff = std::max(edgeFalloff, falloff);
                    }
                }
            }

            // Blend noise + plate/falloff. Continents use a
            // plate-base + small-amplitude noise model so deep inland
            // tiles can't dip below the water threshold from noise
            // alone — eliminates the "swiss cheese" inland-lake
            // problem. Noise still varies the surface (mountain bands,
            // hill swells) but its excursion is bounded so neither side
            // of the threshold flips spuriously.
            // 2026-05-03: Fractal removed; only Continents branch remains.
            if (config.mapType == MapType::Continents) {
                // Continents: plate-anchored base + integrated orogeny
                // from the multi-epoch tectonic-sim phase + small noise
                // ripple. Mountains appear ONLY where stress accumulated
                // (real subduction zones / collision belts), so passive
                // coasts stay flat — fixes the "every coast has a
                // mountain range" artefact from the older per-tile
                // stress model.
                const float noiseCentred = elev - 0.5f;
                const float oro = orogeny[static_cast<std::size_t>(row * width + col)];
                // GLACIAL ISOSTATIC REBOUND. Polar regions that were
                // covered by ice sheets during the last glacial maximum
                // are still rising as the crust rebounds (Scandinavia
                // rises ~1 cm/yr, Hudson Bay similar). Add a small
                // elevation boost to high-latitude tiles. Strongest
                // at the poles, falls off into mid-lat.
                const float latFromEq = 2.0f * std::abs(ny - 0.5f);
                float reboundBoost = 0.0f;
                if (latFromEq > 0.55f) {
                    const float t = (latFromEq - 0.55f) / 0.45f;
                    reboundBoost = 0.03f * t;
                }
                elev = edgeFalloff + noiseCentred * 0.16f + oro + reboundBoost
                     + ridgeBonus;
            } else {
                elev = elev * 0.55f + edgeFalloff * 0.45f;
            }

            elevationMap[static_cast<std::size_t>(row * width + col)] = elev;
        }
    }

    // 2026-05-03: plate-id stash + Voronoi smoothing extracted to
    // gen/PlateIdStash.cpp.
    if (config.mapType == MapType::Continents && !plates.empty()) {
        aoc::map::gen::runPlateIdStash(grid, cylSim, plates, noiseRng);
    }
    if (config.mapType == MapType::Continents && !plates.empty()) {
        // Persist hotspot positions for the overlay.
        std::vector<std::pair<float, float>> hsCopy;
        hsCopy.reserve(hotspots.size());
        for (const Hotspot& h : hotspots) {
            hsCopy.emplace_back(h.cx, h.cy);
        }
        grid.setHotspots(std::move(hsCopy));
        // 2026-05-06: PHYSICS-FIRST P4.2 -- HexGrid polygon-overlay
        // setter calls deleted (`setPlateMotions`, `setPlateCenters`,
        // `setPlatePolygons`, `setPlatePolygonEdgeTypes`,
        // `setPlatePolygonNeighborIds`). Renderer/save consumes
        // `HexGrid::sphereFieldElevationSnapshot()` (P1). Other plate-
        // setter calls (LandFrac/CrustAge/MergesAbsorbed/IsPolar/
        // LatLon/Weight/EulerPole/AngularVelDeg/Rot) preserved -- they
        // drive the plate-overlay HUD which still runs.
        std::vector<float>                   landFracs;
        std::vector<float>                   crustAges;
        std::vector<int32_t>                 mergesAbsorbed;
        std::vector<uint8_t>                 isPolar;
        std::vector<std::pair<float, float>> latLons;
        std::vector<float>                   weights;
        std::vector<std::pair<float, float>> eulerPoles;
        std::vector<float>                   angularVelDegs;
        std::vector<float>                   rots;
        landFracs.reserve(plates.size());
        crustAges.reserve(plates.size());
        mergesAbsorbed.reserve(plates.size());
        isPolar.reserve(plates.size());
        latLons.reserve(plates.size());
        weights.reserve(plates.size());
        eulerPoles.reserve(plates.size());
        angularVelDegs.reserve(plates.size());
        rots.reserve(plates.size());
        for (const Plate& p : plates) {
            landFracs.push_back(p.landFraction);
            crustAges.push_back(p.crustAge);
            mergesAbsorbed.push_back(p.mergesAbsorbed);
            isPolar.push_back(p.isPolar ? 1u : 0u);
            latLons.emplace_back(p.latDeg, p.lonDeg);
            weights.push_back(p.weight);
            eulerPoles.emplace_back(p.eulerPoleLatDeg, p.eulerPoleLonDeg);
            angularVelDegs.push_back(p.angularVelDeg);
            rots.push_back(p.rot);
        }
        grid.setPlateLandFrac(std::move(landFracs));
        grid.setPlateCrustAge(std::move(crustAges));
        grid.setPlateMergesAbsorbed(std::move(mergesAbsorbed));
        grid.setPlateIsPolar(std::move(isPolar));
        grid.setPlateLatLon(std::move(latLons));
        grid.setPlateWeight(std::move(weights));
        grid.setPlateEulerPole(std::move(eulerPoles));
        grid.setPlateAngularVelDeg(std::move(angularVelDegs));
        grid.setPlateRot(std::move(rots));

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
        grid, config.mapType, effectiveWaterRatio, config.mountainRatio,
        elevationMap, thresholds);
    std::vector<float>&   mountainElev      = thresholds.mountainElev;
    std::vector<int32_t>& distFromCoast     = thresholds.distFromCoast;
    const float           waterThreshold    = thresholds.waterThreshold;
    const float           mountainThreshold = thresholds.mountainThreshold;

    // 2026-05-03: 2-D climate model + biome assignment extracted to
    // gen/ClimateBiome.cpp.
    aoc::map::gen::runClimateBiomePass(grid, config, rng, elevationMap,
                                        mountainElev, distFromCoast,
                                        orogeny, waterThreshold,
                                        mountainThreshold);

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
        // subduction trenches as the plate slides under continents
        // (Hawaii→Aleutian, Emperor seamounts). Over geological time
        // the entire chain gets recycled into the mantle. We mirror
        // this by drowning more small islands on long sims:
        //   40 epochs  → threshold 14 (Sicily, Iceland survive)
        //   100 epochs → threshold 22
        //   200 epochs → threshold 32 (only proper continents survive)
        const int32_t simEpochs = (config.tectonicEpochs > 0)
            ? config.tectonicEpochs : 40;
        const int32_t MIN_ISLAND_SIZE = std::clamp(
            12 + simEpochs / 10, 12, 50);
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

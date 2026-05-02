/**
 * @file MapGenerator.cpp
 * @brief Procedural map generation using layered value noise.
 */

#include "aoc/map/MapGenerator.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/map/Chokepoint.hpp"

#include <algorithm>
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

// ============================================================================
// Noise utilities
// ============================================================================

/// Simple hash-based value noise. Deterministic for a given (ix, iy, seed).
static float hashNoise(int32_t ix, int32_t iy, uint64_t seed) {
    // Combine coordinates and seed into a single hash
    uint64_t h = seed;
    h ^= static_cast<uint64_t>(ix) * 0x517cc1b727220a95ULL;
    h ^= static_cast<uint64_t>(iy) * 0x6c62272e07bb0142ULL;
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
    h = h ^ (h >> 31);
    return static_cast<float>(h & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

static float smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

static float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float MapGenerator::noise2D(float x, float y, float frequency, aoc::Random& rng) {
    // Use rng state as noise seed so it's deterministic per-generation
    uint64_t noiseSeed = rng.next();

    float fx = x * frequency;
    float fy = y * frequency;

    int32_t ix = static_cast<int32_t>(std::floor(fx));
    int32_t iy = static_cast<int32_t>(std::floor(fy));

    float tx = fx - static_cast<float>(ix);
    float ty = fy - static_cast<float>(iy);

    tx = smoothstep(tx);
    ty = smoothstep(ty);

    float c00 = hashNoise(ix,     iy,     noiseSeed);
    float c10 = hashNoise(ix + 1, iy,     noiseSeed);
    float c01 = hashNoise(ix,     iy + 1, noiseSeed);
    float c11 = hashNoise(ix + 1, iy + 1, noiseSeed);

    float top    = lerp(c00, c10, tx);
    float bottom = lerp(c01, c11, tx);
    return lerp(top, bottom, ty);
}

float MapGenerator::fractalNoise(float x, float y, int octaves, float frequency,
                                  float persistence, aoc::Random& rng) {
    float value     = 0.0f;
    float amplitude = 1.0f;
    float maxValue  = 0.0f;
    float freq      = frequency;

    for (int i = 0; i < octaves; ++i) {
        value    += noise2D(x, y, freq, rng) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        freq      *= 2.0f;
    }

    return value / maxValue;  // Normalize to [0, 1]
}

// ============================================================================
// Generation steps
// ============================================================================

void MapGenerator::generate(const Config& config, HexGrid& outGrid) {
    outGrid.initialize(config.width, config.height, config.topology);

    aoc::Random rng(config.seed);

    if (config.mapType == MapType::LandWithSeas) {
        generateRealisticTerrain(config, outGrid, rng);
        smoothCoastlines(outGrid);
        assignFeatures(config, outGrid, rng);
        generateRivers(outGrid, rng);
        placeNaturalWonders(outGrid, rng);
    } else {
        assignTerrain(config, outGrid, rng);
        smoothCoastlines(outGrid);
        assignFeatures(config, outGrid, rng);
        generateRivers(outGrid, rng);
        placeNaturalWonders(outGrid, rng);
    }

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
    switch (config.placement) {
        case ResourcePlacementMode::Random:
            placeRandomResources(config, outGrid, rng);
            break;
        case ResourcePlacementMode::Fair:
            if (config.mapType == MapType::LandWithSeas) {
                placeGeologyResources(config, outGrid, rng);
            } else {
                placeBasicResources(config, outGrid, rng);
            }
            balanceResourcesFair(config, outGrid, rng);
            break;
        case ResourcePlacementMode::Realistic:
        default:
            if (config.mapType == MapType::LandWithSeas) {
                placeGeologyResources(config, outGrid, rng);
            } else {
                placeBasicResources(config, outGrid, rng);
            }
            break;
    }

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
    struct Plate {
        float cx;
        float cy;
        bool  isLand;
        float vx;       ///< Plate motion x-component (normalised units)
        float vy;       ///< Plate motion y-component
        float aspect;   ///< X stretch factor; >1 elongates east-west
        float rot;      ///< Cell rotation in radians (base + oscillation)
        float baseRot;  ///< Initial rotation, fixed at plate creation
        float baseAspect; ///< Initial aspect, fixed at plate creation
        // Crust-mask seed in PLATE-LOCAL space. Stays fixed for the
        // plate's lifetime, so the land/ocean pattern within the plate
        // travels WITH the plate as it drifts. Real plates carry their
        // continental crust from one ocean basin to another (India
        // moved from off Madagascar to its present collision with
        // Eurasia and kept its shape) — sampling a per-plate noise
        // mask in plate-local coords reproduces that.
        float seedX;
        float seedY;
        float landFraction; ///< 0=pure ocean, 1=pure continental, 0.5=half/half
        int32_t ageEpochs = 0;
        /// Per-plate Voronoi weight. Real Earth plate sizes vary
        /// ~50× (Pacific ≈ 100M km², Cocos ≈ 3M km²). Multiplicatively
        /// weighted Voronoi gives each plate a "claim radius" — high
        /// weight = larger territory.
        float weight = 1.0f;
        /// Extra Voronoi seeds in WORLD coords. When non-empty, the
        /// plate's Voronoi distance uses min over all seeds. Lets a
        /// single plate cover an irregular L/U/wrapped territory
        /// (Eurasian Plate covers Europe + most of Asia + Arctic
        /// fringe — not a convex Voronoi blob).
        std::vector<std::pair<float, float>> extraSeeds;
        /// True for the two stationary polar plates (Arctic / Antarctic
        /// analogues). They drift very little so the polar band stays
        /// covered consistently across the sim.
        bool isPolar = false;
        // Hotspot track in PLATE-LOCAL coords.
        std::vector<std::pair<float, float>> hotspotTrail;
        // Orogeny field in PLATE-LOCAL coordinates. 64×64 grid covering
        // plate-local box [-2, 2] × [-2, 2] (resolution ~0.06 per cell).
        // Stores accumulated mountain-building stress AT POSITIONS ON THE
        // PLATE — when the plate drifts, the orogeny travels with it
        // (Variscan-style: Harz still on Europe after 300 My of motion).
        std::vector<float> orogenyLocal;
        /// Wilson conservation tracking. Real Earth: oceanic crust forms
        /// at mid-ocean ridges and is destroyed at subduction trenches.
        /// We model the destruction side directly: each epoch a plate's
        /// crustArea shrinks by its current subduction-tile count; when
        /// an oceanic plate's area drops below a threshold its existence
        /// ends and the Voronoi cells of neighbouring plates expand into
        /// the vacated space. That expansion IS the ridge-spreading: the
        /// surviving plate's perimeter that just gained territory carries
        /// "fresh crust" with low age.
        float crustArea        = 1.0f;  ///< Current size, shrinks via subduction
        float crustAreaInitial = 1.0f;  ///< Birth size, for fractional threshold
        /// Number of times this plate has absorbed another via merge.
        /// Plates with 0 merges + age > threshold = biogeographically
        /// isolated realm (Australia, Madagascar, Antarctica). Drives
        /// the IsolatedRealm overlay + endemic-civ flag.
        int32_t mergesAbsorbed = 0;
        /// Slab-tear event flag: oceanic plate flagged this epoch as
        /// having torn. Triggers adjacent-plate velocity perturbation.
        int8_t  slabTornThisEpoch = 0;
        /// Mean crust age in epochs. Older oceanic crust is colder and
        /// denser → subducts more aggressively. Slab-pull magnitude scales
        /// with crustAge so a long-lived oceanic plate (e.g. Pacific,
        /// ~180 My oldest crust) produces stronger pull than a fresh one
        /// just rifted off a continent. Continental crust accumulates
        /// age too but it doesn't drive subduction (low density).
        float crustAge         = 0.0f;
    };
    // Per-plate orogeny grid resolution. Default 64×64, scaled by
    // config.superSampleFactor (1 = default, 2 = 128×128, 4 = 256×256).
    // Higher resolution = sharper boundary precision + sub-hex detail
    // at cost of memory/compute. Was constexpr; now driven by config.
    const int32_t OROGENY_GRID = 64 * std::max(1, config.superSampleFactor);
    constexpr float   OROGENY_HALF = 2.0f; // local-frame half-extent
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
            const int32_t landCountTarget = (config.landPlateCount > 0)
                ? std::max(1, config.landPlateCount)
                : centerRng.nextInt(6, 8);
            // More ocean plates = tighter Voronoi cells around land =
            // narrower continental shelves, more separated landmasses.
            // Real Earth: 7 major + 8 minor = ~15 plates total. With
            // ~7 land seeds + ~5 ocean + 6 forced polar = 18 plates,
            // close to Earth's count. Earlier 12-16 produced 25-29
            // initial plates → too many small territories.
            const int32_t oceanCountTarget = centerRng.nextInt(4, 6);
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
                p.vx = centerRng.nextFloat(-0.70f, 0.70f);
                p.vy = centerRng.nextFloat(-0.70f, 0.70f);
                p.aspect     = centerRng.nextFloat(0.95f, 1.10f);
                p.rot        = centerRng.nextFloat(-3.14159f, 3.14159f);
                p.baseRot    = p.rot;
                p.baseAspect = p.aspect;
                // Per-plate crust-mask seed (large random offsets so each
                // plate samples a unique noise neighborhood). Stays fixed
                // through the whole sim — pattern travels with the plate.
                p.seedX = centerRng.nextFloat(0.0f, 1000.0f);
                p.seedY = centerRng.nextFloat(0.0f, 1000.0f);
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
                // Bimodal: continental plates ARE the continent (Africa
                // ≈ African plate). Push to 0.85-0.95 so plate identity
                // and continent identity overlap. Oceanic plates 0.02-
                // 0.08 — almost pure ocean, occasional volcanic island
                // along hotspot trail.
                p.landFraction = isLand
                    ? centerRng.nextFloat(0.85f, 0.95f)
                    : centerRng.nextFloat(0.02f, 0.08f);
                p.orogenyLocal.assign(
                    static_cast<std::size_t>(OROGENY_GRID * OROGENY_GRID), 0.0f);
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
                const float irregRoll = centerRng.nextFloat(0.0f, 1.0f);
                const int32_t extras = (irregRoll < 0.15f) ? 2
                                     : (irregRoll < 0.60f) ? 1 : 0;
                const bool cylC = (config.topology == MapTopology::Cylindrical);
                for (int32_t e = 0; e < extras; ++e) {
                    const float ang = centerRng.nextFloat(0.0f, 6.2832f);
                    const float off = centerRng.nextFloat(0.10f, 0.22f) * p.weight;
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
                p.landFraction = centerRng.nextFloat(0.02f, 0.08f);
                p.orogenyLocal.assign(
                    static_cast<std::size_t>(OROGENY_GRID * OROGENY_GRID), 0.0f);
                // Modest weight + many extra seeds = thin band coverage.
                // weight 1.15 lets normal mid-lat plates still claim
                // their territories, while extra seeds keep the polar
                // strip continuous across the map width.
                p.weight = 1.15f;
                p.crustArea = p.weight * p.weight;
                p.crustAreaInitial = p.crustArea;
                p.crustAge = centerRng.nextFloat(80.0f, 160.0f); // polar = old, stable
                p.isPolar = true;
                p.extraSeeds.emplace_back(0.15f, cy0);
                p.extraSeeds.emplace_back(0.35f, cy0);
                p.extraSeeds.emplace_back(0.65f, cy0);
                p.extraSeeds.emplace_back(0.85f, cy0);
                plates.push_back(p);
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
            effectiveWaterRatio = std::clamp(config.waterRatio, 0.40f, 0.55f);
            break;
        }
        // (continents tectonic-sim runs after the switch — see below)
        case MapType::Islands: {
            // Many small islands. Higher strength = tighter/smaller blobs.
            // Water ratio pushed up so only the noise peaks stay above sea.
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
            // 2-3 strong continents + scattered weaker island chains.
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
            // Single broad center covering the whole map. Very low water ratio
            // leaves only tiny lakes where noise dips deepest.
            landCenters.push_back({0.5f, 0.5f, 0.9f});
            effectiveWaterRatio = std::min(config.waterRatio, 0.08f);
            break;
        }
        case MapType::Fractal: {
            // No gradient centers -- pure noise
            break;
        }
        case MapType::LandWithSeas: {
            // Handled by generateRealisticTerrain(); should not reach here.
            break;
        }
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
    // Suture record: world-coord points where cont-cont mergers happened
    // during the sim. Used post-sim to mark ophiolite tiles + paint rock
    // type along the fossil seam.
    struct SutureSeam { float x; float y; float r; };
    std::vector<SutureSeam> sutureSeams;
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
        // Slower rift cycle (8 epochs) so plate count grows less over
        // a sim. Real Wilson-cycle rifting events are rare (~once per
        // 100 My); over 40 epochs we want maybe 4-5 rifts.
        const int32_t CYCLE = 8;
        // DT derived from EPOCHS and the user-configurable total drift
        // budget. Default drift = 0.6 map widths. Larger drift = bigger
        // plate motion = more dramatic continental shuffle. Smaller =
        // plates barely move, fine-grained evolution at small scale.
        const float driftFrac = (config.driftFraction > 0.0f)
            ? config.driftFraction : 0.6f;
        const float DT = std::clamp(
            (driftFrac / 0.7f) / static_cast<float>(EPOCHS),
            0.001f, 0.040f);
        // Stress gate. 0.30 captures most active convergent margins.
        // With slower DT and per-epoch scaling, contributions are
        // smaller per step, so a lower gate is needed to let stress
        // accumulate to mountain-tall over the sim.
        // Lower gate so MORE boundaries register (with smaller per-
        // epoch contribution) — produces a gradient: weak boundaries
        // equilibrate around Hills, strongest reach Mountain.
        constexpr float   STRESS_GATE = 0.40f;

        // Snapshot starting positions so we can restore the final ones
        // for the rendering pass below — stress accumulates across the
        // whole sim regardless of which positions render.
        const std::vector<Plate> startPlates = plates;

        for (int32_t epoch = 0; epoch < EPOCHS; ++epoch) {
            // Advance every plate by (vx, vy) * DT. Cylindrical maps WRAP
            // around the X axis (no east/west edge — like a globe band);
            // flat maps BOUNCE so plates stay on the rectangle.
            // Y always bounces (poles aren't wrap-connected).
            for (Plate& p : plates) {
                // Polar plates barely drift — Antarctica is essentially
                // stationary on geological timescales. Apply 0.3× DT
                // to keep them anchored at the poles.
                const float motionScale = p.isPolar ? 0.3f : 1.0f;
                p.cx += p.vx * DT * motionScale;
                p.cy += p.vy * DT * motionScale;
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
            }

            // WILSON SUPERCONTINENT CYCLE. Real Earth alternates
            // between supercontinent assembly (Pangea, Rodinia, ...)
            // and dispersal over ~400-600 My. We approximate by
            // applying a periodic radial force on land plates around
            // their collective centroid: cos(phase) > 0 pulls plates
            // toward the centroid (assembly), cos(phase) < 0 pushes
            // them outward (dispersal). One Wilson period spans
            // EPOCHS/2.5 ≈ 48 epochs at default 120 → ~3 cycles per
            // sim. Without this, plates set their initial velocity
            // and never reverse → no Pangea cycle is visible.
            {
                const int32_t WILSON_PERIOD = std::max(20,
                    static_cast<int32_t>(static_cast<float>(EPOCHS) / 2.5f));
                const float phase = 6.2832f
                    * static_cast<float>(epoch)
                    / static_cast<float>(WILSON_PERIOD);
                // Positive sign = assembly (pull toward centroid).
                const float assemblyForce = std::cos(phase);
                // Centroid of land plates (excluding polar — they
                // don't participate in supercontinent cycles).
                float ccx = 0.0f;
                float ccy = 0.0f;
                int32_t nLand = 0;
                for (const Plate& p : plates) {
                    if (p.isLand && !p.isPolar) {
                        ccx += p.cx;
                        ccy += p.cy;
                        ++nLand;
                    }
                }
                if (nLand >= 2) {
                    ccx /= static_cast<float>(nLand);
                    ccy /= static_cast<float>(nLand);
                    constexpr float WILSON_AMP = 0.012f;
                    for (Plate& p : plates) {
                        if (!p.isLand || p.isPolar) { continue; }
                        float dxC = ccx - p.cx;
                        float dyC = ccy - p.cy;
                        if (cylSim) {
                            if (dxC >  0.5f) { dxC -= 1.0f; }
                            if (dxC < -0.5f) { dxC += 1.0f; }
                        }
                        // Toward centroid when assembling, away when
                        // dispersing. Magnitude proportional to dist
                        // so distant plates feel a stronger pull.
                        p.vx += WILSON_AMP * assemblyForce * dxC;
                        p.vy += WILSON_AMP * assemblyForce * dyC;
                    }
                }
            }

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
                    constexpr float MERGE_DIST   = 0.10f;
                    constexpr float CONTACT_DIST = 0.16f;
                    constexpr float SLOW_V       = 0.08f;
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
                    // STAGE 3: full merge. Distance close + collision
                    // velocity nearly zero → plates have welded.
                    const bool readyToMerge = (plates[a].isLand && plates[b].isLand
                                                && d < MERGE_DIST
                                                && relVMag < SLOW_V);
                    if (readyToMerge) {
                        // Continental collision: fuse plates.
                        //
                        // TERRANE ACCRETION. When B docks against A,
                        // its mountain belts (orogeny field) become
                        // part of A's geology — like the Cordilleran
                        // terranes glued onto western N America, or
                        // Avalonia onto eastern N America. Transfer
                        // B's positive orogeny into A's local grid at
                        // a position offset toward the merger seam.
                        // This appears as a fossil mountain belt
                        // welded into the merged continent.
                        const float offX = plates[b].cx - plates[a].cx;
                        const float offY = plates[b].cy - plates[a].cy;
                        const float csA = std::cos(plates[a].rot);
                        const float snA = std::sin(plates[a].rot);
                        const float seamLx = (offX * csA + offY * snA)
                                           / plates[a].aspect * 0.5f;
                        const float seamLy = (-offX * snA + offY * csA)
                                           * plates[a].aspect * 0.5f;
                        // Sum B's positive orogeny mass and deposit
                        // it as a Hills-tier patch at the seam.
                        float terraneMass = 0.0f;
                        for (float v : plates[b].orogenyLocal) {
                            if (v > 0.0f) { terraneMass += v; }
                        }
                        // Spread over a small disc (~0.25 plate-local
                        // radius) at the seam location.
                        constexpr float TERRANE_RADIUS = 0.25f;
                        // Rough normalisation: total mass / area = uniform height.
                        const float depositHeight = std::min(0.18f,
                            terraneMass * 0.001f);
                        for (int32_t gy = 0; gy < OROGENY_GRID; ++gy) {
                            for (int32_t gx = 0; gx < OROGENY_GRID; ++gx) {
                                const float lx = (static_cast<float>(gx) + 0.5f)
                                               / static_cast<float>(OROGENY_GRID)
                                               * (2.0f * OROGENY_HALF) - OROGENY_HALF;
                                const float ly = (static_cast<float>(gy) + 0.5f)
                                               / static_cast<float>(OROGENY_GRID)
                                               * (2.0f * OROGENY_HALF) - OROGENY_HALF;
                                const float dxL = lx - seamLx;
                                const float dyL = ly - seamLy;
                                const float dL = std::sqrt(dxL * dxL + dyL * dyL);
                                if (dL < TERRANE_RADIUS) {
                                    const float t = 1.0f - dL / TERRANE_RADIUS;
                                    plates[a].orogenyLocal[static_cast<std::size_t>(
                                        gy * OROGENY_GRID + gx)] += depositHeight * t * t;
                                }
                            }
                        }
                        // Suture seam record: world-coord midpoint and
                        // a radius proportional to the merged plate's
                        // size. Used post-sim to mark ophiolite tiles
                        // along the fossil collision boundary (Indus-
                        // Tsangpo Suture, Iapetus Suture, etc).
                        sutureSeams.push_back({
                            (plates[a].cx + plates[b].cx) * 0.5f,
                            (plates[a].cy + plates[b].cy) * 0.5f,
                            std::min(plates[a].weight, plates[b].weight) * 0.18f
                        });
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
                        // Smaller rebound area + height — earlier values
                        // dumped +0.05 across most of the merged plate
                        // each merge, eventually pushing every tile
                        // above mountain threshold.
                        constexpr float SLAB_REBOUND_RADIUS = 0.18f;
                        constexpr float SLAB_REBOUND_HEIGHT = 0.02f;
                        for (int32_t gy = 0; gy < OROGENY_GRID; ++gy) {
                            for (int32_t gx = 0; gx < OROGENY_GRID; ++gx) {
                                const float lx = (static_cast<float>(gx) + 0.5f)
                                               / static_cast<float>(OROGENY_GRID)
                                               * (2.0f * OROGENY_HALF) - OROGENY_HALF;
                                const float ly = (static_cast<float>(gy) + 0.5f)
                                               / static_cast<float>(OROGENY_GRID)
                                               * (2.0f * OROGENY_HALF) - OROGENY_HALF;
                                const float dxL = lx - seamLx;
                                const float dyL = ly - seamLy;
                                const float dL = std::sqrt(dxL * dxL + dyL * dyL);
                                if (dL < SLAB_REBOUND_RADIUS) {
                                    const float t = 1.0f - dL / SLAB_REBOUND_RADIUS;
                                    plates[a].orogenyLocal[static_cast<std::size_t>(
                                        gy * OROGENY_GRID + gx)] += SLAB_REBOUND_HEIGHT * t;
                                }
                            }
                        }
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
                    const float blockDrift =
                        static_cast<float>(epoch) * 0.0025f
                        * std::sin(p.seedX * 0.007f);
                    rotOffset += blockDrift;
                }
                p.rot = p.baseRot + rotOffset;
                // Aspect oscillates ±0.08 around baseAspect.
                const float aspectOsc = std::cos(phase * 0.7f + p.seedX * 0.002f) * 0.08f;
                p.aspect = std::clamp(p.baseAspect + aspectOsc, 0.7f, 1.40f);
            }

            // Periodic rift events. Picks 1-3 land plates per CYCLE epochs
            // and splits each one in 1-2 ways, sometimes producing a
            // triple junction (3 children from one parent). Each child
            // sits offset on a randomly oriented fault axis and inherits
            // a velocity rotated by a small angle from the parent —
            // models how Pangaea broke into Africa, S America, India, etc.
            // along irregular non-orthogonal fault systems.
            const bool isRiftEpoch = (epoch > 0) && ((epoch % CYCLE) == 0);
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
                // Tight cap: 18-22 plates total max. Earth has ~15.
                // Earlier 2× allowed 36+ plates → fragmented appearance.
                const std::size_t maxPlates = std::max(
                    static_cast<std::size_t>(18),
                    startPlates.size() + static_cast<std::size_t>(4));
                // Only ONE rift per CYCLE epochs (was up to 3).
                // Multiple simultaneous rifts produced abrupt visual
                // jumps — real Wilson-cycle rifting happens piecemeal
                // over millions of years, not all at once.
                const int32_t splitsThisEpoch =
                    (plates.size() >= maxPlates)
                        ? 0
                        : std::min(1, static_cast<int32_t>(landIdx.size()));
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
                    const bool tripleSplit = (centerRng.nextFloat(0.0f, 1.0f) < 0.30f);
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
                        const float ax = std::cos(faultAxis);
                        const float ay = std::sin(faultAxis);
                        constexpr float SCAR_HALFLEN = 0.7f;
                        constexpr float SCAR_HALFWID = 0.05f;
                        constexpr float SCAR_DEPTH   = -0.10f;
                        for (int32_t gy = 0; gy < OROGENY_GRID; ++gy) {
                            for (int32_t gx = 0; gx < OROGENY_GRID; ++gx) {
                                const float lx =
                                    (static_cast<float>(gx) + 0.5f)
                                    / static_cast<float>(OROGENY_GRID)
                                    * (2.0f * OROGENY_HALF) - OROGENY_HALF;
                                const float ly =
                                    (static_cast<float>(gy) + 0.5f)
                                    / static_cast<float>(OROGENY_GRID)
                                    * (2.0f * OROGENY_HALF) - OROGENY_HALF;
                                const float along =  lx * ax + ly * ay;
                                const float perp  = -lx * ay + ly * ax;
                                if (std::abs(along) < SCAR_HALFLEN
                                    && std::abs(perp)  < SCAR_HALFWID) {
                                    const float falloff = (1.0f
                                        - std::abs(perp) / SCAR_HALFWID);
                                    plates[pi].orogenyLocal[
                                        static_cast<std::size_t>(
                                            gy * OROGENY_GRID + gx)]
                                        += SCAR_DEPTH * falloff;
                                }
                            }
                        }
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
                        // First child: opposite side of the rift line
                        // from the parent (i.e., along -pushAxis).
                        // Second child (triple junction): rotated by
                        // 120° from parent's pushAxis to form the third
                        // arm of an Afar-style triple junction.
                        const float childPushAngle = (c == 0)
                            ? (pushAxis + 3.14159f)        // -pushAxis
                            : (pushAxis + 2.0944f);        // +120°
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
                        // Children inherit most of parent's landFraction
                        // — a child plate is a chunk of the original
                        // continent and stays mostly land. Mild reduction
                        // represents the smaller area covered.
                        child.landFraction = std::clamp(
                            parent.landFraction * centerRng.nextFloat(0.80f, 1.00f),
                            0.30f, 0.85f);
                        // Children get a fresh empty orogeny grid —
                        // they're "new crust" formed at the rift, not
                        // Hotspot trails reset (each fragment has its
                        // own future trail). Orogeny grid also reset:
                        // child is "new crust" formed at the rift,
                        // not carrying parent's mountain memory.
                        child.hotspotTrail.clear();
                        child.orogenyLocal.assign(
                            static_cast<std::size_t>(OROGENY_GRID * OROGENY_GRID), 0.0f);
                        // Rift child = young plate. Crust age resets
                        // because crust formed at the rift axis is fresh
                        // (Atlantic is younger than Pacific because it's
                        // post-Pangea). Crust area scales with new
                        // weight squared.
                        child.crustArea = child.weight * child.weight;
                        child.crustAreaInitial = child.crustArea;
                        child.crustAge = 0.0f;
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
                if (plates.size() < maxPlates
                    && plates.size() >= 4
                    && centerRng.nextFloat(0.0f, 1.0f) < 0.05f) {
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
                        micro.vx = centerRng.nextFloat(-0.5f, 0.5f);
                        micro.vy = centerRng.nextFloat(-0.5f, 0.5f);
                        micro.aspect = centerRng.nextFloat(0.85f, 1.20f);
                        micro.rot    = centerRng.nextFloat(-3.14f, 3.14f);
                        micro.baseRot    = micro.rot;
                        micro.baseAspect = micro.aspect;
                        micro.seedX  = centerRng.nextFloat(0.0f, 1000.0f);
                        micro.seedY  = centerRng.nextFloat(0.0f, 1000.0f);
                        // Mixed land/ocean character — typical for
                        // microplates (Caribbean has both).
                        // Bimodal microplates too: either land (Caribbean
                        // arc, Anatolia) or ocean (Cocos, Juan de Fuca).
                        micro.isLand = (centerRng.nextFloat(0.0f, 1.0f) < 0.5f);
                        micro.landFraction = micro.isLand
                            ? centerRng.nextFloat(0.75f, 0.90f)
                            : centerRng.nextFloat(0.05f, 0.12f);
                        micro.ageEpochs = 0;
                        micro.orogenyLocal.assign(
                            static_cast<std::size_t>(OROGENY_GRID * OROGENY_GRID), 0.0f);
                        // Microplate weight (smaller than majors) — set
                        // crust area accordingly. Microplates are young
                        // by definition (formed at junction events).
                        micro.weight = std::max(0.35f, micro.weight);
                        micro.crustArea = micro.weight * micro.weight;
                        micro.crustAreaInitial = micro.crustArea;
                        micro.crustAge = 0.0f;
                        plates.push_back(micro);
                    }
                }
            }

            // Accumulate orogeny stress for this epoch. For each tile
            // perform the same Voronoi + boundary stress as the render
            // pass, but feed the result into the orogeny field rather
            // than directly into elev. Subduction land-side stress is
            // gated by STRESS_GATE so only strong convergent motion
            // builds mountains — passive margins stay flat.
            // Parallel: scatterPL uses atomic adds (see lambda above)
            // so concurrent boundary contributions stack safely.
            AOC_PARALLEL_FOR_ROWS
            for (int32_t row = 0; row < height; ++row) {
                for (int32_t col = 0; col < width; ++col) {
                    const float nx = static_cast<float>(col)
                                    / static_cast<float>(width);
                    const float ny = static_cast<float>(row)
                                    / static_cast<float>(height);
                    float d1Sq = 1e9f, d2Sq = 1e9f;
                    int32_t nearest = -1, second = -1;
                    float lxNearest = 0.0f, lyNearest = 0.0f;
                    for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                        float dx = nx - plates[pi].cx;
                        float dy = ny - plates[pi].cy;
                        if (cylSim) {
                            if (dx >  0.5f) { dx -= 1.0f; }
                            if (dx < -0.5f) { dx += 1.0f; }
                        }
                        const float cs = std::cos(plates[pi].rot);
                        const float sn = std::sin(plates[pi].rot);
                        const float lx = (dx * cs + dy * sn) / plates[pi].aspect;
                        const float ly = (-dx * sn + dy * cs) * plates[pi].aspect;
                        const float dsq = (lx * lx + ly * ly) / (plates[pi].weight * plates[pi].weight);
                        if (dsq < d1Sq) {
                            d2Sq = d1Sq; second = nearest;
                            d1Sq = dsq;  nearest = static_cast<int32_t>(pi);
                            lxNearest = lx;
                            lyNearest = ly;
                        } else if (dsq < d2Sq) {
                            d2Sq = dsq;  second = static_cast<int32_t>(pi);
                        }
                    }
                    if (nearest < 0 || second < 0) { continue; }
                    const float d1 = std::sqrt(d1Sq);
                    const float d2 = std::sqrt(d2Sq);
                    // Very tight boundary band (0.93+) — orogeny only on
                    // tiles right at the seam. Avoids dispersing
                    // mountain belts across a wide region.
                    const float boundary = (d2 > 0.0001f)
                        ? std::clamp((d1 / d2 - 0.93f) / 0.07f, 0.0f, 1.0f)
                        : 0.0f;
                    if (boundary < 0.15f) { continue; }
                    const Plate& A = plates[static_cast<std::size_t>(nearest)];
                    const Plate& B = plates[static_cast<std::size_t>(second)];
                    float bnx = B.cx - A.cx;
                    float bny = B.cy - A.cy;
                    const float bnLen = std::sqrt(bnx * bnx + bny * bny);
                    if (bnLen < 0.0001f) { continue; }
                    bnx /= bnLen; bny /= bnLen;
                    const float relVx = A.vx - B.vx;
                    const float relVy = A.vy - B.vy;
                    const float stress = relVx * bnx + relVy * bny;
                    // Per-epoch orogeny scaling: total uplift over the
                    // sim should be roughly invariant of epoch count.
                    // Without this, longer sims stack so much orogeny
                    // (capped at +0.32 each tile) that nearly every
                    // boundary saturates → world becomes mostly land.
                    // 40-epoch reference; longer sims dampen per-epoch.
                    const float epochScale = std::clamp(
                        40.0f / static_cast<float>(EPOCHS), 0.50f, 1.0f);
                    const float bandWeight = boundary * (1.0f - boundary) * 4.0f * epochScale;
                    const std::size_t idx = static_cast<std::size_t>(
                        row * width + col);
                    // Boundary-tile crust check: sample EACH plate's
                    // local crust mask at this tile's plate-local coords.
                    // Real plates are mixed — what crust is locally at
                    // the boundary determines the boundary type, not
                    // the plate's global isLand flag. Possible cases:
                    //   continental–continental  → both crumple (Himalayas, Alps)
                    //   continental–oceanic      → ocean subducts, continental
                    //                              uplifts (Andes), trench
                    //                              digs the ocean side
                    //   oceanic–oceanic          → denser one subducts,
                    //                              other gets island arc
                    //                              (Japan, Aleutian, Mariana)
                    auto sampleCrustLand = [&](const Plate& p, float wxN, float wyN) {
                        float dxp = wxN - p.cx;
                        float dyp = wyN - p.cy;
                        if (cylSim) {
                            if (dxp >  0.5f) { dxp -= 1.0f; }
                            if (dxp < -0.5f) { dxp += 1.0f; }
                        }
                        const float csp = std::cos(p.rot);
                        const float snp = std::sin(p.rot);
                        const float lxp = (dxp * csp + dyp * snp) / p.aspect;
                        const float lyp = (-dxp * snp + dyp * csp) * p.aspect;
                        aoc::Random crustRng(0u);
                        // Lower frequency (was 4.5): plate identity
                        // dominates over sub-plate speckle. Continents
                        // align with plate boundaries instead of leaking
                        // across them.
                        const float c = fractalNoise(
                            lxp * 2.0f + p.seedX,
                            lyp * 2.0f + p.seedY,
                            4, 2.0f, 0.55f, crustRng);
                        return c > (1.0f - p.landFraction);
                    };
                    const bool A_land = sampleCrustLand(A, nx, ny);
                    const bool B_land = sampleCrustLand(B, nx, ny);

                    // Bilinear scatter into the OWNING plate's local
                    // orogeny grid. The grid covers plate-local box
                    // [-OROGENY_HALF, +OROGENY_HALF] in 64×64 cells,
                    // so this stress contribution will TRAVEL with the
                    // plate even as it drifts to new world positions.
                    // NEAREST-cell scatter (was bilinear). Bilinear's
                    // 4-cell footprint plus blur smeared orogeny across
                    // wide areas. Real orogeny is sharp along the
                    // boundary line — single-cell deposits keep it tight.
                    auto scatterPL = [&](Plate& p, float lx, float ly, float val) {
                        const float gx = (lx + OROGENY_HALF) / (2.0f * OROGENY_HALF)
                                       * static_cast<float>(OROGENY_GRID);
                        const float gy = (ly + OROGENY_HALF) / (2.0f * OROGENY_HALF)
                                       * static_cast<float>(OROGENY_GRID);
                        const int32_t ix = static_cast<int32_t>(std::floor(gx));
                        const int32_t iy = static_cast<int32_t>(std::floor(gy));
                        if (ix < 0 || ix >= OROGENY_GRID
                            || iy < 0 || iy >= OROGENY_GRID) { return; }
                        // Atomic add when called from a parallel-for so
                        // multiple tiles writing the same plate grid
                        // cell don't lose contributions.
                        float* cell = &p.orogenyLocal[
                            static_cast<std::size_t>(iy * OROGENY_GRID + ix)];
                        #ifdef AOC_HAS_OPENMP
                        #pragma omp atomic
                        #endif
                        *cell += val;
                    };

                    Plate& Aw = plates[static_cast<std::size_t>(nearest)];
                    // STRAIN-RATE RHEOLOGY. At very high relative
                    // velocity (fast convergence), crust deforms
                    // ductilely — energy goes into lateral flow rather
                    // than orogeny. Cap effective stress so super-fast
                    // collisions don't produce runaway mountain growth.
                    const float effectiveStress = std::min(stress, 1.6f);
                    float contrib = 0.0f;
                    if (effectiveStress > STRESS_GATE) {
                        if (A_land && B_land) {
                            contrib = 0.008f * bandWeight * effectiveStress;
                        } else if (A_land && !B_land) {
                            contrib = 0.011f * bandWeight * effectiveStress;

                            // FOREARC ACCRETIONARY WEDGE. Sediment
                            // scraped off the subducting oceanic plate
                            // piles up at the trench, forming a low
                            // mountain-like feature on A's side BETWEEN
                            // trench and arc (Olympic Peninsula, Coast
                            // Ranges of California).
                            // SLAB DIP VARIATION. Old, dense oceanic
                            // crust subducts STEEPLY (Mariana-style) →
                            // arc sits close to trench → narrow forearc.
                            // Young, light crust subducts SHALLOWLY
                            // (Andean-style flat-slab) → arc retreats
                            // far inland → wide forearc. Scale offset
                            // by inverse age.
                            const float bAgeScale = std::clamp(
                                B.crustAge / 150.0f, 0.0f, 1.0f);
                            const float wedgeOffset = 0.06f
                                + 0.12f * (1.0f - bAgeScale);
                            const float wedgeLx = lxNearest + bnx * wedgeOffset;
                            const float wedgeLy = lyNearest + bny * wedgeOffset;
                            scatterPL(Aw, wedgeLx, wedgeLy,
                                      0.04f * bandWeight * effectiveStress);
                        } else if (!A_land && B_land) {
                            contrib = -0.07f * bandWeight * effectiveStress;
                        } else {
                            const bool aDenser = (A.landFraction <= B.landFraction);
                            contrib = aDenser
                                ? -0.02f * bandWeight * effectiveStress
                                :  0.04f * bandWeight * effectiveStress;
                        }
                    } else if (stress < -STRESS_GATE) {
                        if (A_land && B_land) {
                            // Continental rift (East African Rift).
                            contrib = 0.03f * bandWeight * effectiveStress;
                        } else if (!A_land && !B_land) {
                            // Mid-ocean ridge — divergent boundary
                            // between two oceanic plates. New crust
                            // upwells from the mantle and forms a
                            // shallow bathymetric high above the
                            // surrounding abyssal plain
                            // (Mid-Atlantic Ridge, East Pacific Rise).
                            // Stress is negative for divergent → -stress
                            // gives a positive uplift contribution.
                            contrib = -0.05f * bandWeight * effectiveStress;
                        }
                    }
                    if (contrib != 0.0f) {
                        // RIDGE-TRANSFORM SEGMENTATION. Real mid-ocean
                        // ridges aren't continuous lines — they're
                        // segmented by perpendicular transform faults
                        // every 50-100 km. Each segment is offset from
                        // its neighbours by 5-50 km. Approximate by
                        // perturbing the scatter location with a small
                        // along-boundary square-wave offset perpendicular
                        // to the boundary normal. Only fires for ocean-
                        // ocean divergent (true mid-ocean ridge).
                        float scatterLx = lxNearest;
                        float scatterLy = lyNearest;
                        if (stress < -STRESS_GATE && !A_land && !B_land) {
                            // Compute perpendicular-to-normal axis in
                            // plate-local frame. The boundary normal
                            // (bnx, bny) is in WORLD frame; convert to
                            // plate-local of A.
                            const float csA = std::cos(A.rot);
                            const float snA = std::sin(A.rot);
                            const float bnxL = (bnx * csA + bny * snA) / A.aspect;
                            const float bnyL = (-bnx * snA + bny * csA) * A.aspect;
                            // Perpendicular = (-bnyL, bnxL).
                            // Square-wave segmentation along that axis.
                            const float along =
                                -lxNearest * bnyL + lyNearest * bnxL;
                            const float seg = std::sin(along * 32.0f); // ~32 segments per plate
                            const float jog = (seg > 0.0f ? 0.04f : -0.04f);
                            scatterLx += -bnyL * jog;
                            scatterLy +=  bnxL * jog;
                        }
                        scatterPL(Aw, scatterLx, scatterLy, contrib);
                    }

                    // BACKARC SPREADING. Behind a subduction zone the
                    // overriding plate stretches as the slab rolls
                    // back, creating an extensional basin (Sea of
                    // Japan, Aegean, Tyrrhenian). Detect: A is the
                    // continental overriding plate, B is the subducting
                    // ocean. Add small NEGATIVE orogeny ~0.6 plate-
                    // local-units inland from the boundary on A side,
                    // representing the backarc basin floor.
                    if (stress > STRESS_GATE && A_land && !B_land) {
                        const float boundaryDirX = -bnx; // inward into A
                        const float boundaryDirY = -bny;
                        // Convert world boundary direction to A's
                        // plate-local direction (rotation only).
                        const float csA = std::cos(A.rot);
                        const float snA = std::sin(A.rot);
                        const float bnxLocal = (boundaryDirX * csA + boundaryDirY * snA);
                        const float bnyLocal = (-boundaryDirX * snA + boundaryDirY * csA);
                        const float backArcLx = lxNearest + bnxLocal * 0.20f;
                        const float backArcLy = lyNearest + bnyLocal * 0.20f;
                        scatterPL(Aw, backArcLx, backArcLy,
                                  -0.025f * bandWeight * effectiveStress);
                    }

                    // OUTER RISE / FOREARC BULGE. The subducting plate
                    // (B in this branch) flexes UP just before plunging
                    // into the trench — the so-called "outer rise"
                    // (e.g., the bulge seaward of the Peru-Chile or
                    // Mariana trenches). Small positive uplift on B's
                    // plate, ~0.3 plate-local-units seaward of trench.
                    if (stress > STRESS_GATE && !B_land && A_land) {
                        // We're on A here; outer rise is on B side.
                        // Compute B-local coords for this tile.
                        Plate& Bw = plates[static_cast<std::size_t>(second)];
                        float dxB = nx - Bw.cx;
                        float dyB = ny - Bw.cy;
                        if (cylSim) {
                            if (dxB > 0.5f)  { dxB -= 1.0f; }
                            if (dxB < -0.5f) { dxB += 1.0f; }
                        }
                        const float csB = std::cos(Bw.rot);
                        const float snB = std::sin(Bw.rot);
                        const float lxB = (dxB * csB + dyB * snB) / Bw.aspect;
                        const float lyB = (-dxB * snB + dyB * csB) * Bw.aspect;
                        // Move along bnx,bny (toward A from B) negated
                        // to push outer rise SEAWARD (away from A).
                        const float bnxLocalB = (-bnx * csB + -bny * snB);
                        const float bnyLocalB = (bnx * snB + -bny * csB);
                        // Outer rise offset depends on subducting slab
                        // dip (proxied by Bw crustAge). Old crust =
                        // steep dip = outer rise close to trench.
                        // Young = shallow dip = outer rise farther
                        // seaward.
                        const float orAgeScale = std::clamp(
                            Bw.crustAge / 150.0f, 0.0f, 1.0f);
                        const float orOffset = 0.20f
                            + 0.20f * (1.0f - orAgeScale);
                        const float orLx = lxB + bnxLocalB * orOffset;
                        const float orLy = lyB + bnyLocalB * orOffset;
                        scatterPL(Bw, orLx, orLy,
                                  0.018f * bandWeight * effectiveStress);
                    }
                    (void)idx;
                }
            }

            // Slab pull + ridge push. Real plate motion is driven by:
            //   1. Slab pull — dense oceanic crust subducting at a
            //      trench pulls the rest of the plate toward it.
            //      ~50 % of plate-motion energy budget.
            //   2. Ridge push — mid-ocean ridges' elevation pushes
            //      plates away from spreading centres (~25 %).
            //   3. Mantle drag — viscous resistance from the mantle.
            // We approximate slab pull by accumulating the convergent-
            // boundary stress on each plate (the more it's subducting
            // somewhere, the harder it's pulled). Per-epoch we apply a
            // small velocity nudge to each plate based on its accumulated
            // pull vector. Net effect: plates with active subduction
            // zones accelerate through the sim (Pacific Plate today
            // moves ~10 cm/yr — fastest on Earth — because it's
            // subducting on three sides).
            std::vector<float>   slabPullX(plates.size(), 0.0f);
            std::vector<float>   slabPullY(plates.size(), 0.0f);
            // Crust accounting: trench tile count per plate (drives
            // Wilson-cycle plate destruction) and divergent tile count
            // (informs ridge-side young crust on the survivor of ridge
            // expansion). Used after the loop to debit/credit area.
            std::vector<int32_t> trenchTilesPerPlate(plates.size(), 0);
            std::vector<int32_t> ridgeTilesPerPlate (plates.size(), 0);
            for (int32_t row = 0; row < height; row += 2) {
                for (int32_t col = 0; col < width; col += 2) {
                    const float nxs = static_cast<float>(col) / static_cast<float>(width);
                    const float nys = static_cast<float>(row) / static_cast<float>(height);
                    // Find nearest plate at this point (cheap: ignore
                    // anisotropy for the slab-pull accumulator).
                    float bestSq = 1e9f; int32_t bestPi = -1;
                    for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                        float dx = nxs - plates[pi].cx;
                        float dy = nys - plates[pi].cy;
                        if (cylSim) {
                            if (dx > 0.5f) { dx -= 1.0f; }
                            if (dx < -0.5f) { dx += 1.0f; }
                        }
                        const float dsq = dx * dx + dy * dy;
                        if (dsq < bestSq) { bestSq = dsq; bestPi = static_cast<int32_t>(pi); }
                    }
                    if (bestPi < 0) { continue; }
                    const std::size_t idx = static_cast<std::size_t>(row * width + col);
                    // Negative orogeny indicates active subduction
                    // trench at this tile. Pull the OWNING plate toward
                    // the trench (i.e., toward the second-nearest plate
                    // which is the overriding side).
                    if (orogeny[idx] < -0.05f) {
                        // Direction: from this tile toward the plate
                        // overrider (we don't store second-nearest here;
                        // approximate by pulling toward map regions of
                        // higher orogeny).
                        const Plate& pp = plates[static_cast<std::size_t>(bestPi)];
                        float dx = nxs - pp.cx;
                        float dy = nys - pp.cy;
                        if (cylSim) {
                            if (dx > 0.5f) { dx -= 1.0f; }
                            if (dx < -0.5f) { dx += 1.0f; }
                        }
                        const float L = std::max(1e-4f,
                            std::sqrt(dx * dx + dy * dy));
                        slabPullX[static_cast<std::size_t>(bestPi)] += dx / L;
                        slabPullY[static_cast<std::size_t>(bestPi)] += dy / L;
                        // Trench tile → crust on the SUBDUCTING plate
                        // is being consumed at the trench. Tally it.
                        ++trenchTilesPerPlate[static_cast<std::size_t>(bestPi)];
                    }
                    // Mid-ocean ridge tile: small POSITIVE orogeny on
                    // ocean tiles (set by the divergent-boundary code
                    // earlier). Marks where new oceanic crust is being
                    // accreted at the spreading centre. Used to credit
                    // young crust to adjacent plates.
                    else if (orogeny[idx] > 0.005f && orogeny[idx] < 0.06f
                             && plates[static_cast<std::size_t>(bestPi)].landFraction < 0.40f) {
                        ++ridgeTilesPerPlate[static_cast<std::size_t>(bestPi)];
                    }
                }
            }
            // Apply slab pull as a small velocity nudge. Magnitude
            // tuned to be small per-epoch; over many epochs a heavily
            // subducting plate will visibly accelerate. Older crust
            // (cold, dense) pulls harder — Pacific is fastest because
            // its oldest edge is ~180 My old.
            for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                const float pullLen = std::sqrt(
                    slabPullX[pi] * slabPullX[pi] + slabPullY[pi] * slabPullY[pi]);
                if (pullLen < 1.0f) { continue; }
                constexpr float SLAB_PULL_GAIN = 0.012f;
                // Crust-age scale: factor 0.7 → 1.5 across age range
                // [0, 150]. Young rifted plates pull weakly; old Pacific-
                // class plates pull strongly.
                const float ageScale = std::clamp(
                    0.7f + plates[pi].crustAge / 150.0f * 0.8f, 0.7f, 1.5f);
                const float gain = SLAB_PULL_GAIN * ageScale;
                plates[pi].vx += (slabPullX[pi] / pullLen) * gain;
                plates[pi].vy += (slabPullY[pi] / pullLen) * gain;
                // SLAB ROLLBACK / TRENCH RETREAT. Sustained subduction
                // causes the slab to steepen over time as it sinks.
                // The trench (boundary on the OVERRIDING plate) migrates
                // SEAWARD relative to the overriding plate; equivalently
                // the overriding plate retreats AWAY from the trench
                // slightly. Tonga and Mariana arcs both show this.
                // We approximate by giving the OVERRIDING plate a small
                // velocity component AWAY from the slab pull direction
                // (where the slab is going DOWN). Weaker than slab pull.
                if (plates[pi].landFraction > 0.40f) {
                    constexpr float ROLLBACK_GAIN = 0.005f;
                    plates[pi].vx -= (slabPullX[pi] / pullLen) * ROLLBACK_GAIN;
                    plates[pi].vy -= (slabPullY[pi] / pullLen) * ROLLBACK_GAIN;
                }
                // Clamp velocity to prevent runaway acceleration.
                const float vLen = std::sqrt(
                    plates[pi].vx * plates[pi].vx + plates[pi].vy * plates[pi].vy);
                if (vLen > 1.2f) {
                    plates[pi].vx *= (1.2f / vLen);
                    plates[pi].vy *= (1.2f / vLen);
                }
            }

            // ---- WILSON CRUST CONSERVATION ----
            // Each epoch: oceanic plates lose area at trenches and gain
            // area along ridges. Continental plates conserve crust (low
            // density makes them buoyant; they ride on top instead of
            // subducting). When an oceanic plate's area drops below a
            // fraction of its birth size it has been fully consumed at
            // its trenches → erase. The Voronoi cells of surviving
            // neighbouring plates expand into the vacated space; that
            // expansion IS the new crust forming at the spreading
            // centre on the survivor's side.
            //
            // Tunables sized so a heavily-subducting plate (~30-50 trench
            // tiles per epoch) is fully consumed in 20-40 epochs (Wilson
            // cycle = ~half a sim).
            constexpr float SUBDUCTION_AREA_PER_TILE = 0.0008f;
            constexpr float RIDGE_AREA_PER_TILE      = 0.0003f;
            for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                Plate& p = plates[pi];
                p.crustAge += 1.0f;
                // Area accounting: continental crust is buoyant — it
                // doesn't get consumed at trenches the way oceanic does.
                // Cap continental losses at a tenth-rate for the
                // continental-margin terranes that DO get scraped off.
                const float subRate = SUBDUCTION_AREA_PER_TILE
                    * (p.landFraction > 0.40f ? 0.1f : 1.0f);
                p.crustArea -= subRate
                    * static_cast<float>(trenchTilesPerPlate[pi]);
                p.crustArea += RIDGE_AREA_PER_TILE
                    * static_cast<float>(ridgeTilesPerPlate[pi]);
                // Ridge-side accretion = young crust mixed in. Lower
                // mean age proportional to ridge tile fraction.
                if (ridgeTilesPerPlate[pi] > 0) {
                    const float dilution = std::min(1.0f,
                        static_cast<float>(ridgeTilesPerPlate[pi]) / 60.0f);
                    p.crustAge *= (1.0f - dilution * 0.05f);
                }
                p.crustArea = std::max(0.0f, p.crustArea);
                p.slabTornThisEpoch = 0;
            }
            // ---- ACTIVE SLAB TEARING ----
            // Real Earth: a subducting oceanic slab can tear horizontally
            // mid-subduction (Apennines, Carpathians, Anatolia). Causes
            // sudden change in arc volcanism + uplift of adjacent crust.
            // Trigger: oceanic plate, age > 60 epochs, heavy trench
            // count (>20), low random chance per epoch. Effect: 50 %
            // crustArea immediate loss + adjacent overriding plate
            // velocity perturbation (slab disconnect = slab pull lost).
            for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                Plate& p = plates[pi];
                if (p.landFraction >= 0.40f) { continue; }
                if (p.isPolar) { continue; }
                if (p.crustAge < 60.0f) { continue; }
                if (trenchTilesPerPlate[pi] < 20) { continue; }
                if (centerRng.nextFloat(0.0f, 1.0f) > 0.015f) { continue; }
                p.crustArea *= 0.50f;          // sudden mass loss
                p.slabTornThisEpoch = 1;
                // Perturb adjacent plates' velocity — slab pull lost
                // means overriding plate decelerates.
                for (std::size_t k = 0; k < plates.size(); ++k) {
                    if (k == pi) { continue; }
                    float dx = plates[k].cx - p.cx;
                    float dy = plates[k].cy - p.cy;
                    if (cylSim) {
                        if (dx >  0.5f) { dx -= 1.0f; }
                        if (dx < -0.5f) { dx += 1.0f; }
                    }
                    const float dd = std::sqrt(dx * dx + dy * dy);
                    if (dd > 0.20f || dd < 1e-4f) { continue; }
                    plates[k].vx *= 0.85f;
                    plates[k].vy *= 0.85f;
                }
            }
            // Erase pass: oceanic plates fully subducted away.
            for (std::size_t pi = plates.size(); pi-- > 0; ) {
                const Plate& p = plates[pi];
                const bool oceanic = (p.landFraction < 0.40f);
                if (!oceanic) { continue; }
                if (p.isPolar) { continue; } // polar plates persist
                if (p.ageEpochs < 8) { continue; } // grace period
                if (p.crustArea < 0.10f * p.crustAreaInitial) {
                    plates.erase(plates.begin()
                        + static_cast<std::ptrdiff_t>(pi));
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
                        fresh.landFraction = centerRng.nextFloat(0.02f, 0.08f);
                        fresh.weight = centerRng.nextFloat(0.7f, 1.05f);
                        fresh.crustArea = fresh.weight * fresh.weight;
                        fresh.crustAreaInitial = fresh.crustArea;
                        fresh.crustAge = 0.0f;  // ridge-fresh
                        fresh.ageEpochs = 0;
                        fresh.orogenyLocal.assign(
                            static_cast<std::size_t>(OROGENY_GRID * OROGENY_GRID),
                            0.0f);
                        plates.push_back(fresh);
                    }
                }
            }
            // Mantle drag — gentle damping per epoch. Was 0.995 → with
            // 120-epoch sims that decays motion to 55 % of initial,
            // killing the second + third Wilson cycles. 0.997 holds
            // 70 % at epoch 120 → plates keep moving across multi-
            // cycle simulations.
            for (Plate& p : plates) {
                p.vx *= 0.997f;
                p.vy *= 0.997f;
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
            for (Plate& p : plates) {
                for (float& v : p.orogenyLocal) {
                    if (v > 0.18f) {
                        v *= 0.965f; // 3.5 %/epoch
                    } else if (v > 0.10f) {
                        v *= 0.965f; // 3.5 %/epoch — same as tall, stops mid-tier
                                      // creep into Mountain when contribution ~equals
                                      // erosion at low values.
                    } else if (v > 0.0f) {
                        v *= 0.997f; // root preservation 0.3 %/epoch
                    } else if (v < 0.0f) {
                        v *= 0.994f;
                    }
                }
            }
            // Legacy world-frame array kept zeroed; present only for
            // reuse by code paths that haven't been switched to the
            // plate-local sampling yet.
            for (float& v : orogeny) {
                (void)v;
                if (v > 0.20f) {
                    v *= 0.985f;
                } else if (v > 0.10f) {
                    v *= 0.993f;
                } else if (v > 0.0f) {
                    v *= 0.9985f;
                } else if (v < 0.0f) {
                    v *= 0.994f;
                }
            }
        }

        // Cap orogeny so a tile that sat in a hot zone every epoch
        // doesn't shoot above plausible heights. Range mirrors real-
        // Earth relief: highest mountains are ~9 km vs 6 km ocean
        // depth → ratio ~1.5, so we cap mountains at +0.25 and
        // trenches at −0.18 in our normalised height space.
        // Use a SEPARATE RNG seeded from config.seed alone for LIPs
        // (and any other post-sim randomness). centerRng has advanced
        // a different amount based on EPOCHS, so using it would cause
        // LIPs to land at different positions for adjacent scrubber
        // states → visible jumps between epochs N and N+1.
        aoc::Random postRng(config.seed ^ 0x4C495021u); // "LIP!"
        // LARGE IGNEOUS PROVINCES (LIPs). Rare massive flood-basalt
        // events: Deccan Traps (~65 Mya, India, ~500k km² of basalt),
        // Siberian Traps (~250 Mya, ~7M km²), Columbia River Basalt
        // Group, Karoo, Ontong Java. These are mantle-plume-driven
        // events that cover huge regions in a few million years.
        // We fire 1-3 random LIPs per sim, depositing a circular
        // basalt province on a random plate at a random plate-local
        // position. ~0.30 plate-local-units radius, +0.15 elevation.
        {
            const int32_t numLIPs = postRng.nextInt(1, 3);
            for (int32_t lip = 0; lip < numLIPs && !plates.empty(); ++lip) {
                const std::size_t pi = static_cast<std::size_t>(
                    postRng.nextInt(0, static_cast<int32_t>(plates.size()) - 1));
                Plate& p = plates[pi];
                const float lipLx = postRng.nextFloat(-1.2f, 1.2f);
                const float lipLy = postRng.nextFloat(-1.2f, 1.2f);
                // LIPs were also too big — covered substantial fraction
                // of plates which then crossed mountain threshold.
                constexpr float LIP_RADIUS = 0.14f;
                constexpr float LIP_HEIGHT = 0.08f;
                for (int32_t gy = 0; gy < OROGENY_GRID; ++gy) {
                    for (int32_t gx = 0; gx < OROGENY_GRID; ++gx) {
                        const float lx = (static_cast<float>(gx) + 0.5f)
                                       / static_cast<float>(OROGENY_GRID)
                                       * (2.0f * OROGENY_HALF) - OROGENY_HALF;
                        const float ly = (static_cast<float>(gy) + 0.5f)
                                       / static_cast<float>(OROGENY_GRID)
                                       * (2.0f * OROGENY_HALF) - OROGENY_HALF;
                        const float dx = lx - lipLx;
                        const float dy = ly - lipLy;
                        const float d = std::sqrt(dx * dx + dy * dy);
                        if (d < LIP_RADIUS) {
                            const float t = 1.0f - d / LIP_RADIUS;
                            p.orogenyLocal[static_cast<std::size_t>(
                                gy * OROGENY_GRID + gx)] += LIP_HEIGHT * t * t;
                        }
                    }
                }
            }
        }

        // Cap orogeny tighter — 0.22 max. With mountain threshold at
        // 0.18, only the actively-stressed boundary tiles cross. Earth
        // ratio: ~7 % land has Himalayan-class elevation.
        for (Plate& p : plates) {
            for (float& v : p.orogenyLocal) {
                v = std::clamp(v, -0.15f, 0.22f);
            }
        }

        // No blur. Nearest-cell scatter keeps orogeny pinned to the
        // exact boundary cells — sharp mountain RIDGES, not blobs.
        // Foothill apron will come from a separate ONE-tile dilation
        // below targeted at Hills-tier (avoids inflating mountains).
        const int32_t erosionPasses = 0;
        for (Plate& p : plates) {
            std::vector<float> tmp(p.orogenyLocal.size(), 0.0f);
            for (int32_t passN = 0; passN < erosionPasses; ++passN) {
                for (int32_t r = 0; r < OROGENY_GRID; ++r) {
                    for (int32_t c = 0; c < OROGENY_GRID; ++c) {
                        float sum = 0.0f;
                        int32_t cnt = 0;
                        for (int32_t dr = -1; dr <= 1; ++dr) {
                            for (int32_t dc = -1; dc <= 1; ++dc) {
                                const int32_t r2 = r + dr;
                                const int32_t c2 = c + dc;
                                if (r2 < 0 || r2 >= OROGENY_GRID
                                    || c2 < 0 || c2 >= OROGENY_GRID) { continue; }
                                sum += p.orogenyLocal[static_cast<std::size_t>(
                                    r2 * OROGENY_GRID + c2)];
                                ++cnt;
                            }
                        }
                        tmp[static_cast<std::size_t>(r * OROGENY_GRID + c)] =
                            (cnt > 0) ? sum / static_cast<float>(cnt) : 0.0f;
                    }
                }
                p.orogenyLocal.swap(tmp);
            }
        }

        // Rebuild world-frame `orogeny[]` array by sampling each tile
        // from its owning plate's local grid. Existing elevation pass
        // and side-correctness consume the world-frame array unchanged.
        // Sampling is bilinear so transitions across the plate-local
        // grid are smooth.
        auto samplePL = [&](const Plate& p, float lx, float ly) -> float {
            const float gx = (lx + OROGENY_HALF) / (2.0f * OROGENY_HALF)
                           * static_cast<float>(OROGENY_GRID);
            const float gy = (ly + OROGENY_HALF) / (2.0f * OROGENY_HALF)
                           * static_cast<float>(OROGENY_GRID);
            const int32_t ix = static_cast<int32_t>(std::floor(gx));
            const int32_t iy = static_cast<int32_t>(std::floor(gy));
            if (ix < 0 || ix >= OROGENY_GRID - 1
                || iy < 0 || iy >= OROGENY_GRID - 1) { return 0.0f; }
            const float fx = gx - static_cast<float>(ix);
            const float fy = gy - static_cast<float>(iy);
            const float v00 = p.orogenyLocal[static_cast<std::size_t>(iy * OROGENY_GRID + ix)];
            const float v10 = p.orogenyLocal[static_cast<std::size_t>(iy * OROGENY_GRID + ix + 1)];
            const float v01 = p.orogenyLocal[static_cast<std::size_t>((iy + 1) * OROGENY_GRID + ix)];
            const float v11 = p.orogenyLocal[static_cast<std::size_t>((iy + 1) * OROGENY_GRID + ix + 1)];
            return v00 * (1.0f - fx) * (1.0f - fy)
                 + v10 *        fx  * (1.0f - fy)
                 + v01 * (1.0f - fx) *        fy
                 + v11 *        fx  *        fy;
        };
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
                    const float cs = std::cos(plates[pi].rot);
                    const float sn = std::sin(plates[pi].rot);
                    const float lx = (dx * cs + dy * sn) / plates[pi].aspect;
                    const float ly = (-dx * sn + dy * cs) * plates[pi].aspect;
                    const float dsq = (lx * lx + ly * ly) / (plates[pi].weight * plates[pi].weight);
                    if (dsq < bestSq) {
                        bestSq = dsq; bestPi = static_cast<int32_t>(pi);
                        bestLx = lx; bestLy = ly;
                    }
                }
                if (bestPi < 0) { continue; }
                float oroSampled = samplePL(
                    plates[static_cast<std::size_t>(bestPi)], bestLx, bestLy);
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

        // Hole-closing pass on world-frame orogeny. Bilinear scatter
        // and per-plate blur can leave gaps INSIDE a contiguous
        // mountain range — tiles below threshold surrounded by
        // above-threshold neighbours. Real ranges don't have those
        // dips at our resolution (intermontane valleys exist but
        // aren't 1-tile holes). Fill any tile that has ≥ 4 neighbours
        // with substantially higher orogeny by lifting it to the
        // neighbour median. Two passes catches 2-tile-wide gaps too.
        // Single conservative hole-fill pass. Only lift to 0.65× of
        // neighbour mean (was 0.85) and require ALL 6 neighbours of
        // mountain-tier orogeny (was 4) to fill a hole. Stops the
        // pass from inflating ridges into wide mountain blocks.
        for (int32_t fillPass = 0; fillPass < 1; ++fillPass) {
            std::vector<float> filled(orogeny);
            for (int32_t row = 0; row < height; ++row) {
                for (int32_t col = 0; col < width; ++col) {
                    const std::size_t idx = static_cast<std::size_t>(
                        row * width + col);
                    const float my = orogeny[idx];
                    if (my >= 0.18f) { continue; }
                    int32_t high = 0;
                    float sumHigh = 0.0f;
                    const hex::AxialCoord ax = hex::offsetToAxial({col, row});
                    for (const hex::AxialCoord& n : hex::neighbors(ax)) {
                        if (!grid.isValid(n)) { continue; }
                        const float nv = orogeny[static_cast<std::size_t>(grid.toIndex(n))];
                        if (nv > 0.20f) {
                            ++high;
                            sumHigh += nv;
                        }
                    }
                    if (high >= 6) {
                        filled[idx] = sumHigh / static_cast<float>(high) * 0.65f;
                    }
                }
            }
            orogeny.swap(filled);
        }
        // Side-correctness pass — runs AFTER erosion blur so the
        // smearing can't re-leak positive orogeny onto ocean tiles or
        // negative orogeny onto land tiles. For each tile, re-resolve
        // its final Voronoi assignment using the SAME domain-warped
        // sample coordinates as the elevation pass below, so the
        // "is this tile land?" answer matches what the renderer will
        // do. Zero out orogeny whose sign disagrees with the side.
        aoc::Random sideWarpRng(noiseRng);
        aoc::Random sideCrustRng(noiseRng);
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const float nx = static_cast<float>(col)
                                / static_cast<float>(width);
                const float ny = static_cast<float>(row)
                                / static_cast<float>(height);
                // Match the elevation-pass warp (two-octave gentle warp)
                // so the side-check sees the SAME plate ownership as
                // the renderer.
                const float wpx1 =
                    (fractalNoise(nx * 1.3f, ny * 1.3f, 4, 2.0f, 0.55f, sideWarpRng) - 0.5f) * 0.24f;
                const float wpy1 =
                    (fractalNoise(nx * 1.3f + 17.0f, ny * 1.3f + 31.0f, 4, 2.0f, 0.55f, sideWarpRng) - 0.5f) * 0.24f;
                const float wpx2 =
                    (fractalNoise(nx * 3.5f, ny * 3.5f, 2, 2.0f, 0.5f, sideWarpRng) - 0.5f) * 0.06f;
                const float wpy2 =
                    (fractalNoise(nx * 3.5f + 9.0f, ny * 3.5f + 21.0f, 2, 2.0f, 0.5f, sideWarpRng) - 0.5f) * 0.06f;
                const float wx = nx + wpx1 + wpx2;
                const float wy = ny + wpy1 + wpy2;
                float d1Sq = 1e9f;
                int32_t finalNearest = -1;
                float lxN = 0.0f, lyN = 0.0f;
                for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                    float dx = wx - plates[pi].cx;
                    float dy = wy - plates[pi].cy;
                    if (cylSim) {
                        if (dx >  0.5f) { dx -= 1.0f; }
                        if (dx < -0.5f) { dx += 1.0f; }
                    }
                    const float cs = std::cos(plates[pi].rot);
                    const float sn = std::sin(plates[pi].rot);
                    const float lx = (dx * cs + dy * sn) / plates[pi].aspect;
                    const float ly = (-dx * sn + dy * cs) * plates[pi].aspect;
                    const float dsq = (lx * lx + ly * ly) / (plates[pi].weight * plates[pi].weight);
                    if (dsq < d1Sq) {
                        d1Sq = dsq;
                        finalNearest = static_cast<int32_t>(pi);
                        lxN = lx;
                        lyN = ly;
                    }
                }
                if (finalNearest < 0) { continue; }
                const std::size_t idx = static_cast<std::size_t>(row * width + col);
                // Use the SAME per-plate crust mask as the elevation
                // pass to decide land/ocean — keeps mountains attached
                // to the actual land regions of the plate, not the
                // legacy isLand flag (which doesn't reflect the
                // per-tile crust pattern that travels with the plate).
                const Plate& fp = plates[static_cast<std::size_t>(finalNearest)];
                const float crust = fractalNoise(
                    lxN * 2.0f + fp.seedX,
                    lyN * 2.0f + fp.seedY,
                    4, 2.0f, 0.55f, sideCrustRng);
                const float landThresh = 1.0f - fp.landFraction;
                const bool tileIsLand = crust > landThresh;
                if (tileIsLand) {
                    // No negative orogeny on land (no underwater trenches
                    // under continents).
                    if (orogeny[idx] < 0.0f) { orogeny[idx] = 0.0f; }
                } else {
                    // Tile is currently in an oceanic part of the plate.
                    // Active mountain orogeny gets wiped (no Mountain
                    // tiles underwater). HOWEVER, ancient eroded
                    // remnants in the Hills tier (0.06 < o ≤ 0.20)
                    // SURVIVE: this tile may have been an active
                    // mountain belt 300 My ago whose root rock now
                    // stands ABOVE the surrounding ocean as an island
                    // chain or submarine plateau. Preserves the
                    // Variscan/Harz pattern — old orogeny that the
                    // sea never quite reclaimed.
                    if (orogeny[idx] > 0.20f) { orogeny[idx] = 0.0f; }
                    // Mid-tier orogeny on water tiles fades faster
                    // (marine erosion of submerged ranges).
                    else if (orogeny[idx] > 0.10f) { orogeny[idx] *= 0.5f; }
                    // Low-tier orogeny (Hills root) survives in place.
                }
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

            if (config.mapType == MapType::Fractal) {
                // No gradient -- use raw noise directly
                edgeFalloff = 0.5f;  // Neutral contribution
            } else {
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
                    for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                        // Anisotropic distance: rotate sample point
                        // into plate-local frame, scale x by aspect.
                        float dx = wx - plates[pi].cx;
                        float dy = wy - plates[pi].cy;
                        if (cylSim) {
                            if (dx >  0.5f) { dx -= 1.0f; }
                            if (dx < -0.5f) { dx += 1.0f; }
                        }
                        const float cs = std::cos(plates[pi].rot);
                        const float sn = std::sin(plates[pi].rot);
                        const float lx = (dx * cs + dy * sn) / plates[pi].aspect;
                        const float ly = (-dx * sn + dy * cs) * plates[pi].aspect;
                        const float dsq = (lx * lx + ly * ly) / (plates[pi].weight * plates[pi].weight);
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
                        const float t = std::clamp(
                            (crust - landThresh + 0.025f) / 0.05f, 0.0f, 1.0f);
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

                        // Second-nearest blend at boundary uses a flat
                        // estimate; doesn't need its own crust mask
                        // since the warp + blend already smooths the seam.
                        const bool secondIsLand =
                            (second >= 0) && plates[static_cast<std::size_t>(second)].isLand;
                        const float secondHeight = secondIsLand ? 0.55f : oceanBase;
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
                            edgeFalloff += h.strength * t * t;
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
                    {
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
            if (config.mapType == MapType::Fractal) {
                elev = elev * 0.6f + edgeFalloff * 0.4f;
            } else if (config.mapType == MapType::Continents) {
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
                elev = edgeFalloff + noiseCentred * 0.16f + oro + reboundBoost;
            } else {
                elev = elev * 0.55f + edgeFalloff * 0.45f;
            }

            elevationMap[static_cast<std::size_t>(row * width + col)] = elev;
        }
    }

    // ---- Stash per-tile plate id so the tectonic-overlay UI can
    // colour each tile by its plate. Re-runs the same Voronoi lookup
    // with the FINAL plate positions so what the overlay shows
    // matches the elevation-pass assignment.
    if (config.mapType == MapType::Continents && !plates.empty()) {
        aoc::Random plateWarpRng(noiseRng);
        // Parallel: Voronoi nearest-plate per tile is independent.
        // Each iteration writes ONLY grid.setPlateId(idx, ...). Reads
        // shared `plates` vector + plateWarpRng noise (deterministic
        // for given seed/coords). Race-free because each tile updates
        // a distinct plateId entry.
        // Note: plateWarpRng's nextFloat is stateful but fractalNoise
        // here uses a hash-mixed seed inside, so per-call determinism
        // holds without the rng's internal counter.
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const float nx = static_cast<float>(col) / static_cast<float>(width);
                const float ny = static_cast<float>(row) / static_cast<float>(height);
                // Match the gentle two-octave warp from the elevation
                // pass so overlay tints / borders trace the actual
                // continent outlines smoothly.
                // Multi-octave warp with stronger low-frequency
                // component. Real plate boundaries follow accreted
                // fault lines that wander a lot — they're not
                // Voronoi-curved. Larger warp amplitude (0.18 + 0.05)
                // bends boundaries dramatically. Subsequent 6-pass
                // smoothing erases noise pixels so territories stay
                // coherent.
                const float pwX1 =
                    (fractalNoise(nx * 1.2f, ny * 1.2f, 4, 2.0f, 0.55f, plateWarpRng) - 0.5f) * 0.18f;
                const float pwY1 =
                    (fractalNoise(nx * 1.2f + 17.0f, ny * 1.2f + 31.0f, 4, 2.0f, 0.55f, plateWarpRng) - 0.5f) * 0.18f;
                const float pwX2 =
                    (fractalNoise(nx * 3.0f, ny * 3.0f, 2, 2.0f, 0.5f, plateWarpRng) - 0.5f) * 0.05f;
                const float pwY2 =
                    (fractalNoise(nx * 3.0f + 9.0f, ny * 3.0f + 21.0f, 2, 2.0f, 0.5f, plateWarpRng) - 0.5f) * 0.05f;
                const float pwx = nx + pwX1 + pwX2;
                const float pwy = ny + pwY1 + pwY2;
                float d1Sq = 1e9f;
                int32_t nearest = -1;
                // Multi-seed weighted Voronoi for plate ownership:
                // each plate's distance is the MIN over its primary
                // seed + all extraSeeds, divided by weight². Lets
                // polar plates with ~5 seeds claim the entire polar
                // band, and lets some normal plates have L/lobed
                // territories via their extra seed.
                for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                    const Plate& p = plates[pi];
                    const float cs = std::cos(p.rot);
                    const float sn = std::sin(p.rot);
                    auto seedDsq = [&](float sx, float sy) {
                        float dx = pwx - sx;
                        float dy = pwy - sy;
                        if (cylSim) {
                            if (dx >  0.5f) { dx -= 1.0f; }
                            if (dx < -0.5f) { dx += 1.0f; }
                        }
                        const float lx = (dx * cs + dy * sn) / p.aspect;
                        const float ly = (-dx * sn + dy * cs) * p.aspect;
                        return lx * lx + ly * ly;
                    };
                    float minDsq = seedDsq(p.cx, p.cy);
                    for (const std::pair<float, float>& es : p.extraSeeds) {
                        const float d = seedDsq(es.first, es.second);
                        if (d < minDsq) { minDsq = d; }
                    }
                    const float dsq = minDsq / (p.weight * p.weight);
                    if (dsq < d1Sq) {
                        d1Sq = dsq;
                        nearest = static_cast<int32_t>(pi);
                    }
                }
                if (nearest >= 0 && nearest < 255) {
                    grid.setPlateId(row * width + col,
                        static_cast<uint8_t>(nearest));
                }
            }
        }

        // 6 majority-vote passes at 3-of-6 threshold cleans up the
        // splatter the stronger warp introduces, leaving coherent
        // irregular territories.
        for (int32_t pass = 0; pass < 6; ++pass) {
            std::vector<uint8_t> nextId(static_cast<std::size_t>(width * height), 0xFFu);
            // Parallel: majority vote reads grid.plateId neighbours,
            // writes only nextId[idx]. Race-free.
            AOC_PARALLEL_FOR_ROWS
            for (int32_t row = 0; row < height; ++row) {
                for (int32_t col = 0; col < width; ++col) {
                    const int32_t idx = row * width + col;
                    const uint8_t my = grid.plateId(idx);
                    nextId[static_cast<std::size_t>(idx)] = my;
                    if (my == 0xFFu) { continue; }
                    const hex::AxialCoord ax = hex::offsetToAxial({col, row});
                    std::array<int32_t, 6> nbrCounts{};
                    std::array<uint8_t, 6> nbrIds{};
                    int32_t uniq = 0;
                    int32_t valid = 0;
                    for (const hex::AxialCoord& n : hex::neighbors(ax)) {
                        if (!grid.isValid(n)) { continue; }
                        ++valid;
                        const uint8_t nid = grid.plateId(grid.toIndex(n));
                        if (nid == 0xFFu) { continue; }
                        bool found = false;
                        for (int32_t k = 0; k < uniq; ++k) {
                            if (nbrIds[static_cast<std::size_t>(k)] == nid) {
                                ++nbrCounts[static_cast<std::size_t>(k)];
                                found = true; break;
                            }
                        }
                        if (!found && uniq < 6) {
                            nbrIds[static_cast<std::size_t>(uniq)] = nid;
                            nbrCounts[static_cast<std::size_t>(uniq)] = 1;
                            ++uniq;
                        }
                    }
                    if (valid == 0) { continue; }
                    // Find the majority neighbour plate.
                    int32_t bestCount = 0;
                    uint8_t bestId = my;
                    for (int32_t k = 0; k < uniq; ++k) {
                        if (nbrCounts[static_cast<std::size_t>(k)] > bestCount) {
                            bestCount = nbrCounts[static_cast<std::size_t>(k)];
                            bestId = nbrIds[static_cast<std::size_t>(k)];
                        }
                    }
                    // Switch ownership only when overwhelming majority
                    // disagrees with us (4+ neighbours of a different plate).
                    if (bestId != my && bestCount >= 3) {
                        nextId[static_cast<std::size_t>(idx)] = bestId;
                    }
                }
            }
            for (int32_t i = 0; i < width * height; ++i) {
                const uint8_t nid = nextId[static_cast<std::size_t>(i)];
                if (nid != 0xFFu) {
                    grid.setPlateId(i, nid);
                }
            }
        }
        // Persist hotspot positions for the overlay.
        std::vector<std::pair<float, float>> hsCopy;
        hsCopy.reserve(hotspots.size());
        for (const Hotspot& h : hotspots) {
            hsCopy.emplace_back(h.cx, h.cy);
        }
        grid.setHotspots(std::move(hsCopy));
        // Persist plate motion + center vectors so the plate overlay
        // can colour boundaries by relative-motion type (convergent /
        // divergent / transform).
        std::vector<std::pair<float, float>> motions;
        std::vector<std::pair<float, float>> centers;
        std::vector<float>                   landFracs;
        motions.reserve(plates.size());
        centers.reserve(plates.size());
        landFracs.reserve(plates.size());
        for (const Plate& p : plates) {
            motions.emplace_back(p.vx, p.vy);
            centers.emplace_back(p.cx, p.cy);
            landFracs.push_back(p.landFraction);
        }
        grid.setPlateMotions(std::move(motions));
        grid.setPlateCenters(std::move(centers));
        grid.setPlateLandFrac(std::move(landFracs));

        // ============================================================
        // POST-SIM GEOLOGICAL PASSES
        // ============================================================
        // Run after plate-id assignment + motion storage but BEFORE
        // water threshold is computed, so sediment + foreland basin
        // contributions can lift basin tiles above sea level (filling
        // shallow continental troughs as real Earth does).
        //
        // Passes (in order):
        //   1. Per-tile crust age — for the CrustAge overlay; per-plate
        //      mean age modulated by distance to plate centroid (interior
        //      = older, edges near ridges = younger).
        //   2. Suture / ophiolite marking — tiles within seam radius
        //      of recorded merge events get rockType = Ophiolite (3).
        //   3. Sediment yield + deposition — every high-orogeny tile
        //      donates eroded mass to its lowest-orogeny land neighbour;
        //      mass conservation creates alluvial plains around belts.
        //   4. Foreland basin loading — low-orogeny tiles adjacent to
        //      mountain belts receive flexural sediment loading from
        //      the orogen's weight (Po Valley, Ganges Plain).
        //   5. Active vs passive margin classification — coastal land
        //      tiles tagged based on whether their nearest plate
        //      boundary is convergent (active) or not (passive).
        //   6. Apply sediment + ophiolite to elevation map so the
        //      water threshold derived later includes the new fill.
        // ------------------------------------------------------------
        // Pass 1: per-tile crust age
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t idx = row * width + col;
                const uint8_t pid = grid.plateId(idx);
                if (pid == 0xFFu) { continue; }
                if (pid >= plates.size()) { continue; }
                const Plate& p = plates[pid];
                // Distance from plate centroid in plate-local coords;
                // interior tiles are oldest (continental craton centre),
                // edge tiles youngest (ridge-side or accretionary belt).
                float dx = (static_cast<float>(col) + 0.5f)
                           / static_cast<float>(width)  - p.cx;
                float dy = (static_cast<float>(row) + 0.5f)
                           / static_cast<float>(height) - p.cy;
                if (cylSim) {
                    if (dx >  0.5f) { dx -= 1.0f; }
                    if (dx < -0.5f) { dx += 1.0f; }
                }
                const float dist = std::sqrt(dx * dx + dy * dy);
                // Continental: interior = old craton, edges = younger
                // accreted terranes. Oceanic: interior = old (away from
                // ridge), edges = young (ridge-side fresh crust). Use
                // landFraction to pick gradient direction.
                const float radial = std::clamp(dist / 0.40f, 0.0f, 1.0f);
                float age;
                if (p.landFraction > 0.40f) {
                    // Continental: interior older, edges younger.
                    age = p.crustAge * (1.0f - radial * 0.30f);
                } else {
                    // Oceanic: interior older, edges (ridge side)
                    // younger. Same shape but stronger gradient since
                    // oceanic crust ages purely from ridge outward.
                    age = p.crustAge * (1.0f - radial * 0.60f);
                }
                crustAgeTile[static_cast<std::size_t>(idx)] = std::max(0.0f, age);
            }
        }

        // Pass 2: ophiolite suture marking
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t idx = row * width + col;
                const float wx = (static_cast<float>(col) + 0.5f)
                                 / static_cast<float>(width);
                const float wy = (static_cast<float>(row) + 0.5f)
                                 / static_cast<float>(height);
                for (const SutureSeam& s : sutureSeams) {
                    float ddx = wx - s.x;
                    float ddy = wy - s.y;
                    if (cylSim) {
                        if (ddx >  0.5f) { ddx -= 1.0f; }
                        if (ddx < -0.5f) { ddx += 1.0f; }
                    }
                    const float dd = std::sqrt(ddx * ddx + ddy * ddy);
                    // Narrow band: only tiles within ~0.4 × seam radius
                    // get ophiolite mark — keeps sutures as thin lines
                    // not big circular zones.
                    if (dd < s.r * 0.45f) {
                        ophioliteMask[static_cast<std::size_t>(idx)] = 1;
                        break;
                    }
                }
            }
        }

        // Pass 3 + 4: sediment yield, deposition, foreland basin loading
        // Helper: hex neighbours in offset coords for current tile.
        auto neighbourIdx = [&](int32_t col, int32_t row,
                                 int32_t dir, int32_t& outIdx) {
            // Offset-coord neighbours depend on row parity.
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
        // Sediment yield: every tile with orogeny > 0.08 sheds 8 % of
        // its excess to its 6 neighbours, biased toward the LOWEST-
        // orogeny neighbour (downhill). Iterates twice so plain tiles
        // adjacent to long mountain belts receive sediment from both
        // sides (Ganges + Bengal Fan, Po Valley).
        // Parallel sediment scatter: each thread accumulates into a
        // PRIVATE buffer, then we reduce all buffers at the end. This
        // avoids atomic-add cost on the inner-most write while keeping
        // race-free correctness.
        for (int32_t pass = 0; pass < 2; ++pass) {
            #ifdef AOC_HAS_OPENMP
            const int32_t nThreads = omp_get_max_threads();
            #else
            const int32_t nThreads = 1;
            #endif
            const int32_t sedTotal = width * height;
            std::vector<std::vector<float>> threadSed(
                static_cast<std::size_t>(nThreads),
                std::vector<float>(static_cast<std::size_t>(sedTotal), 0.0f));
            AOC_PARALLEL_FOR_ROWS
            for (int32_t row = 0; row < height; ++row) {
                #ifdef AOC_HAS_OPENMP
                const int32_t tid = omp_get_thread_num();
                #else
                const int32_t tid = 0;
                #endif
                std::vector<float>& myBuf =
                    threadSed[static_cast<std::size_t>(tid)];
                for (int32_t col = 0; col < width; ++col) {
                    const int32_t idx = row * width + col;
                    const float oro = orogeny[
                        static_cast<std::size_t>(idx)];
                    if (oro < 0.08f) { continue; }
                    const float yield = (oro - 0.05f) * 0.10f;
                    int32_t bestIdx[3] = {-1, -1, -1};
                    float   bestOro[3] = {1e9f, 1e9f, 1e9f};
                    for (int32_t d = 0; d < 6; ++d) {
                        int32_t nIdx;
                        if (!neighbourIdx(col, row, d, nIdx)) { continue; }
                        const float nOro = orogeny[
                            static_cast<std::size_t>(nIdx)];
                        if (nOro >= oro) { continue; }
                        for (int32_t k = 0; k < 3; ++k) {
                            if (nOro < bestOro[k]) {
                                for (int32_t j = 2; j > k; --j) {
                                    bestOro[j] = bestOro[j - 1];
                                    bestIdx[j] = bestIdx[j - 1];
                                }
                                bestOro[k] = nOro;
                                bestIdx[k] = nIdx;
                                break;
                            }
                        }
                    }
                    int32_t targets = 0;
                    for (int32_t k = 0; k < 3; ++k) {
                        if (bestIdx[k] >= 0) { ++targets; }
                    }
                    if (targets == 0) { continue; }
                    const float perTarget = yield
                        / static_cast<float>(targets);
                    for (int32_t k = 0; k < 3; ++k) {
                        if (bestIdx[k] < 0) { continue; }
                        myBuf[static_cast<std::size_t>(bestIdx[k])]
                            += perTarget;
                    }
                }
            }
            // Reduce per-thread buffers into shared sediment array.
            for (const auto& buf : threadSed) {
                AOC_PARALLEL_FOR_ROWS
                for (int32_t i = 0; i < sedTotal; ++i) {
                    sediment[static_cast<std::size_t>(i)]
                        += buf[static_cast<std::size_t>(i)];
                }
            }
        }
        // Foreland basin flexural loading: low-orogeny tiles whose
        // neighbours include a heavy mountain belt receive extra
        // sediment loading from the lithospheric flexure response to
        // the orogen's weight. Models the deep foredeep basins that
        // run along thrust belts (Ganges Plain, Po Valley, Andean
        // Foreland Basins).
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t idx = row * width + col;
                const float oro = orogeny[
                    static_cast<std::size_t>(idx)];
                if (oro >= 0.08f) { continue; } // self is mountain, skip
                float nbMtnSum = 0.0f;
                int32_t nbMtnCount = 0;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!neighbourIdx(col, row, d, nIdx)) { continue; }
                    const float nOro = orogeny[
                        static_cast<std::size_t>(nIdx)];
                    if (nOro > 0.10f) {
                        nbMtnSum += nOro;
                        ++nbMtnCount;
                    }
                }
                if (nbMtnCount > 0) {
                    sediment[static_cast<std::size_t>(idx)]
                        += 0.04f * nbMtnSum;
                }
            }
        }

        // Pass 5: active vs passive margin classification
        // For each LAND tile within 4 steps of WATER, look up nearest
        // boundary. If neighboring plate is converging (closing rate
        // negative against this plate): active margin (1). Else: passive
        // margin (2). Interior tiles stay 0.
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t idx = row * width + col;
                if (elevationMap[static_cast<std::size_t>(idx)]
                        < 0.0f) { continue; } // crude land check
                // Check if any neighbour up to 3 steps away is below
                // the projected water threshold (use 0 as a rough cut
                // since the actual threshold is computed later).
                bool nearWater = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!neighbourIdx(col, row, d, nIdx)) { continue; }
                    if (elevationMap[static_cast<std::size_t>(nIdx)]
                            < 0.0f) { nearWater = true; break; }
                }
                if (!nearWater) { continue; }
                // Find this tile's owning plate + nearest other plate
                // with different id among neighbours.
                const uint8_t myPid = grid.plateId(idx);
                if (myPid == 0xFFu || myPid >= plates.size()) { continue; }
                int32_t otherPid = -1;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!neighbourIdx(col, row, d, nIdx)) { continue; }
                    const uint8_t pid = grid.plateId(nIdx);
                    if (pid == 0xFFu) { continue; }
                    if (pid != myPid) { otherPid = pid; break; }
                }
                if (otherPid < 0) {
                    // No different neighbour — coast adjacent to ocean
                    // owned by the SAME plate (shelf), defaults passive.
                    marginTypeTile[static_cast<std::size_t>(idx)] = 2;
                    continue;
                }
                const Plate& A = plates[myPid];
                const Plate& B = plates[static_cast<std::size_t>(otherPid)];
                float bx = B.cx - A.cx;
                float by = B.cy - A.cy;
                if (cylSim) {
                    if (bx >  0.5f) { bx -= 1.0f; }
                    if (bx < -0.5f) { bx += 1.0f; }
                }
                const float bnLen = std::sqrt(bx * bx + by * by);
                if (bnLen < 1e-4f) { continue; }
                bx /= bnLen; by /= bnLen;
                const float relVx = A.vx - B.vx;
                const float relVy = A.vy - B.vy;
                const float closingRate = relVx * bx + relVy * by;
                // closingRate > 0 = closing (since b is from A toward B,
                // positive means A is moving toward B). Active when
                // closing meaningfully.
                if (closingRate > 0.04f) {
                    marginTypeTile[static_cast<std::size_t>(idx)] = 1; // active
                } else {
                    marginTypeTile[static_cast<std::size_t>(idx)] = 2; // passive
                }
            }
        }

        // Pass 6: apply sediment + ophiolite uplift onto elevationMap
        // BEFORE water threshold is computed, so sediment fill raises
        // basins into Plains tier instead of staying submerged.
        for (std::size_t i = 0; i < elevationMap.size(); ++i) {
            elevationMap[i] += sediment[i] * 0.55f;
            if (ophioliteMask[i] != 0) {
                // Ophiolite tiles get a tiny ridge bump (~Hills tier);
                // makes the suture line readable on the elevation map.
                elevationMap[i] += 0.06f;
            }
        }

        // Persist tile-level fields onto the grid for overlays + game-
        // mechanic queries (mineral resources, agricultural fertility,
        // etc, can later read from rockType / sediment / margin).
        grid.setCrustAgeTile(crustAgeTile);
        grid.setSedimentDepth(sediment);
        grid.setMarginType(marginTypeTile);
    }

    // Sort elevations to find the water threshold
    std::vector<float> sortedElevations(elevationMap);
    std::sort(sortedElevations.begin(), sortedElevations.end());
    std::size_t waterCutoff = static_cast<std::size_t>(
        effectiveWaterRatio * static_cast<float>(sortedElevations.size()));
    float waterThreshold = sortedElevations[std::min(waterCutoff, sortedElevations.size() - 1)];

    // Coastal ridge bias: in real tectonics the highest peaks cluster near
    // plate margins, which on a continent maps to ~3-6 hexes inland from
    // the coast.  Previous impl selected mountains purely by absolute
    // elevation → "always a blob of mountains in the middle".  We compute
    // BFS distance to coast for every land tile and boost elevations that
    // sit in the young-margin band before recalculating the mountain cutoff.
    const int32_t totalTiles = width * height;
    std::vector<int32_t> distFromCoast(static_cast<size_t>(totalTiles), -1);
    std::vector<int32_t> coastQ;
    coastQ.reserve(static_cast<size_t>(totalTiles));
    for (int32_t i = 0; i < totalTiles; ++i) {
        if (elevationMap[static_cast<size_t>(i)] < waterThreshold) {
            distFromCoast[static_cast<size_t>(i)] = 0;
            coastQ.push_back(i);
        }
    }
    for (size_t h = 0; h < coastQ.size(); ++h) {
        const int32_t idx = coastQ[h];
        const int32_t d = distFromCoast[static_cast<size_t>(idx)];
        const int32_t col = idx % width;
        const int32_t row = idx / width;
        const hex::AxialCoord axial = hex::offsetToAxial({col, row});
        for (const hex::AxialCoord& n : hex::neighbors(axial)) {
            if (!grid.isValid(n)) { continue; }
            const int32_t ni = grid.toIndex(n);
            if (distFromCoast[static_cast<size_t>(ni)] >= 0) { continue; }
            distFromCoast[static_cast<size_t>(ni)] = d + 1;
            coastQ.push_back(ni);
        }
    }

    // Apply coastal-ridge bonus to elevation before mountain selection.
    // For Continents the orogeny field already encodes which coasts
    // are active subduction belts, so we skip the global coastal bias
    // (which previously dropped a Gaussian mountain hump on every
    // coast). Other map types still get the bias since they don't run
    // the plate sim.
    std::vector<float> mountainElev = elevationMap;
    if (config.mapType != MapType::Continents) {
        for (int32_t i = 0; i < totalTiles; ++i) {
            if (distFromCoast[static_cast<size_t>(i)] <= 0) { continue; }  // water
            const int32_t d = distFromCoast[static_cast<size_t>(i)];
            float bonus = 0.0f;
            if (d >= 2 && d <= 6) {
                const float peak = 4.0f;
                const float sigma = 2.5f;
                const float x = (static_cast<float>(d) - peak) / sigma;
                bonus = 0.18f * std::exp(-x * x);
            } else if (d > 8) {
                bonus = -0.05f;
            }
            mountainElev[static_cast<size_t>(i)] += bonus;
        }
    } else {
        // Continents: small inland penalty only, so flat interior tiles
        // don't accidentally beat orogeny-elevated tiles in the global
        // mountain percentile.
        for (int32_t i = 0; i < totalTiles; ++i) {
            if (distFromCoast[static_cast<size_t>(i)] <= 0) { continue; }
            const int32_t d = distFromCoast[static_cast<size_t>(i)];
            if (d > 8) {
                mountainElev[static_cast<size_t>(i)] -= 0.02f;
            }
        }
    }

    // Mountain threshold — combine an absolute orogeny gate with a
    // percentile-based elevation gate. The absolute gate means erosion
    // can REMOVE mountains over time: as orogeny decays, tiles drop
    // below MIN_OROGENY_FOR_MOUNTAIN and become Hills/Plains. The
    // percentile gate caps the proportion of tiles that can ever be
    // mountains so a mountain-rich initial sim doesn't blanket the world.
    // Real Earth: Appalachians were Himalaya-tall ~250 My ago; today
    // they barely cross the Hills feature threshold.
    constexpr float MIN_OROGENY_FOR_MOUNTAIN = 0.06f;
    std::vector<float> sortedMountainElev(mountainElev);
    std::sort(sortedMountainElev.begin(), sortedMountainElev.end());
    std::size_t mountainCutoff = sortedMountainElev.size() -
        static_cast<std::size_t>(config.mountainRatio * static_cast<float>(sortedMountainElev.size()));
    float mountainThreshold = sortedMountainElev[std::min(mountainCutoff, sortedMountainElev.size() - 1)];

    // 2-D climate model: temperature × moisture → biome.
    // Replaces pure-latitude assignment which produced perfect horizontal
    // bands. Real climate is driven by:
    //  • Temperature: latitude (cosine curve) + altitude correction + noise
    //  • Moisture: Hadley-cell latitude bands + distance-to-ocean + noise
    //
    // Hadley cell (Earth's large-scale circulation):
    //  equatorial band (0-12% from equator) : ITCZ convergence → very wet
    //  subtropical (12-32% from equator)    : Hadley subsidence → dry/desert
    //  mid-latitude (32-60% from equator)   : westerlies/cyclones → moderate
    //  polar (>60% from equator)            : polar high → dry+cold
    //
    // Continental effect: interior tiles far from any ocean get drier and
    // more extreme (Continental climate) versus coastal tiles which stay
    // moderate (Oceanic / Mediterranean).
    aoc::Random tempRng(rng.next());
    aoc::Random moiRng(rng.next());

    // Maximum observed coast distance — used to normalise the continental
    // drying factor so small and large maps scale identically.
    int32_t maxCoastDist = 1;
    for (int32_t i = 0; i < totalTiles; ++i) {
        maxCoastDist = std::max(maxCoastDist, distFromCoast[static_cast<std::size_t>(i)]);
    }

    // Per-row westward / eastward ocean distance — used to model ocean
    // current effects on coastal climate. Real Earth pattern (subtropical
    // gyres rotate clockwise NH / counter-clockwise SH, but the result on
    // continental coasts is symmetric):
    //   • Low-mid lat (10-40 % from equator):
    //       - Cold current rises along WEST coasts (California, Humboldt,
    //         Benguela, Canary). Coastal upwelling cools+dries the land.
    //         Atacama, Namib, Sahara coast, Baja California are deserts
    //         despite being right next to the ocean.
    //       - Warm current along EAST coasts (Gulf Stream Caribbean leg,
    //         Kuroshio, Brazil, Agulhas, E Australian). Adds humidity +
    //         mild winters: Florida, Japan, SE China, SE Africa.
    //   • Mid-high lat (50-75 % from equator):
    //       - Warm current crosses ocean and lands on far-east shore
    //         of the basin = WEST coast of the next continent. Gulf
    //         Stream warming W. Europe, North Atlantic Drift, Alaska
    //         Current. Result: oceanic / mild climate well above where
    //         latitude alone would predict tundra.
    //       - Cold current returns south along EAST coast (Labrador,
    //         Oyashio). Newfoundland / NE Russia are colder than their
    //         latitude implies.
    // Sweep each row twice (L-to-R and R-to-L). On cylindrical maps the
    // sweep wraps so islands' shadow doesn't extend infinitely.
    // Wind/rain model. Earth's prevailing wind belts (mirrored N/S):
    //   |lat from equator| < 0.30 : trade winds → easterly (E→W flow)
    //   0.30 ≤ |lat| < 0.60       : westerlies   (W→E flow)
    //   |lat| ≥ 0.60              : polar easterlies (E→W flow)
    // The trade winds and westerlies are ocean-current drivers — their
    // direction matches what raises Atacama deserts, NW-Europe rain,
    // and the Sahara. For each land tile, walk WIND_WALK_RANGE tiles
    // upwind tracking moisture: start at +1 if walk hits ocean before
    // the range expires, subtract for each mountain crossed (rain
    // shadow), subtract per-tile attenuation. Result fed into moisture.
    constexpr int32_t WIND_WALK_RANGE = 14;
    std::vector<float> windMoist(static_cast<std::size_t>(totalTiles), 0.0f);
    const bool cylClim = (grid.topology() == aoc::map::MapTopology::Cylindrical);
    auto upwindStep = [](float lat) -> int32_t {
        const float lf = 2.0f * std::abs(lat - 0.5f);
        // Easterly bands → upwind is east (+col); Westerly band → upwind is west (-col).
        if (lf < 0.30f || lf >= 0.60f) { return +1; } // trade / polar easterly
        return -1;                                     // westerlies
    };
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const int32_t step = upwindStep(ny);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx = row * width + col;
            if (elevationMap[static_cast<std::size_t>(idx)] < waterThreshold) {
                continue;
            }
            float carry = 0.0f;
            int32_t mountainCount = 0;
            int32_t firstMountainDist = -1; // distance to nearest upwind mountain
            bool reachedOcean = false;
            for (int32_t s = 1; s <= WIND_WALK_RANGE; ++s) {
                int32_t uc = col + step * s;
                if (cylClim) {
                    uc = ((uc % width) + width) % width;
                } else if (uc < 0 || uc >= width) {
                    break;
                }
                const int32_t uidx = row * width + uc;
                if (elevationMap[static_cast<std::size_t>(uidx)] < waterThreshold) {
                    const float distAtten = 1.0f - static_cast<float>(s)
                        / static_cast<float>(WIND_WALK_RANGE);
                    carry = distAtten - 0.30f * static_cast<float>(mountainCount);
                    reachedOcean = true;
                    break;
                }
                if (mountainElev[static_cast<std::size_t>(uidx)] >= mountainThreshold) {
                    ++mountainCount;
                    if (firstMountainDist < 0) { firstMountainDist = s; }
                }
            }
            if (!reachedOcean) { carry = -0.10f; }

            // Rain-shadow distance attenuation. Tiles JUST leeward of a
            // close mountain (1-3 tiles away upwind) get the strongest
            // dry signal — the air just descended after dropping its
            // moisture (foehn / chinook effect). Beyond ~6 tiles the
            // shadow weakens because air picks moisture back up.
            if (firstMountainDist > 0 && firstMountainDist <= 3) {
                carry -= 0.25f;
            }

            // Orographic precipitation. Walk DOWNWIND a few tiles —
            // if a mountain is close downwind, this tile is on the
            // WINDWARD side and gets an air-uplift moisture boost.
            // Real example: Pacific NW (Olympic Peninsula, Cascades
            // west slopes) gets 4000+ mm/yr of rain because moist
            // ocean air is forced upward over the mountains here.
            constexpr int32_t WINDWARD_RANGE = 3;
            for (int32_t s = 1; s <= WINDWARD_RANGE; ++s) {
                int32_t dc = col - step * s; // downwind = -step direction
                if (cylClim) {
                    dc = ((dc % width) + width) % width;
                } else if (dc < 0 || dc >= width) {
                    break;
                }
                const int32_t didx = row * width + dc;
                if (elevationMap[static_cast<std::size_t>(didx)] < waterThreshold) {
                    break; // hit ocean downwind, no orographic effect
                }
                if (mountainElev[static_cast<std::size_t>(didx)] >= mountainThreshold) {
                    // Closer = stronger boost. 1 tile away → +0.30,
                    // 2 → +0.20, 3 → +0.10.
                    carry += 0.30f - 0.10f * static_cast<float>(s - 1);
                    break;
                }
            }

            windMoist[static_cast<std::size_t>(idx)] = std::clamp(carry, -0.50f, 0.50f);
        }
    }

    std::vector<int32_t> westOceanDist(static_cast<std::size_t>(totalTiles), width);
    std::vector<int32_t> eastOceanDist(static_cast<std::size_t>(totalTiles), width);
    for (int32_t row = 0; row < height; ++row) {
        // Westward scan: distance to nearest water at column <= current.
        int32_t lastWaterCol = cylClim ? -width : -width;  // far left = "no water known"
        // Initial pass for cylindrical: locate any water in this row to seed
        // the wrap-around.
        if (cylClim) {
            for (int32_t col = 0; col < width; ++col) {
                if (elevationMap[static_cast<std::size_t>(row * width + col)]
                        < waterThreshold) {
                    lastWaterCol = col - width; // pretend it was on the prev wrap
                    break;
                }
            }
        }
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx = row * width + col;
            if (elevationMap[static_cast<std::size_t>(idx)] < waterThreshold) {
                lastWaterCol = col;
                westOceanDist[static_cast<std::size_t>(idx)] = 0;
            } else {
                westOceanDist[static_cast<std::size_t>(idx)] =
                    std::min(width, col - lastWaterCol);
            }
        }
        // Eastward scan: mirror logic.
        int32_t nextWaterCol = 2 * width;
        if (cylClim) {
            for (int32_t col = width - 1; col >= 0; --col) {
                if (elevationMap[static_cast<std::size_t>(row * width + col)]
                        < waterThreshold) {
                    nextWaterCol = col + width;
                    break;
                }
            }
        }
        for (int32_t col = width - 1; col >= 0; --col) {
            const int32_t idx = row * width + col;
            if (elevationMap[static_cast<std::size_t>(idx)] < waterThreshold) {
                nextWaterCol = col;
                eastOceanDist[static_cast<std::size_t>(idx)] = 0;
            } else {
                eastOceanDist[static_cast<std::size_t>(idx)] =
                    std::min(width, nextWaterCol - col);
            }
        }
    }

    // Percentile cap on mountain orogeny: only the top ~6 % of LAND
    // tiles by orogeny may become Mountain. Combined with the absolute
    // 0.20 floor, this prevents saturated boundary regions from
    // covering an entire continent — narrow ridges along the most
    // stressed seams remain, lower-stress halos drop to Hills.
    float mountainOrogenyPercentile = 1.0e9f;
    {
        std::vector<float> landOro;
        landOro.reserve(static_cast<std::size_t>(totalTiles));
        for (int32_t i = 0; i < totalTiles; ++i) {
            if (elevationMap[static_cast<std::size_t>(i)] >= waterThreshold) {
                const float v = orogeny[static_cast<std::size_t>(i)];
                if (v > 0.0f) {
                    landOro.push_back(v);
                }
            }
        }
        if (!landOro.empty()) {
            // nth-element at the 94th percentile → top 6 % above this.
            const std::size_t n = landOro.size();
            const std::size_t k = static_cast<std::size_t>(
                static_cast<double>(n) * 0.94);
            const std::size_t kClamped = std::min(k, n - 1);
            std::nth_element(landOro.begin(),
                             landOro.begin()
                                 + static_cast<std::ptrdiff_t>(kClamped),
                             landOro.end());
            mountainOrogenyPercentile = landOro[kClamped];
        }
    }

    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            int32_t index = row * width + col;
            float elev = elevationMap[static_cast<std::size_t>(index)];

            if (elev < waterThreshold) {
                // All water starts as Ocean; smoothCoastlines BFS
                // reclassifies tiles within 3 steps of land as ShallowWater.
                grid.setTerrain(index, TerrainType::Ocean);
                grid.setElevation(index, -1);
                continue;
            }

            // Mountain assignment: ABSOLUTE orogeny threshold only.
            // Earlier percentile-based pick was selecting the top X%
            // of elevation tiles regardless of orogeny — caught a lot
            // of base-noise peaks scattered across continents,
            // producing speckled "noise mountains" instead of belts.
            // Now mountains form ONLY where orogeny accumulated to
            // ≥ 0.13 (boundaries), so they appear AS LINES along
            // plate seams (subduction arcs at coast, collision belts
            // inland). Tiles with 0.06 < orogeny < 0.13 become Hills
            // (existing pass below).
            const float oroAt = orogeny[static_cast<std::size_t>(index)];
            constexpr float MOUNTAIN_OROGENY_THRESHOLD = 0.20f;
            // Mountain assignment must clear BOTH the orogeny floor
            // AND the global mountain percentile (top X % of orogeny
            // values). Cap mountain coverage at ~6 % of land —
            // matches Earth's high-mountain proportion and prevents
            // saturated boundary rings from making half the world
            // mountainous.
            if (oroAt >= MOUNTAIN_OROGENY_THRESHOLD
                && oroAt >= mountainOrogenyPercentile) {
                grid.setTerrain(index, TerrainType::Mountain);
                grid.setElevation(index, 3);
                continue;
            }
            (void)mountainThreshold; // legacy percentile no longer used
            (void)MIN_OROGENY_FOR_MOUNTAIN;

            float nx = static_cast<float>(col) / static_cast<float>(width);
            float ny = static_cast<float>(row) / static_cast<float>(height);

            // ---- Temperature ----
            // latFromEquator ∈ [0,1]: 0 = equator (row = height/2), 1 = pole.
            const float latFromEquator = 2.0f * std::abs(ny - 0.5f); // 0=eq,1=pole
            // Cosine curve: warmer equator, colder poles.
            float temperature = std::cos(latFromEquator * 1.5708f); // cos(π/2 * lat)
            // Axial tilt scales the temperate band size. 0° = no tilt
            // (everywhere temperate); higher = stronger pole-equator
            // gradient. Real Earth = 23.5°. Stretch the cosine curve.
            {
                const float tiltScale = std::clamp(
                    config.axialTilt / 23.5f, 0.0f, 2.0f);
                temperature = std::pow(temperature,
                    std::max(0.4f, tiltScale));
            }
            // Climate phase shift.
            if (config.climatePhase == 1) {
                temperature = std::clamp(temperature + 0.10f, 0.0f, 1.0f);
            } else if (config.climatePhase == 2) {
                temperature = std::clamp(temperature - 0.12f, 0.0f, 1.0f);
            }
            // Milankovitch eccentricity adds noise amplitude on top of
            // the latitude curve (more eccentric orbit → larger seasonal
            // contrast → biome-extremity).
            if (config.milankovitchPhase > 0.05f) {
                const float dev = (fractalNoise(nx * 1.5f, ny * 1.5f,
                    2, 2.0f, 0.5f, tempRng) - 0.5f) * 2.0f;
                temperature += dev * config.milankovitchPhase * 0.10f;
                temperature = std::clamp(temperature, 0.0f, 1.0f);
            }
            // Altitude cools: mountains already separated above; hills/plains
            // correction from raw elevation above water.
            const float elevAboveWater = (elev - waterThreshold)
                / std::max(0.01f, 1.0f - waterThreshold);
            temperature -= elevAboveWater * 0.12f;
            // Low-frequency geographic noise breaks horizontal banding.
            temperature += (fractalNoise(nx, ny, 3, 3.0f, 0.5f, tempRng) - 0.5f) * 0.22f;
            temperature = std::clamp(temperature, 0.0f, 1.0f);

            // ---- Moisture ----
            // Hadley-cell base: wet at equator, dry at subtropics, moderate
            // mid-lat, dry+cold at poles.
            float moistureBase;
            if (latFromEquator < 0.12f) {
                moistureBase = 0.85f;  // ITCZ tropical wet
            } else if (latFromEquator < 0.32f) {
                // Hadley subsidence dry belt. Floor 0.36 (was 0.15) —
                // subtropics are drier than the rest, but only the
                // combination with cold-current west coasts, mountain
                // rain shadows, or deep continental interiors should
                // produce true desert. Plain subtropical interior
                // → semi-arid Plains, not Sahara.
                const float t = (latFromEquator - 0.12f) / 0.20f;
                moistureBase = 0.85f - t * 0.49f; // 0.85 → 0.36
            } else if (latFromEquator < 0.62f) {
                // Mid-latitude westerlies: moderate moisture
                // 0.36 → 0.65 over [0.32, 0.62].
                const float t = (latFromEquator - 0.32f) / 0.30f;
                moistureBase = 0.36f + t * 0.29f;
            } else {
                // Polar dry. 0.65 → 0.30 over the polar band.
                const float t = (latFromEquator - 0.62f) / 0.38f;
                moistureBase = 0.65f - t * 0.35f;
            }

            // Continental drying: tiles far from any ocean are drier.
            const float coastDist = static_cast<float>(
                distFromCoast[static_cast<std::size_t>(index)]);
            // Continental factor: 0 at coast, 1 in deep interior.
            // Using 0.70 of maxCoastDist (was 0.55) so only true
            // interiors hit the full drying penalty.
            const float continentalFactor = std::clamp(
                coastDist / (static_cast<float>(maxCoastDist) * 0.70f), 0.0f, 1.0f);

            // ---- Ocean current effects ----
            // Proximity factors fall off over CURRENT_RANGE tiles inland.
            // 0 inland past CURRENT_RANGE; 1.0 right at the coast on the
            // relevant side.
            constexpr int32_t CURRENT_RANGE = 12;
            const int32_t wd = westOceanDist[static_cast<std::size_t>(index)];
            const int32_t ed = eastOceanDist[static_cast<std::size_t>(index)];
            const float westProx = std::max(0.0f,
                1.0f - static_cast<float>(wd) / static_cast<float>(CURRENT_RANGE));
            const float eastProx = std::max(0.0f,
                1.0f - static_cast<float>(ed) / static_cast<float>(CURRENT_RANGE));

            // Heat-transport model. Warm currents move tropical heat
            // poleward (Gulf Stream lifts UK out of tundra latitude
            // into mild oceanic). The warming MAGNITUDE scales with
            // how much COLDER the destination latitude is than the
            // source — Iceland needs MORE warming than the Carolinas
            // to escape the cold default. Multiply the warm-current
            // bonus by `(1 - temperature)` so colder latitudes get
            // the biggest boost; cold currents likewise scale with
            // `temperature` (more cooling effect in already-warm zones).
            const float warmFactor = (1.0f - temperature);
            const float coldFactor = temperature;
            float currentTempDelta = 0.0f;
            float currentMoistDelta = 0.0f;
            if (latFromEquator >= 0.10f && latFromEquator < 0.40f) {
                // Subtropical: cold upwelling on west coasts, warm
                // currents on east coasts.
                currentTempDelta  += -0.20f * westProx * coldFactor
                                    + 0.10f * eastProx * warmFactor;
                currentMoistDelta += -0.32f * westProx + 0.22f * eastProx;
            } else if (latFromEquator >= 0.40f && latFromEquator < 0.70f) {
                // Mid-latitude: Gulf Stream warms west coasts (UK,
                // Norway, Alaska). Up to +0.32 in the coldest mid-lat
                // tile because the contrast with default temp is large.
                currentTempDelta  += 0.32f * westProx * warmFactor
                                    - 0.14f * eastProx * coldFactor;
                currentMoistDelta += 0.28f * westProx + 0.04f * eastProx;
            } else if (latFromEquator >= 0.70f) {
                // Sub-polar: warm-current end on west coast moderates
                // climate (Iceland, Bering). Big effect because base
                // temp is near freezing — the current literally lifts
                // these tiles above the Tundra cutoff.
                currentTempDelta  += 0.30f * westProx * warmFactor
                                    - 0.08f * eastProx * coldFactor;
                currentMoistDelta += 0.15f * westProx;
            }

            // CONTINENTALITY. Real Earth: deep continental interiors
            // experience LARGER seasonal swings (Mongolia: -40 °C
            // winter, +35 °C summer). For a single-snapshot game we
            // approximate by pushing tropical interiors HOTTER (extra
            // summer-effect on biome dryness) and polar interiors
            // COLDER (Siberian winter dominates the climate). Coastal
            // tiles stay near their oceanic mean. Net: continentality
            // amplifies the latitude signal.
            {
                const float meanShift = (latFromEquator < 0.45f)
                    ? +0.06f * continentalFactor   // tropical interior hotter
                    : -0.10f * continentalFactor;  // polar interior colder
                temperature += meanShift;
            }
            temperature = std::clamp(temperature + currentTempDelta, 0.0f, 1.0f);

            // Wind-driven moisture: positive when prevailing wind brings
            // ocean air across few/no mountains; negative inland of a
            // mountain wall (rain shadow). Already accounts for wind
            // direction by latitude band.
            const float windMoistTile = windMoist[static_cast<std::size_t>(index)];

            // MONSOON. Seasonal continent-ocean thermal contrast pulls
            // moist ocean air over warm continental interiors during
            // summer (Indian monsoon, East Asian monsoon, West African
            // monsoon, Australian monsoon). Approximate as moisture
            // boost on tropical-edge tiles (lat 0.10-0.35) that sit on
            // continents adjacent to large warm ocean. We don't have
            // basin-size info but the existing eastProx / westProx
            // capture coast-side adjacency: monsoon adds moisture on
            // the EAST side of continents in the trade-wind belt
            // (Indian monsoon comes from west; East Asian / Australian
            // from east — cancel by using max of either side).
            float monsoonBoost = 0.0f;
            if (latFromEquator >= 0.10f && latFromEquator < 0.40f) {
                const float oceanProx = std::max(westProx, eastProx);
                monsoonBoost = 0.18f * oceanProx
                    * (1.0f - continentalFactor * 0.6f); // strongest near coast
            }

            // ENSO equatorial dipole. El Niño (1) = wet east Pacific
            // (warm pool migrates east) → dry west; La Niña (2) =
            // opposite. Approximate via west/east-coast moisture skew
            // in equatorial band (lat<0.20).
            float ensoDelta = 0.0f;
            if (config.ensoState != 0 && latFromEquator < 0.20f) {
                const float skew = (config.ensoState == 1) ? +1.0f : -1.0f;
                ensoDelta = skew * (eastProx - westProx) * 0.18f;
            }
            const float moisture = std::clamp(
                moistureBase - continentalFactor * 0.32f
                + currentMoistDelta
                + windMoistTile * 0.45f
                + monsoonBoost
                + ensoDelta
                + (fractalNoise(nx * 1.5f, ny * 1.5f + 7.3f, 3, 4.0f, 0.5f, moiRng) - 0.5f) * 0.28f,
                0.0f, 1.0f);

            // ---- Biome table (T × M → terrain) ----
            // Follows Köppen-style classification simplified to the 5
            // terrain types available: Snow, Tundra, Desert, Plains, Grassland.
            TerrainType terrain;
            if (temperature < 0.12f) {
                terrain = TerrainType::Snow;
            } else if (temperature < 0.25f) {
                terrain = TerrainType::Tundra;
            } else {
                // 2D Köppen-style biome lookup. Temperature drives the
                // primary band, moisture differentiates within the band.
                // Crops grown on each:
                //  • Desert (hot+dry)         : dates, drip-irrigated cotton
                //  • Plains (warm semi-arid)  : wheat, paprika, olives,
                //                                vines, sunflower (Mediterranean)
                //  • Plains (cool steppe)     : barley, rye, hardy grains
                //  • Grassland (temperate)    : maize, soy, root vegetables
                //  • Grassland (tropical wet) : rice, bananas, sugar cane,
                //                                exotic fruit (with Jungle feature)
                //  • Tundra                    : reindeer moss, hardy berries
                if (temperature >= 0.65f) {
                    // Hot belt — tropical/subtropical.
                    if (moisture < 0.20f) {
                        terrain = TerrainType::Desert;            // Sahara, Atacama
                    } else if (moisture < 0.45f) {
                        terrain = TerrainType::Plains;            // savanna / Mediterranean
                    } else if (moisture < 0.65f) {
                        terrain = TerrainType::Plains;            // tropical wet-dry (Aw)
                    } else {
                        terrain = TerrainType::Grassland;         // Af tropical rainforest
                    }
                } else if (temperature >= 0.45f) {
                    // Warm temperate.
                    if (moisture < 0.22f) {
                        terrain = TerrainType::Desert;            // continental hot desert
                    } else if (moisture < 0.50f) {
                        terrain = TerrainType::Plains;            // semi-arid temperate
                    } else {
                        terrain = TerrainType::Grassland;         // humid temperate
                    }
                } else {
                    // Cool / cold temperate.
                    if (moisture < 0.35f) {
                        terrain = TerrainType::Plains;            // cold steppe
                    } else {
                        terrain = TerrainType::Grassland;         // boreal grassland
                    }
                }
            }

            grid.setTerrain(index, terrain);
            grid.setElevation(index, static_cast<int8_t>(
                std::clamp(static_cast<int>(elev * 4.0f), 0, 2)));

            // Mountain → Hills lifecycle. Tiles with moderate orogeny
            // (post-erosion remnants of an ancient range) are no longer
            // tall enough to be Mountains but retain hilly relief.
            // Real-world example: Appalachians today (~0.5-1 km) used
            // to be Himalaya-class (~5+ km) ~250 My ago. After erosion,
            // they read as a Hills feature on otherwise-grassland terrain.
            const float oroValue = orogeny[
                static_cast<std::size_t>(row * width + col)];
            if (terrain != TerrainType::Mountain
                && terrain != TerrainType::Snow
                && terrain != TerrainType::Tundra
                && oroValue > 0.06f) {
                grid.setFeature(index, FeatureType::Hills);
            }
        }
    }

    // Rain-shadow / wind conversion is now integrated into the
    // moisture computation above (windMoist field walks upwind across
    // the same wind belts and subtracts moisture per mountain crossed).
    // No separate binary post-pass needed — biome assignment already
    // produces the correct Desert/Plains/Grassland mix from T × M.
    (void)config;

    // ICE SHEET EXPANSION. Real Earth: continents sitting at the poles
    // host continental ice sheets (Antarctica today, Laurentide during
    // Pleistocene). Ice depresses crust isostatically (~30 % of ice
    // thickness, Greenland still rebounding). Override biome to Snow on
    // any LAND tile owned by a polar plate AND apply small elevation
    // depression so coastal tiles flatten into ice shelves.
    if (config.mapType == MapType::Continents) {
        const int32_t totalT = width * height;
        for (int32_t i = 0; i < totalT; ++i) {
            const aoc::map::TerrainType t = grid.terrain(i);
            if (t == aoc::map::TerrainType::Ocean
                || t == aoc::map::TerrainType::ShallowWater) {
                continue;
            }
            const uint8_t pid = grid.plateId(i);
            if (pid == 0xFFu || pid >= plates.size()) { continue; }
            if (!plates[pid].isPolar) { continue; }
            // Polar plate land tile → Snow terrain, Ice feature on
            // tundra-like tiles (so the existing Ice feature renders).
            grid.setTerrain(i, aoc::map::TerrainType::Snow);
            // Drop hills feature — flattened by ice cover.
            if (grid.feature(i) == aoc::map::FeatureType::Hills) {
                grid.setFeature(i, aoc::map::FeatureType::None);
            }
            // Add Ice feature for visualization (ice shelf / glacier).
            if (grid.feature(i) == aoc::map::FeatureType::None) {
                grid.setFeature(i, aoc::map::FeatureType::Ice);
            }
        }
    }

    // ROCK-TYPE ASSIGNMENT. Real Earth tiles host different rock types
    // by tectonic setting:
    //   Sedimentary (0): sediment-fill basins, plains, continental
    //                    shelves, abyssal plains
    //   Igneous     (1): hills built from arc volcanism, hotspot
    //                    volcanic islands, oceanic crust
    //   Metamorphic (2): mountain belts (regional metamorphism +
    //                    deep crustal exposure post-erosion)
    //   Ophiolite   (3): trapped ocean-crust slivers along continental
    //                    sutures (Oman, Cyprus, Troodos), set earlier
    //                    by suture seam pass
    // Drives mineral resource availability + game-mechanic queries.
    if (config.mapType == MapType::Continents) {
        const int32_t totalT = width * height;
        for (int32_t i = 0; i < totalT; ++i) {
            const aoc::map::TerrainType t = grid.terrain(i);
            const aoc::map::FeatureType f = grid.feature(i);
            uint8_t rt = 0; // default sedimentary
            if (ophioliteMask[static_cast<std::size_t>(i)] != 0) {
                rt = 3; // ophiolite
            } else if (t == aoc::map::TerrainType::Mountain) {
                rt = 2; // metamorphic
            } else if (f == aoc::map::FeatureType::Hills) {
                rt = 1; // igneous
            } else if (t == aoc::map::TerrainType::Ocean
                       || t == aoc::map::TerrainType::ShallowWater) {
                // Ocean floor: igneous (basaltic crust) by default,
                // overlaid with sediment in old basins. Use plate
                // landFraction proxy: if owning plate is oceanic,
                // ocean basalt; if sediment depth high, sedimentary.
                const uint8_t pid = grid.plateId(i);
                if (pid != 0xFFu && pid < plates.size()
                    && plates[pid].landFraction < 0.40f
                    && sediment[static_cast<std::size_t>(i)] < 0.04f) {
                    rt = 1; // young oceanic basalt
                }
            }
            rockTypeTile[static_cast<std::size_t>(i)] = rt;
        }
        grid.setRockType(rockTypeTile);
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
        std::vector<float>   soilFert  (static_cast<std::size_t>(totalT), 0.50f);
        std::vector<uint8_t> volcanism (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> hazard    (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> permafrost(static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> lakeFlag  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> upwelling (static_cast<std::size_t>(totalT), 0);

        // Hex-neighbour helper (offset coords).
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

        // Cache nearest-plate-boundary classification per tile from
        // realRock-style lookup. Used for hazard + volcanism.
        const auto& mots = grid.plateMotions();
        const auto& cens = grid.plateCenters();
        const auto& lfrs = grid.plateLandFrac();

        // ---- LAKES (positive generation) ----
        // Convert any LAND tile with strong negative orogeny (failed
        // rift / aulacogen scar OR continental rift band) AND elevation
        // close to threshold into ShallowWater + lake flag. Real
        // continental rift lakes (Tanganyika, Malawi, Baikal) sit in
        // tectonic depressions kilometres deep. Aulacogens that didn't
        // open to ocean often host inland seas (Mississippi Embayment).
        for (int32_t i = 0; i < totalT; ++i) {
            const aoc::map::TerrainType t = grid.terrain(i);
            if (t == aoc::map::TerrainType::Ocean
                || t == aoc::map::TerrainType::ShallowWater
                || t == aoc::map::TerrainType::Mountain) {
                continue;
            }
            const float oro = orogeny[static_cast<std::size_t>(i)];
            // Strong negative orogeny → rift basin → lake.
            if (oro < -0.06f) {
                grid.setTerrain(i, aoc::map::TerrainType::ShallowWater);
                grid.setElevation(i, -1);
                grid.setFeature(i, aoc::map::FeatureType::None);
                lakeFlag[static_cast<std::size_t>(i)] = 1;
            }
        }

        // ---- VOLCANISM markers ----
        // Per-tile volcanism: classify continental-arc tiles (convergent
        // boundary on continental land), hotspot-volcano tiles (where
        // a hotspot currently sits OR recent trail entry), continental-
        // rift tiles, and LIP / flood-basalt tiles.
        // Hotspot volcanism: scan grid.hotspots() positions, mark
        // tiles within 0.025 normalised distance.
        const auto& hsList = grid.hotspots();
        for (const auto& h : hsList) {
            for (int32_t row = 0; row < height; ++row) {
                for (int32_t col = 0; col < width; ++col) {
                    const float wx = (static_cast<float>(col) + 0.5f)
                        / static_cast<float>(width);
                    const float wy = (static_cast<float>(row) + 0.5f)
                        / static_cast<float>(height);
                    float dx = wx - h.first;
                    float dy = wy - h.second;
                    if (cylSim) {
                        if (dx >  0.5f) { dx -= 1.0f; }
                        if (dx < -0.5f) { dx += 1.0f; }
                    }
                    if (dx * dx + dy * dy < 0.0025f * 0.0025f * 100.0f) {
                        // small radius (~0.025)
                        const int32_t i = row * width + col;
                        volcanism[static_cast<std::size_t>(i)] = 2;
                    }
                }
            }
        }
        // Arc volcanism: tiles where this is the OVERRIDING side of a
        // convergent boundary (own plate continental, neighbour oceanic
        // and closing). Walk neighbours to test.
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t idx = row * width + col;
                if (grid.terrain(idx) == aoc::map::TerrainType::Ocean
                    || grid.terrain(idx) == aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                const uint8_t myPid = grid.plateId(idx);
                if (myPid == 0xFFu || myPid >= mots.size()) { continue; }
                if (myPid >= lfrs.size() || lfrs[myPid] < 0.40f) { continue; }
                // Check 6 neighbours up to 4 hexes away for ocean plate
                // converging.
                bool isArc = false;
                for (int32_t d = 0; d < 6 && !isArc; ++d) {
                    int32_t cur = idx;
                    int32_t cc = col, rr = row;
                    for (int32_t step = 0; step < 4 && !isArc; ++step) {
                        int32_t nIdx;
                        if (!nbHelper(cc, rr, d, nIdx)) { break; }
                        cur = nIdx;
                        cc = nIdx % width;
                        rr = nIdx / width;
                        const uint8_t nPid = grid.plateId(cur);
                        if (nPid == 0xFFu || nPid == myPid) { continue; }
                        if (nPid >= mots.size()
                            || nPid >= lfrs.size()) { continue; }
                        if (lfrs[nPid] >= 0.40f) { continue; } // not ocean
                        // Convergent test.
                        float bnx = cens[nPid].first  - cens[myPid].first;
                        float bny = cens[nPid].second - cens[myPid].second;
                        if (cylSim) {
                            if (bnx >  0.5f) { bnx -= 1.0f; }
                            if (bnx < -0.5f) { bnx += 1.0f; }
                        }
                        const float bnLen = std::sqrt(bnx * bnx + bny * bny);
                        if (bnLen < 1e-4f) { break; }
                        bnx /= bnLen; bny /= bnLen;
                        const float relVx = mots[myPid].first
                            - mots[nPid].first;
                        const float relVy = mots[myPid].second
                            - mots[nPid].second;
                        const float closing = relVx * bnx + relVy * bny;
                        if (closing > 0.04f) { isArc = true; }
                        break;
                    }
                }
                if (isArc) {
                    if (volcanism[static_cast<std::size_t>(idx)] == 0) {
                        volcanism[static_cast<std::size_t>(idx)] = 1;
                    }
                }
            }
        }

        // ---- SEISMIC HAZARD ----
        // Same neighbour test for all boundary types. Severe at
        // convergent (subduction earthquakes), high at transform,
        // moderate at passive margins / divergent.
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t idx = row * width + col;
                const uint8_t myPid = grid.plateId(idx);
                if (myPid == 0xFFu || myPid >= mots.size()) { continue; }
                uint8_t worst = 0;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    const uint8_t nPid = grid.plateId(nIdx);
                    if (nPid == 0xFFu || nPid == myPid) { continue; }
                    if (nPid >= mots.size()) { continue; }
                    float bnx = cens[nPid].first  - cens[myPid].first;
                    float bny = cens[nPid].second - cens[myPid].second;
                    if (cylSim) {
                        if (bnx >  0.5f) { bnx -= 1.0f; }
                        if (bnx < -0.5f) { bnx += 1.0f; }
                    }
                    const float bnLen = std::sqrt(bnx * bnx + bny * bny);
                    if (bnLen < 1e-4f) { continue; }
                    bnx /= bnLen; bny /= bnLen;
                    const float relVx = mots[myPid].first
                        - mots[nPid].first;
                    const float relVy = mots[myPid].second
                        - mots[nPid].second;
                    const float normProj = relVx * bnx + relVy * bny;
                    const float tangProj = -relVx * bny + relVy * bnx;
                    const float aN = std::abs(normProj);
                    const float aT = std::abs(tangProj);
                    uint8_t cur = 1; // default moderate near any boundary
                    if (aN > aT && aN > 0.04f) {
                        cur = (normProj > 0.0f) ? 3 : 2;
                    } else if (aT > 0.04f) {
                        cur = 2;
                    }
                    if (cur > worst) { worst = cur; }
                }
                hazard[static_cast<std::size_t>(idx)] = worst;
            }
        }

        // ---- PERMAFROST ----
        // Tundra + Snow land tiles get permafrost = 1 (limits crops,
        // affects unit movement under future systems).
        for (int32_t i = 0; i < totalT; ++i) {
            const aoc::map::TerrainType t = grid.terrain(i);
            if (t == aoc::map::TerrainType::Tundra
                || t == aoc::map::TerrainType::Snow) {
                permafrost[static_cast<std::size_t>(i)] = 1;
            }
        }

        // ---- MOUNTAIN GLACIERS ----
        // Mountain tiles at high latitudes (above 0.55) get Ice feature
        // (alpine glaciation). Real Alps, Andes, Himalaya, Rockies all
        // host glaciers above the snow line.
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            if (lat < 0.55f) { continue; }
            for (int32_t col = 0; col < width; ++col) {
                const int32_t idx = row * width + col;
                if (grid.terrain(idx) == aoc::map::TerrainType::Mountain
                    && grid.feature(idx) == aoc::map::FeatureType::None) {
                    grid.setFeature(idx, aoc::map::FeatureType::Ice);
                }
            }
        }

        // ---- COASTAL UPWELLING ----
        // West-coast continental margins in trade-wind belt (5-30 lat
        // band) drive cold-water upwelling. Real Earth: Peru, Benguela,
        // California, Canary currents — drive ~50 % of global fisheries.
        // Detect: ShallowWater tile with land tile to its EAST in
        // tropics-to-subtropics latitude band.
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            if (lat < 0.10f || lat > 0.60f) { continue; }
            for (int32_t col = 0; col < width; ++col) {
                const int32_t idx = row * width + col;
                if (grid.terrain(idx) != aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                if (lakeFlag[static_cast<std::size_t>(idx)] != 0) { continue; }
                // Check tile to the east is land.
                int32_t ec = col + 1;
                if (cylSim) {
                    if (ec >= width) { ec -= width; }
                } else if (ec >= width) { continue; }
                const int32_t eIdx = row * width + ec;
                const aoc::map::TerrainType te = grid.terrain(eIdx);
                if (te != aoc::map::TerrainType::Ocean
                    && te != aoc::map::TerrainType::ShallowWater) {
                    upwelling[static_cast<std::size_t>(idx)] = 1;
                    // Boost fisheries: re-place FISH if no resource yet.
                    if (!grid.resource(idx).isValid()) {
                        grid.setResource(idx,
                            ResourceId{aoc::sim::goods::FISH});
                    }
                }
            }
        }

        // ---- RIVER DELTAS ----
        // River-mouth land tiles: terrain Plains/Grassland adjacent to
        // water AND with riverEdges set → mark Floodplains feature
        // (high agricultural potential). Models Nile, Mississippi,
        // Ganges-Brahmaputra deltas as the highly fertile deposits at
        // river mouths.
        for (int32_t i = 0; i < totalT; ++i) {
            if (grid.riverEdges(i) == 0) { continue; }
            const aoc::map::TerrainType t = grid.terrain(i);
            if (t != aoc::map::TerrainType::Plains
                && t != aoc::map::TerrainType::Grassland) { continue; }
            const int32_t row = i / width;
            const int32_t col = i % width;
            bool nearWater = false;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nbHelper(col, row, d, nIdx)) { continue; }
                const aoc::map::TerrainType nt = grid.terrain(nIdx);
                if (nt == aoc::map::TerrainType::Ocean
                    || nt == aoc::map::TerrainType::ShallowWater) {
                    nearWater = true; break;
                }
            }
            if (nearWater
                && grid.feature(i) == aoc::map::FeatureType::None) {
                grid.setFeature(i, aoc::map::FeatureType::Floodplains);
            }
        }

        // ---- SALT FLATS / PLAYAS ----
        // Endorheic basin (lake-flagged + arid climate) leaves salt
        // deposits when evaporated. Real: Bonneville, Salar de Uyuni,
        // Atacama, Lop Nor. Place SALT resource on Desert tiles
        // adjacent to lake-flagged water in arid latitudes.
        for (int32_t i = 0; i < totalT; ++i) {
            if (grid.terrain(i) != aoc::map::TerrainType::Desert) { continue; }
            if (grid.resource(i).isValid()) { continue; }
            const int32_t row = i / width;
            const int32_t col = i % width;
            bool nearLake = false;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nbHelper(col, row, d, nIdx)) { continue; }
                if (lakeFlag[static_cast<std::size_t>(nIdx)] != 0) {
                    nearLake = true; break;
                }
            }
            if (nearLake) {
                grid.setResource(i, ResourceId{aoc::sim::goods::SALT});
            }
        }

        // ---- SOIL FERTILITY ----
        // Composite score per tile based on:
        //   • Baseline by terrain type
        //   • Volcanic proximity (+0.30) — Java/Italy fertile volcanic
        //   • Floodplain (+0.30) — Nile/Ganges alluvium
        //   • Old craton chernozem (+0.20) — Ukraine/Pampas soils
        //   • Loess deposits (+0.30) — downwind of polar plate
        //   • Penalties: Jungle laterite (-0.20), Tundra/Snow podzol
        //     (-0.30), Desert (-0.30), Mountain (-0.20)
        for (int32_t i = 0; i < totalT; ++i) {
            const aoc::map::TerrainType t = grid.terrain(i);
            const aoc::map::FeatureType f = grid.feature(i);
            float fert = 0.50f;
            switch (t) {
                case aoc::map::TerrainType::Grassland: fert = 0.65f; break;
                case aoc::map::TerrainType::Plains:    fert = 0.55f; break;
                case aoc::map::TerrainType::Desert:    fert = 0.20f; break;
                case aoc::map::TerrainType::Tundra:    fert = 0.20f; break;
                case aoc::map::TerrainType::Snow:      fert = 0.05f; break;
                case aoc::map::TerrainType::Mountain:  fert = 0.30f; break;
                default: break;
            }
            // Volcanic boost.
            if (volcanism[static_cast<std::size_t>(i)] != 0) {
                fert += 0.30f;
            }
            // Floodplain boost (already includes deltas).
            if (f == aoc::map::FeatureType::Floodplains) {
                fert += 0.30f;
            }
            // Jungle laterite penalty.
            if (f == aoc::map::FeatureType::Jungle) {
                fert -= 0.20f;
            }
            // Marsh — moderately fertile.
            if (f == aoc::map::FeatureType::Marsh) {
                fert += 0.10f;
            }
            // Old craton chernozem (Grassland on old + sed/igneous
            // metamorphic continent).
            const auto& ages = grid.crustAgeTile();
            if (i < static_cast<int32_t>(ages.size())) {
                const float a = ages[static_cast<std::size_t>(i)];
                if (t == aoc::map::TerrainType::Grassland && a > 80.0f) {
                    fert += 0.20f;
                }
            }
            // Loess: tiles in temperate band (lat 0.30-0.55) on
            // continents adjacent to colder polar zones get wind-blown
            // glacial silt → highest natural fertility.
            const int32_t row = i / width;
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            if (lat > 0.40f && lat < 0.65f
                && (t == aoc::map::TerrainType::Plains
                    || t == aoc::map::TerrainType::Grassland)) {
                fert += 0.15f; // mild loess deposit
            }
            // Permafrost penalty.
            if (permafrost[static_cast<std::size_t>(i)] != 0) {
                fert *= 0.4f;
            }
            soilFert[static_cast<std::size_t>(i)] =
                std::clamp(fert, 0.0f, 1.0f);
        }

        // ---- HOT SPRINGS / GEOTHERMAL ----
        // Convergent + hotspot tiles already host volcanism. Add the
        // sub-classification 5 = "geothermal field" for tiles where
        // volcanism is active AND adjacent to water (hot-spring/geyser
        // setting like Iceland, Yellowstone, Kamchatka, NZ).
        for (int32_t i = 0; i < totalT; ++i) {
            if (volcanism[static_cast<std::size_t>(i)] == 0) { continue; }
            const int32_t row = i / width;
            const int32_t col = i % width;
            bool nearWater = false;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nbHelper(col, row, d, nIdx)) { continue; }
                const aoc::map::TerrainType nt = grid.terrain(nIdx);
                if (nt == aoc::map::TerrainType::Ocean
                    || nt == aoc::map::TerrainType::ShallowWater) {
                    nearWater = true; break;
                }
            }
            if (nearWater
                && volcanism[static_cast<std::size_t>(i)] != 5) {
                volcanism[static_cast<std::size_t>(i)] = 5; // geothermal
            }
        }

        // ---- KARST topography ----
        // Limestone-dissolution landscape: Hills feature on Sedimentary
        // (rockType=0) old (crustAge>50) tiles in temperate-tropical
        // moist climates. Real Earth: Guilin/Yangshuo, Dinaric Alps,
        // Nullarbor, Florida sinkholes. Mark via rockType=5 marker
        // (extension; renderer can colour cyan).
        const auto& rockTypeNow = grid.rockType();
        if (!rockTypeNow.empty()) {
            std::vector<uint8_t> rockUpd(rockTypeNow.begin(),
                                         rockTypeNow.end());
            const auto& ages2 = grid.crustAgeTile();
            for (int32_t i = 0; i < totalT; ++i) {
                if (grid.feature(i) != aoc::map::FeatureType::Hills) {
                    continue;
                }
                if (rockUpd[static_cast<std::size_t>(i)] != 0) { continue; }
                if (i < static_cast<int32_t>(ages2.size())
                    && ages2[static_cast<std::size_t>(i)] > 50.0f) {
                    const int32_t row = i / width;
                    const float ny = static_cast<float>(row)
                                   / static_cast<float>(height);
                    const float lat = 2.0f * std::abs(ny - 0.5f);
                    if (lat < 0.55f) { // not too cold
                        rockUpd[static_cast<std::size_t>(i)] = 5; // karst
                    }
                }
            }
            grid.setRockType(std::move(rockUpd));
        }

        // ---- INSELBERGS ----
        // Lone resistant peaks left after surrounding terrain eroded
        // (Uluru, Sugar Loaf, Stone Mountain). Detect: Mountain tile
        // whose 6 neighbours are all flat (Plains/Desert/Grassland).
        for (int32_t i = 0; i < totalT; ++i) {
            if (grid.terrain(i) != aoc::map::TerrainType::Mountain) {
                continue;
            }
            const int32_t row = i / width;
            const int32_t col = i % width;
            int32_t flatNb = 0;
            int32_t totalNb = 0;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nbHelper(col, row, d, nIdx)) { continue; }
                ++totalNb;
                const aoc::map::TerrainType nt = grid.terrain(nIdx);
                if (nt == aoc::map::TerrainType::Plains
                    || nt == aoc::map::TerrainType::Desert
                    || nt == aoc::map::TerrainType::Grassland) {
                    ++flatNb;
                }
            }
            // All flat neighbours → inselberg. Mark via volcanism=6
            // tag (overlay can render distinctly). Doesn't trigger
            // volcanism mechanics; just a feature flag.
            if (totalNb >= 5 && flatNb >= totalNb - 1) {
                volcanism[static_cast<std::size_t>(i)] = 6; // inselberg
            }
        }

        // ---- SAND DUNES ----
        // Desert tiles in trade-wind belt (lat 0.10-0.40) with prevailing
        // east-west wind blowing across long inland fetch get aeolian
        // dune fields (Sahara, Arabian, Australian, Gobi). Mark with
        // volcanism=7 (visual flag only).
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            if (lat < 0.10f || lat > 0.40f) { continue; }
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                if (grid.terrain(i) != aoc::map::TerrainType::Desert) {
                    continue;
                }
                if (volcanism[static_cast<std::size_t>(i)] == 0) {
                    volcanism[static_cast<std::size_t>(i)] = 7; // dunes
                }
            }
        }

        // ---- TSUNAMI ZONES ----
        // Coastal tiles facing a convergent boundary at sea (subduction
        // trench within ~6 hexes) are tsunami-prone. Bit-set hazard
        // value's 4-bit so existing seismic hazard 0-3 is preserved.
        // Parallel: each iteration only OR-bits hazard[i].
        AOC_PARALLEL_FOR_ROWS
        for (int32_t i = 0; i < totalT; ++i) {
            const aoc::map::TerrainType t = grid.terrain(i);
            if (t == aoc::map::TerrainType::Ocean
                || t == aoc::map::TerrainType::ShallowWater) {
                continue;
            }
            const int32_t row = i / width;
            const int32_t col = i % width;
            // Find any sea tile within 4 hexes that has hazard >= 3.
            bool tsunami = false;
            for (int32_t dr = -4; dr <= 4 && !tsunami; ++dr) {
                const int32_t rr = row + dr;
                if (rr < 0 || rr >= height) { continue; }
                for (int32_t dc = -4; dc <= 4 && !tsunami; ++dc) {
                    int32_t cc = col + dc;
                    if (cylSim) {
                        if (cc < 0)        { cc += width; }
                        if (cc >= width)   { cc -= width; }
                    } else if (cc < 0 || cc >= width) { continue; }
                    const int32_t nIdx = rr * width + cc;
                    const aoc::map::TerrainType nt = grid.terrain(nIdx);
                    if (nt != aoc::map::TerrainType::Ocean
                        && nt != aoc::map::TerrainType::ShallowWater) {
                        continue;
                    }
                    if (hazard[static_cast<std::size_t>(nIdx)] >= 3) {
                        tsunami = true;
                    }
                }
            }
            if (tsunami) {
                hazard[static_cast<std::size_t>(i)] |= 0x08; // bit 3 = tsunami
            }
        }

        // ---- SEA ICE ----
        // ShallowWater + Ocean tiles at very high latitude (>0.85) get
        // Ice feature representing winter sea ice cover (Arctic Ocean).
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            if (lat < 0.85f) { continue; }
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                if (t != aoc::map::TerrainType::Ocean
                    && t != aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                if (grid.feature(i) == aoc::map::FeatureType::None) {
                    grid.setFeature(i, aoc::map::FeatureType::Ice);
                }
            }
        }

        // ---- FJORDS ----
        // Glaciated coastal valleys: ShallowWater tile adjacent to a
        // Mountain tile in cold latitude (>0.55). Mark with upwelling=2
        // tag for fjord (visual + game: deep protected harbour).
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            if (lat < 0.55f) { continue; }
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                if (grid.terrain(i) != aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                bool nearMtn = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    if (grid.terrain(nIdx)
                            == aoc::map::TerrainType::Mountain) {
                        nearMtn = true; break;
                    }
                }
                if (nearMtn) {
                    upwelling[static_cast<std::size_t>(i)] = 2; // fjord
                }
            }
        }

        // ---- TREELINE ----
        // High-elevation tiles above the alpine tree line lose Forest
        // / Jungle features. In cold latitudes the treeline is at
        // sea level (no trees on Tundra). In tropical, treeline is
        // near peak elevation — Mountain still won't host forest.
        // Currently Forest can spawn on Mountain tiles inappropriately;
        // strip it.
        for (int32_t i = 0; i < totalT; ++i) {
            const aoc::map::TerrainType t = grid.terrain(i);
            const aoc::map::FeatureType f = grid.feature(i);
            if (t != aoc::map::TerrainType::Mountain) { continue; }
            if (f == aoc::map::FeatureType::Forest
                || f == aoc::map::FeatureType::Jungle) {
                // Replace with Ice (alpine) if cold lat, else None.
                const int32_t row = i / width;
                const float ny = static_cast<float>(row)
                               / static_cast<float>(height);
                const float lat = 2.0f * std::abs(ny - 0.5f);
                if (lat > 0.50f
                    && grid.feature(i) != aoc::map::FeatureType::Ice) {
                    grid.setFeature(i, aoc::map::FeatureType::Ice);
                } else {
                    grid.setFeature(i, aoc::map::FeatureType::None);
                }
            }
        }

        // ---- WETLANDS ----
        // Low-elevation river-adjacent tiles with high moisture.
        // Existing Floodplains pass marks river deltas; this adds
        // Marsh on river-adjacent flat tiles in cool/temperate humid
        // bands (Pripyat, Pantanal, Sudd).
        for (int32_t i = 0; i < totalT; ++i) {
            if (grid.feature(i) != aoc::map::FeatureType::None) { continue; }
            const aoc::map::TerrainType t = grid.terrain(i);
            if (t != aoc::map::TerrainType::Plains
                && t != aoc::map::TerrainType::Grassland) { continue; }
            // River-adjacent (own tile or neighbour has river edges).
            if (grid.riverEdges(i) == 0) { continue; }
            const int32_t row = i / width;
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            // Cool-temperate band 0.40-0.70 lat → marsh. Hot bands
            // already have Floodplains (delta) which dominates.
            if (lat > 0.40f && lat < 0.70f) {
                // Random chance handled by sediment-depth — only thick
                // sediment basins host marshes.
                if (i < static_cast<int32_t>(sediment.size())
                    && sediment[static_cast<std::size_t>(i)] > 0.05f) {
                    grid.setFeature(i, aoc::map::FeatureType::Marsh);
                }
            }
        }

        // ---- BIOGEOGRAPHIC REALMS ----
        // Plates that never merged AND have aged sufficiently are
        // biogeographically isolated. Australia (oldest continuously
        // isolated continent), Madagascar (split from Africa ~165 Mya),
        // Antarctica (isolated since Drake Passage opened ~30 Mya).
        // Real Earth: such realms develop unique endemic species
        // (marsupials, lemurs, etc).
        std::vector<uint8_t> isoRealm(static_cast<std::size_t>(totalT), 0);
        for (int32_t i = 0; i < totalT; ++i) {
            const aoc::map::TerrainType t = grid.terrain(i);
            if (t == aoc::map::TerrainType::Ocean
                || t == aoc::map::TerrainType::ShallowWater) {
                continue;
            }
            const uint8_t pid = grid.plateId(i);
            if (pid == 0xFFu || pid >= plates.size()) { continue; }
            const Plate& p = plates[pid];
            if (p.mergesAbsorbed == 0
                && p.crustAge > 60.0f
                && p.landFraction > 0.40f) {
                isoRealm[static_cast<std::size_t>(i)] = 1;
            }
        }
        grid.setIsolatedRealm(std::move(isoRealm));

        // ---- LAND BRIDGES ----
        // Shallow water tiles that sit between two large continental
        // landmasses (potential Pleistocene-low-sea-level land bridge).
        // Detect: ShallowWater tile with >= 2 of 6 neighbours being
        // land of DIFFERENT plate ids on opposing sides.
        std::vector<uint8_t> bridges(static_cast<std::size_t>(totalT), 0);
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                if (grid.terrain(i)
                        != aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                if (lakeFlag[static_cast<std::size_t>(i)] != 0) { continue; }
                std::array<uint8_t, 6> nbPids{};
                std::array<bool, 6>    nbLand{};
                int32_t landCount = 0;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    nbPids[static_cast<std::size_t>(d)] = 0xFFu;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    const aoc::map::TerrainType nt = grid.terrain(nIdx);
                    if (nt != aoc::map::TerrainType::Ocean
                        && nt != aoc::map::TerrainType::ShallowWater) {
                        nbLand[static_cast<std::size_t>(d)] = true;
                        nbPids[static_cast<std::size_t>(d)] =
                            grid.plateId(nIdx);
                        ++landCount;
                    }
                }
                if (landCount < 2) { continue; }
                // Need DIFFERENT plate ids in the land neighbours
                // (= bridging two distinct continental masses).
                bool different = false;
                uint8_t firstPid = 0xFFu;
                for (int32_t d = 0; d < 6; ++d) {
                    if (!nbLand[static_cast<std::size_t>(d)]) { continue; }
                    const uint8_t pid = nbPids[static_cast<std::size_t>(d)];
                    if (pid == 0xFFu) { continue; }
                    if (firstPid == 0xFFu) { firstPid = pid; }
                    else if (pid != firstPid) { different = true; break; }
                }
                if (different) {
                    bridges[static_cast<std::size_t>(i)] = 1;
                }
            }
        }
        grid.setLandBridge(std::move(bridges));

        // ---- REFUGIA ----
        // Mid-latitude tiles (lat 0.30-0.60) with high soil fertility
        // AND nearby Mountain (within 4 hexes) — these are species
        // refuges that survived ice ages because mountains shelter
        // microclimates. Iberian, Balkan, Caucasian refugia.
        std::vector<uint8_t> refugia(static_cast<std::size_t>(totalT), 0);
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row)
                           / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            if (lat < 0.30f || lat > 0.60f) { continue; }
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                if (t != aoc::map::TerrainType::Plains
                    && t != aoc::map::TerrainType::Grassland) { continue; }
                if (i >= static_cast<int32_t>(soilFert.size())) { continue; }
                if (soilFert[static_cast<std::size_t>(i)] < 0.55f) { continue; }
                // Walk up to 4 hexes in each dir for a Mountain.
                bool nearMtn = false;
                for (int32_t d = 0; d < 6 && !nearMtn; ++d) {
                    int32_t cc = col, rr = row;
                    for (int32_t step = 0; step < 4 && !nearMtn; ++step) {
                        int32_t nIdx;
                        if (!nbHelper(cc, rr, d, nIdx)) { break; }
                        cc = nIdx % width;
                        rr = nIdx / width;
                        if (grid.terrain(nIdx)
                                == aoc::map::TerrainType::Mountain) {
                            nearMtn = true;
                        }
                    }
                }
                if (nearMtn) {
                    refugia[static_cast<std::size_t>(i)] = 1;
                    // Refugia get fertility boost (microclimate bonus).
                    soilFert[static_cast<std::size_t>(i)] = std::min(
                        1.0f,
                        soilFert[static_cast<std::size_t>(i)] + 0.10f);
                }
            }
        }
        grid.setRefugium(std::move(refugia));

        // ---- METAMORPHIC CORE COMPLEX ----
        // After collision + slab break-off, the suture zone hosts
        // exposed deep crust where extensional detachment unroofed
        // mid-crustal rocks. Currently ophiolite tiles mark the seam;
        // mark a wider shoulder of suture-adjacent tiles as
        // metamorphic rock type (drives mineral/gemstone potential).
        // Reuses existing rockType field; sutures already get
        // rockType=3 (ophiolite). Mark shoulder tiles within 2 hexes
        // of ophiolite as rockType=2 (regional metamorphic) if they
        // were sedimentary.
        const auto& rockNow = grid.rockType();
        if (!rockNow.empty()) {
            std::vector<uint8_t> rockUpd2(rockNow.begin(), rockNow.end());
            for (int32_t row = 0; row < height; ++row) {
                for (int32_t col = 0; col < width; ++col) {
                    const int32_t i = row * width + col;
                    if (rockUpd2[static_cast<std::size_t>(i)] != 0) { continue; }
                    bool nearOphiolite = false;
                    for (int32_t d = 0; d < 6 && !nearOphiolite; ++d) {
                        int32_t cc = col, rr = row;
                        for (int32_t step = 0; step < 2 && !nearOphiolite; ++step) {
                            int32_t nIdx;
                            if (!nbHelper(cc, rr, d, nIdx)) { break; }
                            cc = nIdx % width;
                            rr = nIdx / width;
                            if (rockUpd2[static_cast<std::size_t>(nIdx)] == 3) {
                                nearOphiolite = true;
                            }
                        }
                    }
                    if (nearOphiolite) {
                        rockUpd2[static_cast<std::size_t>(i)] = 2; // metamorphic
                    }
                }
            }
            grid.setRockType(std::move(rockUpd2));
        }

        // ============================================================
        // SESSION 3 — atmospheric hazards / glacial features /
        // ocean zones / cloud cover / drainage flow direction.
        // ============================================================
        std::vector<uint8_t> climateHazard(static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> glacialFeat  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> oceanZone    (static_cast<std::size_t>(totalT), 0);
        std::vector<float>   cloudCover   (static_cast<std::size_t>(totalT), 0.0f);
        std::vector<uint8_t> flowDir      (static_cast<std::size_t>(totalT), 0xFFu);

        // ---- HURRICANE BELT ----
        // Tropical warm ocean, lat 0.10-0.40, away from equator (>0.10).
        // Real Earth: hurricanes form over 26 °C+ ocean water in
        // tropical-subtropical band, not directly on equator (Coriolis
        // force needed).
        // ---- TORNADO ALLEY ----
        // Continental temperate Plains/Grassland adjacent to a warm
        // ocean (gulf-style) AND a mountain range (rocky barrier
        // forces cold-warm air collision). Real Earth: US Great Plains
        // east of Rockies, parts of Argentina (Pampas), Bangladesh.
        // ---- STORM TRACK ----
        // Mid-latitude westerly band 0.40-0.65 over ocean.
        // ---- JET STREAM ----
        // Polar-front jet at 0.55-0.75 lat band.
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row)
                           / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                const bool isWaterT = (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater);
                uint8_t flag = 0;
                if (isWaterT && lat >= 0.10f && lat <= 0.40f) {
                    flag |= 0x01; // hurricane
                }
                if (!isWaterT
                    && lat >= 0.30f && lat <= 0.55f
                    && (t == aoc::map::TerrainType::Plains
                        || t == aoc::map::TerrainType::Grassland)) {
                    // Need warm-ocean adjacency within ~6 hex AND
                    // mountain within ~6 hex. Cheap test: scan straight.
                    bool warmOcean = false;
                    bool nearMtn   = false;
                    for (int32_t dr = -6; dr <= 6; ++dr) {
                        const int32_t rr = row + dr;
                        if (rr < 0 || rr >= height) { continue; }
                        for (int32_t dc = -6; dc <= 6; ++dc) {
                            int32_t cc = col + dc;
                            if (cylSim) {
                                if (cc < 0)        { cc += width; }
                                if (cc >= width)   { cc -= width; }
                            } else if (cc < 0 || cc >= width) { continue; }
                            const int32_t nIdx = rr * width + cc;
                            const aoc::map::TerrainType nt = grid.terrain(nIdx);
                            const float nny = static_cast<float>(rr)
                                / static_cast<float>(height);
                            const float nlat = 2.0f * std::abs(nny - 0.5f);
                            if ((nt == aoc::map::TerrainType::Ocean
                                || nt == aoc::map::TerrainType::ShallowWater)
                                && nlat < 0.40f) {
                                warmOcean = true;
                            }
                            if (nt == aoc::map::TerrainType::Mountain) {
                                nearMtn = true;
                            }
                        }
                    }
                    if (warmOcean && nearMtn) {
                        flag |= 0x02; // tornado
                    }
                }
                if (isWaterT && lat >= 0.40f && lat <= 0.65f) {
                    flag |= 0x04; // storm track
                }
                if (lat >= 0.55f && lat <= 0.75f) {
                    flag |= 0x08; // jet stream zone
                }
                climateHazard[static_cast<std::size_t>(i)] = flag;
            }
        }

        // ---- GLACIAL FEATURES ----
        // Tiles formerly glaciated (high-lat continental that became
        // ice-cap during the Pleistocene maximum). Mark moraines along
        // ice-edge band (lat 0.55-0.70 land), drumlins/eskers in
        // sediment-rich glacial paths, U-valleys in mountain-river
        // intersections in cold zones, caves on karst.
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row)
                           / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                const aoc::map::FeatureType f = grid.feature(i);
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                // Caves on karst (rockType=5)
                const auto& rt = grid.rockType();
                if (!rt.empty()
                    && rt[static_cast<std::size_t>(i)] == 5
                    && (i % 17) == 0) {
                    glacialFeat[static_cast<std::size_t>(i)] = 3; // cave
                    continue;
                }
                // U-valley: Mountain-adjacent river tile in cold zone
                if (lat > 0.50f
                    && grid.riverEdges(i) != 0) {
                    bool nearMtn = false;
                    for (int32_t d = 0; d < 6; ++d) {
                        int32_t nIdx;
                        if (!nbHelper(col, row, d, nIdx)) { continue; }
                        if (grid.terrain(nIdx)
                                == aoc::map::TerrainType::Mountain) {
                            nearMtn = true; break;
                        }
                    }
                    if (nearMtn) {
                        glacialFeat[static_cast<std::size_t>(i)] = 2;
                        continue;
                    }
                }
                // Moraine: lat 0.55-0.70 + sediment > 0.04 + Plains
                if (lat >= 0.55f && lat <= 0.70f
                    && i < static_cast<int32_t>(sediment.size())
                    && sediment[static_cast<std::size_t>(i)] > 0.04f
                    && (t == aoc::map::TerrainType::Plains
                        || t == aoc::map::TerrainType::Grassland)) {
                    glacialFeat[static_cast<std::size_t>(i)] = 1; // moraine
                    continue;
                }
                // Drumlin field: similar lat band + Hills feature
                if (lat >= 0.55f && lat <= 0.70f
                    && f == aoc::map::FeatureType::Hills) {
                    glacialFeat[static_cast<std::size_t>(i)] = 4; // drumlin
                    continue;
                }
                // Esker: river tile in formerly glaciated band
                if (lat >= 0.55f && lat <= 0.70f
                    && grid.riverEdges(i) != 0) {
                    glacialFeat[static_cast<std::size_t>(i)] = 5; // esker
                }
            }
        }

        // ---- OCEAN ZONES (tidal range + salinity) ----
        // Tidal range: macro at narrow embayments (Bay of Fundy 16 m,
        // Bristol Channel 14 m). Mega ocean has micro tides. Salinity:
        // restricted basins hypersaline (Mediterranean), polar fresher
        // (sea ice melt), open ocean normal. Parallel.
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row)
                           / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                if (t != aoc::map::TerrainType::Ocean
                    && t != aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                // Count surrounding land tiles up to 3 hexes — many
                // = restricted basin (high tide, hypersaline).
                int32_t landNb = 0;
                for (int32_t dr = -3; dr <= 3; ++dr) {
                    const int32_t rr = row + dr;
                    if (rr < 0 || rr >= height) { continue; }
                    for (int32_t dc = -3; dc <= 3; ++dc) {
                        int32_t cc = col + dc;
                        if (cylSim) {
                            if (cc < 0)        { cc += width; }
                            if (cc >= width)   { cc -= width; }
                        } else if (cc < 0 || cc >= width) { continue; }
                        const aoc::map::TerrainType nt =
                            grid.terrain(rr * width + cc);
                        if (nt != aoc::map::TerrainType::Ocean
                            && nt != aoc::map::TerrainType::ShallowWater) {
                            ++landNb;
                        }
                    }
                }
                // Tidal range bin: more land = more constricted = larger
                // tidal range. Bins 0-3.
                uint8_t tidal = 0;
                if (landNb > 30)      { tidal = 3; } // mega
                else if (landNb > 18) { tidal = 2; } // macro
                else if (landNb > 6)  { tidal = 1; } // meso
                // Salinity bin: 0=brackish (estuary/lake), 1=normal,
                // 2=hypersaline (restricted), 3=fresh (polar/lake)
                uint8_t salin = 1;
                if (lakeFlag[static_cast<std::size_t>(i)] != 0) {
                    salin = 0; // brackish if endorheic, else fresh
                    // Endorheic + arid → hypersaline (Dead Sea, Salt
                    // Lake). Approx via sediment thickness adjacency.
                    if (lat < 0.40f) { salin = 2; }
                    else             { salin = 3; }
                } else if (landNb > 24) {
                    salin = 2; // restricted basin → hypersaline
                } else if (lat > 0.80f) {
                    salin = 3; // polar fresh from ice melt
                }
                oceanZone[static_cast<std::size_t>(i)] =
                    static_cast<uint8_t>(
                        (tidal & 0x03) | ((salin & 0x03) << 2));
            }
        }

        // ---- CLOUD COVER ----
        // Proxy: high moisture + warm temperature → high cloud. Land
        // tiles with high windward-side moisture get cloudy. We don't
        // have temperature stored per tile post-biome assignment so
        // proxy via terrain class.
        for (int32_t i = 0; i < totalT; ++i) {
            const aoc::map::TerrainType t = grid.terrain(i);
            float c = 0.30f;
            switch (t) {
                case aoc::map::TerrainType::Grassland: c = 0.55f; break;
                case aoc::map::TerrainType::Plains:    c = 0.40f; break;
                case aoc::map::TerrainType::Desert:    c = 0.10f; break;
                case aoc::map::TerrainType::Tundra:    c = 0.50f; break;
                case aoc::map::TerrainType::Snow:      c = 0.70f; break;
                case aoc::map::TerrainType::Mountain:  c = 0.55f; break;
                default: break;
            }
            if (grid.feature(i) == aoc::map::FeatureType::Jungle) {
                c = std::min(1.0f, c + 0.30f);
            } else if (grid.feature(i) == aoc::map::FeatureType::Forest) {
                c += 0.10f;
            }
            // Hurricane belt is high cloud.
            if ((climateHazard[static_cast<std::size_t>(i)] & 0x01) != 0) {
                c = std::min(1.0f, c + 0.20f);
            }
            cloudCover[static_cast<std::size_t>(i)]
                = std::clamp(c, 0.0f, 1.0f);
        }

        // ---- DRAINAGE FLOW DIRECTION ----
        // For each land tile, compute the steepest-downhill neighbour.
        // 0xFF means sink (no lower neighbour, endorheic basin).
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                const int8_t myE = grid.elevation(i);
                int8_t lowest = myE;
                uint8_t bestDir = 0xFFu;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    const int8_t nE = grid.elevation(nIdx);
                    if (nE < lowest) {
                        lowest = nE;
                        bestDir = static_cast<uint8_t>(d);
                    }
                }
                flowDir[static_cast<std::size_t>(i)] = bestDir;
            }
        }

        // ============================================================
        // SESSION 4 — natural hazards / biome subtypes / marine zones /
        // wildlife / disease / energy potentials / atmospheric extras /
        // hydrological extras / event markers.
        // ============================================================
        std::vector<uint16_t> natHazard (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t>  bSub      (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t>  marineD   (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t>  wildlife  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t>  disease   (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t>  windE     (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t>  solarE    (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t>  hydroE    (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t>  geoE      (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t>  tidalE    (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t>  waveE     (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t>  atmExtras (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t>  hydExtras (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t>  eventMrk  (static_cast<std::size_t>(totalT), 0);

        // ---- WP1: NATURAL HAZARDS ----
        // Parallel: each iteration writes only natHazard[i].
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                const aoc::map::FeatureType f = grid.feature(i);
                if (t == aoc::map::TerrainType::Ocean) { continue; }
                uint16_t haz = 0;
                // Wildfire: dry biome + flammable feature.
                if ((t == aoc::map::TerrainType::Plains
                     || t == aoc::map::TerrainType::Grassland
                     || t == aoc::map::TerrainType::Desert)
                    && (f == aoc::map::FeatureType::Forest
                        || f == aoc::map::FeatureType::Jungle
                        || t == aoc::map::TerrainType::Plains)) {
                    if (lat > 0.10f && lat < 0.55f) { haz |= 0x0001; }
                }
                // Flood: low elev + river-adjacent + wet biome.
                const int8_t elev = grid.elevation(i);
                if (elev <= 0
                    && grid.riverEdges(i) != 0
                    && (t == aoc::map::TerrainType::Grassland
                        || f == aoc::map::FeatureType::Floodplains
                        || f == aoc::map::FeatureType::Marsh)) {
                    haz |= 0x0002;
                }
                // Drought: arid + interior.
                if (t == aoc::map::TerrainType::Desert
                    || (t == aoc::map::TerrainType::Plains && lat < 0.30f)) {
                    haz |= 0x0004;
                }
                // Avalanche: Mountain at high lat or high elev.
                if (t == aoc::map::TerrainType::Mountain && lat > 0.40f) {
                    haz |= 0x0008;
                }
                // Landslide: Hills + sediment + steep neighbour mountain.
                if (f == aoc::map::FeatureType::Hills
                    && i < static_cast<int32_t>(sediment.size())
                    && sediment[static_cast<std::size_t>(i)] > 0.02f) {
                    bool nearMtn = false;
                    for (int32_t d = 0; d < 6; ++d) {
                        int32_t nIdx;
                        if (!nbHelper(col, row, d, nIdx)) { continue; }
                        if (grid.terrain(nIdx)
                                == aoc::map::TerrainType::Mountain) {
                            nearMtn = true; break;
                        }
                    }
                    if (nearMtn) { haz |= 0x0010; }
                }
                // Ash fall: within 6 hexes of arc/LIP volcanism.
                bool nearVolc = false;
                for (int32_t dr = -6; dr <= 6 && !nearVolc; ++dr) {
                    const int32_t rr = row + dr;
                    if (rr < 0 || rr >= height) { continue; }
                    for (int32_t dc = -6; dc <= 6 && !nearVolc; ++dc) {
                        int32_t cc = col + dc;
                        if (cylSim) {
                            if (cc < 0)        { cc += width; }
                            if (cc >= width)   { cc -= width; }
                        } else if (cc < 0 || cc >= width) { continue; }
                        const int32_t nIdx = rr * width + cc;
                        const uint8_t v =
                            volcanism[static_cast<std::size_t>(nIdx)];
                        if (v == 1 || v == 3) { nearVolc = true; }
                    }
                }
                if (nearVolc) { haz |= 0x0020; }
                // Lahar: glacier (Mountain + Ice) + adjacent river valley.
                if (t == aoc::map::TerrainType::Mountain
                    && f == aoc::map::FeatureType::Ice) {
                    for (int32_t d = 0; d < 6; ++d) {
                        int32_t nIdx;
                        if (!nbHelper(col, row, d, nIdx)) { continue; }
                        if (grid.riverEdges(nIdx) != 0) {
                            haz |= 0x0040; break;
                        }
                    }
                }
                // Sinkhole: karst tile (rockType=5 set earlier).
                const auto& rk = grid.rockType();
                if (!rk.empty()
                    && i < static_cast<int32_t>(rk.size())
                    && rk[static_cast<std::size_t>(i)] == 5) {
                    haz |= 0x0080;
                }
                // Storm surge: coastal land in hurricane belt.
                if ((climateHazard[static_cast<std::size_t>(i)] & 0x01) != 0) {
                    bool nearWater = false;
                    for (int32_t d = 0; d < 6; ++d) {
                        int32_t nIdx;
                        if (!nbHelper(col, row, d, nIdx)) { continue; }
                        const aoc::map::TerrainType nt = grid.terrain(nIdx);
                        if (nt == aoc::map::TerrainType::Ocean
                            || nt == aoc::map::TerrainType::ShallowWater) {
                            nearWater = true; break;
                        }
                    }
                    if (nearWater) { haz |= 0x0100; }
                }
                // Dust storm: desert + windy band (lat 0.10-0.40).
                if (t == aoc::map::TerrainType::Desert
                    && lat > 0.10f && lat < 0.40f) {
                    haz |= 0x0200;
                }
                natHazard[static_cast<std::size_t>(i)] = haz;
            }
        }

        // ---- WP2: BIOME SUBTYPES ----
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                const aoc::map::FeatureType f = grid.feature(i);
                uint8_t sub = 0;
                // Mediterranean (Cs): subtropical west coast, Plains.
                if (lat > 0.30f && lat < 0.45f
                    && t == aoc::map::TerrainType::Plains) {
                    bool westCoast = false;
                    for (int32_t d = 0; d < 6; ++d) {
                        int32_t nIdx;
                        if (!nbHelper(col, row, d, nIdx)) { continue; }
                        const aoc::map::TerrainType nt = grid.terrain(nIdx);
                        if ((nt == aoc::map::TerrainType::Ocean
                             || nt == aoc::map::TerrainType::ShallowWater)
                            && (nIdx % width) < (col % width)) {
                            westCoast = true; break;
                        }
                    }
                    if (westCoast) { sub = 1; }
                }
                // Cloud forest: tropical + Hills + high moisture (Jungle).
                if (lat < 0.20f
                    && f == aoc::map::FeatureType::Hills
                    && (grid.terrain(i) == aoc::map::TerrainType::Grassland)) {
                    sub = 2;
                }
                // Temperate rainforest: cool + west coast + Forest feature.
                if (lat > 0.40f && lat < 0.70f
                    && f == aoc::map::FeatureType::Forest) {
                    bool westCoast = false;
                    for (int32_t d = 0; d < 6; ++d) {
                        int32_t nIdx;
                        if (!nbHelper(col, row, d, nIdx)) { continue; }
                        const aoc::map::TerrainType nt = grid.terrain(nIdx);
                        if ((nt == aoc::map::TerrainType::Ocean
                             || nt == aoc::map::TerrainType::ShallowWater)
                            && (nIdx % width) < (col % width)) {
                            westCoast = true; break;
                        }
                    }
                    if (westCoast) { sub = 3; }
                }
                // Mangrove: tropical (lat<0.20) coastal Marsh / Floodplains.
                if (lat < 0.25f
                    && (f == aoc::map::FeatureType::Marsh
                        || f == aoc::map::FeatureType::Floodplains)) {
                    bool nearWater = false;
                    for (int32_t d = 0; d < 6; ++d) {
                        int32_t nIdx;
                        if (!nbHelper(col, row, d, nIdx)) { continue; }
                        const aoc::map::TerrainType nt = grid.terrain(nIdx);
                        if (nt == aoc::map::TerrainType::Ocean
                            || nt == aoc::map::TerrainType::ShallowWater) {
                            nearWater = true; break;
                        }
                    }
                    if (nearWater) { sub = 4; }
                }
                // Taiga: boreal Forest (lat 0.55-0.75 + Forest).
                if (lat >= 0.55f && lat <= 0.78f
                    && f == aoc::map::FeatureType::Forest) {
                    sub = 5;
                }
                // Alpine tundra: high-lat Mountain.
                if (t == aoc::map::TerrainType::Tundra
                    && grid.elevation(i) >= 1) {
                    sub = 6;
                }
                // Polar desert: very-high-lat Snow.
                if (t == aoc::map::TerrainType::Snow && lat > 0.85f) {
                    sub = 7;
                }
                // Cold desert: Desert at high lat (Patagonia, Gobi).
                if (t == aoc::map::TerrainType::Desert && lat > 0.45f) {
                    sub = 8;
                }
                // Steppe: dry Plains (lat 0.30-0.60, no special features).
                if (t == aoc::map::TerrainType::Plains
                    && lat > 0.30f && lat < 0.60f
                    && f == aoc::map::FeatureType::None) {
                    sub = 9;
                }
                // Prairie: temperate Grassland (lat 0.35-0.55).
                if (t == aoc::map::TerrainType::Grassland
                    && lat > 0.35f && lat < 0.55f
                    && f == aoc::map::FeatureType::None) {
                    sub = 10;
                }
                bSub[static_cast<std::size_t>(i)] = sub;
            }
        }

        // ---- WP3: MARINE DEPTH ZONATION ----
        // Map ocean tiles to shelf/slope/rise/abyssal/trench using
        // distance-from-coast as a depth proxy.
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                if (t != aoc::map::TerrainType::Ocean
                    && t != aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                if (lakeFlag[static_cast<std::size_t>(i)] != 0) { continue; }
                // Distance to nearest land in steps (cap at 8).
                int32_t dist = 99;
                for (int32_t r = 1; r <= 8 && dist == 99; ++r) {
                    for (int32_t dr = -r; dr <= r && dist == 99; ++dr) {
                        const int32_t rr = row + dr;
                        if (rr < 0 || rr >= height) { continue; }
                        for (int32_t dc = -r; dc <= r && dist == 99; ++dc) {
                            if (std::abs(dc) != r && std::abs(dr) != r) {
                                continue; // ring at radius r
                            }
                            int32_t cc = col + dc;
                            if (cylSim) {
                                if (cc < 0)        { cc += width; }
                                if (cc >= width)   { cc -= width; }
                            } else if (cc < 0 || cc >= width) { continue; }
                            const aoc::map::TerrainType nt =
                                grid.terrain(rr * width + cc);
                            if (nt != aoc::map::TerrainType::Ocean
                                && nt != aoc::map::TerrainType::ShallowWater) {
                                dist = r;
                            }
                        }
                    }
                }
                uint8_t zone = 4; // abyssal default
                if (dist <= 1)      { zone = 1; } // shelf
                else if (dist <= 3) { zone = 2; } // slope
                else if (dist <= 5) { zone = 3; } // rise
                // Trench: tile has high seismic hazard at convergent.
                if (hazard[static_cast<std::size_t>(i)] >= 3) { zone = 5; }
                marineD[static_cast<std::size_t>(i)] = zone;
            }
        }
        // Atoll (subtype 11): hotspot volcanism on submerged tile.
        for (int32_t i = 0; i < totalT; ++i) {
            if (volcanism[static_cast<std::size_t>(i)] == 2
                && (grid.terrain(i) == aoc::map::TerrainType::ShallowWater
                    || grid.terrain(i) == aoc::map::TerrainType::Ocean)) {
                bSub[static_cast<std::size_t>(i)] = 11; // atoll
            }
        }
        // Kelp forest (subtype 12): cool ShallowWater west coasts.
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            if (lat < 0.40f || lat > 0.65f) { continue; }
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                if (grid.terrain(i) != aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                if (upwelling[static_cast<std::size_t>(i)] == 1) {
                    bSub[static_cast<std::size_t>(i)] = 12; // kelp forest
                }
            }
        }
        // Estuary (subtype 13): river-mouth ShallowWater.
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                if (grid.terrain(i) != aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                bool riverMouth = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    if (grid.riverEdges(nIdx) != 0
                        && grid.terrain(nIdx)
                            != aoc::map::TerrainType::ShallowWater
                        && grid.terrain(nIdx)
                            != aoc::map::TerrainType::Ocean) {
                        riverMouth = true; break;
                    }
                }
                if (riverMouth) {
                    bSub[static_cast<std::size_t>(i)] = 13;
                }
            }
        }
        // Carbonate platform (subtype 14): tropical shelf.
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            if (lat > 0.30f) { continue; }
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                if (grid.terrain(i) != aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                if (marineD[static_cast<std::size_t>(i)] == 1
                    && bSub[static_cast<std::size_t>(i)] == 0) {
                    bSub[static_cast<std::size_t>(i)] = 14;
                }
            }
        }

        // ---- WP4: WILDLIFE ----
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                const aoc::map::FeatureType f = grid.feature(i);
                uint8_t w = 0;
                // Big game: savanna (Plains tropical), Forest temperate.
                if ((t == aoc::map::TerrainType::Plains
                     || t == aoc::map::TerrainType::Grassland)
                    && lat < 0.40f
                    && f == aoc::map::FeatureType::None) {
                    w = 1;
                }
                if (f == aoc::map::FeatureType::Forest && lat < 0.55f) {
                    w = 1;
                }
                // Fur game: boreal Forest, Tundra.
                if ((t == aoc::map::TerrainType::Tundra
                     || (f == aoc::map::FeatureType::Forest && lat > 0.55f))) {
                    w = 2;
                }
                // Marine mammals: cold ocean (lat>0.55).
                if ((t == aoc::map::TerrainType::Ocean
                     || t == aoc::map::TerrainType::ShallowWater)
                    && lat > 0.55f) {
                    w = 3;
                }
                // Salmon: cold river or river-mouth.
                if (lat > 0.40f
                    && grid.riverEdges(i) != 0
                    && (t == aoc::map::TerrainType::Plains
                        || t == aoc::map::TerrainType::Grassland)) {
                    w = 4;
                }
                // Migratory bird stopover: wetland mid-lat.
                if ((f == aoc::map::FeatureType::Marsh
                     || f == aoc::map::FeatureType::Floodplains)
                    && lat > 0.30f && lat < 0.60f) {
                    w = 5;
                }
                wildlife[static_cast<std::size_t>(i)] = w;
            }
        }

        // ---- WP5: DISEASE ZONES ----
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                const aoc::map::FeatureType f = grid.feature(i);
                uint8_t d = 0;
                // Malaria: tropical wet (lat<0.25, Marsh/Floodplains/Jungle).
                if (lat < 0.25f
                    && (f == aoc::map::FeatureType::Marsh
                        || f == aoc::map::FeatureType::Floodplains
                        || f == aoc::map::FeatureType::Jungle)) {
                    d |= 0x01;
                }
                // Yellow fever: tropical Jungle.
                if (lat < 0.20f && f == aoc::map::FeatureType::Jungle) {
                    d |= 0x02;
                }
                // Sleeping sickness: tropical savanna (lat<0.30 + Plains).
                if (lat < 0.30f && t == aoc::map::TerrainType::Plains) {
                    d |= 0x04;
                }
                // Typhus: temperate dense — proxy via Plains+Hills mid-lat.
                if (lat > 0.30f && lat < 0.60f
                    && f == aoc::map::FeatureType::Hills) {
                    d |= 0x08;
                }
                // Plague reservoir: steppe (Plains lat 0.40-0.55).
                if (t == aoc::map::TerrainType::Plains
                    && lat > 0.40f && lat < 0.55f) {
                    d |= 0x10;
                }
                // Cholera: river delta tropical.
                if (lat < 0.30f
                    && f == aoc::map::FeatureType::Floodplains) {
                    d |= 0x20;
                }
                disease[static_cast<std::size_t>(i)] = d;
            }
        }

        // ---- WP6: RENEWABLE ENERGY POTENTIALS ----
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                // Wind: jet-stream-zone or storm-track tile + open
                // (Plains/Ocean/coast).
                uint8_t wind = 30;
                if ((climateHazard[static_cast<std::size_t>(i)] & 0x08) != 0) {
                    wind = 200; // jet stream
                } else if ((climateHazard[static_cast<std::size_t>(i)] & 0x04) != 0) {
                    wind = 170; // storm track
                } else if (lat > 0.40f && lat < 0.65f
                    && (t == aoc::map::TerrainType::Plains
                        || t == aoc::map::TerrainType::Grassland)) {
                    wind = 100;
                }
                windE[static_cast<std::size_t>(i)] = wind;
                // Solar: low cloud + low lat + Desert ideal.
                float solarF = 1.0f - lat;
                if (i < static_cast<int32_t>(cloudCover.size())) {
                    solarF *= (1.0f - cloudCover[static_cast<std::size_t>(i)]);
                }
                if (t == aoc::map::TerrainType::Desert) { solarF *= 1.4f; }
                solarE[static_cast<std::size_t>(i)] = static_cast<uint8_t>(
                    std::clamp(solarF * 255.0f, 0.0f, 255.0f));
                // Hydro: river tile + elevation gradient (downhill flow).
                uint8_t hydro = 0;
                if (grid.riverEdges(i) != 0) {
                    const int8_t myE = grid.elevation(i);
                    int8_t dropMax = 0;
                    for (int32_t d = 0; d < 6; ++d) {
                        int32_t nIdx;
                        if (!nbHelper(col, row, d, nIdx)) { continue; }
                        const int8_t nE = grid.elevation(nIdx);
                        const int8_t drop = static_cast<int8_t>(myE - nE);
                        if (drop > dropMax) { dropMax = drop; }
                    }
                    hydro = static_cast<uint8_t>(50 + dropMax * 50);
                }
                hydroE[static_cast<std::size_t>(i)] = hydro;
                // Geothermal: volcanism + hot springs.
                uint8_t geo = 0;
                const uint8_t v = volcanism[static_cast<std::size_t>(i)];
                if (v == 5)      { geo = 220; } // hot spring
                else if (v == 1
                      || v == 2
                      || v == 3
                      || v == 4) { geo = 150; }
                geoE[static_cast<std::size_t>(i)] = geo;
                // Tidal: high tidal range (oceanZone bits 0-1).
                const uint8_t oz = oceanZone[static_cast<std::size_t>(i)];
                const uint8_t tidalBin = oz & 0x03;
                tidalE[static_cast<std::size_t>(i)] =
                    static_cast<uint8_t>(tidalBin * 80);
                // Wave: storm-track ocean.
                uint8_t wave = 0;
                if ((t == aoc::map::TerrainType::Ocean
                     || t == aoc::map::TerrainType::ShallowWater)
                    && (climateHazard[static_cast<std::size_t>(i)] & 0x04) != 0) {
                    wave = 200;
                }
                waveE[static_cast<std::size_t>(i)] = wave;
            }
        }

        // ---- WP7: ATMOSPHERIC EXTRAS ----
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                uint8_t ax = 0;
                // Föhn: leeward of mountain (rain shadow).
                if (grid.feature(i) == aoc::map::FeatureType::Hills) {
                    bool nearMtn = false;
                    for (int32_t d = 0; d < 6; ++d) {
                        int32_t nIdx;
                        if (!nbHelper(col, row, d, nIdx)) { continue; }
                        if (grid.terrain(nIdx)
                                == aoc::map::TerrainType::Mountain) {
                            nearMtn = true; break;
                        }
                    }
                    if (nearMtn) { ax |= 0x01; }
                }
                // Katabatic: polar plate plateau (Snow + adjacent lower).
                if (grid.terrain(i) == aoc::map::TerrainType::Snow) {
                    ax |= 0x02;
                }
                // High pressure cell: subtropical 0.20-0.35.
                if (lat > 0.20f && lat < 0.35f
                    && (grid.terrain(i) == aoc::map::TerrainType::Ocean
                        || grid.terrain(i) == aoc::map::TerrainType::ShallowWater)) {
                    ax |= 0x04;
                }
                // Polar vortex: very-high lat.
                if (lat > 0.85f) { ax |= 0x08; }
                // ITCZ migrant: equatorial (lat<0.10).
                if (lat < 0.10f) { ax |= 0x10; }
                // Monsoon belt explicit: lat 0.10-0.40 continental coast.
                if (lat > 0.10f && lat < 0.40f
                    && (grid.terrain(i) == aoc::map::TerrainType::Plains
                        || grid.terrain(i) == aoc::map::TerrainType::Grassland)) {
                    ax |= 0x20;
                }
                atmExtras[static_cast<std::size_t>(i)] = ax;
            }
        }

        // ---- WP8: HYDROLOGICAL EXTRAS ----
        for (int32_t i = 0; i < totalT; ++i) {
            const aoc::map::TerrainType t = grid.terrain(i);
            const aoc::map::FeatureType f = grid.feature(i);
            uint8_t hx = 0;
            // Aquifer: sediment basin tiles.
            if (i < static_cast<int32_t>(sediment.size())
                && sediment[static_cast<std::size_t>(i)] > 0.04f) {
                hx |= 0x01;
            }
            // Spring: karst rockType=5 with elevation drop nearby.
            const auto& rk2 = grid.rockType();
            if (!rk2.empty()
                && i < static_cast<int32_t>(rk2.size())
                && rk2[static_cast<std::size_t>(i)] == 5) {
                hx |= 0x02;
            }
            // Crater lake: lake flag + volcanism nearby.
            if (lakeFlag[static_cast<std::size_t>(i)] != 0) {
                const int32_t row = i / width;
                const int32_t col = i % width;
                bool volcAdjacent = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    if (volcanism[static_cast<std::size_t>(nIdx)] != 0) {
                        volcAdjacent = true; break;
                    }
                }
                if (volcAdjacent) { hx |= 0x04; }
                // Fresh lake or salt lake based on salinity.
                const uint8_t oz = oceanZone[static_cast<std::size_t>(i)];
                const uint8_t salin = (oz >> 2) & 0x03;
                if (salin == 2)      { hx |= 0x40; } // salt lake
                else                  { hx |= 0x80; } // fresh lake
            }
            // Tarn: high-lat Mountain-adjacent lake.
            // Thermokarst: permafrost + lake flag.
            if (lakeFlag[static_cast<std::size_t>(i)] != 0
                && permafrost[static_cast<std::size_t>(i)] != 0) {
                hx |= 0x10;
            }
            // Oxbow: river feature on Marsh/Floodplains away from water.
            if (grid.riverEdges(i) != 0
                && (f == aoc::map::FeatureType::Marsh
                    || f == aoc::map::FeatureType::Floodplains)
                && t != aoc::map::TerrainType::ShallowWater) {
                hx |= 0x20;
            }
            hydExtras[static_cast<std::size_t>(i)] = hx;
        }

        // ---- WP9: EVENT MARKERS ----
        // Place 1-3 historical eruption sites on highest-volcanism arc
        // tiles. Place 1 meteor crater on a random old craton tile.
        {
            std::vector<int32_t> arcTiles;
            std::vector<int32_t> cratonTiles;
            for (int32_t i = 0; i < totalT; ++i) {
                if (volcanism[static_cast<std::size_t>(i)] == 1
                    || volcanism[static_cast<std::size_t>(i)] == 3) {
                    arcTiles.push_back(i);
                }
                const auto& ages = grid.crustAgeTile();
                if (i < static_cast<int32_t>(ages.size())
                    && ages[static_cast<std::size_t>(i)] > 130.0f
                    && grid.terrain(i)
                            != aoc::map::TerrainType::Ocean) {
                    cratonTiles.push_back(i);
                }
            }
            aoc::Random evRng(config.seed ^ 0xE7E7u);
            for (int32_t k = 0; k < 3 && !arcTiles.empty(); ++k) {
                const int32_t pick = evRng.nextInt(0,
                    static_cast<int32_t>(arcTiles.size()) - 1);
                eventMrk[static_cast<std::size_t>(arcTiles[pick])] = 1;
                arcTiles.erase(arcTiles.begin()
                    + static_cast<std::ptrdiff_t>(pick));
            }
            // Supervolcano caldera: 30 % chance to upgrade one to caldera.
            if (evRng.nextFloat(0.0f, 1.0f) < 0.30f) {
                for (std::size_t k = 0; k < eventMrk.size(); ++k) {
                    if (eventMrk[k] == 1) {
                        eventMrk[k] = 3; break;
                    }
                }
            }
            // Meteor crater.
            if (!cratonTiles.empty()) {
                const int32_t pick = evRng.nextInt(0,
                    static_cast<int32_t>(cratonTiles.size()) - 1);
                eventMrk[static_cast<std::size_t>(cratonTiles[pick])] = 2;
            }
        }

        // ---- MOUNTAIN PASSES ----
        // Saddle detection: tile lower than all 6 neighbours that has
        // ≥ 2 Mountain neighbours AND ≥ 2 non-Mountain neighbours,
        // i.e. a low corridor between two mountain massifs (Khyber,
        // Brenner, St. Bernard).
        std::vector<uint8_t> passFlag(static_cast<std::size_t>(totalT), 0);
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater
                    || t == aoc::map::TerrainType::Mountain) {
                    continue;
                }
                int32_t mtnNb = 0;
                int32_t openNb = 0;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    const aoc::map::TerrainType nt = grid.terrain(nIdx);
                    if (nt == aoc::map::TerrainType::Mountain) {
                        ++mtnNb;
                    } else if (nt == aoc::map::TerrainType::Plains
                            || nt == aoc::map::TerrainType::Grassland
                            || nt == aoc::map::TerrainType::Desert) {
                        ++openNb;
                    }
                }
                if (mtnNb >= 2 && openNb >= 2) {
                    passFlag[static_cast<std::size_t>(i)] = 1;
                }
            }
        }
        grid.setMountainPass(std::move(passFlag));

        // ---- DEFENSIBILITY SCORE ----
        // Higher score: own tile elevated relative to neighbours (high
        // ground), flanked by water/mountain (natural barriers), with
        // few easy approaches. River-adjacent gives moderate boost.
        std::vector<uint8_t> defScore(static_cast<std::size_t>(totalT), 0);
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                const int8_t myE = grid.elevation(i);
                int32_t score = 0;
                if (t == aoc::map::TerrainType::Mountain) { score += 80; }
                if (grid.feature(i) == aoc::map::FeatureType::Hills) {
                    score += 40;
                }
                int32_t blockedFlanks = 0;
                int32_t totalFlanks = 0;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    ++totalFlanks;
                    const aoc::map::TerrainType nt = grid.terrain(nIdx);
                    const int8_t nE = grid.elevation(nIdx);
                    if (nt == aoc::map::TerrainType::Ocean
                        || nt == aoc::map::TerrainType::ShallowWater
                        || nt == aoc::map::TerrainType::Mountain) {
                        ++blockedFlanks;
                    }
                    if (myE > nE) { score += 10; } // high ground
                }
                if (totalFlanks > 0) {
                    score += static_cast<int32_t>(
                        50.0f * static_cast<float>(blockedFlanks)
                              / static_cast<float>(totalFlanks));
                }
                if (grid.riverEdges(i) != 0) { score += 20; }
                defScore[static_cast<std::size_t>(i)] =
                    static_cast<uint8_t>(std::clamp(score, 0, 255));
            }
        }
        grid.setDefensibility(std::move(defScore));

        // ---- DOMESTICABLE SPECIES ----
        // Tile-based per-species availability (ties to Diamond GGS
        // model — Eurasia got cattle/horse/sheep/pig/goat all available;
        // Americas + Australia got nothing big = no draft animals).
        // b0 cattle/buffalo: temperate Plains/Grassland (lat 0.20-0.55)
        // b1 horses: steppe (lat 0.35-0.55, Plains, prairie)
        // b2 sheep: Hills feature in temperate
        // b3 goats: rocky Hills/foothills cool
        // b4 llamas: high Andes (Mountain or Plains, lat 0.10-0.30)
        // b5 camels: Desert + arid (lat 0.10-0.40)
        // b6 reindeer: Tundra/Snow lat > 0.55
        // b7 pigs: forested temperate-tropical
        std::vector<uint8_t> dom(static_cast<std::size_t>(totalT), 0);
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row)
                           / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                const aoc::map::FeatureType f = grid.feature(i);
                uint8_t b = 0;
                if ((t == aoc::map::TerrainType::Plains
                     || t == aoc::map::TerrainType::Grassland)
                    && lat > 0.20f && lat < 0.55f) {
                    b |= 0x01; // cattle
                }
                if (t == aoc::map::TerrainType::Plains
                    && lat > 0.35f && lat < 0.55f) {
                    b |= 0x02; // horses (steppe)
                }
                if (f == aoc::map::FeatureType::Hills
                    && lat > 0.30f && lat < 0.55f) {
                    b |= 0x04; // sheep
                }
                if (f == aoc::map::FeatureType::Hills
                    && lat > 0.40f) {
                    b |= 0x08; // goats
                }
                if ((t == aoc::map::TerrainType::Mountain
                     || (t == aoc::map::TerrainType::Plains
                         && grid.elevation(i) >= 1))
                    && lat > 0.10f && lat < 0.30f) {
                    b |= 0x10; // llamas
                }
                if (t == aoc::map::TerrainType::Desert
                    && lat > 0.10f && lat < 0.40f) {
                    b |= 0x20; // camels
                }
                if ((t == aoc::map::TerrainType::Tundra
                     || t == aoc::map::TerrainType::Snow)
                    && lat > 0.55f) {
                    b |= 0x40; // reindeer
                }
                if (f == aoc::map::FeatureType::Forest
                    && lat > 0.20f && lat < 0.55f) {
                    b |= 0x80; // pigs
                }
                dom[static_cast<std::size_t>(i)] = b;
            }
        }
        grid.setDomesticable(std::move(dom));

        // ---- TRADE ROUTE POTENTIAL ----
        // High where movement is cheap: rivers, coast, passes, flat
        // open terrain in temperate zones. Drives AI trade-route AI +
        // historical-route placement.
        std::vector<uint8_t> tradePot(static_cast<std::size_t>(totalT), 0);
        for (int32_t i = 0; i < totalT; ++i) {
            const aoc::map::TerrainType t = grid.terrain(i);
            if (t == aoc::map::TerrainType::Mountain) {
                if (grid.mountainPass()[
                        static_cast<std::size_t>(i)] != 0) {
                    tradePot[static_cast<std::size_t>(i)] = 200;
                }
                continue;
            }
            int32_t s = 30; // baseline land
            if (t == aoc::map::TerrainType::Plains
                || t == aoc::map::TerrainType::Grassland) { s += 60; }
            if (t == aoc::map::TerrainType::Desert) { s -= 30; }
            if (t == aoc::map::TerrainType::Snow
                || t == aoc::map::TerrainType::Tundra) { s -= 20; }
            if (grid.riverEdges(i) != 0) { s += 60; }
            if (grid.feature(i) == aoc::map::FeatureType::Hills) {
                s -= 20;
            }
            if (grid.feature(i) == aoc::map::FeatureType::Forest) {
                s -= 10;
            }
            if (grid.feature(i) == aoc::map::FeatureType::Jungle) {
                s -= 30;
            }
            // Ocean / coast — open trade.
            if (t == aoc::map::TerrainType::Ocean
                || t == aoc::map::TerrainType::ShallowWater) {
                s = 80;
                if (i < static_cast<int32_t>(marineD.size())
                    && marineD[static_cast<std::size_t>(i)] == 1) {
                    s = 130; // coastal shipping lane
                }
            }
            tradePot[static_cast<std::size_t>(i)] =
                static_cast<uint8_t>(std::clamp(s, 0, 255));
        }
        grid.setTradeRoutePotential(std::move(tradePot));

        // ---- HABITABILITY ----
        // Composite tile score for AI city placement: soil fertility +
        // moderate climate + freshwater access + low hazard + biome
        // quality.
        std::vector<uint8_t> habit(static_cast<std::size_t>(totalT), 0);
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater
                    || t == aoc::map::TerrainType::Mountain) {
                    continue;
                }
                float score = 0.0f;
                if (i < static_cast<int32_t>(soilFert.size())) {
                    score += soilFert[static_cast<std::size_t>(i)] * 0.40f;
                }
                if (lat > 0.20f && lat < 0.55f) { score += 0.20f; }
                else if (lat < 0.20f)            { score += 0.10f; }
                if (grid.riverEdges(i) != 0) { score += 0.15f; }
                bool nearWater = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    const aoc::map::TerrainType nt = grid.terrain(nIdx);
                    if (nt == aoc::map::TerrainType::Ocean
                        || nt == aoc::map::TerrainType::ShallowWater) {
                        nearWater = true; break;
                    }
                }
                if (nearWater) { score += 0.10f; }
                // Hazard penalty.
                if (i < static_cast<int32_t>(natHazard.size())) {
                    const uint16_t h = natHazard[static_cast<std::size_t>(i)];
                    const int32_t hCount = __builtin_popcount(h);
                    score -= static_cast<float>(hCount) * 0.04f;
                }
                if (i < static_cast<int32_t>(disease.size())
                    && disease[static_cast<std::size_t>(i)] != 0) {
                    score -= 0.10f;
                }
                if (permafrost[static_cast<std::size_t>(i)] != 0) {
                    score -= 0.20f;
                }
                habit[static_cast<std::size_t>(i)] =
                    static_cast<uint8_t>(
                        std::clamp(score * 255.0f, 0.0f, 255.0f));
            }
        }
        grid.setHabitability(std::move(habit));

        // ---- WETLAND SUBTYPES ----
        // Differentiate Marsh into peat bog (cold + acidic),
        // swamp (warm + tree-rich), fen (alkaline groundwater),
        // floodplain (river-driven).
        std::vector<uint8_t> wetSub(static_cast<std::size_t>(totalT), 0);
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::FeatureType f = grid.feature(i);
                if (f == aoc::map::FeatureType::Floodplains) {
                    wetSub[static_cast<std::size_t>(i)] = 4;
                    continue;
                }
                if (f != aoc::map::FeatureType::Marsh) { continue; }
                if (lat > 0.55f) {
                    wetSub[static_cast<std::size_t>(i)] = 1; // peat bog
                } else if (lat < 0.30f) {
                    wetSub[static_cast<std::size_t>(i)] = 2; // swamp
                } else {
                    wetSub[static_cast<std::size_t>(i)] = 3; // fen
                }
            }
        }
        grid.setWetlandSubtype(std::move(wetSub));

        // ---- CORAL REEF placement ----
        // Real Earth: coral reefs build on tropical shelf (lat<0.30,
        // marine depth = shelf, biome subtype 14 carbonate platform OR
        // adjacent to mangrove). Add Reef feature for ecosystem +
        // resource value.
        for (int32_t i = 0; i < totalT; ++i) {
            if (grid.terrain(i) != aoc::map::TerrainType::ShallowWater) {
                continue;
            }
            if (grid.feature(i) != aoc::map::FeatureType::None) { continue; }
            const int32_t row = i / width;
            const float ny = static_cast<float>(row)
                           / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            if (lat > 0.30f) { continue; }
            const std::size_t si = static_cast<std::size_t>(i);
            const uint8_t sub = bSub[si];
            const bool platform = (sub == 14 || sub == 13 || sub == 11);
            if (platform) {
                grid.setFeature(i, aoc::map::FeatureType::Reef);
            }
        }

        // ============================================================
        // SESSION 9 — Köppen / mountain structure / ore grade /
        // straits / harbor / channel pattern / vegetation density /
        // coastal feature / submarine vent / volcanic profile / karst
        // subtypes / desert subtypes / mass wasting / named winds /
        // forest age / soil moisture.
        // ============================================================
        std::vector<uint8_t> kop  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> mtnS (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> oreG (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> strait(static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> harbor(static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> chanP (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> vegD  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> coast (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> subV  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> volP  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> karstS(static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> desS  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> massW (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> namedW(static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> forA  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> soilM (static_cast<std::size_t>(totalT), 0);

        // Parallel: every iteration writes ONLY to its own tile index;
        // reads grid.terrain/feature/elevation/etc which are
        // unchanged during this pass. Safe race-free.
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                const aoc::map::FeatureType f = grid.feature(i);

                // ---- KÖPPEN classification ----
                // Approximate from terrain + lat. 30 codes won't all be
                // hit but main bins: Af/Am/Aw, BWh/BWk/BSh/BSk,
                // Csa/Csb/Cwa/Cwb/Cfa/Cfb, Dwa/Dwb/Dwc/Dwd/Dfa/Dfb/
                // Dfc/Dfd, ET, EF.
                uint8_t kc = 0;
                if (t == aoc::map::TerrainType::Snow) {
                    kc = 27; // EF (eternal frost)
                } else if (t == aoc::map::TerrainType::Tundra) {
                    kc = 26; // ET
                } else if (t == aoc::map::TerrainType::Desert) {
                    if (lat < 0.40f) {
                        kc = 4;  // BWh hot desert
                    } else {
                        kc = 5;  // BWk cold desert
                    }
                } else if (t == aoc::map::TerrainType::Mountain) {
                    kc = 28; // alpine highland
                } else if (lat < 0.18f) {
                    if (f == aoc::map::FeatureType::Jungle)      { kc = 1; } // Af
                    else if (f == aoc::map::FeatureType::Forest) { kc = 2; } // Am
                    else                                          { kc = 3; } // Aw
                } else if (lat < 0.30f) {
                    kc = 6; // BSh hot semi-arid
                } else if (lat < 0.50f) {
                    if (f == aoc::map::FeatureType::Forest)      { kc = 12; } // Cfa
                    else                                          { kc = 8;  } // Csa Med
                } else if (lat < 0.65f) {
                    if (f == aoc::map::FeatureType::Forest)      { kc = 22; } // Dfa
                    else                                          { kc = 9;  } // Csb
                } else if (lat < 0.80f) {
                    kc = 24; // Dfc subarctic continental
                } else {
                    kc = 26; // ET
                }
                kop[static_cast<std::size_t>(i)] = kc;

                // ---- MOUNTAIN STRUCTURE ----
                if (t == aoc::map::TerrainType::Mountain) {
                    const auto& vc = grid.volcanism();
                    if (vc.size() > static_cast<std::size_t>(i)
                        && (vc[static_cast<std::size_t>(i)] == 1
                            || vc[static_cast<std::size_t>(i)] == 2
                            || vc[static_cast<std::size_t>(i)] == 3)) {
                        mtnS[static_cast<std::size_t>(i)] = 3; // volcanic
                    } else if (i < static_cast<int32_t>(orogeny.size())
                        && orogeny[static_cast<std::size_t>(i)] > 0.18f) {
                        mtnS[static_cast<std::size_t>(i)] = 1; // folded
                    } else if (grid.crustAgeTile().size()
                            > static_cast<std::size_t>(i)
                        && grid.crustAgeTile()[static_cast<std::size_t>(i)] > 100.0f) {
                        mtnS[static_cast<std::size_t>(i)] = 6; // eroded ancient
                    } else if (grid.marginType().size()
                            > static_cast<std::size_t>(i)
                        && grid.marginType()[static_cast<std::size_t>(i)] == 1) {
                        mtnS[static_cast<std::size_t>(i)] = 2; // faulted active margin
                    } else {
                        mtnS[static_cast<std::size_t>(i)] = 4; // uplifted block
                    }
                }

                // ---- ORE GRADE ----
                if (grid.resource(i).isValid()) {
                    int32_t g = 100;
                    // Hotspot/arc/LIP volcanism + age = bonanza.
                    const auto& vc = grid.volcanism();
                    if (vc.size() > static_cast<std::size_t>(i)
                        && vc[static_cast<std::size_t>(i)] != 0) {
                        g += 50;
                    }
                    if (grid.crustAgeTile().size()
                            > static_cast<std::size_t>(i)) {
                        const float a =
                            grid.crustAgeTile()[
                                static_cast<std::size_t>(i)];
                        if (a > 100.0f) { g += 30; }
                        else if (a > 50.0f) { g += 15; }
                    }
                    // Convergent boundary = active mineralization.
                    // Use the seismic hazard proxy (3 = subduction).
                    if (grid.seismicHazard().size()
                            > static_cast<std::size_t>(i)
                        && (grid.seismicHazard()[
                                static_cast<std::size_t>(i)] & 0x07) >= 3) {
                        g += 40;
                    }
                    g += static_cast<int32_t>(
                        (i * 1103515245u + 12345u) % 50u) - 25;
                    oreG[static_cast<std::size_t>(i)] =
                        static_cast<uint8_t>(std::clamp(g, 30, 255));
                }

                // ---- VEGETATION DENSITY ----
                {
                    int32_t v = 30;
                    if (f == aoc::map::FeatureType::Jungle)      { v = 240; }
                    else if (f == aoc::map::FeatureType::Forest) { v = 180; }
                    else if (f == aoc::map::FeatureType::Marsh
                          || f == aoc::map::FeatureType::Floodplains) {
                        v = 120;
                    } else if (t == aoc::map::TerrainType::Grassland) {
                        v = 80;
                    } else if (t == aoc::map::TerrainType::Plains) {
                        v = 60;
                    } else if (t == aoc::map::TerrainType::Tundra) {
                        v = 30;
                    } else if (t == aoc::map::TerrainType::Desert
                            || t == aoc::map::TerrainType::Snow) {
                        v = 5;
                    }
                    vegD[static_cast<std::size_t>(i)] =
                        static_cast<uint8_t>(std::clamp(v, 0, 255));
                }

                // ---- FOREST AGE CLASS ----
                if (f == aoc::map::FeatureType::Forest
                    || f == aoc::map::FeatureType::Jungle) {
                    // Old growth in low-anthropogenic + isolated realm
                    // tiles. Mature elsewhere; secondary is a future
                    // game-state update via anthropogenic.
                    if (grid.isolatedRealm().size()
                            > static_cast<std::size_t>(i)
                        && grid.isolatedRealm()[
                                static_cast<std::size_t>(i)] != 0) {
                        forA[static_cast<std::size_t>(i)] = 4; // old growth
                    } else {
                        forA[static_cast<std::size_t>(i)] = 3; // mature
                    }
                } else if (t == aoc::map::TerrainType::Plains
                    && lat < 0.30f) {
                    forA[static_cast<std::size_t>(i)] = 1; // scrub
                }

                // ---- SOIL MOISTURE REGIME ----
                {
                    uint8_t sm = 3; // ustic default
                    if (t == aoc::map::TerrainType::Desert) { sm = 1; }   // aridic
                    else if (lat > 0.30f && lat < 0.50f
                        && t == aoc::map::TerrainType::Plains) {
                        sm = 2; // xeric (Mediterranean)
                    } else if (f == aoc::map::FeatureType::Marsh
                            || f == aoc::map::FeatureType::Floodplains) {
                        sm = 5; // aquic
                    } else if (lakeFlag[static_cast<std::size_t>(i)] != 0) {
                        sm = 6; // peraquic
                    } else if (f == aoc::map::FeatureType::Forest
                            || f == aoc::map::FeatureType::Jungle) {
                        sm = 4; // udic
                    }
                    soilM[static_cast<std::size_t>(i)] = sm;
                }

                // ---- DESERT SUBTYPE ----
                if (t == aoc::map::TerrainType::Desert) {
                    const auto& vc = grid.volcanism();
                    if (vc.size() > static_cast<std::size_t>(i)
                        && vc[static_cast<std::size_t>(i)] == 7) {
                        desS[static_cast<std::size_t>(i)] = 1; // erg sand
                    } else {
                        bool nearLake = false;
                        for (int32_t d = 0; d < 6; ++d) {
                            int32_t nIdx;
                            if (!nbHelper(col, row, d, nIdx)) { continue; }
                            if (lakeFlag[
                                    static_cast<std::size_t>(nIdx)] != 0) {
                                nearLake = true; break;
                            }
                        }
                        if (nearLake) {
                            desS[static_cast<std::size_t>(i)] = 4; // playa
                        } else if (grid.elevation(i) >= 1) {
                            desS[static_cast<std::size_t>(i)] = 3; // hammada
                        } else if (i < static_cast<int32_t>(sediment.size())
                                && sediment[static_cast<std::size_t>(i)] < 0.02f) {
                            desS[static_cast<std::size_t>(i)] = 2; // reg gravel
                        } else if (i < static_cast<int32_t>(sediment.size())
                                && sediment[static_cast<std::size_t>(i)] > 0.05f) {
                            desS[static_cast<std::size_t>(i)] = 5; // badlands
                        }
                    }
                }

                // ---- KARST SUBTYPE ----
                {
                    const auto& rk = grid.rockType();
                    if (rk.size() > static_cast<std::size_t>(i)
                        && rk[static_cast<std::size_t>(i)] == 5) {
                        if (lat < 0.25f) {
                            karstS[static_cast<std::size_t>(i)] = 3; // tropical tower
                        } else if (f == aoc::map::FeatureType::Hills) {
                            karstS[static_cast<std::size_t>(i)] = 1; // doline
                        } else if (lat > 0.40f
                            && t == aoc::map::TerrainType::Plains) {
                            karstS[static_cast<std::size_t>(i)] = 2; // polje
                        } else {
                            karstS[static_cast<std::size_t>(i)] = 4; // pavement
                        }
                    }
                }

                // ---- MASS WASTING ----
                if (t == aoc::map::TerrainType::Mountain) {
                    if (lat > 0.55f) {
                        massW[static_cast<std::size_t>(i)] = 5; // solifluction
                    } else {
                        massW[static_cast<std::size_t>(i)] = 1; // rockfall
                    }
                } else if (f == aoc::map::FeatureType::Hills
                    && i < static_cast<int32_t>(sediment.size())
                    && sediment[static_cast<std::size_t>(i)] > 0.04f) {
                    if (lat < 0.30f) {
                        massW[static_cast<std::size_t>(i)] = 4; // mudflow
                    } else {
                        massW[static_cast<std::size_t>(i)] = 2; // slump
                    }
                }

                // ---- NAMED WINDS ----
                if (f == aoc::map::FeatureType::Hills) {
                    bool nearMtn = false;
                    for (int32_t d = 0; d < 6; ++d) {
                        int32_t nIdx;
                        if (!nbHelper(col, row, d, nIdx)) { continue; }
                        if (grid.terrain(nIdx)
                                == aoc::map::TerrainType::Mountain) {
                            nearMtn = true; break;
                        }
                    }
                    if (nearMtn) {
                        if (lat > 0.40f && lat < 0.55f) {
                            namedW[static_cast<std::size_t>(i)] = 5; // föhn/chinook
                        }
                    }
                }
                if (t == aoc::map::TerrainType::Plains
                    && lat > 0.35f && lat < 0.50f) {
                    namedW[static_cast<std::size_t>(i)] = 1; // mistral-like
                }
                if (t == aoc::map::TerrainType::Desert
                    && lat > 0.20f && lat < 0.40f) {
                    namedW[static_cast<std::size_t>(i)] = 4; // harmattan
                }
                if (lat > 0.85f) {
                    namedW[static_cast<std::size_t>(i)] = 6; // williwaw
                }

                // ---- COASTAL FEATURE ----
                if (t != aoc::map::TerrainType::Ocean
                    && t != aoc::map::TerrainType::ShallowWater) {
                    int32_t waterNb = 0;
                    int32_t totalNb = 0;
                    for (int32_t d = 0; d < 6; ++d) {
                        int32_t nIdx;
                        if (!nbHelper(col, row, d, nIdx)) { continue; }
                        ++totalNb;
                        const aoc::map::TerrainType nt = grid.terrain(nIdx);
                        if (nt == aoc::map::TerrainType::Ocean
                            || nt == aoc::map::TerrainType::ShallowWater) {
                            ++waterNb;
                        }
                    }
                    if (totalNb > 0 && waterNb >= 4) {
                        coast[static_cast<std::size_t>(i)] = 1; // cape
                    } else if (waterNb == 3) {
                        coast[static_cast<std::size_t>(i)] = 4; // headland
                    } else if (waterNb == 1 && totalNb == 6) {
                        // Bay tile = land deep into water indent.
                        // Detection: opposite-direction land present.
                        coast[static_cast<std::size_t>(i)] = 2; // bay-side
                    }
                }

                // ---- CHANNEL PATTERN ----
                if (grid.riverEdges(i) != 0) {
                    const int8_t myE = grid.elevation(i);
                    int8_t maxDrop = 0;
                    for (int32_t d = 0; d < 6; ++d) {
                        int32_t nIdx;
                        if (!nbHelper(col, row, d, nIdx)) { continue; }
                        const int8_t nE = grid.elevation(nIdx);
                        const int8_t drop = static_cast<int8_t>(myE - nE);
                        if (drop > maxDrop) { maxDrop = drop; }
                    }
                    const float sd = (i < static_cast<int32_t>(sediment.size()))
                        ? sediment[static_cast<std::size_t>(i)] : 0.0f;
                    if (maxDrop >= 2)               { chanP[static_cast<std::size_t>(i)] = 1; } // straight steep
                    else if (sd > 0.08f && lat > 0.55f)
                                                    { chanP[static_cast<std::size_t>(i)] = 3; } // braided glacial
                    else if (sd > 0.05f)            { chanP[static_cast<std::size_t>(i)] = 2; } // meandering
                    else                            { chanP[static_cast<std::size_t>(i)] = 4; } // anastomosing
                }

                // ---- SUBMARINE VENT ----
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater) {
                    if (i < static_cast<int32_t>(orogeny.size())
                        && orogeny[static_cast<std::size_t>(i)] > 0.005f
                        && orogeny[static_cast<std::size_t>(i)] < 0.06f) {
                        // ridge spawning area → vents
                        if (((i * 2654435761u) >> 16) % 64u == 0) {
                            subV[static_cast<std::size_t>(i)] = 1; // black smoker
                        }
                    } else if (i < static_cast<int32_t>(orogeny.size())
                        && orogeny[static_cast<std::size_t>(i)] < -0.04f) {
                        if (((i * 2654435761u) >> 16) % 96u == 0) {
                            subV[static_cast<std::size_t>(i)] = 2; // cold seep
                        }
                    }
                }

                // ---- VOLCANIC PROFILE (VEI + magma type) ----
                if (eventMrk[static_cast<std::size_t>(i)] == 1
                    || eventMrk[static_cast<std::size_t>(i)] == 3) {
                    const auto& vc = grid.volcanism();
                    uint8_t magma = 1; // andesitic default
                    if (vc.size() > static_cast<std::size_t>(i)) {
                        if (vc[static_cast<std::size_t>(i)] == 2) {
                            magma = 0; // basaltic (hotspot)
                        } else if (vc[static_cast<std::size_t>(i)] == 1) {
                            magma = 1; // andesitic (arc)
                        } else if (vc[static_cast<std::size_t>(i)] == 3) {
                            magma = 2; // rhyolitic (LIP / supervolcano)
                        }
                    }
                    uint8_t vei = 4;
                    if (eventMrk[static_cast<std::size_t>(i)] == 3) {
                        vei = 7; // supervolcano
                    } else if (magma == 2) {
                        vei = 5;
                    } else if (magma == 0) {
                        vei = 2;
                    }
                    volP[static_cast<std::size_t>(i)] =
                        static_cast<uint8_t>(vei | (magma << 4));
                }
            }
        }

        // ---- STRAIT detection ----
        // Narrow water passage: ShallowWater/Ocean tile with ≥ 2 land
        // neighbours on at least 2 OPPOSITE sides AND connected to
        // larger water on 2 sides. Parallel.
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                if (t != aoc::map::TerrainType::Ocean
                    && t != aoc::map::TerrainType::ShallowWater) { continue; }
                if (lakeFlag[static_cast<std::size_t>(i)] != 0) { continue; }
                std::array<bool, 6> nbLand{};
                int32_t landN = 0, waterN = 0;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    const aoc::map::TerrainType nt = grid.terrain(nIdx);
                    if (nt == aoc::map::TerrainType::Ocean
                        || nt == aoc::map::TerrainType::ShallowWater) {
                        ++waterN;
                    } else {
                        ++landN;
                        nbLand[static_cast<std::size_t>(d)] = true;
                    }
                }
                if (landN >= 2 && waterN >= 2) {
                    // Opposite-side land = strait.
                    bool opposite = false;
                    for (int32_t d = 0; d < 3; ++d) {
                        if (nbLand[static_cast<std::size_t>(d)]
                            && nbLand[static_cast<std::size_t>(d + 3)]) {
                            opposite = true; break;
                        }
                    }
                    if (opposite) {
                        strait[static_cast<std::size_t>(i)] = 1;
                    }
                }
            }
        }

        // ---- HARBOR SCORE ----
        // ShallowWater tile with high land-enclosure count = sheltered
        // bay. Add island-protection bonus (intervening land within 4
        // hexes seaward). Parallel.
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                if (grid.terrain(i) != aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                int32_t landNb = 0;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    const aoc::map::TerrainType nt = grid.terrain(nIdx);
                    if (nt != aoc::map::TerrainType::Ocean
                        && nt != aoc::map::TerrainType::ShallowWater) {
                        ++landNb;
                    }
                }
                if (landNb >= 3) {
                    int32_t s = 80 + 30 * landNb;
                    harbor[static_cast<std::size_t>(i)] =
                        static_cast<uint8_t>(std::clamp(s, 0, 255));
                }
            }
        }

        // ============================================================
        // SESSION 10 — lithology / soil order / crustal thickness /
        // geothermal gradient / albedo / vegetation type / atmos river
        // / cyclone basin / SST / ice shelf / bedrock / permafrost depth.
        // ============================================================
        std::vector<uint8_t> litho   (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> bedrock (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> sOrder  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> crustTh (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> geoGrad (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> albedo  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> vegType (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> atmRiv  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> cycBasin(static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> sst     (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> iceShelf(static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> permaD  (static_cast<std::size_t>(totalT), 0);

        // Parallel: each iteration writes only to its own tile index.
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                const aoc::map::FeatureType f = grid.feature(i);
                const std::size_t si = static_cast<std::size_t>(i);
                const uint8_t pid = grid.plateId(i);
                const float pLand = (pid != 0xFFu && pid < grid.plateLandFrac().size())
                    ? grid.plateLandFrac()[pid] : 0.5f;
                const float tileAge = (si < grid.crustAgeTile().size())
                    ? grid.crustAgeTile()[si] : 0.0f;

                // ---- LITHOLOGY (specific rock) ----
                {
                    const uint8_t rt = (si < grid.rockType().size())
                        ? grid.rockType()[si] : 0;
                    uint8_t L = 0;
                    if (rt == 3) {
                        L = 19; // serpentinite (ophiolite)
                    } else if (rt == 5) {
                        L = 9;  // limestone (karst host)
                    } else if (t == aoc::map::TerrainType::Mountain) {
                        const auto& mst = grid.mountainStructure();
                        const uint8_t s = (si < mst.size()) ? mst[si] : 0;
                        if (s == 3) {
                            L = 5; // andesite (volcanic mountain)
                        } else if (s == 1 || s == 6) {
                            L = 14; // gneiss (folded/eroded)
                        } else {
                            L = 1; // granite (uplifted block)
                        }
                    } else if (rt == 1) {
                        L = (pLand < 0.40f) ? 3 : 1; // basalt vs granite
                    } else if (rt == 2) {
                        L = 13; // schist (regional metamorphic)
                    } else {
                        // Sedimentary baseline by climate.
                        if (t == aoc::map::TerrainType::Desert) {
                            L = 7; // sandstone
                        } else if (lat < 0.30f) {
                            L = 9; // limestone (warm shallow seas legacy)
                        } else if (lat > 0.55f) {
                            L = 8; // shale (cool deep basin)
                        } else {
                            L = 12; // conglomerate / mixed
                        }
                    }
                    litho[si] = L;
                }
                // Bedrock = parent rock under sediment cover. For sed
                // tiles use age-old metamorphic basement; else same.
                if (litho[si] == 7 || litho[si] == 8 || litho[si] == 9
                    || litho[si] == 10 || litho[si] == 12) {
                    bedrock[si] = (tileAge > 80.0f) ? 14 : 1; // gneiss vs granite
                } else {
                    bedrock[si] = litho[si];
                }

                // ---- SOIL ORDER (USDA) ----
                {
                    uint8_t S = 0;
                    if (t == aoc::map::TerrainType::Snow
                        || t == aoc::map::TerrainType::Tundra) {
                        S = 12; // gelisol
                    } else if (t == aoc::map::TerrainType::Desert) {
                        S = 8; // aridisol
                    } else if (f == aoc::map::FeatureType::Marsh
                            || f == aoc::map::FeatureType::Floodplains) {
                        S = 11; // histosol
                    } else if (t == aoc::map::TerrainType::Mountain) {
                        S = 13; // alpine bare
                    } else if (lat < 0.20f) {
                        if (f == aoc::map::FeatureType::Jungle) {
                            S = 4; // oxisol (tropical weathered)
                        } else {
                            S = 5; // ultisol (humid subtropical)
                        }
                    } else if (lat < 0.45f) {
                        if (f == aoc::map::FeatureType::Forest) {
                            S = 6; // alfisol
                        } else {
                            S = 9; // vertisol (clay-rich subtropical)
                        }
                    } else if (lat < 0.60f) {
                        if (f == aoc::map::FeatureType::Forest) {
                            S = 6; // alfisol
                        } else {
                            S = 3; // mollisol (chernozem prairie)
                        }
                    } else {
                        S = 7; // spodosol (boreal podzol)
                    }
                    // Volcanic boost: andisol on volcanic-rich tiles.
                    const auto& vc = grid.volcanism();
                    if (vc.size() > si && vc[si] != 0
                        && vc[si] != 6 && vc[si] != 7) {
                        S = 10; // andisol
                    }
                    sOrder[si] = S;
                }

                // ---- CRUSTAL THICKNESS ----
                // Continental ~30-70 km, oceanic ~5-10 km, orogenic
                // root ~70+. Encode 0-255 for 0-100 km.
                {
                    int32_t kmDepth = 30; // default sed-cont
                    if (pLand < 0.40f) {
                        kmDepth = 8; // oceanic
                    } else if (t == aoc::map::TerrainType::Mountain) {
                        kmDepth = 70 + static_cast<int32_t>(grid.elevation(i)) * 5;
                    } else if (tileAge > 100.0f) {
                        kmDepth = 50; // craton thicker
                    }
                    crustTh[si] = static_cast<uint8_t>(
                        std::clamp(kmDepth * 255 / 100, 0, 255));
                }

                // ---- GEOTHERMAL GRADIENT ----
                {
                    int32_t flux = 50; // default mW/m²
                    const auto& vc = grid.volcanism();
                    if (vc.size() > si) {
                        if (vc[si] == 5)      { flux = 250; }   // hot spring
                        else if (vc[si] == 1
                              || vc[si] == 2
                              || vc[si] == 3
                              || vc[si] == 4) { flux = 150; }
                    }
                    if (pLand >= 0.40f && tileAge > 100.0f) {
                        flux = 35; // craton low
                    } else if (pLand < 0.40f) {
                        flux = std::max(flux, 80); // oceanic
                    }
                    geoGrad[si] = static_cast<uint8_t>(
                        std::clamp(flux * 255 / 300, 0, 255));
                }

                // ---- ALBEDO ----
                {
                    int32_t alb = 60; // default Plains
                    if (t == aoc::map::TerrainType::Snow)        { alb = 220; }
                    else if (t == aoc::map::TerrainType::Tundra) { alb = 150; }
                    else if (t == aoc::map::TerrainType::Desert) { alb = 100; }
                    else if (t == aoc::map::TerrainType::Mountain) { alb = 130; }
                    else if (t == aoc::map::TerrainType::Grassland) { alb = 50; }
                    else if (t == aoc::map::TerrainType::Ocean
                          || t == aoc::map::TerrainType::ShallowWater) { alb = 18; }
                    if (f == aoc::map::FeatureType::Forest)        { alb -= 20; }
                    if (f == aoc::map::FeatureType::Jungle)        { alb -= 30; }
                    if (f == aoc::map::FeatureType::Ice)           { alb = 230; }
                    albedo[si] = static_cast<uint8_t>(
                        std::clamp(alb, 0, 255));
                }

                // ---- VEGETATION TYPE ----
                {
                    uint8_t V = 0; // grass default
                    if (f == aoc::map::FeatureType::Jungle) {
                        V = 2; // evergreen broadleaf
                    } else if (f == aoc::map::FeatureType::Forest) {
                        if (lat < 0.30f) {
                            V = 1; // deciduous broadleaf temperate
                        } else if (lat < 0.55f) {
                            V = 5; // mixed
                        } else if (lat < 0.75f) {
                            V = 3; // evergreen needleleaf boreal
                        } else {
                            V = 4; // deciduous needleleaf (larch)
                        }
                    } else if (t == aoc::map::TerrainType::Plains
                            && lat < 0.30f) {
                        V = 6; // savanna
                    } else if (t == aoc::map::TerrainType::Plains
                            && lat > 0.30f && lat < 0.45f) {
                        V = 7; // shrubland
                    }
                    // Mangrove subtype overrides: biomeSubtype 4
                    const auto& bs = grid.biomeSubtype();
                    if (bs.size() > si && bs[si] == 4) {
                        V = 8; // mangrove
                    }
                    vegType[si] = V;
                }

                // ---- ATMOSPHERIC RIVER ----
                // Mid-latitude westerly band over ocean +
                // continent-margin combo: 0.40-0.60 lat tiles in
                // storm track that head landward.
                if ((t == aoc::map::TerrainType::Ocean
                     || t == aoc::map::TerrainType::ShallowWater
                     || t == aoc::map::TerrainType::Plains
                     || t == aoc::map::TerrainType::Grassland)
                    && lat > 0.40f && lat < 0.60f) {
                    const auto& ch = grid.climateHazard();
                    if (ch.size() > si && (ch[si] & 0x04) != 0) {
                        atmRiv[si] = 1;
                    }
                }

                // ---- CYCLONE BASIN ----
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater) {
                    const auto& ch = grid.climateHazard();
                    if (ch.size() > si && (ch[si] & 0x01) != 0) {
                        // Pick basin by hemisphere + longitude bin.
                        const float nx0 = static_cast<float>(col)
                            / static_cast<float>(width);
                        const bool north = (ny < 0.5f);
                        if (north) {
                            if (nx0 < 0.30f)      { cycBasin[si] = 1; } // N Atlantic
                            else if (nx0 < 0.55f) { cycBasin[si] = 2; } // NE Pacific
                            else if (nx0 < 0.80f) { cycBasin[si] = 3; } // NW Pacific
                            else                  { cycBasin[si] = 4; } // N Indian
                        } else {
                            if (nx0 < 0.40f)      { cycBasin[si] = 5; } // SW Indian
                            else if (nx0 < 0.70f) { cycBasin[si] = 6; } // SE Indian / Australian
                            else                  { cycBasin[si] = 7; } // S Pacific
                        }
                    }
                }

                // ---- SEA SURFACE TEMPERATURE ----
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater) {
                    // Range -2 to +30 °C → 0..255.
                    const float c = std::cos(lat * 1.5708f);
                    float sstC = -2.0f + c * 30.0f;
                    // Upwelling cools.
                    const auto& up = grid.upwelling();
                    if (up.size() > si && up[si] == 1) {
                        sstC -= 6.0f;
                    }
                    sst[si] = static_cast<uint8_t>(
                        std::clamp((sstC + 2.0f) * 255.0f / 32.0f,
                            0.0f, 255.0f));
                }

                // ---- ICE SHELF ZONE ----
                // Polar ocean adjacent to continental ice = ice shelf.
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater) {
                    if (lat > 0.85f) {
                        // Check for adjacent Snow continental.
                        bool adjIce = false;
                        for (int32_t d = 0; d < 6; ++d) {
                            int32_t nIdx;
                            if (!nbHelper(col, row, d, nIdx)) { continue; }
                            if (grid.terrain(nIdx)
                                    == aoc::map::TerrainType::Snow) {
                                adjIce = true; break;
                            }
                        }
                        if (adjIce) {
                            iceShelf[si] = 1; // shelf
                        } else if (lat > 0.92f) {
                            iceShelf[si] = 3; // fast ice
                        } else {
                            iceShelf[si] = 2; // iceberg-spawn
                        }
                    }
                }

                // ---- PERMAFROST DEPTH ----
                {
                    if (permafrost[si] != 0) {
                        // Active layer depth: warmer = deeper thaw =
                        // less severe. Encode 0-255 for 0-2.5 m.
                        // Sub-arctic ~1m = 102; arctic ~30cm = 30.
                        if (lat > 0.85f) { permaD[si] = 30; }
                        else if (lat > 0.75f) { permaD[si] = 80; }
                        else if (lat > 0.65f) { permaD[si] = 130; }
                        else                  { permaD[si] = 180; }
                    }
                }
            }
        }

        // ============================================================
        // CLIFF COAST GENERATION
        // ============================================================
        // Real Earth coasts aren't uniformly traversable. Cliff types:
        //   1 hard rock cliff — Mountain meets water OR active-margin
        //     uplifted block coast (Big Sur, Dover, Cape Horn). Steep
        //     drop, cannot embark/disembark units.
        //   2 fjord wall — high-lat glaciated coast where Mountain
        //     stands directly over ShallowWater. Sheltered deep
        //     harbour but vertical sides; only inner-fjord landings.
        //   3 wave-cut headland — Hills meeting water on convergent
        //     coast. Lower cliff, possible beach landing at low tide.
        //   4 ice cliff — polar Snow tile bordering Ocean (ice shelf
        //     calving margin, Antarctica, Ross Ice Shelf).
        // Beach (passable) coasts are everything else: Plains/
        // Grassland/Desert with adjacent water at sea-level elevation.
        std::vector<uint8_t> cliff(static_cast<std::size_t>(totalT), 0);
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row)
                           / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                bool nextWater = false;
                bool nextShallow = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    const aoc::map::TerrainType nt = grid.terrain(nIdx);
                    if (nt == aoc::map::TerrainType::ShallowWater) {
                        nextShallow = true; nextWater = true;
                    } else if (nt == aoc::map::TerrainType::Ocean) {
                        nextWater = true;
                    }
                }
                if (!nextWater) { continue; }
                uint8_t cType = 0;
                // Ice cliff at polar Snow.
                if (t == aoc::map::TerrainType::Snow && lat > 0.80f) {
                    cType = 4;
                }
                // Hard cliff: Mountain meets water OR active-margin
                // (mountainStructure 2 = faulted) coastal land at any
                // elevation.
                else if (t == aoc::map::TerrainType::Mountain) {
                    cType = (lat > 0.55f && nextShallow) ? 2 : 1;
                }
                // Wave-cut headland: Hills feature on convergent /
                // active margin coast.
                else if (grid.feature(i) == aoc::map::FeatureType::Hills
                    && grid.marginType().size() > static_cast<std::size_t>(i)
                    && grid.marginType()[static_cast<std::size_t>(i)] == 1) {
                    cType = 3;
                }
                // Hard cliff: any coast tile with mountain structure
                // type 2 (faulted active uplift) within 1 hex.
                else if (grid.marginType().size()
                        > static_cast<std::size_t>(i)
                    && grid.marginType()[static_cast<std::size_t>(i)] == 1
                    && grid.elevation(i) >= 1) {
                    cType = 1;
                }
                // Fjord wall: cold-lat coast next to Mountain neighbour.
                else if (lat > 0.55f) {
                    bool nearMtn = false;
                    for (int32_t d = 0; d < 6; ++d) {
                        int32_t nIdx;
                        if (!nbHelper(col, row, d, nIdx)) { continue; }
                        if (grid.terrain(nIdx)
                                == aoc::map::TerrainType::Mountain) {
                            nearMtn = true; break;
                        }
                    }
                    if (nearMtn) { cType = 2; }
                }
                cliff[static_cast<std::size_t>(i)] = cType;
            }
        }
        grid.setCliffCoast(std::move(cliff));

        // ============================================================
        // SESSION 12 — coastal landforms / river regime / arid erosion
        // / transform-fault subtypes / lake-effect snow / drumlin
        // alignment / suture reactivation.
        // ============================================================
        std::vector<uint8_t> coastLF (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> rivReg  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> aridLF  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> tfType  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> lakeFX  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> drumDir (static_cast<std::size_t>(totalT), 0xFFu);
        std::vector<uint8_t> sutReact(static_cast<std::size_t>(totalT), 0);

        // ---- COASTAL LANDFORMS ----
        // Sea stack: tiny island (1-tile land surrounded by water).
        // Spit: linear coastal land protruding into water (3-4 water
        //   neighbours, oriented along longshore drift = wind axis).
        // Sandbar: ShallowWater tile parallel to coast, separated from
        //   land by lagoon water.
        // Tombolo: ShallowWater tile bridging mainland to small island.
        // Lagoon: ShallowWater enclosed by sandbar/coast on most sides.
        // Tidal flat: ShallowWater with very shallow elev next to coast
        //   in low-tide-range basin.
        // Cuspate foreland: triangular protruding land tile (cape on
        //   convex coast).
        // Hooked spit: spit at recurved end (skip explicit detection).
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                int32_t waterNb = 0;
                int32_t landNb = 0;
                std::array<bool, 6> nbWater{};
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    const aoc::map::TerrainType nt = grid.terrain(nIdx);
                    if (nt == aoc::map::TerrainType::Ocean
                        || nt == aoc::map::TerrainType::ShallowWater) {
                        ++waterNb;
                        nbWater[static_cast<std::size_t>(d)] = true;
                    } else { ++landNb; }
                }
                // Sea stack: any land tile fully surrounded by water.
                if ((t != aoc::map::TerrainType::Ocean
                     && t != aoc::map::TerrainType::ShallowWater)
                    && waterNb == 6) {
                    coastLF[static_cast<std::size_t>(i)] = 1;
                    continue;
                }
                // Spit: land with 4-5 water neighbours (peninsula tip).
                if ((t != aoc::map::TerrainType::Ocean
                     && t != aoc::map::TerrainType::ShallowWater)
                    && waterNb >= 4 && waterNb <= 5) {
                    coastLF[static_cast<std::size_t>(i)] = 2;
                    continue;
                }
                // Cuspate foreland: triangular protrusion (3 water +
                // 3 land in alternating pattern → convex cape).
                if ((t != aoc::map::TerrainType::Ocean
                     && t != aoc::map::TerrainType::ShallowWater)
                    && waterNb == 3) {
                    int32_t alternations = 0;
                    for (int32_t d = 0; d < 6; ++d) {
                        const std::size_t a = static_cast<std::size_t>(d);
                        const std::size_t b = static_cast<std::size_t>((d + 1) % 6);
                        if (nbWater[a] != nbWater[b]) { ++alternations; }
                    }
                    if (alternations >= 4) {
                        coastLF[static_cast<std::size_t>(i)] = 7;
                    }
                }
                // Sandbar / lagoon / tombolo / tidal-flat detection on
                // ShallowWater tiles.
                if (t == aoc::map::TerrainType::ShallowWater) {
                    if (lakeFlag[static_cast<std::size_t>(i)] != 0) {
                        continue; // lake water, not coastal
                    }
                    // Lagoon: water with ≥ 4 land neighbours = enclosed.
                    if (landNb >= 4) {
                        coastLF[static_cast<std::size_t>(i)] = 5;
                        continue;
                    }
                    // Tidal flat: water + adjacent shoreline + low-tide
                    // basin (oceanZone tidal=0 micro), cool latitudes.
                    const auto& oz = grid.oceanZone();
                    const std::size_t si = static_cast<std::size_t>(i);
                    if (oz.size() > si && (oz[si] & 0x03) == 0
                        && landNb >= 1 && landNb <= 3) {
                        coastLF[static_cast<std::size_t>(i)] = 6;
                        continue;
                    }
                    // Sandbar: water with land on 1-2 sides + adjacent
                    // marineDepth=1 shelf.
                    if (landNb == 1 || landNb == 2) {
                        const auto& md = grid.marineDepth();
                        if (md.size() > si && md[si] == 1) {
                            coastLF[static_cast<std::size_t>(i)] = 3;
                        }
                    }
                    // Tombolo: water with land on opposite sides
                    // (bridging two land masses).
                    if (landNb == 2) {
                        for (int32_t d = 0; d < 3; ++d) {
                            const std::size_t a = static_cast<std::size_t>(d);
                            const std::size_t b = static_cast<std::size_t>(d + 3);
                            if (!nbWater[a] && !nbWater[b]) {
                                coastLF[static_cast<std::size_t>(i)] = 4;
                                break;
                            }
                        }
                    }
                }
            }
        }

        // ---- RIVER REGIME ----
        // 1 perennial: river tile in humid biome (Plains/Grassland +
        //   moderate-to-wet climate, not desert)
        // 2 intermittent: river tile in semi-arid (Mediterranean band)
        // 3 ephemeral: river tile in Desert
        // 4 glacier-fed: river tile near Mountain Ice feature
        // 5 snow-fed: river tile near high-lat Mountain
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                if (grid.riverEdges(i) == 0) { continue; }
                const aoc::map::TerrainType t = grid.terrain(i);
                bool nearGlacier = false;
                bool nearHighMtn = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    if (grid.terrain(nIdx) == aoc::map::TerrainType::Mountain) {
                        if (grid.feature(nIdx) == aoc::map::FeatureType::Ice) {
                            nearGlacier = true;
                        }
                        if (lat > 0.45f) { nearHighMtn = true; }
                    }
                }
                uint8_t r = 1;
                if (nearGlacier) {
                    r = 4;
                } else if (nearHighMtn) {
                    r = 5;
                } else if (t == aoc::map::TerrainType::Desert) {
                    r = 3;
                } else if (lat > 0.30f && lat < 0.45f) {
                    // Mediterranean band → intermittent (dry summer).
                    r = 2;
                }
                rivReg[static_cast<std::size_t>(i)] = r;
            }
        }

        // ---- ARID EROSION LANDFORMS ----
        // Mesa: high-elev Desert/Plains tile flat top + steep sides.
        // Butte: smaller mesa.
        // Plateau: large continuous high-elev arid area.
        // Yardang: linear ridges in deserts (parallel to wind).
        // Hoodoo: spire pinnacle (Hills feature in arid + high elev).
        // Pediment: low-elev sloping plain at mountain base.
        // Slot canyon: river edge in Desert + high-elev neighbour.
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                const aoc::map::FeatureType f = grid.feature(i);
                if (t != aoc::map::TerrainType::Desert
                    && t != aoc::map::TerrainType::Plains) { continue; }
                const int8_t myE = grid.elevation(i);
                int32_t lowerNb = 0;
                int32_t higherNb = 0;
                int32_t totalNb = 0;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    ++totalNb;
                    const int8_t nE = grid.elevation(nIdx);
                    if (nE < myE)      { ++lowerNb; }
                    else if (nE > myE) { ++higherNb; }
                }
                if (myE >= 2 && lowerNb >= 5) {
                    aridLF[static_cast<std::size_t>(i)] = 2; // butte
                } else if (myE >= 2 && lowerNb >= 4 && lowerNb < 6) {
                    aridLF[static_cast<std::size_t>(i)] = 1; // mesa
                } else if (myE >= 2 && lowerNb < 4 && totalNb >= 4) {
                    aridLF[static_cast<std::size_t>(i)] = 3; // plateau
                } else if (t == aoc::map::TerrainType::Desert
                    && f == aoc::map::FeatureType::None
                    && myE == 1
                    && lowerNb >= 2 && lowerNb <= 4) {
                    aridLF[static_cast<std::size_t>(i)] = 4; // yardang
                } else if (t == aoc::map::TerrainType::Desert
                    && f == aoc::map::FeatureType::Hills
                    && myE >= 1) {
                    aridLF[static_cast<std::size_t>(i)] = 5; // hoodoo
                } else if (t == aoc::map::TerrainType::Desert
                    && myE == 0 && higherNb >= 3) {
                    aridLF[static_cast<std::size_t>(i)] = 6; // pediment
                }
                if (t == aoc::map::TerrainType::Desert
                    && grid.riverEdges(i) != 0
                    && higherNb >= 4) {
                    aridLF[static_cast<std::size_t>(i)] = 7; // slot canyon
                }
            }
        }

        // ---- TRANSFORM-FAULT SUBTYPES ----
        // For each tile flagged as transform (seismicHazard bit
        // matching transform), classify as pull-apart (transtensional,
        // sediment-rich) or restraining bend (transpressional, uplift).
        for (int32_t i = 0; i < totalT; ++i) {
            const auto& sh = grid.seismicHazard();
            if (sh.size() <= static_cast<std::size_t>(i)) { continue; }
            // Hazard 2 = transform/divergent or active margin.
            // Approximate: look at orogeny field — negative => pull-
            // apart basin, positive => restraining bend uplift.
            const float oro = orogeny[static_cast<std::size_t>(i)];
            if ((sh[static_cast<std::size_t>(i)] & 0x07) == 2) {
                if (oro < -0.04f) {
                    tfType[static_cast<std::size_t>(i)] = 1; // pull-apart
                } else if (oro > 0.06f) {
                    tfType[static_cast<std::size_t>(i)] = 2; // restraining
                } else {
                    tfType[static_cast<std::size_t>(i)] = 3; // plain transform
                }
            }
        }

        // ---- LAKE-EFFECT SNOW ----
        // Tile downwind of large lake in cold air mass: high-lat tile
        // with lake-flagged water within 1-3 hexes upwind. Wind by
        // latitude (existing band rules).
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            if (lat < 0.45f) { continue; }
            // Wind direction: easterly in polar (lat>0.60), westerly
            // mid-lat (0.30-0.60). Step direction: -1 for easterly (E→W),
            // +1 for westerly (W→E). Lake-effect tile is DOWNWIND of
            // lake → check upwind cells for lake flag.
            const float windStep = (lat >= 0.60f) ? -1.0f : 1.0f;
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                bool foundLake = false;
                for (int32_t s = 1; s <= 3 && !foundLake; ++s) {
                    int32_t cc = col - static_cast<int32_t>(windStep) * s;
                    if (cylSim) {
                        if (cc < 0)        { cc += width; }
                        if (cc >= width)   { cc -= width; }
                    } else if (cc < 0 || cc >= width) { continue; }
                    const int32_t uIdx = row * width + cc;
                    if (lakeFlag[static_cast<std::size_t>(uIdx)] != 0) {
                        foundLake = true;
                    }
                }
                if (foundLake) {
                    lakeFX[static_cast<std::size_t>(i)] = 1;
                }
            }
        }

        // ---- DRUMLIN ALIGNMENT ----
        // Tiles flagged glacialFeature=4 (drumlin field) get direction
        // = paleo ice flow vector. Approximate: ice flowed equator-
        // ward from polar plates (away from poles). Direction = south
        // in N hemisphere, north in S.
        for (int32_t i = 0; i < totalT; ++i) {
            const auto& gf = grid.glacialFeature();
            if (gf.size() <= static_cast<std::size_t>(i)) { continue; }
            if (gf[static_cast<std::size_t>(i)] != 4) { continue; }
            const int32_t row = i / width;
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            // Paleo ice flow: away from pole → toward equator. In our
            // hex offset coords, NW/N/NE come from the top, SW/S/SE
            // from below. North hemisphere ice came FROM the top, so
            // flow direction is SE-ish (dir 5 in hex offset).
            uint8_t flowDirH = (ny < 0.5f) ? 5 : 1; // N: SE, S: NE
            drumDir[static_cast<std::size_t>(i)] = flowDirH;
        }

        // ---- SUTURE REACTIVATION ----
        // Tiles where ophiolite (rockType=3) AND current convergent
        // boundary is active = old suture being re-deformed. Atlas-
        // style. Use rockType + seismicHazard bit 3 (subduction).
        for (int32_t i = 0; i < totalT; ++i) {
            const auto& rk = grid.rockType();
            const auto& sh = grid.seismicHazard();
            const std::size_t si = static_cast<std::size_t>(i);
            if (rk.size() > si && rk[si] == 3
                && sh.size() > si && (sh[si] & 0x07) == 3) {
                sutReact[si] = 1;
            }
        }

        grid.setCoastalLandform(std::move(coastLF));
        grid.setRiverRegime(std::move(rivReg));
        grid.setAridLandform(std::move(aridLF));
        grid.setTransformFaultType(std::move(tfType));
        grid.setLakeEffectSnow(std::move(lakeFX));
        grid.setDrumlinDirection(std::move(drumDir));
        grid.setSutureReactivated(std::move(sutReact));

        // ============================================================
        // SESSION 13 — solar insolation / topographic aspect / slope /
        // ecotones / pelagic productivity / shelf sediment thickness /
        // glacial rebound / sediment transport direction / coastal
        // accretion-erosion.
        // ============================================================
        std::vector<uint8_t> insol  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> aspect (static_cast<std::size_t>(totalT), 0xFFu);
        std::vector<uint8_t> slope  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> ecot   (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> pelagP (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> shelfSed(static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> rebound(static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> sedDir (static_cast<std::size_t>(totalT), 0xFFu);
        std::vector<uint8_t> coastChg(static_cast<std::size_t>(totalT), 0);

        // Earth-default tilt 23.5°, but config.axialTilt may override.
        // Annual-mean insolation at latitude θ for tilt φ (simplified):
        // I(θ) ≈ S0/π × cos(θ) × tilt-modulation.
        // Pole gets ~40 % of equator at Earth tilt.
        const float tiltDeg = (config.axialTilt > 0.0f)
            ? config.axialTilt : 23.5f;
        const float tiltRad = tiltDeg * 3.14159f / 180.0f;
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = std::abs(ny - 0.5f) * 3.14159f; // 0 equator → π/2 pole
            // Annual-mean insolation: cos(lat) baseline + tilt effect.
            // Tilt gives polar regions some summer sun → annual mean
            // doesn't drop to 0 at pole. Approximation:
            // I(θ) = S0/π × ((π/2 - |θ|) cos(φ) + sin(φ) cos(θ))   (gross simplification)
            // We use simpler: I(θ) = cos(θ) × (1 - 0.4) + 0.4 × sin(φ).
            const float baseI = std::cos(std::min(lat, 1.5708f));
            const float polar = 0.30f * std::sin(tiltRad);
            const float annualMean = std::clamp(baseI + polar, 0.0f, 1.0f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                float Ival = annualMean;
                // Altitude bonus: less atmosphere = higher TOA flux.
                // +5 % per elevation tier (rough: ~10 %/km altitude).
                const int8_t elev = grid.elevation(i);
                if (elev > 0) {
                    Ival *= (1.0f + 0.06f * static_cast<float>(elev));
                }
                if (t == aoc::map::TerrainType::Mountain) {
                    Ival *= 1.10f;
                }
                Ival = std::clamp(Ival, 0.0f, 1.0f);
                insol[static_cast<std::size_t>(i)] =
                    static_cast<uint8_t>(Ival * 255.0f);
            }
        }

        // ---- SLOPE ANGLE + TOPOGRAPHIC ASPECT ----
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const int8_t myE = grid.elevation(i);
                int8_t maxDrop = 0;
                int8_t maxRise = 0;
                int32_t dropDir = -1;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    const int8_t nE = grid.elevation(nIdx);
                    const int8_t diff = static_cast<int8_t>(nE - myE);
                    if (diff > maxRise) { maxRise = diff; }
                    if (-diff > maxDrop) { maxDrop = static_cast<int8_t>(-diff); dropDir = d; }
                }
                // Steepness = max(rise, drop). Tier elev 0-3 max drop 3.
                const int32_t steep = std::max<int32_t>(maxRise, maxDrop);
                slope[static_cast<std::size_t>(i)] =
                    static_cast<uint8_t>(std::clamp(steep * 80, 0, 255));
                // Aspect: direction the slope FACES (= direction of
                // descent = dropDir). 0xFF if flat.
                if (dropDir >= 0 && maxDrop > 0) {
                    aspect[static_cast<std::size_t>(i)] =
                        static_cast<uint8_t>(dropDir);
                }
            }
        }

        // ---- ECOTONES ----
        // Tile is ecotone if neighbour terrain class differs.
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                bool transition = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    const aoc::map::TerrainType nt = grid.terrain(nIdx);
                    if (nt == aoc::map::TerrainType::Ocean
                        || nt == aoc::map::TerrainType::ShallowWater) {
                        continue;
                    }
                    if (nt != t) { transition = true; break; }
                }
                if (transition) {
                    ecot[static_cast<std::size_t>(i)] = 1;
                }
            }
        }

        // ---- PELAGIC PRIMARY PRODUCTIVITY ----
        // Cold mid-lat upwelling = highest. Tropical open ocean (gyres)
        // = oligotrophic = lowest. River-mouth nutrient plumes boost.
        // Coastal shelves moderate, abyssal low.
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                if (t != aoc::map::TerrainType::Ocean
                    && t != aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                int32_t p = 60; // baseline
                if (lat < 0.20f) {
                    p = 30;     // tropical gyre — oligotrophic
                } else if (lat > 0.40f && lat < 0.70f) {
                    p = 110;    // mid-lat productive
                } else if (lat > 0.85f) {
                    p = 50;     // polar — cold but light-limited
                }
                const auto& up = grid.upwelling();
                const std::size_t si = static_cast<std::size_t>(i);
                if (up.size() > si && up[si] == 1) { p += 100; }
                // River runoff adjacency.
                bool nearRiver = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    if (grid.riverEdges(nIdx) != 0) {
                        nearRiver = true; break;
                    }
                }
                if (nearRiver) { p += 60; }
                pelagP[si] = static_cast<uint8_t>(std::clamp(p, 0, 255));
            }
        }

        // ---- CONTINENTAL SHELF SEDIMENT THICKNESS ----
        // Continental shelf tiles (marineDepth=1) accumulate sediment
        // from adjacent land + river runoff. Passive margins = thick
        // (Gulf of Mexico, Niger Delta), active margins = thin (Andes
        // coast).
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const auto& md = grid.marineDepth();
                const std::size_t si = static_cast<std::size_t>(i);
                if (md.size() <= si || md[si] != 1) { continue; }
                int32_t thick = 30;
                bool nearRiver = false;
                bool nearLand = false;
                bool nearActive = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    const aoc::map::TerrainType nt = grid.terrain(nIdx);
                    if (nt != aoc::map::TerrainType::Ocean
                        && nt != aoc::map::TerrainType::ShallowWater) {
                        nearLand = true;
                    }
                    if (grid.riverEdges(nIdx) != 0) { nearRiver = true; }
                    const auto& mt = grid.marginType();
                    if (mt.size() > static_cast<std::size_t>(nIdx)
                        && mt[static_cast<std::size_t>(nIdx)] == 1) {
                        nearActive = true;
                    }
                }
                if (nearLand) { thick += 80; }
                if (nearRiver) { thick += 90; }
                if (nearActive) { thick = std::max(20, thick - 60); }
                shelfSed[si] = static_cast<uint8_t>(
                    std::clamp(thick, 0, 255));
            }
        }

        // ---- GLACIAL ISOSTATIC REBOUND ----
        // High-lat formerly-ice-loaded continental tiles still rising
        // from Pleistocene unloading. Proxy: lat > 0.55 + non-mountain
        // land + adjacent former-ice flag.
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            if (lat < 0.55f) { continue; }
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater
                    || t == aoc::map::TerrainType::Mountain) { continue; }
                // Strong rebound for Plains/Grassland in lat > 0.65.
                int32_t r = 0;
                if (lat > 0.65f) { r = 200; }
                else             { r = 120; }
                rebound[static_cast<std::size_t>(i)] =
                    static_cast<uint8_t>(std::clamp(r, 0, 255));
            }
        }

        // ---- SEDIMENT TRANSPORT DIRECTION ----
        // Land tiles use flowDir (drainage). Coastal water tiles use
        // longshore drift (parallel to coast in trade-wind direction).
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater) {
                    // Longshore drift: in trade-wind belt 0-30°, drift
                    // westward (drift dir 3 = W). Mid-lat westerlies:
                    // drift eastward (dir 0 = E).
                    if (lat < 0.30f) {
                        sedDir[static_cast<std::size_t>(i)] = 3;
                    } else if (lat < 0.60f) {
                        sedDir[static_cast<std::size_t>(i)] = 0;
                    } else {
                        sedDir[static_cast<std::size_t>(i)] = 3;
                    }
                } else {
                    // Land: use flowDir.
                    const auto& fd = grid.flowDir();
                    const std::size_t si = static_cast<std::size_t>(i);
                    if (fd.size() > si) {
                        sedDir[si] = fd[si];
                    }
                }
            }
        }

        // ---- COASTAL ACCRETION / EROSION ----
        // 1 accreting: river-mouth land tile (sediment plume builds).
        // 2 eroding: cliff coast tile (waves cut back).
        // 3 stable: other coastal tiles.
        // 0: inland.
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                bool nearWater = false;
                bool nearRiver = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    const aoc::map::TerrainType nt = grid.terrain(nIdx);
                    if (nt == aoc::map::TerrainType::Ocean
                        || nt == aoc::map::TerrainType::ShallowWater) {
                        nearWater = true;
                    }
                    if (grid.riverEdges(nIdx) != 0) { nearRiver = true; }
                }
                if (!nearWater) { continue; }
                const auto& cf = grid.cliffCoastAll();
                const std::size_t si = static_cast<std::size_t>(i);
                if (cf.size() > si
                    && (cf[si] == 1 || cf[si] == 2 || cf[si] == 3)) {
                    coastChg[si] = 2; // eroding (cliff)
                } else if (nearRiver
                    || grid.riverEdges(i) != 0) {
                    coastChg[si] = 1; // accreting (delta)
                } else {
                    coastChg[si] = 3; // stable
                }
            }
        }

        grid.setSolarInsolation(std::move(insol));
        grid.setTopographicAspect(std::move(aspect));
        grid.setSlopeAngle(std::move(slope));
        grid.setEcotone(std::move(ecot));
        grid.setPelagicProductivity(std::move(pelagP));
        grid.setShelfSedimentThickness(std::move(shelfSed));
        grid.setGlacialRebound(std::move(rebound));
        grid.setSedimentTransportDir(std::move(sedDir));
        grid.setCoastalChange(std::move(coastChg));

        // ============================================================
        // SESSION 14 — stream order / navigability / dam site / riparian
        // / aquifer recharge / per-crop suitability (8) / pasture /
        // forestry / fold axis / metamorphic facies / plate stress /
        // cyclone intensity / drought severity / storm wave height /
        // snow line / habitat fragmentation / endemism / species richness.
        // ============================================================
        std::vector<uint8_t> streamOrd(static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> nav      (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> damS     (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> ripa     (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> aqRecharge(static_cast<std::size_t>(totalT), 0);
        std::array<std::vector<uint8_t>, 8> crops;
        for (auto& c : crops) {
            c.assign(static_cast<std::size_t>(totalT), 0);
        }
        std::vector<uint8_t> past   (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> forY   (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> foldA  (static_cast<std::size_t>(totalT), 0xFFu);
        std::vector<uint8_t> metaF  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> pStress(static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> cycInt (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> drSev  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> waveH  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> snowL  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> habFrag(static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> endIdx (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> spRich (static_cast<std::size_t>(totalT), 0);

        // ---- STRAHLER STREAM ORDER ----
        // Approximate: walk down flow direction. Tile gets order N+1
        // if it merges two N-order flows; else inherits max upstream
        // order. Iterative sweep convergence: ~5 passes suffice.
        // Initialize all river tiles to order 1.
        for (int32_t i = 0; i < totalT; ++i) {
            if (grid.riverEdges(i) != 0) { streamOrd[
                static_cast<std::size_t>(i)] = 1; }
        }
        for (int32_t pass = 0; pass < 6; ++pass) {
            for (int32_t i = 0; i < totalT; ++i) {
                if (grid.riverEdges(i) == 0) { continue; }
                const int32_t row = i / width;
                const int32_t col = i % width;
                // Find upstream river-tile orders (those that flow INTO me).
                int32_t upstreamCount[8] = {0};
                int32_t maxUp = 0;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    if (grid.riverEdges(nIdx) == 0) { continue; }
                    const auto& fd = grid.flowDir();
                    const std::size_t nsi = static_cast<std::size_t>(nIdx);
                    if (fd.size() <= nsi) { continue; }
                    // Reverse direction: if neighbour drains TOWARD me,
                    // it flows in. Hex direction d from me; neighbour's
                    // flow dir should equal (d+3)%6 to flow back.
                    if (fd[nsi] == static_cast<uint8_t>((d + 3) % 6)) {
                        const uint8_t up = streamOrd[nsi];
                        if (up > 0 && up <= 7) { upstreamCount[up]++; }
                        if (up > maxUp) { maxUp = up; }
                    }
                }
                uint8_t order = static_cast<uint8_t>(std::max(1, maxUp));
                if (upstreamCount[maxUp] >= 2) {
                    order = static_cast<uint8_t>(maxUp + 1);
                }
                if (order > streamOrd[static_cast<std::size_t>(i)]) {
                    streamOrd[static_cast<std::size_t>(i)] = order;
                }
            }
        }

        // ---- RIVER NAVIGABILITY + DAM SITE + RIPARIAN ----
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                if (grid.riverEdges(i) != 0) {
                    const std::size_t si = static_cast<std::size_t>(i);
                    // Navigable: order ≥ 4 + perennial regime + low slope.
                    const uint8_t ord = streamOrd[si];
                    const auto& rr = grid.riverRegime();
                    const auto& sl = grid.slopeAngle();
                    const bool peren = (rr.size() > si && rr[si] == 1);
                    const bool flat = (sl.size() > si && sl[si] < 80);
                    if (ord >= 4 && peren && flat) {
                        nav[si] = 1;
                    }
                    // Dam site: high slope + perennial + downstream
                    // basin (lower-elev neighbour).
                    if (peren && sl.size() > si && sl[si] > 100) {
                        damS[si] = static_cast<uint8_t>(
                            std::clamp(static_cast<int32_t>(sl[si]) + 50, 0, 255));
                    }
                }
                // Riparian: 1-tile band along river.
                bool nearRiver = (grid.riverEdges(i) != 0);
                if (!nearRiver) {
                    for (int32_t d = 0; d < 6; ++d) {
                        int32_t nIdx;
                        if (!nbHelper(col, row, d, nIdx)) { continue; }
                        if (grid.riverEdges(nIdx) != 0) {
                            nearRiver = true; break;
                        }
                    }
                }
                if (nearRiver) {
                    ripa[static_cast<std::size_t>(i)] = 1;
                }
            }
        }

        // ---- AQUIFER RECHARGE ----
        AOC_PARALLEL_FOR_ROWS
        for (int32_t i = 0; i < totalT; ++i) {
            const auto& he = grid.hydroExtras();
            const std::size_t si = static_cast<std::size_t>(i);
            if (he.size() <= si || (he[si] & 0x01) == 0) { continue; }
            // Has aquifer flag — score recharge.
            int32_t rate = 80;
            if (i < static_cast<int32_t>(soilFert.size())) {
                rate += static_cast<int32_t>(soilFert[si] * 80.0f);
            }
            const auto& cc = grid.cloudCover();
            if (cc.size() > si) {
                rate += static_cast<int32_t>(cc[si] * 60.0f);
            }
            aqRecharge[si] = static_cast<uint8_t>(
                std::clamp(rate, 0, 255));
        }

        // ---- PER-CROP SUITABILITY ----
        // Wheat: temperate Plains/Grassland, lat 0.30-0.55, fertile soil
        // Rice: hot+humid lowland, Floodplains/Marsh, lat<0.40
        // Maize: warm Grassland, lat 0.20-0.45
        // Potato: cool highland, Hills + lat 0.40-0.60
        // Banana: tropical Grassland/Jungle, lat<0.20
        // Coffee: subtropical mountain Hills, lat 0.15-0.35
        // Wine: Mediterranean Plains, lat 0.30-0.45 + fertile
        // Cotton: subtropical Plains, lat 0.20-0.40
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                const aoc::map::FeatureType f = grid.feature(i);
                const std::size_t si = static_cast<std::size_t>(i);
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater
                    || t == aoc::map::TerrainType::Mountain
                    || t == aoc::map::TerrainType::Snow) {
                    continue;
                }
                const float fert = (si < soilFert.size())
                    ? soilFert[si] : 0.5f;
                // Wheat
                if ((t == aoc::map::TerrainType::Plains
                     || t == aoc::map::TerrainType::Grassland)
                    && lat > 0.30f && lat < 0.55f) {
                    crops[0][si] = static_cast<uint8_t>(
                        std::clamp(fert * 200.0f + 40.0f, 0.0f, 255.0f));
                }
                // Rice
                if (lat < 0.40f
                    && (f == aoc::map::FeatureType::Floodplains
                        || f == aoc::map::FeatureType::Marsh
                        || (t == aoc::map::TerrainType::Grassland
                            && grid.riverEdges(i) != 0))) {
                    crops[1][si] = static_cast<uint8_t>(
                        std::clamp(fert * 220.0f + 30.0f, 0.0f, 255.0f));
                }
                // Maize
                if (t == aoc::map::TerrainType::Grassland
                    && lat > 0.20f && lat < 0.45f) {
                    crops[2][si] = static_cast<uint8_t>(
                        std::clamp(fert * 200.0f + 30.0f, 0.0f, 255.0f));
                }
                // Potato (cool highland)
                if (f == aoc::map::FeatureType::Hills
                    && lat > 0.40f && lat < 0.60f) {
                    crops[3][si] = static_cast<uint8_t>(
                        std::clamp(fert * 180.0f + 50.0f, 0.0f, 255.0f));
                }
                // Banana
                if (lat < 0.20f
                    && (t == aoc::map::TerrainType::Grassland
                        || f == aoc::map::FeatureType::Jungle)) {
                    crops[4][si] = static_cast<uint8_t>(
                        std::clamp(fert * 200.0f + 40.0f, 0.0f, 255.0f));
                }
                // Coffee
                if (f == aoc::map::FeatureType::Hills
                    && lat > 0.15f && lat < 0.35f) {
                    crops[5][si] = static_cast<uint8_t>(
                        std::clamp(fert * 200.0f + 40.0f, 0.0f, 255.0f));
                }
                // Wine grape (Mediterranean)
                if (t == aoc::map::TerrainType::Plains
                    && lat > 0.30f && lat < 0.45f) {
                    crops[6][si] = static_cast<uint8_t>(
                        std::clamp(fert * 200.0f + 30.0f, 0.0f, 255.0f));
                }
                // Cotton
                if (t == aoc::map::TerrainType::Plains
                    && lat > 0.20f && lat < 0.40f) {
                    crops[7][si] = static_cast<uint8_t>(
                        std::clamp(fert * 200.0f + 20.0f, 0.0f, 255.0f));
                }
            }
        }

        // ---- PASTURE + FORESTRY ----
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const aoc::map::TerrainType t = grid.terrain(i);
                const aoc::map::FeatureType f = grid.feature(i);
                const std::size_t si = static_cast<std::size_t>(i);
                if (t == aoc::map::TerrainType::Grassland && lat > 0.20f
                    && lat < 0.60f) {
                    const float fert = (si < soilFert.size())
                        ? soilFert[si] : 0.5f;
                    past[si] = static_cast<uint8_t>(
                        std::clamp(fert * 220.0f + 30.0f, 0.0f, 255.0f));
                }
                if (f == aoc::map::FeatureType::Forest
                    || f == aoc::map::FeatureType::Jungle) {
                    const auto& vd = grid.vegetationDensity();
                    if (vd.size() > si) {
                        forY[si] = vd[si];
                    }
                }
            }
        }

        // ---- FOLD AXIS + METAMORPHIC FACIES + PLATE STRESS ----
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const std::size_t si = static_cast<std::size_t>(i);
                // Fold axis: Mountain tiles with mountainStructure=1
                // (folded). Axis = perpendicular to compression =
                // direction along boundary trend. Use the boundary
                // line direction (orthogonal to nearest-other-plate
                // vector). Approximation: use neighbour with greatest
                // elevation match (along-strike).
                const auto& mst = grid.mountainStructure();
                if (mst.size() > si && mst[si] == 1) {
                    // Find neighbour direction with min elev diff.
                    const int8_t myE = grid.elevation(i);
                    int32_t bestDir = -1;
                    int8_t bestDiff = 127;
                    for (int32_t d = 0; d < 6; ++d) {
                        int32_t nIdx;
                        if (!nbHelper(col, row, d, nIdx)) { continue; }
                        if (grid.terrain(nIdx)
                                != aoc::map::TerrainType::Mountain) {
                            continue;
                        }
                        const int8_t df = static_cast<int8_t>(
                            std::abs(grid.elevation(nIdx) - myE));
                        if (df < bestDiff) {
                            bestDiff = df;
                            bestDir = d;
                        }
                    }
                    if (bestDir >= 0) {
                        foldA[si] = static_cast<uint8_t>(bestDir);
                    }
                }
                // Metamorphic facies: based on rockType + crustal
                // thickness + temperature gradient (geothermal).
                const auto& rk = grid.rockType();
                if (rk.size() > si && rk[si] == 2) {
                    // Metamorphic. Pick facies by depth (crustalThick)
                    // + temp (geothermal). Approximation by elevation.
                    const auto& gg = grid.geothermalGradient();
                    const auto& ct = grid.crustalThickness();
                    const uint8_t G = (gg.size() > si) ? gg[si] : 50;
                    const uint8_t T = (ct.size() > si) ? ct[si] : 100;
                    if (G > 200 && T > 100) {
                        metaF[si] = 4; // granulite (deep, hot)
                    } else if (T > 130 && G < 60) {
                        metaF[si] = 5; // blueschist (deep cold subduct)
                    } else if (T > 200 && G < 70) {
                        metaF[si] = 6; // eclogite
                    } else if (T > 80 && G > 100) {
                        metaF[si] = 3; // amphibolite
                    } else if (T > 50) {
                        metaF[si] = 2; // greenschist
                    } else {
                        metaF[si] = 1; // zeolite
                    }
                }
                // Plate stress: read owning plate's orogenyLocal at
                // tile's plate-local coords. Already done into orogeny[]
                // for world-frame; re-encode 0-255 from |orogeny|.
                const float oro = orogeny[si];
                pStress[si] = static_cast<uint8_t>(
                    std::clamp(std::abs(oro) * 800.0f, 0.0f, 255.0f));
            }
        }

        // ---- CYCLONE INTENSITY + DROUGHT SEVERITY + STORM WAVE ----
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const std::size_t si = static_cast<std::size_t>(i);
                // Cyclone: rated by SST × hurricane bit. SST > 200
                // (= ~26°C) supports tropical cyclones. Cat 5 needs
                // SST > 230 (29°C). Map SST 200-255 to Cat 1-5.
                const auto& ch = grid.climateHazard();
                const auto& sst = grid.seaSurfaceTemp();
                if (ch.size() > si && (ch[si] & 0x01) != 0
                    && sst.size() > si && sst[si] > 200) {
                    const int32_t cat = std::min(5,
                        static_cast<int32_t>((sst[si] - 200) / 11));
                    cycInt[si] = static_cast<uint8_t>(std::max(1, cat));
                }
                // Drought severity: nat-hazard bit 2 (drought) tile.
                // Severity = function of latitude in dry band + low
                // cloud cover.
                const auto& nh = grid.naturalHazard();
                if (nh.size() > si && (nh[si] & 0x0004) != 0) {
                    int32_t sev = 1;
                    if (lat > 0.20f && lat < 0.30f) { sev = 2; }
                    if (grid.terrain(i) == aoc::map::TerrainType::Desert) {
                        sev += 1;
                    }
                    const auto& cc = grid.cloudCover();
                    if (cc.size() > si && cc[si] < 0.20f) { sev += 1; }
                    drSev[si] = static_cast<uint8_t>(std::min(4, sev));
                }
                // Storm wave height: storm-track ocean (climateHazard
                // bit 2) + open water far from coast.
                if (ch.size() > si && (ch[si] & 0x04) != 0) {
                    const auto& md = grid.marineDepth();
                    int32_t wave = 100;
                    if (md.size() > si && md[si] >= 4) { wave = 220; }
                    waveH[si] = static_cast<uint8_t>(wave);
                }
                // Snow line: elevation check vs latitude-dependent threshold.
                // Tropics snow line ~5000m, polar 0m.
                const float snowThr = std::max(0.0f, 1.0f - lat * 1.3f);
                const int8_t elev = grid.elevation(i);
                if (static_cast<float>(elev) >= snowThr * 3.0f
                    && elev >= 1) {
                    snowL[si] = 1;
                }
            }
        }

        // ---- HABITAT FRAGMENTATION + ENDEMISM + SPECIES RICHNESS ----
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const std::size_t si = static_cast<std::size_t>(i);
                const aoc::map::TerrainType t = grid.terrain(i);
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                // Fragmentation: count distinct neighbour terrain types.
                int32_t types[16] = {0};
                int32_t totalNb = 0;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    ++totalNb;
                    const uint8_t nt = static_cast<uint8_t>(grid.terrain(nIdx));
                    if (nt < 16) { types[nt]++; }
                }
                int32_t distinctCount = 0;
                for (int32_t k = 0; k < 16; ++k) {
                    if (types[k] > 0) { ++distinctCount; }
                }
                habFrag[si] = static_cast<uint8_t>(
                    std::clamp(distinctCount * 50, 0, 255));
                // Endemism: graded version of isolatedRealm. Higher
                // score for older isolated plates.
                const auto& iso = grid.isolatedRealm();
                if (iso.size() > si && iso[si] != 0) {
                    const uint8_t pid = grid.plateId(i);
                    int32_t score = 150;
                    // Plate age boost.
                    if (pid != 0xFFu && pid < grid.plateLandFrac().size()
                        && i < static_cast<int32_t>(grid.crustAgeTile().size())) {
                        const float age = grid.crustAgeTile()[si];
                        score += static_cast<int32_t>(age * 0.5f);
                    }
                    endIdx[si] = static_cast<uint8_t>(
                        std::clamp(score, 0, 255));
                }
                // Species richness: tropical wet > temperate > polar.
                int32_t rich = 60;
                if (lat < 0.20f) { rich = 200; }
                else if (lat < 0.40f) { rich = 150; }
                else if (lat < 0.60f) { rich = 100; }
                if (grid.feature(i) == aoc::map::FeatureType::Jungle) {
                    rich += 40;
                }
                // Refugia boost.
                const auto& ref = grid.refugium();
                if (ref.size() > si && ref[si] != 0) { rich += 30; }
                spRich[si] = static_cast<uint8_t>(
                    std::clamp(rich, 0, 255));
            }
        }

        grid.setStreamOrder(std::move(streamOrd));
        grid.setNavigable(std::move(nav));
        grid.setDamSite(std::move(damS));
        grid.setRiparian(std::move(ripa));
        grid.setAquiferRecharge(std::move(aqRecharge));
        for (int32_t k = 0; k < 8; ++k) {
            grid.setCropSuitability(k, std::move(crops[k]));
        }
        grid.setPastureScore(std::move(past));
        grid.setForestryYield(std::move(forY));
        grid.setFoldAxis(std::move(foldA));
        grid.setMetamorphicFacies(std::move(metaF));
        grid.setPlateStress(std::move(pStress));
        grid.setCycloneIntensity(std::move(cycInt));
        grid.setDroughtSeverity(std::move(drSev));
        grid.setStormWaveHeight(std::move(waveH));
        grid.setSnowLine(std::move(snowL));
        grid.setHabitatFragmentation(std::move(habFrag));
        grid.setEndemismIndex(std::move(endIdx));
        grid.setSpeciesRichness(std::move(spRich));

        // ============================================================
        // SESSION 15 — NPP / growing season / frost days / carrying
        // capacity / soil texture (3) / seasonal & diurnal temp range /
        // UV index / coral bleach / magnetic stripes / heat flow /
        // volcano return / tsunami runup.
        // ============================================================
        std::vector<uint8_t> npp     (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> growSeas(static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> frost   (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> carryCap(static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> clay    (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> silt    (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> sand    (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> seasRng (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> diurRng (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> uv      (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> coralB  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> magAnom (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> heatF   (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> volRet  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> tsunRun (static_cast<std::size_t>(totalT), 0);

        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const std::size_t si = static_cast<std::size_t>(i);
                const aoc::map::TerrainType t = grid.terrain(i);
                const aoc::map::FeatureType f = grid.feature(i);

                // ---- GROWING SEASON / FROST DAYS ----
                // Frost-free band: warmer = longer. Tropical 365,
                // mid-lat ~200, polar ~30.
                int32_t gs = 0;
                if (lat < 0.20f)      { gs = 240; } // 360 d (tropical)
                else if (lat < 0.40f) { gs = 200; } // 280 d
                else if (lat < 0.55f) { gs = 150; } // 210 d
                else if (lat < 0.70f) { gs = 80;  } // 110 d
                else                  { gs = 20;  } // 30 d
                if (t == aoc::map::TerrainType::Mountain) { gs /= 2; }
                if (t == aoc::map::TerrainType::Snow)     { gs = 0; }
                if (t == aoc::map::TerrainType::Tundra)   { gs = std::min(gs, 30); }
                growSeas[si] = static_cast<uint8_t>(gs);
                frost[si] = static_cast<uint8_t>(255 - growSeas[si]);

                // ---- SEASONAL / DIURNAL TEMP RANGE ----
                // Continentality from continentalFactor analog: tiles
                // far from ocean have higher swing. Approx via lat:
                // seasonal range scales with lat (more at poles), modulated
                // by water proximity (less at coast).
                int32_t seasonalK = static_cast<int32_t>(lat * 200.0f);
                bool nearWater = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    const aoc::map::TerrainType nt = grid.terrain(nIdx);
                    if (nt == aoc::map::TerrainType::Ocean
                        || nt == aoc::map::TerrainType::ShallowWater) {
                        nearWater = true; break;
                    }
                }
                if (nearWater) { seasonalK = seasonalK * 60 / 100; }
                seasRng[si] = static_cast<uint8_t>(
                    std::clamp(seasonalK, 0, 255));
                // Diurnal: high in deserts (clear sky → fast cooling),
                // low on water + humid biomes.
                int32_t diurnalK = 80;
                if (t == aoc::map::TerrainType::Desert) { diurnalK = 200; }
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater) {
                    diurnalK = 30;
                }
                if (f == aoc::map::FeatureType::Jungle) { diurnalK = 40; }
                diurRng[si] = static_cast<uint8_t>(
                    std::clamp(diurnalK, 0, 255));

                // ---- UV INDEX ----
                // High at equator + high altitude. Polar ozone hole
                // pulls polar UV up at extreme latitudes.
                int32_t uvK = static_cast<int32_t>(
                    (1.0f - lat) * 200.0f);
                const int8_t elev = grid.elevation(i);
                uvK += elev * 20;
                if (lat > 0.85f) { uvK += 40; } // polar ozone hole
                uv[si] = static_cast<uint8_t>(std::clamp(uvK, 0, 255));

                // ---- NPP ----
                // Composite: temperature × moisture proxy from biome.
                int32_t nppK = 60;
                if (f == aoc::map::FeatureType::Jungle)      { nppK = 240; }
                else if (f == aoc::map::FeatureType::Forest) { nppK = 180; }
                else if (f == aoc::map::FeatureType::Floodplains
                      || f == aoc::map::FeatureType::Marsh)  { nppK = 200; }
                else if (t == aoc::map::TerrainType::Grassland) { nppK = 120; }
                else if (t == aoc::map::TerrainType::Plains)   { nppK = 80;  }
                else if (t == aoc::map::TerrainType::Tundra)   { nppK = 40;  }
                else if (t == aoc::map::TerrainType::Desert
                      || t == aoc::map::TerrainType::Snow)     { nppK = 10;  }
                // Soil fertility boost.
                if (si < soilFert.size()) {
                    nppK = static_cast<int32_t>(
                        static_cast<float>(nppK) * (0.5f + soilFert[si]));
                }
                npp[si] = static_cast<uint8_t>(std::clamp(nppK, 0, 255));

                // ---- CARRYING CAPACITY ----
                // Composite: NPP + freshwater + climate hospitability.
                int32_t cap = static_cast<int32_t>(npp[si]) / 2;
                if (grid.riverEdges(i) != 0) { cap += 50; }
                if (lat > 0.20f && lat < 0.55f) { cap += 30; }
                if (t == aoc::map::TerrainType::Snow
                    || t == aoc::map::TerrainType::Tundra
                    || t == aoc::map::TerrainType::Mountain) {
                    cap = cap / 4;
                }
                carryCap[si] = static_cast<uint8_t>(std::clamp(cap, 0, 255));

                // ---- SOIL TEXTURE (clay/silt/sand %) ----
                // Approximate from soilOrder + climate:
                //  oxisol/ultisol → high clay (tropical weathered)
                //  mollisol/chernozem → loam (balanced)
                //  aridisol → sand-rich
                //  alfisol → loam
                //  spodosol → sand-rich (boreal podzol)
                //  vertisol → very high clay
                const auto& so = grid.soilOrder();
                uint8_t cP = 0, sP = 0, sandP = 0;
                if (so.size() > si) {
                    switch (so[si]) {
                        case 4:  cP=180; sP=40;  sandP=35;  break; // oxisol
                        case 5:  cP=140; sP=60;  sandP=55;  break; // ultisol
                        case 3:  cP=80;  sP=110; sandP=65;  break; // mollisol
                        case 6:  cP=80;  sP=100; sandP=75;  break; // alfisol
                        case 7:  cP=40;  sP=70;  sandP=145; break; // spodosol
                        case 8:  cP=30;  sP=60;  sandP=165; break; // aridisol
                        case 9:  cP=200; sP=35;  sandP=20;  break; // vertisol
                        case 10: cP=80;  sP=120; sandP=55;  break; // andisol
                        case 11: cP=130; sP=85;  sandP=40;  break; // histosol
                        case 12: cP=60;  sP=90;  sandP=105; break; // gelisol
                        default: cP=80;  sP=85;  sandP=90;  break;
                    }
                }
                clay[si] = cP;
                silt[si] = sP;
                sand[si] = sandP;

                // ---- CORAL BLEACH RISK ----
                // High SST + tropical + reef tile.
                if (t == aoc::map::TerrainType::ShallowWater
                    && lat < 0.30f) {
                    const auto& sst = grid.seaSurfaceTemp();
                    if (sst.size() > si) {
                        const uint8_t s = sst[si];
                        if (s > 230) {
                            // 30-32°C = bleach risk; > 33°C = severe.
                            coralB[si] = static_cast<uint8_t>(
                                std::clamp((static_cast<int32_t>(s) - 230) * 10,
                                    0, 255));
                        }
                    }
                }

                // ---- MAGNETIC ANOMALY STRIPES ----
                // Banded oceanic crust: distance from nearest plate
                // boundary (proxied by Voronoi distance) modulo stripe
                // wavelength gives the stripe id. Only set on oceanic
                // tiles.
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater) {
                    const auto& ages = grid.crustAgeTile();
                    if (ages.size() > si) {
                        // Convert age to stripe id (~10 My per stripe).
                        const float a = ages[si];
                        magAnom[si] = static_cast<uint8_t>(
                            (static_cast<int32_t>(a * 5.0f)) & 0xFF);
                    }
                }

                // ---- HEAT FLOW REFINED ----
                // Combine geothermalGradient + crust age (older = lower).
                int32_t hf = 80;
                const auto& gg = grid.geothermalGradient();
                if (gg.size() > si) { hf = static_cast<int32_t>(gg[si]); }
                const auto& ages = grid.crustAgeTile();
                if (ages.size() > si) {
                    const float a = ages[si];
                    if (a > 100.0f) { hf = std::max(20, hf - 30); }
                }
                heatF[si] = static_cast<uint8_t>(std::clamp(hf, 0, 255));

                // ---- VOLCANO RETURN PERIOD ----
                // Volcanism flag → low return period (frequent).
                const auto& vc = grid.volcanism();
                if (vc.size() > si && vc[si] != 0
                    && vc[si] != 6 && vc[si] != 7) {
                    // Active arc/hotspot/LIP/rift: 30-300 yr scale.
                    int32_t rp = 50;
                    if (vc[si] == 2) { rp = 100; } // hotspot rare
                    if (vc[si] == 3) { rp = 200; } // LIP rare large
                    if (vc[si] == 5) { rp = 30;  } // hot spring frequent
                    volRet[si] = static_cast<uint8_t>(rp);
                }

                // ---- TSUNAMI RUNUP ----
                // Coastal land tile + tsunami hazard bit set → runup
                // height proxy from cyclone basin proximity + slope.
                const auto& sh = grid.seismicHazard();
                if (sh.size() > si && (sh[si] & 0x08) != 0) {
                    int32_t r = 40;
                    const auto& sl = grid.slopeAngle();
                    if (sl.size() > si && sl[si] < 60) { r += 80; } // flat amplifies
                    tsunRun[si] = static_cast<uint8_t>(
                        std::clamp(r, 0, 255));
                }
            }
        }

        grid.setNetPrimaryProductivity(std::move(npp));
        grid.setGrowingSeasonDays(std::move(growSeas));
        grid.setFrostDays(std::move(frost));
        grid.setCarryingCapacity(std::move(carryCap));
        grid.setSoilClayPct(std::move(clay));
        grid.setSoilSiltPct(std::move(silt));
        grid.setSoilSandPct(std::move(sand));
        grid.setSeasonalTempRange(std::move(seasRng));
        grid.setDiurnalTempRange(std::move(diurRng));
        grid.setUvIndex(std::move(uv));
        grid.setCoralBleachRisk(std::move(coralB));
        grid.setMagneticAnomaly(std::move(magAnom));
        grid.setHeatFlow(std::move(heatF));
        grid.setVolcanoReturnPeriod(std::move(volRet));
        grid.setTsunamiRunup(std::move(tsunRun));

        // ============================================================
        // SESSION 16 — TPI / TWI / roughness / curvature / river
        // discharge / drainage area / watershed id / per-livestock /
        // active fault / reef terraces / mine suitability / coal seam
        // thickness / soil pH / ice-cover duration / hydropower.
        // ============================================================
        std::vector<uint8_t> tpi    (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> twi    (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> rough  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> curv   (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> rivDisc(static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> drainA (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> wsId   (static_cast<std::size_t>(totalT), 0);
        std::array<std::vector<uint8_t>, 6> livestockS;
        for (auto& v : livestockS) {
            v.assign(static_cast<std::size_t>(totalT), 0);
        }
        std::vector<uint8_t> fault  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> reefTr (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> mineS  (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> coalSm (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> sPh    (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> iceCov (static_cast<std::size_t>(totalT), 0);
        std::vector<uint8_t> hydPow (static_cast<std::size_t>(totalT), 0);

        // ---- TPI / TWI / ROUGHNESS / CURVATURE ----
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const std::size_t si = static_cast<std::size_t>(i);
                const int8_t myE = grid.elevation(i);
                int8_t maxE = myE;
                int8_t minE = myE;
                int32_t sumE = 0;
                int32_t nbCount = 0;
                int32_t lowerNb = 0;
                int32_t higherNb = 0;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    const int8_t nE = grid.elevation(nIdx);
                    if (nE > maxE) { maxE = nE; }
                    if (nE < minE) { minE = nE; }
                    sumE += nE;
                    ++nbCount;
                    if (nE < myE) { ++lowerNb; }
                    if (nE > myE) { ++higherNb; }
                }
                // Roughness = max-min in 1-hex window.
                rough[si] = static_cast<uint8_t>(
                    std::clamp((maxE - minE) * 60, 0, 255));
                // TPI: compare myE to mean of neighbours.
                if (nbCount > 0) {
                    const float meanE = static_cast<float>(sumE)
                        / static_cast<float>(nbCount);
                    const float diff = static_cast<float>(myE) - meanE;
                    if (diff > 0.5f)        { tpi[si] = 3; } // ridge/hilltop
                    else if (diff < -0.5f)  { tpi[si] = 1; } // valley
                    else if (rough[si] > 30){ tpi[si] = 2; } // slope
                    else                    { tpi[si] = 0; } // flat
                }
                // Curvature: more lower or higher neighbours.
                if (lowerNb >= 4)        { curv[si] = 2; } // convex
                else if (higherNb >= 4)  { curv[si] = 1; } // concave
                else                     { curv[si] = 0; }
                // TWI: low slope + concave = high water accumulation.
                {
                    int32_t w = 30;
                    if (curv[si] == 1) { w += 100; }
                    if (rough[si] < 40) { w += 60; }
                    if (grid.terrain(i) != aoc::map::TerrainType::Mountain
                        && grid.terrain(i) != aoc::map::TerrainType::Ocean
                        && grid.terrain(i) != aoc::map::TerrainType::ShallowWater) {
                        twi[si] = static_cast<uint8_t>(
                            std::clamp(w, 0, 255));
                    }
                }
            }
        }

        // ---- DRAINAGE BASIN AREA + RIVER DISCHARGE ----
        // Iterative accumulation: each tile's basin area = its own
        // contribution + sum of upstream tiles. Approximate via 6-pass
        // sweep using flowDir.
        std::vector<int32_t> basinAccum(
            static_cast<std::size_t>(totalT), 1);
        for (int32_t pass = 0; pass < 8; ++pass) {
            for (int32_t i = 0; i < totalT; ++i) {
                const auto& fd = grid.flowDir();
                const std::size_t si = static_cast<std::size_t>(i);
                if (fd.size() <= si || fd[si] == 0xFFu) { continue; }
                const int32_t row = i / width;
                const int32_t col = i % width;
                int32_t nIdx;
                if (!nbHelper(col, row, fd[si], nIdx)) { continue; }
                basinAccum[static_cast<std::size_t>(nIdx)] +=
                    basinAccum[si];
            }
        }
        for (int32_t i = 0; i < totalT; ++i) {
            const int32_t a = basinAccum[
                static_cast<std::size_t>(i)];
            // log scale to fit 0-255.
            const float la = std::log2(static_cast<float>(std::max(1, a)));
            drainA[static_cast<std::size_t>(i)] = static_cast<uint8_t>(
                std::clamp(la * 18.0f, 0.0f, 255.0f));
            if (grid.riverEdges(i) != 0) {
                rivDisc[static_cast<std::size_t>(i)] =
                    drainA[static_cast<std::size_t>(i)];
            }
        }

        // ---- WATERSHED ID ----
        // Cluster tiles by drainage destination. Walk flowDir until
        // we hit ocean/lake/sink, hash that endpoint to an id.
        for (int32_t i = 0; i < totalT; ++i) {
            const aoc::map::TerrainType t = grid.terrain(i);
            if (t == aoc::map::TerrainType::Ocean
                || t == aoc::map::TerrainType::ShallowWater) {
                continue;
            }
            // Walk flowDir up to 50 steps until water/sink.
            int32_t cur = i;
            for (int32_t step = 0; step < 50; ++step) {
                const auto& fd = grid.flowDir();
                const std::size_t scur = static_cast<std::size_t>(cur);
                if (fd.size() <= scur || fd[scur] == 0xFFu) { break; }
                const int32_t row = cur / width;
                const int32_t col = cur % width;
                int32_t nIdx;
                if (!nbHelper(col, row, fd[scur], nIdx)) { break; }
                cur = nIdx;
                const aoc::map::TerrainType nt = grid.terrain(cur);
                if (nt == aoc::map::TerrainType::Ocean
                    || nt == aoc::map::TerrainType::ShallowWater) {
                    break;
                }
            }
            // Hash endpoint to id 1-255.
            const uint32_t h = static_cast<uint32_t>(cur)
                * 2654435761u;
            wsId[static_cast<std::size_t>(i)] = static_cast<uint8_t>(
                1 + ((h >> 16) % 254));
        }

        // ---- PER-LIVESTOCK SUITABILITY ----
        // Cattle: temperate Plains/Grassland fertile.
        // Pig: forested temperate, NPP-rich.
        // Sheep: Hills temperate-cool.
        // Horse: Plains/Grassland steppe.
        // Goat: Hills/Mountain rocky cool.
        // Chicken: most arable land — broad suitability.
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const std::size_t si = static_cast<std::size_t>(i);
                const aoc::map::TerrainType t = grid.terrain(i);
                const aoc::map::FeatureType f = grid.feature(i);
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                const float fert = (si < soilFert.size())
                    ? soilFert[si] : 0.5f;
                // Cattle
                if ((t == aoc::map::TerrainType::Grassland
                     || t == aoc::map::TerrainType::Plains)
                    && lat > 0.20f && lat < 0.55f) {
                    livestockS[0][si] = static_cast<uint8_t>(
                        std::clamp(fert * 220.0f + 30.0f, 0.0f, 255.0f));
                }
                // Pig
                if ((f == aoc::map::FeatureType::Forest
                     || t == aoc::map::TerrainType::Grassland)
                    && lat < 0.55f) {
                    livestockS[1][si] = static_cast<uint8_t>(
                        std::clamp(fert * 180.0f + 50.0f, 0.0f, 255.0f));
                }
                // Sheep
                if (f == aoc::map::FeatureType::Hills
                    && lat > 0.30f && lat < 0.60f) {
                    livestockS[2][si] = static_cast<uint8_t>(
                        std::clamp(fert * 200.0f + 40.0f, 0.0f, 255.0f));
                }
                // Horse
                if (t == aoc::map::TerrainType::Plains
                    && lat > 0.30f && lat < 0.55f
                    && f == aoc::map::FeatureType::None) {
                    livestockS[3][si] = static_cast<uint8_t>(
                        std::clamp(fert * 200.0f + 50.0f, 0.0f, 255.0f));
                }
                // Goat
                if (f == aoc::map::FeatureType::Hills
                    || t == aoc::map::TerrainType::Mountain) {
                    livestockS[4][si] = static_cast<uint8_t>(
                        std::clamp(fert * 150.0f + 60.0f, 0.0f, 255.0f));
                }
                // Chicken — broad
                if (t == aoc::map::TerrainType::Plains
                    || t == aoc::map::TerrainType::Grassland) {
                    livestockS[5][si] = static_cast<uint8_t>(
                        std::clamp(fert * 180.0f + 50.0f, 0.0f, 255.0f));
                }
            }
        }

        // ---- ACTIVE / INACTIVE FAULT ----
        AOC_PARALLEL_FOR_ROWS
        for (int32_t i = 0; i < totalT; ++i) {
            const std::size_t si = static_cast<std::size_t>(i);
            const auto& sh = grid.seismicHazard();
            if (sh.size() <= si) { continue; }
            const uint8_t sev = sh[si] & 0x07;
            // Active fault: hazard ≥ 2 (transform) or 3 (subduction).
            if (sev >= 2) {
                fault[si] = 1;
            } else {
                // Inactive fossil fault: ophiolite tile (rockType=3)
                // not currently active.
                const auto& rk = grid.rockType();
                if (rk.size() > si && rk[si] == 3) {
                    fault[si] = 2;
                }
            }
        }

        // ---- REEF TERRACES (Quaternary glacioeustatic stairsteps) ----
        // Coastal land tiles between elev 1-2 in tropical-subtropical
        // zones host raised-reef terraces from past sea-level highstands.
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            if (lat > 0.35f) { continue; }
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const std::size_t si = static_cast<std::size_t>(i);
                const aoc::map::TerrainType t = grid.terrain(i);
                if (t == aoc::map::TerrainType::Ocean
                    || t == aoc::map::TerrainType::ShallowWater) {
                    continue;
                }
                const int8_t elev = grid.elevation(i);
                if (elev < 1 || elev > 2) { continue; }
                // Adjacent to water?
                bool nearWater = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nbHelper(col, row, d, nIdx)) { continue; }
                    const aoc::map::TerrainType nt = grid.terrain(nIdx);
                    if (nt == aoc::map::TerrainType::Ocean
                        || nt == aoc::map::TerrainType::ShallowWater) {
                        nearWater = true; break;
                    }
                }
                if (nearWater) {
                    reefTr[si] = static_cast<uint8_t>(elev * 80);
                }
            }
        }

        // ---- MINE SUITABILITY ----
        AOC_PARALLEL_FOR_ROWS
        for (int32_t i = 0; i < totalT; ++i) {
            const std::size_t si = static_cast<std::size_t>(i);
            if (!grid.resource(i).isValid()) { continue; }
            uint8_t bits = 0;
            const auto& sl = grid.slopeAngle();
            if (sl.size() > si) {
                if (sl[si] < 100) { bits |= 0x01; } // open-pit
                if (sl[si] >= 60) { bits |= 0x02; } // underground
            } else {
                bits = 0x03; // both default
            }
            mineS[si] = bits;
        }

        // ---- COAL SEAM THICKNESS ----
        // Sed basin tiles + coal lithology potential. If COAL resource
        // placed, thickness scales with sediment depth + lat (former
        // tropical equatorial swamps).
        AOC_PARALLEL_FOR_ROWS
        for (int32_t i = 0; i < totalT; ++i) {
            const std::size_t si = static_cast<std::size_t>(i);
            const auto& res = grid.resource(i);
            if (res.value != aoc::sim::goods::COAL) { continue; }
            int32_t thick = 60;
            if (i < static_cast<int32_t>(sediment.size())) {
                thick += static_cast<int32_t>(
                    sediment[si] * 600.0f);
            }
            const auto& ages = grid.crustAgeTile();
            if (ages.size() > si && ages[si] > 80.0f) {
                thick += 40;
            }
            coalSm[si] = static_cast<uint8_t>(
                std::clamp(thick, 0, 255));
        }

        // ---- SOIL pH ----
        // Map by USDA soil order:
        //   oxisol/ultisol → acidic (~5.5)
        //   spodosol → very acidic (~4.5)
        //   mollisol/alfisol → near neutral (~6.5-7.0)
        //   aridisol → alkaline (~8.0)
        //   vertisol → slightly alkaline (~7.5)
        //   andisol → slightly acidic (~6.0)
        //   gelisol → neutral (~6.5)
        //   histosol → very acidic (~4.5)
        AOC_PARALLEL_FOR_ROWS
        for (int32_t i = 0; i < totalT; ++i) {
            const std::size_t si = static_cast<std::size_t>(i);
            const auto& so = grid.soilOrder();
            if (so.size() <= si) { continue; }
            uint8_t ph = 0;
            switch (so[si]) {
                case 4:  ph = 75;  break; // oxisol pH 5.5
                case 5:  ph = 75;  break; // ultisol pH 5.5
                case 7:  ph = 25;  break; // spodosol pH 4.5
                case 11: ph = 25;  break; // histosol pH 4.5
                case 3:  ph = 130; break; // mollisol pH 6.5
                case 6:  ph = 140; break; // alfisol pH 6.7
                case 8:  ph = 200; break; // aridisol pH 8.0
                case 9:  ph = 175; break; // vertisol pH 7.5
                case 10: ph = 100; break; // andisol pH 6.0
                case 12: ph = 130; break; // gelisol pH 6.5
                case 1:  ph = 130; break; // entisol neutral
                case 2:  ph = 130; break; // inceptisol neutral
                default: ph = 130; break;
            }
            sPh[si] = ph;
        }

        // ---- ICE-COVER DURATION (lakes) ----
        AOC_PARALLEL_FOR_ROWS
        for (int32_t row = 0; row < height; ++row) {
            const float ny = static_cast<float>(row) / static_cast<float>(height);
            const float lat = 2.0f * std::abs(ny - 0.5f);
            for (int32_t col = 0; col < width; ++col) {
                const int32_t i = row * width + col;
                const std::size_t si = static_cast<std::size_t>(i);
                if (lakeFlag[si] == 0) { continue; }
                int32_t months = 0;
                if (lat > 0.85f)      { months = 12; }
                else if (lat > 0.70f) { months = 8;  }
                else if (lat > 0.55f) { months = 5;  }
                else if (lat > 0.40f) { months = 2;  }
                iceCov[si] = static_cast<uint8_t>(months * 21);
            }
        }

        // ---- HYDROPOWER CAPACITY ----
        // Composite: dam-site score × river discharge × upstream area.
        AOC_PARALLEL_FOR_ROWS
        for (int32_t i = 0; i < totalT; ++i) {
            const std::size_t si = static_cast<std::size_t>(i);
            if (grid.riverEdges(i) == 0) { continue; }
            int32_t cap = 0;
            const auto& sl = grid.slopeAngle();
            if (sl.size() > si) { cap += static_cast<int32_t>(sl[si]); }
            if (rivDisc[si] > 0) { cap += rivDisc[si]; }
            cap = cap / 2;
            hydPow[si] = static_cast<uint8_t>(
                std::clamp(cap, 0, 255));
        }

        grid.setTopoPositionIndex(std::move(tpi));
        grid.setTopoWetnessIndex(std::move(twi));
        grid.setRoughness(std::move(rough));
        grid.setCurvature(std::move(curv));
        grid.setRiverDischarge(std::move(rivDisc));
        grid.setDrainageBasinArea(std::move(drainA));
        grid.setWatershedId(std::move(wsId));
        for (int32_t k = 0; k < 6; ++k) {
            grid.setLivestockSuit(k, std::move(livestockS[k]));
        }
        grid.setFaultTrace(std::move(fault));
        grid.setReefTerrace(std::move(reefTr));
        grid.setMineSuitability(std::move(mineS));
        grid.setCoalSeamThickness(std::move(coalSm));
        grid.setSoilPh(std::move(sPh));
        grid.setIceCoverDuration(std::move(iceCov));
        grid.setHydropowerCapacity(std::move(hydPow));

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

    // Lake / inland-sea purge. Connected-component flood-fill on WATER
    // tiles. Components below MIN_LAKE_SIZE that don't touch the map
    // border are filled with land (Plains, sediment-deposited basins).
    // Real Earth has a few large internal seas (Caspian, Black, Aral)
    // but most "lakes" are small. Our generator was producing a swiss-
    // cheese pattern of mid-size internal seas inside continents from
    // the noise dipping under waterThreshold. Purging components
    // smaller than the threshold consolidates landmasses.
    {
        constexpr int32_t MIN_LAKE_SIZE = 60; // tiles smaller than this fill in
        std::vector<int32_t> lakeId(static_cast<std::size_t>(width * height), -1);
        std::vector<int32_t> lakeQueue;
        lakeQueue.reserve(static_cast<std::size_t>(width * height));
        int32_t nextLake = 0;
        std::vector<int32_t> lakeSize;
        std::vector<bool>    lakeTouchesEdge;
        for (int32_t i = 0; i < width * height; ++i) {
            if (lakeId[static_cast<std::size_t>(i)] >= 0) { continue; }
            if (!isWater(grid.terrain(i))) { continue; }
            lakeId[static_cast<std::size_t>(i)] = nextLake;
            int32_t size = 0;
            bool touchesEdge = false;
            lakeQueue.clear();
            lakeQueue.push_back(i);
            while (!lakeQueue.empty()) {
                const int32_t idx = lakeQueue.back();
                lakeQueue.pop_back();
                ++size;
                const int32_t col = idx % width;
                const int32_t row = idx / width;
                if (row == 0 || row == height - 1) { touchesEdge = true; }
                if (!cylSim && (col == 0 || col == width - 1)) {
                    touchesEdge = true;
                }
                const hex::AxialCoord ax = hex::offsetToAxial({col, row});
                for (const hex::AxialCoord& n : hex::neighbors(ax)) {
                    if (!grid.isValid(n)) { touchesEdge = true; continue; }
                    const int32_t ni = grid.toIndex(n);
                    if (lakeId[static_cast<std::size_t>(ni)] >= 0) { continue; }
                    if (!isWater(grid.terrain(ni))) { continue; }
                    lakeId[static_cast<std::size_t>(ni)] = nextLake;
                    lakeQueue.push_back(ni);
                }
            }
            lakeSize.push_back(size);
            lakeTouchesEdge.push_back(touchesEdge);
            ++nextLake;
        }
        // Fill in all water components that DON'T touch any map edge
        // and are smaller than the threshold. Uses Plains terrain as a
        // sediment-deposited filled basin.
        for (int32_t i = 0; i < width * height; ++i) {
            const int32_t cid = lakeId[static_cast<std::size_t>(i)];
            if (cid < 0) { continue; }
            if (lakeTouchesEdge[static_cast<std::size_t>(cid)]) { continue; }
            if (lakeSize[static_cast<std::size_t>(cid)] < MIN_LAKE_SIZE) {
                grid.setTerrain(i, TerrainType::Plains);
                grid.setElevation(i, 0);
            }
        }
    }

    // Coastal-arm erosion. Two passes of cellular-automaton smoothing:
    // any land tile with ≥ 4 water neighbours drowns. Removes 1-2 hex
    // wide peninsulas, fjord-like protrusions, and bay-fingers,
    // leaving more compact continent silhouettes. Two passes peel off
    // thin tendrils without nibbling rounded coastlines (which have
    // ≤ 3 water neighbours per tile). Mountains exempt — they are
    // structurally locked by orogeny and shouldn't be eroded by
    // coastal smoothing alone.
    // Two CA passes: peel hair-thin tendrils, then peel anything STILL
    // attached by a bridge. Mountains exempt (locked by orogeny).
    // First pass kills 1-tile peninsulas and arms; second peels the
    // 2-tile-wide stubs that get exposed by the first pass.
    for (int32_t pass = 0; pass < 2; ++pass) {
        std::vector<int32_t> drown;
        drown.reserve(static_cast<std::size_t>(width * height) / 16);
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t idx = row * width + col;
                const TerrainType t = grid.terrain(idx);
                if (isWater(t) || t == TerrainType::Mountain) { continue; }
                int32_t waterCount = 0;
                int32_t validCount = 0;
                const hex::AxialCoord ax = hex::offsetToAxial({col, row});
                for (const hex::AxialCoord& n : hex::neighbors(ax)) {
                    if (!grid.isValid(n)) {
                        ++waterCount; ++validCount; // map edge ≈ open water
                        continue;
                    }
                    ++validCount;
                    if (isWater(grid.terrain(grid.toIndex(n)))) {
                        ++waterCount;
                    }
                }
                if (validCount > 0 && waterCount >= 4) {
                    drown.push_back(idx);
                }
            }
        }
        for (int32_t i : drown) {
            grid.setTerrain(i, TerrainType::Ocean);
            grid.setElevation(i, -1);
            grid.setFeature(i, FeatureType::None);
        }
        if (drown.empty()) { break; }
    }
}

void MapGenerator::smoothCoastlines(HexGrid& grid) {
    // BFS from land tiles outward into the ocean. Each water tile gets
    // a per-tile shelf depth threshold sampled from a low-frequency
    // noise field, so the continental-shelf width varies along the
    // coast (1-5 hex steps, mean 3) instead of being a uniform 3-tile
    // ring. Real continental shelves vary from <50 km (Pacific NW) to
    // >1000 km (Patagonian shelf, Siberian shelf) — this captures
    // similar variation procedurally.
    constexpr int32_t SHALLOW_BFS_MAX = 4; // BFS depth limit

    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t total  = width * height;

    std::vector<int32_t> distFromLand(static_cast<std::size_t>(total), -1);
    std::vector<int32_t> queue;
    queue.reserve(static_cast<std::size_t>(total));

    // Seed BFS with land tiles (dist 0).
    for (int32_t i = 0; i < total; ++i) {
        if (!isWater(grid.terrain(i))) {
            distFromLand[static_cast<std::size_t>(i)] = 0;
            queue.push_back(i);
        }
    }

    // BFS flood into water tiles up to the maximum possible threshold.
    for (std::size_t h = 0; h < queue.size(); ++h) {
        const int32_t idx = queue[h];
        const int32_t d   = distFromLand[static_cast<std::size_t>(idx)];
        if (d >= SHALLOW_BFS_MAX) { continue; }
        const int32_t col  = idx % width;
        const int32_t row  = idx / width;
        const hex::AxialCoord axial = hex::offsetToAxial({col, row});
        for (const hex::AxialCoord& n : hex::neighbors(axial)) {
            if (!grid.isValid(n)) { continue; }
            const int32_t ni = grid.toIndex(n);
            if (distFromLand[static_cast<std::size_t>(ni)] >= 0) { continue; }
            distFromLand[static_cast<std::size_t>(ni)] = d + 1;
            queue.push_back(ni);
        }
    }

    // Per-tile threshold via low-frequency noise. Seeded from grid size
    // so the same world has the same shelf pattern run-to-run.
    aoc::Random shelfRng(static_cast<uint64_t>(width) * 9176u
                         + static_cast<uint64_t>(height) * 31337u);
    for (int32_t i = 0; i < total; ++i) {
        if (!isWater(grid.terrain(i))) { continue; }
        const int32_t d = distFromLand[static_cast<std::size_t>(i)];
        if (d <= 0) { continue; }
        const int32_t col = i % width;
        const int32_t row = i / width;
        const float nx = static_cast<float>(col) / static_cast<float>(width);
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        // Higher-freq noise (5 vs 3) gives more local variation so
        // shelves actually fluctuate along a single coastline instead
        // of being uniform-wide for long stretches.
        const float n = fractalNoise(nx * 5.0f, ny * 5.0f, 3, 2.0f, 0.5f, shelfRng);
        // Bias toward narrow shelves: square the noise so most of the
        // distribution falls below 0.5. Range still [1,4] but mean ~2,
        // matching real coastlines where wide continental shelves are
        // rare (Patagonian, Siberian) and most coasts have <50 km.
        const float biased = n * n;
        const int32_t thresh = std::clamp(
            1 + static_cast<int32_t>(biased * 3.999f), 1, SHALLOW_BFS_MAX);
        if (d <= thresh) {
            grid.setTerrain(i, TerrainType::ShallowWater);
        } else {
            grid.setTerrain(i, TerrainType::Ocean);
        }
    }
}

void MapGenerator::assignFeatures(const Config& config, HexGrid& grid, aoc::Random& rng) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();

    aoc::Random featureRng(rng.next());

    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            int32_t index = row * width + col;
            TerrainType terrain = grid.terrain(index);

            // Skip water and mountains
            if (isWater(terrain) || terrain == TerrainType::Mountain) {
                continue;
            }

            // Hills feature is now placed by the orogeny pass in
            // assignTerrain (tied to actual mountain root rock).
            // Random Hills sprinkling scattered them across plains
            // unrelated to geology — disabled.

            // Forest / Jungle placement via low-frequency noise so
            // neighboring tiles share their forest status — produces
            // coherent forest/jungle patches instead of random
            // scattered tiles. Jungle = tropical-only band; Forest =
            // outside that band.
            if (terrain == TerrainType::Grassland || terrain == TerrainType::Plains) {
                const float fnx = static_cast<float>(col) / static_cast<float>(width);
                const float fny = static_cast<float>(row) / static_cast<float>(height);
                aoc::Random forestNoiseRng(featureRng);
                const float forestN = fractalNoise(
                    fnx * 4.5f + 11.0f, fny * 4.5f + 7.0f,
                    3, 2.0f, 0.5f, forestNoiseRng);
                // Threshold tuned so ~30 % of land in Grassland/Plains
                // gets a forest patch. Higher forestRatio = more cover.
                const float thresh = std::clamp(
                    1.0f - config.forestRatio * 1.5f, 0.30f, 0.85f);
                if (forestN > thresh) {
                    float latitudeT = static_cast<float>(row) / static_cast<float>(height);
                    float temperature = 1.0f - 2.0f * std::abs(latitudeT - 0.5f);
                    if (temperature > 0.70f && terrain == TerrainType::Grassland) {
                        grid.setFeature(index, FeatureType::Jungle);
                    } else {
                        grid.setFeature(index, FeatureType::Forest);
                    }
                    continue;
                }
            }

            // Tundra can have sparse forest
            if (terrain == TerrainType::Tundra && featureRng.chance(0.10f)) {
                grid.setFeature(index, FeatureType::Forest);
                continue;
            }

            // Desert floodplains (rare)
            if (terrain == TerrainType::Desert && featureRng.chance(0.03f)) {
                grid.setFeature(index, FeatureType::Floodplains);
                continue;
            }

            // Oasis (very rare, desert only)
            if (terrain == TerrainType::Desert && featureRng.chance(0.02f)) {
                grid.setFeature(index, FeatureType::Oasis);
                continue;
            }

            // Marsh (grassland near water)
            if (terrain == TerrainType::Grassland && featureRng.chance(0.05f)) {
                // Check if near water
                hex::AxialCoord axial = hex::offsetToAxial({col, row});
                std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(axial);
                bool nearWater = false;
                for (const hex::AxialCoord& n : nbrs) {
                    if (grid.isValid(n) && isWater(grid.terrain(grid.toIndex(n)))) {
                        nearWater = true;
                        break;
                    }
                }
                if (nearWater) {
                    grid.setFeature(index, FeatureType::Marsh);
                }
            }
        }
    }
}

void MapGenerator::generateRivers(HexGrid& grid, aoc::Random& rng) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();

    // Rivers must originate at a mountain or hill and flow monotonically to
    // a water tile.  Previous impl accepted any land with elevation >= 1 and
    // committed edges even when the descent dead-ended inland, producing
    // floating midland river stubs that the rotation-fixed edge renderer now
    // exposed clearly.
    //
    // Algorithm:
    //   1. Collect candidate source tiles (Mountain terrain OR Hills feature).
    //   2. For each source: build a path following strict descent + tie-break
    //      by highest distance-to-water heuristic.
    //   3. Only commit the path's river edges if the path terminates at water.
    //      A failed path contributes zero river edges.

    // Pre-pass: distance-to-water BFS so we can tie-break toward outlets.
    std::vector<int32_t> distToWater(static_cast<size_t>(grid.tileCount()), -1);
    std::vector<int32_t> bfsQueue;
    bfsQueue.reserve(static_cast<size_t>(grid.tileCount()));
    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        if (isWater(grid.terrain(i))) {
            distToWater[static_cast<size_t>(i)] = 0;
            bfsQueue.push_back(i);
        }
    }
    for (size_t qh = 0; qh < bfsQueue.size(); ++qh) {
        const int32_t idx = bfsQueue[qh];
        const int32_t d = distToWater[static_cast<size_t>(idx)];
        const hex::AxialCoord c = grid.toAxial(idx);
        const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(c);
        for (const hex::AxialCoord& n : nbrs) {
            if (!grid.isValid(n)) { continue; }
            const int32_t ni = grid.toIndex(n);
            if (distToWater[static_cast<size_t>(ni)] >= 0) { continue; }
            // BFS expands through land INCLUDING mountains.  Rivers originate
            // at mountain/hill and flow to water, so mountain tiles must
            // receive a finite distance or the source filter rejects them all
            // (Mountain is unit-impassable but rivers ignore that).
            distToWater[static_cast<size_t>(ni)] = d + 1;
            bfsQueue.push_back(ni);
        }
    }

    // Gather river sources.
    std::vector<int32_t> sources;
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx = row * width + col;
            const TerrainType t = grid.terrain(idx);
            if (isWater(t) || isImpassable(t)) { continue; }
            // Civ-6 behaviour: rivers spring only from mountain tiles.
            // Earlier we also accepted Hills features but that produced
            // springs on plain-with-hills tiles, which read as "river
            // appears on a plain". Restricting to Mountain makes the
            // origin always read as a mountain on the map.
            if (t != TerrainType::Mountain) { continue; }
            // Needs a reachable outlet.  Islands of hills with no BFS path to
            // water are disqualified.
            if (distToWater[static_cast<size_t>(idx)] < 1) { continue; }
            sources.push_back(idx);
        }
    }
    if (sources.empty()) { return; }

    // Target density: ~1 river per 200 tiles, capped to source count.
    // Target density: ~1 river per 60 tiles.  Previous 1-per-200 was three
    // times too sparse, and strict ±2 zigzag rejects ~40-60% of candidate
    // sources (paths dead-end before reaching water), so the effective
    // committed count comes out well below even the nominal target.  Over-
    // provision sources 3x so we end up with roughly the real-world density
    // of "most medium landmasses touch 2-3 rivers".
    const int32_t desiredRivers = std::max(3, grid.tileCount() / 60);
    const int32_t riverCount = std::min(desiredRivers, static_cast<int32_t>(sources.size()));

    // Shuffle sources so the first N picks are random.
    for (size_t i = sources.size(); i > 1; --i) {
        const size_t j = static_cast<size_t>(rng.nextInt(0, static_cast<int32_t>(i) - 1));
        std::swap(sources[i - 1], sources[j]);
    }

    struct Step { int32_t tileIndex; int32_t direction; };

    // BFS over (tile, prevDir) state-space with strict ±2 transitions.  Each
    // state represents "we are standing on `tile`, and the last step into
    // this tile used direction `prevDir` (from the previous tile to this
    // tile)".  A step is legal iff its direction satisfies (dir - prevDir)
    // mod 6 ∈ {2, 4} — that constraint guarantees the two river-edges on
    // any intermediate tile share a vertex, producing a Civ 6-style
    // continuous curve instead of opposite-side bars.
    //
    // Starting state has prevDir = -1 (no constraint on first step).  Path
    // must arrive at a water tile.  Elevation is used only as a cost term
    // (monotone descent is softly preferred via non-decreasing distance-
    // to-water), so any geometrically valid ±2 path to water is accepted.
    auto findPath = [&](int32_t startIdx, std::vector<Step>& outPath) -> bool {
        outPath.clear();
        const int32_t tc = grid.tileCount();
        // parent[tile * 7 + (prevDir+1)] = encoded prev state + incoming dir.
        // We allocate 7 slots per tile (prevDir ∈ {-1, 0..5}).
        constexpr int32_t DIR_SLOTS = 7;
        const size_t stateCount = static_cast<size_t>(tc) * DIR_SLOTS;
        std::vector<int32_t> parentTile(stateCount, -1);
        std::vector<int8_t>  parentSlot(stateCount, -1);
        std::vector<int8_t>  incomingDir(stateCount, -1);
        std::vector<uint8_t> visited(stateCount, 0);

        auto slot = [](int32_t prevDir) -> int32_t {
            return prevDir + 1;  // -1 → 0, 0..5 → 1..6
        };

        std::vector<std::pair<int32_t, int32_t>> queue;  // (tileIdx, prevDir)
        queue.reserve(static_cast<size_t>(tc));
        const size_t startState = static_cast<size_t>(startIdx) * DIR_SLOTS + static_cast<size_t>(slot(-1));
        visited[startState] = 1;
        queue.emplace_back(startIdx, -1);

        int32_t foundTile = -1;
        int32_t foundPrev = -1;

        for (size_t head = 0; head < queue.size(); ++head) {
            const int32_t curTile = queue[head].first;
            const int32_t curPrev = queue[head].second;
            const hex::AxialCoord c = grid.toAxial(curTile);
            const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(c);

            for (int dir = 0; dir < 6; ++dir) {
                if (curPrev >= 0) {
                    const int32_t diff = ((dir - curPrev) % 6 + 6) % 6;
                    if (diff != 2 && diff != 4) { continue; }
                }
                const hex::AxialCoord& n = nbrs[static_cast<size_t>(dir)];
                if (!grid.isValid(n)) { continue; }
                const int32_t nIdx = grid.toIndex(n);
                const TerrainType nt = grid.terrain(nIdx);
                if (isImpassable(nt) && !isWater(nt)) { continue; }

                const size_t nState = static_cast<size_t>(nIdx) * DIR_SLOTS + static_cast<size_t>(slot(dir));
                if (visited[nState]) { continue; }
                visited[nState] = 1;
                parentTile[nState]  = curTile;
                parentSlot[nState]  = static_cast<int8_t>(slot(curPrev));
                incomingDir[nState] = static_cast<int8_t>(dir);

                if (isWater(nt)) {
                    foundTile = nIdx;
                    foundPrev = dir;
                    break;
                }
                queue.emplace_back(nIdx, dir);
            }
            if (foundTile >= 0) { break; }
        }

        if (foundTile < 0) { return false; }

        // Reconstruct path by walking parent pointers backwards.
        std::vector<Step> reversePath;
        int32_t curT = foundTile;
        int32_t curS = slot(foundPrev);
        while (true) {
            const size_t s = static_cast<size_t>(curT) * DIR_SLOTS + static_cast<size_t>(curS);
            const int32_t pT = parentTile[s];
            if (pT < 0) { break; }
            const int8_t  inDir = incomingDir[s];
            reversePath.push_back({pT, inDir});
            curT = pT;
            curS = parentSlot[s];
        }
        outPath.assign(reversePath.rbegin(), reversePath.rend());
        return true;
    };

    int32_t committedRivers = 0;
    int32_t rejectedRivers  = 0;
    for (int32_t r = 0; r < riverCount; ++r) {
        const int32_t startIndex = sources[static_cast<size_t>(r)];
        std::vector<Step> path;
        if (!findPath(startIndex, path)) { ++rejectedRivers; continue; }
        if (path.empty())                { ++rejectedRivers; continue; }

        // Hard sanity: the final committed boundary must actually touch water.
        // findPath guarantees this (BFS success case), but double-check here
        // so a silent future bug cannot leave dry-land rivers.
        const Step& last = path.back();
        const hex::AxialCoord lastC = grid.toAxial(last.tileIndex);
        const hex::AxialCoord lastN = hex::neighbors(lastC)[static_cast<size_t>(last.direction)];
        if (!grid.isValid(lastN)) { ++rejectedRivers; continue; }
        const int32_t lastNIdx = grid.toIndex(lastN);
        if (!isWater(grid.terrain(lastNIdx))) { ++rejectedRivers; continue; }

        for (const Step& s : path) {
            const hex::AxialCoord c = grid.toAxial(s.tileIndex);
            const hex::AxialCoord n = hex::neighbors(c)[static_cast<size_t>(s.direction)];
            const int32_t nIdx = grid.toIndex(n);

            // Realism guard: a river edge on the boundary land↔water IS the
            // coastline. Civ-6 rivers spill into the ocean without painting
            // an extra segment on the shore. Skip those edges so the river
            // visually terminates at the last land tile.
            if (isWater(grid.terrain(nIdx))) {
                continue;
            }

            uint8_t edges = grid.riverEdges(s.tileIndex);
            edges |= static_cast<uint8_t>(1u << s.direction);
            grid.setRiverEdges(s.tileIndex, edges);

            const int reverseDir = (s.direction + 3) % 6;
            uint8_t neighborEdges = grid.riverEdges(nIdx);
            neighborEdges |= static_cast<uint8_t>(1u << reverseDir);
            grid.setRiverEdges(nIdx, neighborEdges);
        }
        ++committedRivers;
        LOG_INFO("River committed: source=(%d,%d) outlet=(%d,%d) terrain=%.*s steps=%zu",
                 grid.toAxial(startIndex).q, grid.toAxial(startIndex).r,
                 lastN.q, lastN.r,
                 static_cast<int>(terrainName(grid.terrain(lastNIdx)).size()),
                 terrainName(grid.terrain(lastNIdx)).data(),
                 path.size());
    }
    LOG_INFO("River generation: %d committed, %d rejected (of %d sources tried)",
             committedRivers, rejectedRivers, riverCount);
}

void MapGenerator::placeNaturalWonders(HexGrid& grid, aoc::Random& rng) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();

    // Place 3-5 natural wonders at appropriate terrain locations
    const int32_t wonderCount = rng.nextInt(3, 5);

    // Candidate wonders (skip None and Count)
    constexpr uint8_t WONDER_TYPE_COUNT = static_cast<uint8_t>(NaturalWonderType::Count) - 1;

    // Track placed wonder positions for minimum distance enforcement
    std::vector<hex::AxialCoord> placedPositions;
    constexpr int32_t MIN_WONDER_DISTANCE = 10;

    int32_t placed = 0;
    for (int32_t w = 0; w < wonderCount && placed < wonderCount; ++w) {
        // Pick a wonder type (cycle through available types)
        NaturalWonderType wonderType = static_cast<NaturalWonderType>(
            (static_cast<uint8_t>(w) % WONDER_TYPE_COUNT) + 1);

        // Try to find a valid tile
        for (int32_t attempt = 0; attempt < 200; ++attempt) {
            const int32_t col = rng.nextInt(3, width - 4);
            const int32_t row = rng.nextInt(3, height - 4);
            const int32_t index = row * width + col;
            const TerrainType terrain = grid.terrain(index);
            const FeatureType feature = grid.feature(index);

            // Already has a wonder
            if (grid.naturalWonder(index) != NaturalWonderType::None) {
                continue;
            }

            // Check terrain suitability per wonder type
            bool suitable = false;
            switch (wonderType) {
                case NaturalWonderType::MountainOfGods:
                    suitable = (terrain == TerrainType::Mountain);
                    break;
                case NaturalWonderType::GrandCanyon:
                    suitable = (terrain == TerrainType::Desert || terrain == TerrainType::Plains);
                    break;
                case NaturalWonderType::GreatBarrierReef:
                    suitable = (terrain == TerrainType::Coast);
                    break;
                case NaturalWonderType::KillerVolcano:
                    suitable = (terrain == TerrainType::Mountain);
                    break;
                case NaturalWonderType::SacredForest:
                    suitable = (feature == FeatureType::Forest || feature == FeatureType::Jungle);
                    break;
                case NaturalWonderType::CrystalCave:
                    suitable = (feature == FeatureType::Hills ||
                                terrain == TerrainType::Plains ||
                                terrain == TerrainType::Grassland);
                    break;
                default:
                    break;
            }

            if (!suitable) {
                continue;
            }

            // Check minimum distance from other wonders
            const hex::AxialCoord candidate = hex::offsetToAxial({col, row});
            bool tooClose = false;
            for (const hex::AxialCoord& prev : placedPositions) {
                if (grid.distance(candidate, prev) < MIN_WONDER_DISTANCE) {
                    tooClose = true;
                    break;
                }
            }
            if (tooClose) {
                continue;
            }

            // Place the wonder: clear features and improvements
            grid.setNaturalWonder(index, wonderType);
            if (wonderType != NaturalWonderType::SacredForest) {
                grid.setFeature(index, FeatureType::None);
            }
            grid.setImprovement(index, ImprovementType::None);

            placedPositions.push_back(candidate);
            ++placed;

            LOG_INFO("Placed natural wonder %.*s at (%d,%d)",
                     static_cast<int>(naturalWonderName(wonderType).size()),
                     naturalWonderName(wonderType).data(),
                     col, row);
            break;
        }
    }
}

// ============================================================================
// Realistic map: tectonic plate simulation
// ============================================================================

/// Boundary type between two tectonic plates.
enum class BoundaryType : uint8_t {
    None,
    Convergent,
    Divergent,
    Transform,
};

void MapGenerator::generateRealisticTerrain(const Config& config, HexGrid& grid,
                                             aoc::Random& rng) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t tileCount = width * height;

    // ---- Step 1: Generate Tectonic Plates as Voronoi regions ----

    aoc::Random plateRng(rng.next());
    const int32_t plateCount = plateRng.nextInt(4, 6);

    struct PlateSeed {
        float x;       ///< Normalized [0,1]
        float y;       ///< Normalized [0,1]
        float driftX;  ///< Drift vector X component
        float driftY;  ///< Drift vector Y component
    };

    std::vector<PlateSeed> plates(static_cast<std::size_t>(plateCount));
    for (int32_t p = 0; p < plateCount; ++p) {
        plates[static_cast<std::size_t>(p)].x = plateRng.nextFloat(0.05f, 0.95f);
        plates[static_cast<std::size_t>(p)].y = plateRng.nextFloat(0.05f, 0.95f);
        // Random drift direction (unit-ish vector)
        const float angle = plateRng.nextFloat(0.0f, 6.28318f);
        plates[static_cast<std::size_t>(p)].driftX = std::cos(angle);
        plates[static_cast<std::size_t>(p)].driftY = std::sin(angle);
    }

    // Assign each tile to nearest plate (Voronoi)
    std::vector<int32_t> plateId(static_cast<std::size_t>(tileCount));

    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const float nx = static_cast<float>(col) / static_cast<float>(width);
            const float ny = static_cast<float>(row) / static_cast<float>(height);

            float bestDist = 1e9f;
            int32_t bestPlate = 0;
            for (int32_t p = 0; p < plateCount; ++p) {
                const float dx = nx - plates[static_cast<std::size_t>(p)].x;
                const float dy = ny - plates[static_cast<std::size_t>(p)].y;
                const float dist = dx * dx + dy * dy;
                if (dist < bestDist) {
                    bestDist = dist;
                    bestPlate = p;
                }
            }
            plateId[static_cast<std::size_t>(row * width + col)] = bestPlate;
        }
    }

    // ---- Step 2: Classify plate boundaries ----

    std::vector<BoundaryType> boundary(static_cast<std::size_t>(tileCount), BoundaryType::None);

    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t index = row * width + col;
            const int32_t myPlate = plateId[static_cast<std::size_t>(index)];
            const hex::AxialCoord axial = hex::offsetToAxial({col, row});
            const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(axial);

            for (const hex::AxialCoord& n : nbrs) {
                if (!grid.isValid(n)) {
                    continue;
                }
                const int32_t nIndex = grid.toIndex(n);
                const int32_t nPlate = plateId[static_cast<std::size_t>(nIndex)];

                if (nPlate == myPlate) {
                    continue;
                }

                // Determine boundary type from drift vectors
                const PlateSeed& myP = plates[static_cast<std::size_t>(myPlate)];
                const PlateSeed& nP  = plates[static_cast<std::size_t>(nPlate)];

                // Direction from my plate seed toward neighbor plate seed
                const float towardX = nP.x - myP.x;
                const float towardY = nP.y - myP.y;

                // Dot product of my drift with toward-vector
                const float dotMy = myP.driftX * towardX + myP.driftY * towardY;
                // Dot product of neighbor drift with reverse direction
                const float dotN  = nP.driftX * (-towardX) + nP.driftY * (-towardY);

                if (dotMy > 0.0f && dotN > 0.0f) {
                    // Both moving toward each other -> convergent
                    boundary[static_cast<std::size_t>(index)] = BoundaryType::Convergent;
                } else if (dotMy < 0.0f && dotN < 0.0f) {
                    // Both moving apart -> divergent
                    boundary[static_cast<std::size_t>(index)] = BoundaryType::Divergent;
                } else {
                    // Mixed -> transform
                    boundary[static_cast<std::size_t>(index)] = BoundaryType::Transform;
                }
                break;  // Only need to detect one neighbor on different plate
            }
        }
    }

    // ---- Step 3: Generate elevation from tectonics + noise ----

    aoc::Random noiseRng(rng.next());
    std::vector<float> elevationMap(static_cast<std::size_t>(tileCount));

    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t index = row * width + col;
            const float nx = static_cast<float>(col) / static_cast<float>(width);
            const float ny = static_cast<float>(row) / static_cast<float>(height);

            float elev = fractalNoise(nx, ny, 6, 3.0f, 0.5f, noiseRng);

            // Apply tectonic elevation bias
            switch (boundary[static_cast<std::size_t>(index)]) {
                case BoundaryType::Convergent:
                    elev += 0.4f;
                    break;
                case BoundaryType::Divergent:
                    elev -= 0.2f;
                    break;
                case BoundaryType::Transform:
                    elev += 0.1f;
                    break;
                case BoundaryType::None:
                    // Continental interior: slight positive bias
                    elev += 0.05f;
                    break;
            }

            elevationMap[static_cast<std::size_t>(index)] = elev;
        }
    }

    // Sort elevations to find the water threshold
    std::vector<float> sortedElevations(elevationMap);
    std::sort(sortedElevations.begin(), sortedElevations.end());
    const std::size_t waterCutoff = static_cast<std::size_t>(
        config.waterRatio * static_cast<float>(sortedElevations.size()));
    const float waterThreshold = sortedElevations[std::min(waterCutoff, sortedElevations.size() - 1)];

    // Mountain threshold
    const std::size_t mountainCutoff = sortedElevations.size() -
        static_cast<std::size_t>(config.mountainRatio * static_cast<float>(sortedElevations.size()));
    const float mountainThreshold = sortedElevations[std::min(mountainCutoff, sortedElevations.size() - 1)];

    // ---- Step 4: Water + mountain pass ----
    // Biome classification is deferred to a second pass so the precipitation
    // model can see the final mountain/ocean distribution (rain shadow +
    // ocean-proximity moisture).

    std::vector<bool> isLand(static_cast<std::size_t>(tileCount), false);
    std::vector<bool> isMountain(static_cast<std::size_t>(tileCount), false);

    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t index = row * width + col;
            const float elev = elevationMap[static_cast<std::size_t>(index)];

            if (elev < waterThreshold) {
                if (elev < waterThreshold * 0.6f) {
                    grid.setTerrain(index, TerrainType::Ocean);
                } else {
                    grid.setTerrain(index, TerrainType::Coast);
                }
                grid.setElevation(index, -1);
                continue;
            }

            if (elev >= mountainThreshold) {
                grid.setTerrain(index, TerrainType::Mountain);
                grid.setElevation(index, 3);
                isMountain[static_cast<std::size_t>(index)] = true;
                continue;
            }

            isLand[static_cast<std::size_t>(index)] = true;
            grid.setElevation(index, static_cast<int8_t>(
                std::clamp(static_cast<int>(elev * 4.0f), 0, 2)));
        }
    }

    // ---- Step 5: Distance to ocean (BFS over land) ----
    // Continental interior tiles sit many hexes from moisture; classic
    // world-builder treats this as a linear dryness term.

    constexpr int32_t OCEAN_DIST_CAP = 24;
    std::vector<int32_t> distToOcean(static_cast<std::size_t>(tileCount), OCEAN_DIST_CAP);
    std::vector<int32_t> bfsQueue;
    bfsQueue.reserve(static_cast<std::size_t>(tileCount));

    for (int32_t i = 0; i < tileCount; ++i) {
        const TerrainType tt = grid.terrain(i);
        if (tt == TerrainType::Ocean || tt == TerrainType::Coast) {
            distToOcean[static_cast<std::size_t>(i)] = 0;
            bfsQueue.push_back(i);
        }
    }

    for (std::size_t head = 0; head < bfsQueue.size(); ++head) {
        const int32_t cur = bfsQueue[head];
        const int32_t curDist = distToOcean[static_cast<std::size_t>(cur)];
        if (curDist >= OCEAN_DIST_CAP) { continue; }
        const hex::AxialCoord axial = hex::offsetToAxial(
            {cur % width, cur / width});
        for (const hex::AxialCoord& n : hex::neighbors(axial)) {
            if (!grid.isValid(n)) { continue; }
            const int32_t nIdx = grid.toIndex(n);
            if (distToOcean[static_cast<std::size_t>(nIdx)] > curDist + 1) {
                distToOcean[static_cast<std::size_t>(nIdx)] = curDist + 1;
                bfsQueue.push_back(nIdx);
            }
        }
    }

    // ---- Step 6: Temperature + precipitation + biome ----
    // Whittaker-style biome classification. Temperature drops with latitude
    // and elevation. Precipitation comes from four additive sources:
    //  - Latitudinal bands modelling Hadley/Ferrel circulation: wet at the
    //    equator (ITCZ), dry ~30deg (subtropical deserts), wet ~60deg
    //    (polar front), dry at the poles (polar desert).
    //  - Fractal rainfall noise so bands are not stripes.
    //  - Ocean proximity (continental interiors are drier).
    //  - Orographic rain shadow: if a mountain lies upwind within a few
    //    hexes, subtract precipitation. Wind direction flips by latitude
    //    (tropical easterlies vs mid-latitude westerlies).

    aoc::Random tempRng(rng.next());
    aoc::Random precipRng(rng.next());
    constexpr float TAU = 6.28318530717958f;

    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t index = row * width + col;
            if (!isLand[static_cast<std::size_t>(index)]) { continue; }

            const float latitudeT = (static_cast<float>(row) + 0.5f) /
                                    static_cast<float>(height);
            const float d         = std::abs(latitudeT - 0.5f);

            const float tnx = static_cast<float>(col) / static_cast<float>(width);
            const float tny = static_cast<float>(row) / static_cast<float>(height);

            // Temperature: latitude base + noise - elevation cooling.
            float temperature = 1.0f - 2.0f * d;
            temperature += (fractalNoise(tnx, tny, 3, 5.0f, 0.5f, tempRng) - 0.5f) * 0.30f;
            const int8_t elevByte = grid.elevation(index);
            if (elevByte > 0) {
                temperature -= 0.05f * static_cast<float>(elevByte);
            }
            temperature = std::clamp(temperature, 0.0f, 1.0f);

            // Latitudinal precipitation band. cos(4*pi*d) gives peaks at
            // d=0 (equator) and d=0.5 (pole), troughs at d=0.25 (30deg) and
            // d=0.75 (impossible, d maxes at 0.5). Scale so subtropics are
            // genuinely arid.
            float precipBand = 0.50f + 0.35f * std::cos(2.0f * TAU * d);

            // Cold air carries less moisture: rescale polar precip down.
            if (temperature < 0.30f) {
                precipBand *= (0.3f + temperature);
            }

            // Fractal rainfall noise (independent from temperature noise).
            const float precipNoise = fractalNoise(tnx + 0.37f, tny + 0.19f,
                                                   4, 4.0f, 0.55f, precipRng);

            // Ocean proximity: 1.0 adjacent to water, 0.0 at continental
            // interior. Linear decay over 10 hexes captures "warm wet coasts
            // vs dry interior" without needing full climate simulation.
            const int32_t dOcean = distToOcean[static_cast<std::size_t>(index)];
            const float oceanBoost = std::max(0.0f, 1.0f - static_cast<float>(dOcean) / 10.0f);

            // Orographic rain shadow. Wind direction is a 2D offset on the
            // offset-grid; negative k walks upwind from this tile.
            int32_t windDX = 0;
            if (d < 0.33f) {
                windDX = -1; // Tropics: trade winds blow east-to-west.
            } else if (d < 0.66f) {
                windDX = 1;  // Mid-latitudes: westerlies.
            } else {
                windDX = -1; // Polar easterlies.
            }

            float rainShadow = 0.0f;
            for (int32_t k = 1; k <= 5; ++k) {
                int32_t cx = col - windDX * k;
                // Wrap horizontally (cylindrical maps); clamp on flat.
                if (grid.topology() == MapTopology::Cylindrical) {
                    cx = ((cx % width) + width) % width;
                } else if (cx < 0 || cx >= width) {
                    break;
                }
                const int32_t nIdx = row * width + cx;
                if (isMountain[static_cast<std::size_t>(nIdx)]) {
                    // Closer mountains cast stronger shadow.
                    rainShadow -= 0.35f / static_cast<float>(k);
                    break;
                }
            }

            float precipitation = 0.45f * precipBand
                                + 0.30f * precipNoise
                                + 0.25f * oceanBoost
                                + rainShadow;
            precipitation = std::clamp(precipitation, 0.0f, 1.0f);

            // Biome lookup (Whittaker-ish). Temperature gates first -- polar
            // tiles are snow/tundra regardless of precipitation (ice deserts
            // still read as "snow"). Warm tiles then branch by moisture.
            TerrainType terrain;
            // Tightened cuts to reduce desert/snow dominance (user feedback).
            if (temperature < 0.10f) {
                terrain = TerrainType::Snow;
            } else if (temperature < 0.22f) {
                terrain = TerrainType::Tundra;
            } else if (precipitation < 0.15f && temperature > 0.55f) {
                // Was: precipitation<0.22 + temp>0.40. Now requires hotter+drier.
                terrain = TerrainType::Desert;
            } else if (precipitation < 0.40f) {
                terrain = TerrainType::Plains;
            } else if (temperature > 0.70f && precipitation > 0.65f) {
                terrain = TerrainType::Grassland;
            } else {
                terrain = TerrainType::Grassland;
            }

            grid.setTerrain(index, terrain);
        }
    }

    LOG_INFO("Realistic terrain generated: %d plates, %dx%d",
             plateCount, width, height);
}

// ============================================================================
// Realistic map: geology-based resource placement
// ============================================================================

void MapGenerator::placeGeologyResources(const Config& config, HexGrid& grid,
                                          aoc::Random& rng) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t tileCount = width * height;

    // Use REAL tectonic data captured by the Continents generator:
    // plateId per tile, plateMotions, plateCenters, plateLandFrac,
    // rockType, marginType, sedimentDepth, crustAgeTile. Earlier this
    // function rebuilt 4-6 fake plate seeds and ignored the actual
    // simulated tectonics — resources were placed against bogus
    // boundaries. Now we classify each tile's nearest plate-boundary
    // type by looking at the velocity-relative-to-neighbor direction
    // (same logic the renderer's PlateBoundaries overlay uses) and
    // place resources by real geology + rock type + margin type.
    const auto& realMotions = grid.plateMotions();
    const auto& realCenters = grid.plateCenters();
    const auto& realLandFr  = grid.plateLandFrac();
    const auto& realRock    = grid.rockType();
    const auto& realMargin  = grid.marginType();
    const auto& realSed     = grid.sedimentDepth();
    const auto& realAge     = grid.crustAgeTile();
    const bool  cylRes = (grid.topology() == aoc::map::MapTopology::Cylindrical);

    std::vector<BoundaryType> boundary(
        static_cast<std::size_t>(tileCount), BoundaryType::None);
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t index = row * width + col;
            const uint8_t myPid = grid.plateId(index);
            if (myPid == 0xFFu) { continue; }
            const hex::AxialCoord axial = hex::offsetToAxial({col, row});
            const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(axial);
            for (const hex::AxialCoord& n : nbrs) {
                if (!grid.isValid(n)) { continue; }
                const int32_t nIndex = grid.toIndex(n);
                const uint8_t nPid = grid.plateId(nIndex);
                if (nPid == 0xFFu || nPid == myPid) { continue; }
                if (myPid >= realMotions.size()
                    || nPid >= realMotions.size()) { continue; }
                const std::pair<float, float>& vA = realMotions[myPid];
                const std::pair<float, float>& vB = realMotions[nPid];
                const std::pair<float, float>& cA = realCenters[myPid];
                const std::pair<float, float>& cB = realCenters[nPid];
                float bnx = cB.first  - cA.first;
                float bny = cB.second - cA.second;
                if (cylRes) {
                    if (bnx >  0.5f) { bnx -= 1.0f; }
                    if (bnx < -0.5f) { bnx += 1.0f; }
                }
                const float bnLen = std::sqrt(bnx * bnx + bny * bny);
                if (bnLen < 1e-4f) { continue; }
                bnx /= bnLen; bny /= bnLen;
                const float relVx = vA.first  - vB.first;
                const float relVy = vA.second - vB.second;
                const float normProj = relVx * bnx + relVy * bny;
                const float tangProj = -relVx * bny + relVy * bnx;
                const float aN = std::abs(normProj);
                const float aT = std::abs(tangProj);
                if (aN > aT && aN > 0.02f) {
                    boundary[static_cast<std::size_t>(index)] =
                        (normProj > 0.0f)
                            ? BoundaryType::Convergent
                            : BoundaryType::Divergent;
                } else if (aT > 0.02f) {
                    boundary[static_cast<std::size_t>(index)] =
                        BoundaryType::Transform;
                }
                break;
            }
        }
    }

    // ---- Place resources based on geology zones ----

    aoc::Random resRng(rng.next());
    int32_t totalPlaced = 0;

    // auto required: lambda type is unnameable
    const auto isNearCoast = [&](int32_t row, int32_t col) -> bool {
        const hex::AxialCoord axial = hex::offsetToAxial({col, row});
        const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(axial);
        for (const hex::AxialCoord& n : nbrs) {
            if (grid.isValid(n) && isWater(grid.terrain(grid.toIndex(n)))) {
                return true;
            }
        }
        return false;
    };

    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t index = row * width + col;
            const TerrainType terrain = grid.terrain(index);

            if (isWater(terrain) || terrain == TerrainType::Mountain) {
                continue;
            }

            // Already has a resource (from natural wonders, etc.)
            if (grid.resource(index).isValid()) {
                continue;
            }

            const BoundaryType bType = boundary[static_cast<std::size_t>(index)];
            const int8_t elev = grid.elevation(index);
            const float latitudeT = static_cast<float>(row) / static_cast<float>(height);
            const float temperature = 1.0f - 2.0f * std::abs(latitudeT - 0.5f);
            const bool nearCoast = isNearCoast(row, col);

            // Real-tectonic context per tile.
            const std::size_t sIdx = static_cast<std::size_t>(index);
            const uint8_t rType   = (sIdx < realRock.size())   ? realRock[sIdx]   : 0;
            const uint8_t mType   = (sIdx < realMargin.size()) ? realMargin[sIdx] : 0;
            const float   sedDep  = (sIdx < realSed.size())    ? realSed[sIdx]    : 0.0f;
            const float   tileAge = (sIdx < realAge.size())    ? realAge[sIdx]    : 0.0f;
            const uint8_t myPid   = grid.plateId(index);
            const float   landFr  = (myPid != 0xFFu && myPid < realLandFr.size())
                ? realLandFr[myPid] : 0.5f;

            ResourceId placed{};

            // Volcanic arc — convergent + mountain elevation. Cu+Au
            // porphyry deposits cluster on subduction arcs (Andes,
            // Carpathians). Rare-earth on alkaline intrusives.
            if (bType == BoundaryType::Convergent && elev >= 2) {
                if (resRng.chance(0.07f)) {
                    placed = ResourceId{aoc::sim::goods::COPPER_ORE};
                } else if (resRng.chance(0.05f)) {
                    placed = ResourceId{aoc::sim::goods::GOLD_ORE};
                } else if (resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::SILVER_ORE};
                } else if (resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::ALUMINUM};
                } else if (resRng.chance(0.03f)) {
                    placed = ResourceId{aoc::sim::goods::RARE_EARTH};
                }
            }
            // Lower-elevation convergent: foothills / forearc accretionary
            // wedge — tin (greisens), copper (volcanic-hosted massive
            // sulfide), gold (orogenic), silver.
            else if (bType == BoundaryType::Convergent) {
                if (resRng.chance(0.06f)) {
                    placed = ResourceId{aoc::sim::goods::COPPER_ORE};
                } else if (resRng.chance(0.05f)) {
                    placed = ResourceId{aoc::sim::goods::TIN};
                } else if (resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::GOLD_ORE};
                } else if (resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::SILVER_ORE};
                }
            }
            // Divergent boundary — continental rift basins (East African
            // Rift accumulates oil + gas in graben sediments). Mid-ocean
            // ridges proper are submarine (already filtered out: water).
            else if (bType == BoundaryType::Divergent) {
                if (elev <= 0 && resRng.chance(0.06f)) {
                    placed = ResourceId{aoc::sim::goods::OIL};
                } else if (resRng.chance(0.05f)) {
                    placed = ResourceId{aoc::sim::goods::NATURAL_GAS};
                } else if (resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::COPPER_ORE};
                }
            }
            // Passive margin — wide sediment apron, prolific oil + gas.
            // Real Earth: Gulf of Mexico, North Sea, West African margin
            // host most offshore-onshore hydrocarbon basins. Salt domes
            // from old evaporite layers trap oil.
            else if (mType == 1 || mType == 2) {
                // Active margin (1) → uplifted, less sediment → fewer
                // oil traps. Passive (2) → sediment pile + salt domes.
                if (mType == 2) {
                    if (resRng.chance(0.13f)) {
                        placed = ResourceId{aoc::sim::goods::OIL};
                    } else if (resRng.chance(0.08f)) {
                        placed = ResourceId{aoc::sim::goods::NATURAL_GAS};
                    } else if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::SALT};
                    } else if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::COAL};
                    }
                } else { // active margin
                    if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::COPPER_ORE};
                    } else if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::GOLD_ORE};
                    }
                }
            }
            // Continental interior. Branch by rock type + age:
            //   Old craton (age > 100, metamorphic + igneous shield) →
            //     iron-ore (BIF), gold (orogenic gold in greenstone),
            //     stone, marble, gemstones (kimberlite pipes).
            //   Young / sediment-rich basin → oil, gas, coal, niter.
            else if (bType == BoundaryType::None) {
                const bool oldCraton = (tileAge > 100.0f
                    && (rType == 1 || rType == 2));
                const bool sedBasin  = (sedDep > 0.04f
                    || (rType == 0 && elev <= 1));
                if (oldCraton) {
                    // Cratonic kimberlite pipes host diamonds (S Africa,
                    // Botswana, Russia, Canada). Slot GEMS resource onto
                    // the oldest craton tiles as a proxy for diamonds.
                    if (tileAge > 130.0f && resRng.chance(0.025f)) {
                        placed = ResourceId{aoc::sim::goods::GEMS};
                    } else if (resRng.chance(0.10f)) {
                        placed = ResourceId{aoc::sim::goods::IRON_ORE};
                    } else if (resRng.chance(0.06f)) {
                        placed = ResourceId{aoc::sim::goods::GOLD_ORE};
                    } else if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::STONE};
                    } else if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::SILVER_ORE};
                    }
                } else if (sedBasin) {
                    if (resRng.chance(0.10f)) {
                        placed = ResourceId{aoc::sim::goods::OIL};
                    } else if (resRng.chance(0.06f)) {
                        placed = ResourceId{aoc::sim::goods::NATURAL_GAS};
                    } else if (resRng.chance(0.07f)) {
                        placed = ResourceId{aoc::sim::goods::COAL};
                    } else if (resRng.chance(0.03f)) {
                        placed = ResourceId{aoc::sim::goods::NITER};
                    }
                } else if (elev >= 2) {
                    if (resRng.chance(0.07f)) {
                        placed = ResourceId{aoc::sim::goods::IRON_ORE};
                    } else if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::STONE};
                    }
                }
            }
            // Ophiolite-rock tiles (suture lines): chromite, copper,
            // platinum-group metals real Earth: Oman, Cyprus.
            if (!placed.isValid() && rType == 3) {
                if (resRng.chance(0.10f)) {
                    placed = ResourceId{aoc::sim::goods::COPPER_ORE};
                } else if (resRng.chance(0.06f)) {
                    placed = ResourceId{aoc::sim::goods::IRON_ORE};
                } else if (resRng.chance(0.03f)) {
                    placed = ResourceId{aoc::sim::goods::ALUMINUM};
                }
            }
            // BAUXITE / ALUMINUM via lateritic weathering: tropical lat
            // + Hills feature on old igneous bedrock. Real: Jamaica,
            // Guinea, Australia (Weipa), Brazil.
            if (!placed.isValid() && grid.feature(index)
                    == aoc::map::FeatureType::Hills) {
                const float laterite_lat =
                    static_cast<float>(row) / static_cast<float>(height);
                const float lat = 2.0f * std::abs(laterite_lat - 0.5f);
                if (lat < 0.25f && tileAge > 30.0f
                    && (rType == 1 || rType == 2)
                    && resRng.chance(0.06f)) {
                    placed = ResourceId{aoc::sim::goods::ALUMINUM};
                }
            }
            // URANIUM: sandstone-hosted (sediment basin + age) OR
            // IOCG-style at old craton + igneous host.
            if (!placed.isValid()) {
                const bool sandstone = (rType == 0
                    && sedDep > 0.05f
                    && tileAge > 40.0f);
                const bool iocg = (tileAge > 110.0f
                    && (rType == 1 || rType == 2));
                if ((sandstone || iocg) && resRng.chance(0.025f)) {
                    placed = ResourceId{aoc::sim::goods::URANIUM};
                }
            }
            // LITHIUM: salar brine in arid endorheic OR cratonic
            // pegmatite. Real: Salar de Uyuni / Atacama / Greenbushes.
            if (!placed.isValid()
                && grid.terrain(index) == aoc::map::TerrainType::Desert) {
                bool nearLake = false;
                const aoc::hex::AxialCoord axL =
                    aoc::hex::offsetToAxial({col, row});
                const std::array<aoc::hex::AxialCoord, 6> nbsL =
                    aoc::hex::neighbors(axL);
                for (const auto& n : nbsL) {
                    if (!grid.isValid(n)) { continue; }
                    const int32_t nIdxL = grid.toIndex(n);
                    if (grid.lakeFlag().size()
                            > static_cast<std::size_t>(nIdxL)
                        && grid.lakeFlag()[
                            static_cast<std::size_t>(nIdxL)] != 0) {
                        nearLake = true; break;
                    }
                }
                if (nearLake && resRng.chance(0.10f)) {
                    placed = ResourceId{aoc::sim::goods::LITHIUM};
                }
            }
            // RARE_EARTH bonus on continental rift volcanics
            // (carbonatite-hosted, Mountain Pass, Bayan Obo).
            if (!placed.isValid()
                && grid.volcanism().size() > sIdx
                && grid.volcanism()[sIdx] == 4
                && resRng.chance(0.025f)) {
                placed = ResourceId{aoc::sim::goods::RARE_EARTH};
            }
            // ----- Session 8 geology-driven specialty placement -----
            // NICKEL: laterite weathering (tropical Hills + age) OR
            // magmatic Ni-Cu (igneous + craton).
            if (!placed.isValid()) {
                const float ny0 = static_cast<float>(row)
                                / static_cast<float>(height);
                const float lat0 = 2.0f * std::abs(ny0 - 0.5f);
                if (lat0 < 0.20f
                    && grid.feature(index)
                            == aoc::map::FeatureType::Hills
                    && rType == 1
                    && resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::NICKEL};
                } else if (rType == 1
                    && tileAge > 100.0f
                    && resRng.chance(0.025f)) {
                    placed = ResourceId{aoc::sim::goods::NICKEL};
                }
            }
            // COBALT: sed-Cu basins + magmatic. Old craton + igneous.
            if (!placed.isValid()
                && (rType == 1 || rType == 2)
                && tileAge > 100.0f
                && resRng.chance(0.02f)) {
                placed = ResourceId{aoc::sim::goods::COBALT};
            }
            // HELIUM: co-produced with natural-gas in old continental
            // basins. Sedimentary + age > 50.
            if (!placed.isValid()
                && rType == 0
                && tileAge > 50.0f
                && sedDep > 0.05f
                && resRng.chance(0.012f)) {
                placed = ResourceId{aoc::sim::goods::HELIUM};
            }
            // PLATINUM: ophiolite (PGM) + layered intrusion (igneous +
            // craton).
            if (!placed.isValid()) {
                if (rType == 3 && resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::PLATINUM};
                } else if (rType == 1 && tileAge > 130.0f
                    && resRng.chance(0.02f)) {
                    placed = ResourceId{aoc::sim::goods::PLATINUM};
                }
            }
            // SULFUR: volcanic fumaroles + evaporite.
            if (!placed.isValid()) {
                const auto& vc = grid.volcanism();
                if (sIdx < vc.size()
                    && (vc[sIdx] == 1 || vc[sIdx] == 5)
                    && resRng.chance(0.05f)) {
                    placed = ResourceId{aoc::sim::goods::SULFUR};
                }
            }
            // GYPSUM: evaporite basin (passive margin or arid sed
            // basin).
            if (!placed.isValid()
                && rType == 0
                && (mType == 2
                    || grid.terrain(index) == aoc::map::TerrainType::Desert)
                && resRng.chance(0.04f)) {
                placed = ResourceId{aoc::sim::goods::GYPSUM};
            }
            // FLUORITE: hydrothermal vein. Convergent + Hills tier.
            if (!placed.isValid()
                && bType == BoundaryType::Convergent
                && grid.feature(index) == aoc::map::FeatureType::Hills
                && resRng.chance(0.025f)) {
                placed = ResourceId{aoc::sim::goods::FLUORITE};
            }
            // DOLOMITE: tropical carbonate / shelf platform — but we
            // skip water tiles (water already filtered out at top).
            // Place on temperate sediment + age (diagenetic).
            if (!placed.isValid()
                && rType == 0 && tileAge > 30.0f
                && grid.feature(index) == aoc::map::FeatureType::Hills
                && resRng.chance(0.03f)) {
                placed = ResourceId{aoc::sim::goods::DOLOMITE};
            }
            // BARITE: bedded sedimentary + hydrothermal at convergent.
            if (!placed.isValid()
                && (bType == BoundaryType::Convergent || rType == 0)
                && resRng.chance(0.02f)) {
                placed = ResourceId{aoc::sim::goods::BARITE};
            }
            // ALLUVIAL_GOLD: river-edge tile + cratonic source upstream.
            if (!placed.isValid()
                && grid.riverEdges(index) != 0
                && tileAge > 80.0f
                && resRng.chance(0.025f)) {
                placed = ResourceId{aoc::sim::goods::ALLUVIAL_GOLD};
            }
            // BEACH_PLACER: coastal land tile (heavy mineral sands).
            if (!placed.isValid() && nearCoast
                && grid.terrain(index) == aoc::map::TerrainType::Plains
                && resRng.chance(0.03f)) {
                placed = ResourceId{aoc::sim::goods::BEACH_PLACER};
            }
            // PYRITE: hydrothermal sulfide + sed.
            if (!placed.isValid()
                && (rType == 0 || bType == BoundaryType::Convergent)
                && resRng.chance(0.02f)) {
                placed = ResourceId{aoc::sim::goods::PYRITE};
            }
            // PHOSPHATE: biogenic — coastal land tiles in arid zones
            // adjacent to upwelling water.
            if (!placed.isValid()
                && nearCoast
                && grid.terrain(index) == aoc::map::TerrainType::Desert
                && resRng.chance(0.05f)) {
                placed = ResourceId{aoc::sim::goods::PHOSPHATE};
            }
            // VMS_ORE: volcanic massive sulfide — ophiolite-region rare.
            if (!placed.isValid()
                && rType == 3
                && resRng.chance(0.06f)) {
                placed = ResourceId{aoc::sim::goods::VMS_ORE};
            }
            // SKARN_ORE: contact metamorphic at intrusion-sediment
            // boundary. Mountain edge with rockType=2 (metamorphic) +
            // adjacent sed.
            if (!placed.isValid()
                && rType == 2
                && bType == BoundaryType::Convergent
                && resRng.chance(0.03f)) {
                placed = ResourceId{aoc::sim::goods::SKARN_ORE};
            }
            // MVT_ORE: Mississippi-Valley Pb-Zn — sediment + age,
            // continental interior carbonate platform proxy.
            if (!placed.isValid()
                && rType == 0 && tileAge > 80.0f
                && bType == BoundaryType::None
                && resRng.chance(0.025f)) {
                placed = ResourceId{aoc::sim::goods::MVT_ORE};
            }
            (void)landFr;

            // Climate-based resources (only if no geology resource was placed).
            // WP-C2 cut: INCENSE/IVORY/COFFEE/TOBACCO/TEA lines stripped —
            // those goods were dead-end luxuries with no downstream recipe.
            // Remaining lines pick up the probability mass.
            if (!placed.isValid()) {
                if (terrain == TerrainType::Desert) {
                    if (elev <= 0 && resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::OIL};
                    } else if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::NATURAL_GAS};
                    }
                } else if (temperature > 0.65f && terrain != TerrainType::Desert) {
                    // Tropical (wet, hot). Cotton / Rubber / Spices / Sugar only.
                    const bool jungle = grid.feature(index) == FeatureType::Jungle;
                    if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::COTTON};
                    } else if (jungle && resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::RUBBER};
                    } else if (resRng.chance(0.06f)) {
                        placed = ResourceId{aoc::sim::goods::SPICES};
                    } else if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::SUGAR};
                    }
                } else if (temperature >= 0.55f && temperature <= 0.70f) {
                    // Subtropical band. SILK + WINE only.
                    if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::SILK};
                    } else if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::WINE};
                    }
                } else if (temperature >= 0.30f && temperature < 0.55f) {
                    // Temperate: bonus + luxury resources
                    if (resRng.chance(0.06f)) {
                        placed = ResourceId{aoc::sim::goods::WHEAT};
                    } else if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::WOOD};
                    } else if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::CATTLE};
                    } else if (resRng.chance(0.03f)) {
                        placed = ResourceId{aoc::sim::goods::SALT};
                    } else if (resRng.chance(0.03f)) {
                        placed = ResourceId{aoc::sim::goods::DYES};
                    } else if (grid.feature(index) == FeatureType::Hills && resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::MARBLE};
                    } else if (grid.riverEdges(index) != 0 && resRng.chance(0.06f)) {
                        placed = ResourceId{aoc::sim::goods::RICE};
                    } else if (resRng.chance(0.03f)) {
                        placed = ResourceId{aoc::sim::goods::RICE};
                    } else if (resRng.chance(0.03f)) {
                        placed = ResourceId{aoc::sim::goods::CLAY};
                    }
                } else if (temperature < 0.30f) {
                    // Cold: furs. WP-C2 cut GEMS (dead-end).
                    if (resRng.chance(0.06f)) {
                        placed = ResourceId{aoc::sim::goods::FURS};
                    } else if (nearCoast && resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::FISH};
                    }
                }

                // Desert also gets salt
                if (!placed.isValid() && terrain == TerrainType::Desert) {
                    if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::SALT};
                    }
                }
            }

            // Coast adjacency resources (only if still nothing placed).
            // WP-C2 cut PEARLS (dead-end).
            if (!placed.isValid() && nearCoast) {
                if (resRng.chance(0.06f)) {
                    placed = ResourceId{aoc::sim::goods::FISH};
                } else if (resRng.chance(0.02f)) {
                    placed = ResourceId{aoc::sim::goods::SUGAR};
                }
            }

            if (placed.isValid()) {
                grid.setResource(index, placed);
                grid.setReserves(index, aoc::sim::defaultReserves(placed.value));
                ++totalPlaced;
                if (placed.value == aoc::sim::goods::OIL
                    || placed.value == aoc::sim::goods::NATURAL_GAS) {
                    LOG_INFO("Strategic resource placed: %.*s at (%d,%d) terrain=%.*s elev=%d",
                             static_cast<int>(aoc::sim::goodDef(placed.value).name.size()),
                             aoc::sim::goodDef(placed.value).name.data(),
                             col, row,
                             static_cast<int>(terrainName(terrain).size()),
                             terrainName(terrain).data(),
                             static_cast<int>(grid.elevation(index)));
                }
            }
        }
    }

    // ---- Mountain-metal pass: a fraction of metal deposits spawn on mountains
    // that are accessible from an adjacent non-mountain land tile. This keeps
    // mountains impassable while rewarding players who explore rugged terrain
    // via the Mountain Mine improvement.
    int32_t mountainMetalsPlaced = 0;
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t index = row * width + col;
            if (grid.terrain(index) != TerrainType::Mountain) {
                continue;
            }
            if (grid.resource(index).isValid()) {
                continue;
            }
            if (grid.naturalWonder(index) != NaturalWonderType::None) {
                continue;
            }

            // Require at least one non-mountain, non-water neighbour so the
            // tile is reachable by a Builder on adjacent land.
            const hex::AxialCoord axial = hex::offsetToAxial({col, row});
            const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(axial);
            bool hasAccessibleNeighbour = false;
            for (const hex::AxialCoord& n : nbrs) {
                if (!grid.isValid(n)) { continue; }
                const int32_t nIndex = grid.toIndex(n);
                const TerrainType nt = grid.terrain(nIndex);
                if (nt == TerrainType::Mountain) { continue; }
                if (isWater(nt)) { continue; }
                hasAccessibleNeighbour = true;
                break;
            }
            if (!hasAccessibleNeighbour) {
                continue;
            }

            // Volcanic / convergent mountains are the richest; others still have
            // a small chance. Total expected metal rate on accessible mountains
            // is roughly 15%.
            const BoundaryType bType = boundary[static_cast<std::size_t>(index)];
            const bool isVolcanic = (bType == BoundaryType::Convergent);

            ResourceId placed{};
            if (isVolcanic) {
                if      (resRng.chance(0.15f)) { placed = ResourceId{aoc::sim::goods::IRON_ORE}; }
                else if (resRng.chance(0.12f)) { placed = ResourceId{aoc::sim::goods::COPPER_ORE}; }
                else if (resRng.chance(0.07f)) { placed = ResourceId{aoc::sim::goods::SILVER_ORE}; }
                else if (resRng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::GOLD_ORE}; }
            } else {
                if      (resRng.chance(0.10f)) { placed = ResourceId{aoc::sim::goods::IRON_ORE}; }
                else if (resRng.chance(0.07f)) { placed = ResourceId{aoc::sim::goods::COPPER_ORE}; }
                else if (resRng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::SILVER_ORE}; }
                else if (resRng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::GOLD_ORE}; }
            }

            if (placed.isValid()) {
                grid.setResource(index, placed);
                grid.setReserves(index, aoc::sim::defaultReserves(placed.value));
                ++mountainMetalsPlaced;
                LOG_INFO("Mountain metal placed at (%d,%d): %.*s",
                         col, row,
                         static_cast<int>(aoc::sim::goodDef(placed.value).name.size()),
                         aoc::sim::goodDef(placed.value).name.data());
            }
        }
    }
    totalPlaced += mountainMetalsPlaced;

    // Guaranteed strategic-energy pass: every map must seed at least a few
    // Oil + Natural Gas + Niter tiles on accessible land.  Without this,
    // probabilistic placement can leave a whole continent dry — which is
    // what killed the OIL chain (no oil) AND the Ammunition chain (no
    // niter) in 5-seed batches.  Tin added too so Bronze recipe has raw.
    {
        const int32_t minOilTiles = std::max(6, (width * height) / 400);
        const int32_t minGasTiles = std::max(3, (width * height) / 800);
        const int32_t minNiterTiles = std::max(4, (width * height) / 600);
        const int32_t minTinTiles   = std::max(3, (width * height) / 800);

        std::vector<int32_t> oilCandidates;
        oilCandidates.reserve(static_cast<size_t>(width * height));
        for (int32_t r = 0; r < height; ++r) {
            for (int32_t c = 0; c < width; ++c) {
                const int32_t idx = r * width + c;
                const TerrainType tt = grid.terrain(idx);
                if (isWater(tt) || tt == TerrainType::Mountain) { continue; }
                if (grid.resource(idx).isValid())               { continue; }
                if (grid.naturalWonder(idx) != NaturalWonderType::None) { continue; }
                oilCandidates.push_back(idx);
            }
        }

        aoc::Random fillRng(resRng);
        for (size_t i = oilCandidates.size(); i > 1; --i) {
            const size_t j = static_cast<size_t>(fillRng.nextInt(0, static_cast<int32_t>(i) - 1));
            std::swap(oilCandidates[i - 1], oilCandidates[j]);
        }

        int32_t oilPlaced = 0, gasPlaced = 0, niterPlaced = 0, tinPlaced = 0;
        for (const int32_t idx : oilCandidates) {
            if (oilPlaced >= minOilTiles && gasPlaced >= minGasTiles
                && niterPlaced >= minNiterTiles && tinPlaced >= minTinTiles) { break; }
            uint16_t res = 0xFFFFu;
            if      (oilPlaced   < minOilTiles)   { res = aoc::sim::goods::OIL;         ++oilPlaced; }
            else if (gasPlaced   < minGasTiles)   { res = aoc::sim::goods::NATURAL_GAS; ++gasPlaced; }
            else if (niterPlaced < minNiterTiles) { res = aoc::sim::goods::NITER;       ++niterPlaced; }
            else                                  { res = aoc::sim::goods::TIN;         ++tinPlaced; }
            grid.setResource(idx, ResourceId{res});
            grid.setReserves(idx, aoc::sim::defaultReserves(res));
            ++totalPlaced;
        }
        LOG_INFO("Strategic fill: oil=%d gas=%d niter=%d tin=%d (targets %d/%d/%d/%d)",
                 oilPlaced, gasPlaced, niterPlaced, tinPlaced,
                 minOilTiles, minGasTiles, minNiterTiles, minTinTiles);
    }

    (void)config;  // mapSize/type already used indirectly
    LOG_INFO("Geology-based resource placement: %d resources placed (%d on mountains)",
             totalPlaced, mountainMetalsPlaced);
}

// ============================================================================
// Basic resource placement for non-Realistic map types
// ============================================================================

void MapGenerator::placeBasicResources(const Config& config, HexGrid& grid,
                                        aoc::Random& rng) {
    const int32_t width = grid.width();
    const int32_t height = grid.height();
    int32_t totalPlaced = 0;

    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            int32_t index = row * width + col;
            TerrainType terrain = grid.terrain(index);
            FeatureType feature = grid.feature(index);

            if (isWater(terrain) || isImpassable(terrain)) {
                continue;
            }

            ResourceId placed{};

            // Hills/mountains area: strategic metals
            if (feature == FeatureType::Hills) {
                if (rng.chance(0.08f))      { placed = ResourceId{aoc::sim::goods::IRON_ORE}; }
                else if (rng.chance(0.05f)) { placed = ResourceId{aoc::sim::goods::COPPER_ORE}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::GOLD_ORE}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::SILVER_ORE}; }
                else if (rng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::COAL}; }
                else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::TIN}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::STONE}; }
            }
            // Desert: oil (high density), natural gas, incense
            else if (terrain == TerrainType::Desert) {
                if (rng.chance(0.10f))      { placed = ResourceId{aoc::sim::goods::OIL}; }
                else if (rng.chance(0.05f)) { placed = ResourceId{aoc::sim::goods::NATURAL_GAS}; }
                // WP-C2: INCENSE cut (dead-end). Lithium favors dry-lake basins.
                else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::LITHIUM}; }
            }
            // Forest/jungle: wood, rubber, spices, dyes
            else if (feature == FeatureType::Forest) {
                if (rng.chance(0.08f))      { placed = ResourceId{aoc::sim::goods::WOOD}; }
                else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::FURS}; }
            }
            else if (feature == FeatureType::Jungle) {
                if (rng.chance(0.04f))      { placed = ResourceId{aoc::sim::goods::RUBBER}; }
                else if (rng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::SPICES}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::DYES}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::SUGAR}; }
            }
            // Grassland: food, cotton, horses, rice (river-adjacent), clay
            else if (terrain == TerrainType::Grassland) {
                if (rng.chance(0.06f))      { placed = ResourceId{aoc::sim::goods::WHEAT}; }
                else if (rng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::CATTLE}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::COTTON}; }
                else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::HORSES}; }
                // Rice: river-adjacent gets a higher chance (paddy field), but
                // any Grassland is also valid (upland rice) so the recipe
                // actually gets raw inputs across more seeds.
                else if (grid.riverEdges(index) != 0 && rng.chance(0.06f)) {
                    placed = ResourceId{aoc::sim::goods::RICE};
                }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::RICE}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::CLAY}; }
            }
            // Plains: food, stone, horses, niter, oil (inland basins)
            else if (terrain == TerrainType::Plains) {
                if (rng.chance(0.05f))      { placed = ResourceId{aoc::sim::goods::WHEAT}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::HORSES}; }
                else if (rng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::STONE}; }
                else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::NITER}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::WOOD}; }
                else if (rng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::OIL}; }
                else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::NATURAL_GAS}; }
            }
            // Tundra: furs, gems, oil (arctic basins), coal
            else if (terrain == TerrainType::Tundra) {
                if (rng.chance(0.04f))      { placed = ResourceId{aoc::sim::goods::FURS}; }
                // WP-C2: GEMS cut (dead-end luxury).
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::COAL}; }
                else if (rng.chance(0.05f)) { placed = ResourceId{aoc::sim::goods::OIL}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::NATURAL_GAS}; }
                // WP-C2: Lithium also in high-altitude tundra hard rock.
                else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::LITHIUM}; }
            }

            // Coastal tiles: fish
            if (!placed.isValid() && terrain == TerrainType::Grassland) {
                hex::AxialCoord axial = hex::offsetToAxial({col, row});
                std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(axial);
                for (const hex::AxialCoord& n : nbrs) {
                    if (grid.isValid(n) && isWater(grid.terrain(grid.toIndex(n)))) {
                        if (rng.chance(0.06f)) {
                            placed = ResourceId{aoc::sim::goods::FISH};
                        }
                        break;
                    }
                }
            }

            if (placed.isValid()) {
                grid.setResource(index, placed);
                int16_t reserves = aoc::sim::defaultReserves(placed.value);
                grid.setReserves(index, reserves);
                ++totalPlaced;
                if (placed.value == aoc::sim::goods::OIL
                    || placed.value == aoc::sim::goods::NATURAL_GAS) {
                    LOG_INFO("Strategic resource placed: %.*s at (%d,%d) terrain=%.*s elev=%d",
                             static_cast<int>(aoc::sim::goodDef(placed.value).name.size()),
                             aoc::sim::goodDef(placed.value).name.data(),
                             col, row,
                             static_cast<int>(terrainName(terrain).size()),
                             terrainName(terrain).data(),
                             static_cast<int>(grid.elevation(index)));
                }
            }
        }
    }

    // ---- Mountain-metal pass (basic map generator) ----
    // Drop metal deposits on mountains that have a non-mountain land neighbour,
    // so a Builder standing on adjacent land can access them via Mountain Mine.
    int32_t mountainMetalsPlaced = 0;
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            int32_t index = row * width + col;
            if (grid.terrain(index) != TerrainType::Mountain) {
                continue;
            }
            if (grid.resource(index).isValid()) {
                continue;
            }
            if (grid.naturalWonder(index) != NaturalWonderType::None) {
                continue;
            }

            hex::AxialCoord axial = hex::offsetToAxial({col, row});
            std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(axial);
            bool hasAccessibleNeighbour = false;
            for (const hex::AxialCoord& n : nbrs) {
                if (!grid.isValid(n)) { continue; }
                int32_t nIndex = grid.toIndex(n);
                TerrainType nt = grid.terrain(nIndex);
                if (nt == TerrainType::Mountain) { continue; }
                if (isWater(nt)) { continue; }
                hasAccessibleNeighbour = true;
                break;
            }
            if (!hasAccessibleNeighbour) {
                continue;
            }

            ResourceId placed{};
            if      (rng.chance(0.05f)) { placed = ResourceId{aoc::sim::goods::IRON_ORE}; }
            else if (rng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::COPPER_ORE}; }
            else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::SILVER_ORE}; }
            else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::GOLD_ORE}; }

            if (placed.isValid()) {
                grid.setResource(index, placed);
                grid.setReserves(index, aoc::sim::defaultReserves(placed.value));
                ++mountainMetalsPlaced;
                LOG_INFO("Mountain metal placed at (%d,%d): %.*s",
                         col, row,
                         static_cast<int>(aoc::sim::goodDef(placed.value).name.size()),
                         aoc::sim::goodDef(placed.value).name.data());
            }
        }
    }
    totalPlaced += mountainMetalsPlaced;

    // Guaranteed strategic pass (mirrors placeGeologyResources): seed
    // oil + gas + niter + tin on empty land so every production chain has
    // at least minimal raw supply.
    {
        const int32_t minOilTiles   = std::max(6, (width * height) / 400);
        const int32_t minGasTiles   = std::max(3, (width * height) / 800);
        const int32_t minNiterTiles = std::max(4, (width * height) / 600);
        const int32_t minTinTiles   = std::max(3, (width * height) / 800);
        std::vector<int32_t> oilCandidates;
        oilCandidates.reserve(static_cast<size_t>(width * height));
        for (int32_t r = 0; r < height; ++r) {
            for (int32_t c = 0; c < width; ++c) {
                const int32_t idx = r * width + c;
                const TerrainType tt = grid.terrain(idx);
                if (isWater(tt) || tt == TerrainType::Mountain) { continue; }
                if (grid.resource(idx).isValid())               { continue; }
                if (grid.naturalWonder(idx) != NaturalWonderType::None) { continue; }
                oilCandidates.push_back(idx);
            }
        }
        aoc::Random fillRng(rng);
        for (size_t i = oilCandidates.size(); i > 1; --i) {
            const size_t j = static_cast<size_t>(fillRng.nextInt(0, static_cast<int32_t>(i) - 1));
            std::swap(oilCandidates[i - 1], oilCandidates[j]);
        }
        int32_t oilPlaced = 0, gasPlaced = 0, niterPlaced = 0, tinPlaced = 0;
        for (const int32_t idx : oilCandidates) {
            if (oilPlaced >= minOilTiles && gasPlaced >= minGasTiles
                && niterPlaced >= minNiterTiles && tinPlaced >= minTinTiles) { break; }
            uint16_t res = 0xFFFFu;
            if      (oilPlaced   < minOilTiles)   { res = aoc::sim::goods::OIL;         ++oilPlaced; }
            else if (gasPlaced   < minGasTiles)   { res = aoc::sim::goods::NATURAL_GAS; ++gasPlaced; }
            else if (niterPlaced < minNiterTiles) { res = aoc::sim::goods::NITER;       ++niterPlaced; }
            else                                  { res = aoc::sim::goods::TIN;         ++tinPlaced; }
            grid.setResource(idx, ResourceId{res});
            grid.setReserves(idx, aoc::sim::defaultReserves(res));
            ++totalPlaced;
        }
        LOG_INFO("Strategic fill (basic): oil=%d gas=%d niter=%d tin=%d",
                 oilPlaced, gasPlaced, niterPlaced, tinPlaced);
    }

    (void)config;
    LOG_INFO("Basic resource placement: %d resources placed (%d on mountains)",
             totalPlaced, mountainMetalsPlaced);
}

// ============================================================================
// Random placement — uniform per-tile chance, geology-blind
// ============================================================================

void MapGenerator::placeRandomResources(const Config& config, HexGrid& grid,
                                         aoc::Random& rng) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();

    // Flat per-tile probabilities chosen so total counts land in the same
    // ballpark as placeBasicResources.  Mountain/water/impassable tiles opt
    // out of land resources; mountains get a separate metals pass below.
    struct GoodChance { uint16_t id; float chance; };
    // WP-C2: LITHIUM seeded alongside legacy strategics. Rarer than coal/oil
    // (0.006) so early-game maps still have chain variety without Lithium
    // saturating every civ.
    // WP-C2 cut GEMS + INCENSE (dead-end luxuries with no downstream).
    const std::array<GoodChance, 19> pool = {{
        {aoc::sim::goods::IRON_ORE,   0.030f},
        {aoc::sim::goods::COPPER_ORE, 0.030f},
        {aoc::sim::goods::COAL,       0.030f},
        {aoc::sim::goods::OIL,        0.020f},
        {aoc::sim::goods::NITER,      0.010f},
        {aoc::sim::goods::HORSES,     0.020f},
        {aoc::sim::goods::STONE,      0.035f},
        {aoc::sim::goods::WOOD,       0.030f},
        {aoc::sim::goods::WHEAT,      0.030f},
        {aoc::sim::goods::CATTLE,     0.020f},
        {aoc::sim::goods::COTTON,     0.015f},
        {aoc::sim::goods::SILK,       0.010f},
        {aoc::sim::goods::SPICES,     0.012f},
        {aoc::sim::goods::DYES,       0.010f},
        {aoc::sim::goods::FURS,       0.012f},
        {aoc::sim::goods::GOLD_ORE,   0.008f},
        {aoc::sim::goods::SILVER_ORE, 0.010f},
        {aoc::sim::goods::TIN,        0.010f},
        {aoc::sim::goods::LITHIUM,    0.006f},
    }};

    int32_t totalPlaced = 0;
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t index = row * width + col;
            const TerrainType terrain = grid.terrain(index);
            if (isWater(terrain) || isImpassable(terrain)) { continue; }
            if (terrain == TerrainType::Mountain)          { continue; }
            if (grid.resource(index).isValid())            { continue; }

            for (const GoodChance& gc : pool) {
                if (rng.chance(gc.chance)) {
                    grid.setResource(index, ResourceId{gc.id});
                    grid.setReserves(index, aoc::sim::defaultReserves(gc.id));
                    ++totalPlaced;
                    break;
                }
            }
        }
    }

    // Mountain metals: uniform chance, no geology check.
    int32_t mountainMetalsPlaced = 0;
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t index = row * width + col;
            if (grid.terrain(index) != TerrainType::Mountain) { continue; }
            if (grid.resource(index).isValid())               { continue; }
            if (grid.naturalWonder(index) != NaturalWonderType::None) { continue; }

            const hex::AxialCoord axial = hex::offsetToAxial({col, row});
            const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(axial);
            bool accessible = false;
            for (const hex::AxialCoord& n : nbrs) {
                if (!grid.isValid(n)) { continue; }
                const TerrainType nt = grid.terrain(grid.toIndex(n));
                if (nt != TerrainType::Mountain && !isWater(nt)) {
                    accessible = true;
                    break;
                }
            }
            if (!accessible) { continue; }

            ResourceId placed{};
            if      (rng.chance(0.05f)) { placed = ResourceId{aoc::sim::goods::IRON_ORE}; }
            else if (rng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::COPPER_ORE}; }
            else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::SILVER_ORE}; }
            else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::GOLD_ORE}; }

            if (placed.isValid()) {
                grid.setResource(index, placed);
                grid.setReserves(index, aoc::sim::defaultReserves(placed.value));
                ++mountainMetalsPlaced;
            }
        }
    }
    totalPlaced += mountainMetalsPlaced;

    (void)config;
    LOG_INFO("Random resource placement: %d resources placed (%d on mountains)",
             totalPlaced, mountainMetalsPlaced);
}

// ============================================================================
// Fair placement — redistribute strategic resources across quadrants
// ============================================================================

void MapGenerator::balanceResourcesFair(const Config& config, HexGrid& grid,
                                         aoc::Random& rng) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t midCol = width / 2;
    const int32_t midRow = height / 2;

    auto quadrantOf = [&](int32_t col, int32_t row) -> int32_t {
        const int32_t qx = (col < midCol) ? 0 : 1;
        const int32_t qy = (row < midRow) ? 0 : 1;
        return qy * 2 + qx;  // 0..3
    };

    // Strategic goods that actually matter for industrial/military gates.
    const std::array<uint16_t, 6> balanced = {
        aoc::sim::goods::IRON_ORE,
        aoc::sim::goods::COPPER_ORE,
        aoc::sim::goods::COAL,
        aoc::sim::goods::OIL,
        aoc::sim::goods::HORSES,
        aoc::sim::goods::WHEAT,
    };

    int32_t totalMoved = 0;
    for (const uint16_t goodId : balanced) {
        // Count + gather tile indices per quadrant.
        std::array<std::vector<int32_t>, 4> tiles;
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t index = row * width + col;
                const ResourceId r = grid.resource(index);
                if (r.isValid() && r.value == goodId) {
                    tiles[static_cast<size_t>(quadrantOf(col, row))].push_back(index);
                }
            }
        }
        const int32_t total = static_cast<int32_t>(
            tiles[0].size() + tiles[1].size() + tiles[2].size() + tiles[3].size());
        if (total == 0) { continue; }
        const int32_t target = total / 4;

        // Pass 1: strip surplus from over-served quadrants.
        std::vector<int32_t> surplus;
        for (int32_t q = 0; q < 4; ++q) {
            while (static_cast<int32_t>(tiles[static_cast<size_t>(q)].size()) > target + 1) {
                const size_t n = tiles[static_cast<size_t>(q)].size();
                const size_t pick = static_cast<size_t>(rng.nextInt(0, static_cast<int32_t>(n) - 1));
                const int32_t idx = tiles[static_cast<size_t>(q)][pick];
                tiles[static_cast<size_t>(q)][pick] = tiles[static_cast<size_t>(q)].back();
                tiles[static_cast<size_t>(q)].pop_back();
                grid.setResource(idx, ResourceId{});
                grid.setReserves(idx, 0);
                surplus.push_back(idx);
            }
        }

        // Pass 2: place surplus on any suitable empty land tile in under-served
        // quadrants.  "Suitable" = not water, not impassable, no existing
        // resource, not a natural wonder.  Geology constraints are waived; Fair
        // mode intentionally bulldozes realism to guarantee parity.
        std::vector<int32_t> deficitQuadrants;
        for (int32_t q = 0; q < 4; ++q) {
            const int32_t have = static_cast<int32_t>(tiles[static_cast<size_t>(q)].size());
            for (int32_t need = have; need < target; ++need) {
                deficitQuadrants.push_back(q);
            }
        }

        for (int32_t q : deficitQuadrants) {
            if (surplus.empty()) { break; }

            const int32_t colLo = (q % 2 == 0) ? 0 : midCol;
            const int32_t colHi = (q % 2 == 0) ? midCol : width;
            const int32_t rowLo = (q / 2 == 0) ? 0 : midRow;
            const int32_t rowHi = (q / 2 == 0) ? midRow : height;

            // Collect candidate empty land tiles within the quadrant.
            std::vector<int32_t> candidates;
            candidates.reserve(static_cast<size_t>((colHi - colLo) * (rowHi - rowLo)));
            for (int32_t row = rowLo; row < rowHi; ++row) {
                for (int32_t col = colLo; col < colHi; ++col) {
                    const int32_t index = row * width + col;
                    const TerrainType t = grid.terrain(index);
                    if (isWater(t) || isImpassable(t))       { continue; }
                    if (t == TerrainType::Mountain)          { continue; }
                    if (grid.resource(index).isValid())      { continue; }
                    if (grid.naturalWonder(index) != NaturalWonderType::None) { continue; }
                    candidates.push_back(index);
                }
            }
            if (candidates.empty()) { continue; }

            const size_t pick = static_cast<size_t>(rng.nextInt(0, static_cast<int32_t>(candidates.size()) - 1));
            const int32_t idx = candidates[pick];
            grid.setResource(idx, ResourceId{goodId});
            grid.setReserves(idx, aoc::sim::defaultReserves(goodId));
            surplus.pop_back();
            ++totalMoved;
        }
    }

    (void)config;
    LOG_INFO("Fair-placement rebalance: %d strategic resources relocated across quadrants",
             totalMoved);
}

} // namespace aoc::map

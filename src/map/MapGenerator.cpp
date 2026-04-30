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
        float rot;      ///< Cell rotation in radians
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
        // Hotspot track in PLATE-LOCAL coords.
        std::vector<std::pair<float, float>> hotspotTrail;
    };
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
            const int32_t oceanCountTarget = centerRng.nextInt(12, 16);
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
                p.aspect = centerRng.nextFloat(0.85f, 1.20f);
                p.rot    = centerRng.nextFloat(-3.14159f, 3.14159f);
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
                // Continental plates: 0.70-0.88 land. Even Eurasian
                // (the largest land plate) is ~85% land + ~15% inland
                // seas. Higher floor keeps continents solid before
                // erosion + island purge gnaw at the edges.
                p.landFraction = isLand
                    ? centerRng.nextFloat(0.70f, 0.88f)
                    : centerRng.nextFloat(0.05f, 0.20f);
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
            const int32_t POLAR_OCEANS_PER_BAND = 3;
            for (int32_t b = 0; b < POLAR_OCEANS_PER_BAND; ++b) {
                // Top band (Arctic).
                const float topCx = (b + 0.5f)
                    * ((xHiOcn - xLoOcn) / static_cast<float>(POLAR_OCEANS_PER_BAND))
                    + xLoOcn;
                pushPlate(std::clamp(topCx + centerRng.nextFloat(-0.05f, 0.05f),
                                      xLoOcn, xHiOcn),
                          centerRng.nextFloat(0.03f, LAND_LAT_LO - 0.02f),
                          false);
                ++oceanPlaced;
                // Bottom band (Southern Ocean / Antarctic surroundings).
                const float botCx = (b + 0.5f)
                    * ((xHiOcn - xLoOcn) / static_cast<float>(POLAR_OCEANS_PER_BAND))
                    + xLoOcn;
                pushPlate(std::clamp(botCx + centerRng.nextFloat(-0.05f, 0.05f),
                                      xLoOcn, xHiOcn),
                          centerRng.nextFloat(LAND_LAT_HI + 0.02f, 0.97f),
                          false);
                ++oceanPlaced;
            }
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
        const int32_t CYCLE = 5;
        // DT scales with EPOCHS so total simulated drift is roughly
        // constant (~60% of map width over the full sim) regardless of
        // how granular the user wants. Long sims = many small steps =
        // smoother evolution; short sims = fewer larger jumps.
        // Target: max plate displacement of 60% of map width.
        // total_drift = EPOCHS * DT * v_max(0.7) → DT = 0.6 / (EPOCHS * 0.7) ≈ 0.86 / EPOCHS.
        const float DT = std::clamp(
            0.86f / static_cast<float>(EPOCHS), 0.003f, 0.030f);
        // Stress gate. 0.30 captures most active convergent margins.
        // With slower DT and per-epoch scaling, contributions are
        // smaller per step, so a lower gate is needed to let stress
        // accumulate to mountain-tall over the sim.
        constexpr float   STRESS_GATE = 0.30f;

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
                p.cx += p.vx * DT;
                p.cy += p.vy * DT;
                if (cylSim) {
                    if (p.cx < 0.0f) { p.cx += 1.0f; }
                    if (p.cx > 1.0f) { p.cx -= 1.0f; }
                } else {
                    if (p.cx < 0.05f) { p.cx = 0.05f; p.vx = -p.vx; }
                    if (p.cx > 0.95f) { p.cx = 0.95f; p.vx = -p.vx; }
                }
                if (p.cy < 0.05f) { p.cy = 0.05f; p.vy = -p.vy; }
                if (p.cy > 0.95f) { p.cy = 0.95f; p.vy = -p.vy; }
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
                    constexpr float MERGE_DIST = 0.13f;
                    bool isClosing = false;
                    if (plates[a].isLand && plates[b].isLand && d < MERGE_DIST) {
                        const float relVx = plates[a].vx - plates[b].vx;
                        const float relVy = plates[a].vy - plates[b].vy;
                        const float closing = (relVx * dx + relVy * dy)
                                            / std::max(0.0001f, d);
                        isClosing = (closing < -0.05f);
                    }
                    if (isClosing) {
                        // Land-land collision: fuse. The merged plate
                        // sits at midpoint, gets averaged motion. We
                        // drop the second plate so the loop length
                        // shortens by one and the next iteration sees
                        // a smaller list. Restart inner loop after
                        // erasure.
                        plates[a].cx = (plates[a].cx + plates[b].cx) * 0.5f;
                        plates[a].cy = (plates[a].cy + plates[b].cy) * 0.5f;
                        plates[a].vx = (plates[a].vx + plates[b].vx) * 0.5f;
                        plates[a].vy = (plates[a].vy + plates[b].vy) * 0.5f;
                        plates.erase(plates.begin()
                            + static_cast<std::ptrdiff_t>(b));
                        --b;
                    }
                }
            }
            // Per-epoch plate deformation. Real plates rotate slowly
            // over geological time (~10-30° over a 500 My Wilson
            // cycle), so per-epoch drift must be SMALL — otherwise
            // the plate-local crust mask samples a wildly different
            // region each epoch and tiles flip ocean↔land between
            // sim steps. ±0.018 rad/epoch ≈ 1°/epoch matches Earth-
            // scale rotation rates and keeps coastlines stable.
            // Aspect ratio also drifts slowly toward a random target
            // (very low blend rate).
            for (Plate& p : plates) {
                p.rot += centerRng.nextFloat(-0.018f, 0.018f);
                const float targetAspect = centerRng.nextFloat(0.85f, 1.20f);
                p.aspect = p.aspect * 0.985f + targetAspect * 0.015f;
                if (p.aspect < 0.7f) { p.aspect = 0.7f; }
                if (p.aspect > 1.40f) { p.aspect = 1.40f; }
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
                const std::size_t maxPlates = std::max(
                    static_cast<std::size_t>(20),
                    startPlates.size() * 2);
                const int32_t splitsThisEpoch =
                    (plates.size() >= maxPlates)
                        ? 0
                        : std::min(3, static_cast<int32_t>(landIdx.size()));
                for (int32_t s = 0; s < splitsThisEpoch; ++s) {
                    if (landIdx.empty()) { break; }
                    const std::size_t pickPos = static_cast<std::size_t>(
                        centerRng.nextInt(0,
                            static_cast<int32_t>(landIdx.size()) - 1));
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
                    const float offsetMag = centerRng.nextFloat(0.10f, 0.18f);
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
                        child.rot          = centerRng.nextFloat(-3.14f, 3.14f);
                        child.aspect       = centerRng.nextFloat(0.85f, 1.30f);
                        child.seedX        = centerRng.nextFloat(0.0f, 1000.0f);
                        child.seedY        = centerRng.nextFloat(0.0f, 1000.0f);
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
                        // own future trail).
                        child.hotspotTrail.clear();
                        plates.push_back(child);
                    }
                }
            }

            // Accumulate orogeny stress for this epoch. For each tile
            // perform the same Voronoi + boundary stress as the render
            // pass, but feed the result into the orogeny field rather
            // than directly into elev. Subduction land-side stress is
            // gated by STRESS_GATE so only strong convergent motion
            // builds mountains — passive margins stay flat.
            for (int32_t row = 0; row < height; ++row) {
                for (int32_t col = 0; col < width; ++col) {
                    const float nx = static_cast<float>(col)
                                    / static_cast<float>(width);
                    const float ny = static_cast<float>(row)
                                    / static_cast<float>(height);
                    float d1Sq = 1e9f, d2Sq = 1e9f;
                    int32_t nearest = -1, second = -1;
                    for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                        float dx = nx - plates[pi].cx;
                        float dy = ny - plates[pi].cy;
                        // Cylindrical wrap so plates near col=0 are
                        // detected as nearest from tiles near col=W-1.
                        if (cylSim) {
                            if (dx >  0.5f) { dx -= 1.0f; }
                            if (dx < -0.5f) { dx += 1.0f; }
                        }
                        const float cs = std::cos(plates[pi].rot);
                        const float sn = std::sin(plates[pi].rot);
                        const float lx = (dx * cs + dy * sn) / plates[pi].aspect;
                        const float ly = (-dx * sn + dy * cs) * plates[pi].aspect;
                        const float dsq = lx * lx + ly * ly;
                        if (dsq < d1Sq) {
                            d2Sq = d1Sq; second = nearest;
                            d1Sq = dsq;  nearest = static_cast<int32_t>(pi);
                        } else if (dsq < d2Sq) {
                            d2Sq = dsq;  second = static_cast<int32_t>(pi);
                        }
                    }
                    if (nearest < 0 || second < 0) { continue; }
                    const float d1 = std::sqrt(d1Sq);
                    const float d2 = std::sqrt(d2Sq);
                    // Wider boundary band (0.65 cutoff vs 0.85) so the
                    // orogenic effect reaches deeper into the plate.
                    // Real volcanic arcs (Andes, Cascades, Rockies)
                    // form ~100-300 km INLAND from the trench, not at
                    // the coast — the subducting slab needs depth to
                    // reach the melting zone. Wide band reproduces this.
                    const float boundary = (d2 > 0.0001f)
                        ? std::clamp((d1 / d2 - 0.65f) / 0.35f, 0.0f, 1.0f)
                        : 0.0f;
                    if (boundary < 0.10f) { continue; }
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
                        const float c = fractalNoise(
                            lxp * 4.5f + p.seedX,
                            lyp * 4.5f + p.seedY,
                            4, 2.0f, 0.55f, crustRng);
                        return c > (1.0f - p.landFraction);
                    };
                    const bool A_land = sampleCrustLand(A, nx, ny);
                    const bool B_land = sampleCrustLand(B, nx, ny);
                    if (stress > STRESS_GATE) {
                        // Convergent.
                        if (A_land && B_land) {
                            // Continent–continent collision. Both crumple.
                            // Himalayas-style — broad, jumbled, very tall.
                            orogeny[idx] += 0.09f * bandWeight * stress;
                        } else if (A_land && !B_land) {
                            // Oceanic B subducts under continental A.
                            // Volcanic arc on A side (Andes / Cascades /
                            // Rockies). Boosted vs continental collision
                            // because subducting slab releases water →
                            // partial melting → vigorous volcanism.
                            orogeny[idx] += 0.13f * bandWeight * stress;
                        } else if (!A_land && B_land) {
                            // Oceanic A subducts. Trench on A's side
                            // (Mariana / Peru-Chile trench analog).
                            orogeny[idx] -= 0.07f * bandWeight * stress;
                        } else {
                            // Oceanic–oceanic. Denser/older subducts;
                            // we don't track plate age, so use the
                            // plate with lower landFraction as denser.
                            // Overriding plate gets a small island-arc
                            // uplift (Japan / Aleutian / Mariana scale).
                            const bool aDenser = (A.landFraction <= B.landFraction);
                            if (aDenser) {
                                // A subducts → uplift on A's side small,
                                // arc on overriding side. We're on A's
                                // territory (Voronoi nearest), so dig
                                // a slight trench here.
                                orogeny[idx] -= 0.02f * bandWeight * stress;
                            } else {
                                // A overrides B → small arc uplift on A.
                                orogeny[idx] += 0.04f * bandWeight * stress;
                            }
                        }
                    } else if (stress < -STRESS_GATE) {
                        // Divergent.
                        if (A_land && B_land) {
                            orogeny[idx] += 0.03f * bandWeight * stress;
                        }
                    }
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
            std::vector<float> slabPullX(plates.size(), 0.0f);
            std::vector<float> slabPullY(plates.size(), 0.0f);
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
                    }
                }
            }
            // Apply slab pull as a small velocity nudge. Magnitude
            // tuned to be small per-epoch; over many epochs a heavily
            // subducting plate will visibly accelerate.
            for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                const float pullLen = std::sqrt(
                    slabPullX[pi] * slabPullX[pi] + slabPullY[pi] * slabPullY[pi]);
                if (pullLen < 1.0f) { continue; }
                constexpr float SLAB_PULL_GAIN = 0.012f;
                plates[pi].vx += (slabPullX[pi] / pullLen) * SLAB_PULL_GAIN;
                plates[pi].vy += (slabPullY[pi] / pullLen) * SLAB_PULL_GAIN;
                // Clamp velocity to prevent runaway acceleration.
                const float vLen = std::sqrt(
                    plates[pi].vx * plates[pi].vx + plates[pi].vy * plates[pi].vy);
                if (vLen > 1.2f) {
                    plates[pi].vx *= (1.2f / vLen);
                    plates[pi].vy *= (1.2f / vLen);
                }
            }
            // Mantle drag — gentle damping per epoch. Without this,
            // velocity perturbations accumulate; with this, plates
            // settle into roughly steady-state motion over time.
            for (Plate& p : plates) {
                p.vx *= 0.995f;
                p.vy *= 0.995f;
            }

            // Hotspot trails. For each hotspot, find the plate above
            // it RIGHT NOW and record the hotspot's position in that
            // plate's LOCAL frame. As the plate drifts, future epochs
            // record a different plate-local coord (since the plate
            // moved) → trail forms. At the elevation pass we sample
            // each plate's trail to add small volcanic island bumps.
            for (const Hotspot& h : hotspots) {
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
            for (float& v : orogeny) {
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
        for (float& v : orogeny) {
            v = std::clamp(v, -0.20f, 0.32f);
        }
        // Erosion pass count scales with simulated age (EPOCHS): a
        // long sim represents hundreds of My of weathering, so older
        // worlds get more blur passes → softer mountains. Newer
        // (Pangea-style short sim) keeps sharper relief.
        // Erosion: 1 pass per ~10 epochs, 1-4 max. Older worlds get
        // softer mountain ranges (smoothing > 100 My old ranges down
        // toward Appalachian-like rolling hills).
        // Orogeny blur passes: spread peak orogeny into surrounding
        // tiles so mountain → foothill → plains forms a smooth gradient.
        // Wider spread (max 6 passes vs 4) → broader Hills aprons
        // around active ranges, matching real-world geography where
        // every major mountain has a band of foothills.
        const int32_t erosionPasses = std::clamp(EPOCHS / 8, 2, 6);
        for (int passN = 0; passN < erosionPasses; ++passN) {
            std::vector<float> blurred = orogeny;
            for (int32_t row = 0; row < height; ++row) {
                for (int32_t col = 0; col < width; ++col) {
                    float sum = 0.0f;
                    int32_t cnt = 0;
                    for (int32_t dr = -1; dr <= 1; ++dr) {
                        for (int32_t dc = -1; dc <= 1; ++dc) {
                            const int32_t r2 = row + dr;
                            const int32_t c2 = col + dc;
                            if (r2 < 0 || r2 >= height || c2 < 0 || c2 >= width) {
                                continue;
                            }
                            sum += orogeny[static_cast<std::size_t>(
                                r2 * width + c2)];
                            ++cnt;
                        }
                    }
                    blurred[static_cast<std::size_t>(row * width + col)] =
                        (cnt > 0) ? sum / static_cast<float>(cnt) : 0.0f;
                }
            }
            orogeny.swap(blurred);
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
                    const float dsq = lx * lx + ly * ly;
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
                    lxN * 4.5f + fp.seedX,
                    lyN * 4.5f + fp.seedY,
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
                        const float dsq = lx * lx + ly * ly;
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
                            lxNearest * 4.5f + pNearest.seedX,
                            lyNearest * 4.5f + pNearest.seedY,
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
                                // Age factor: most-recent point (end
                                // of vector) is highest; oldest is most
                                // eroded. Linear falloff over trail.
                                const float ageT = static_cast<float>(k + 1)
                                                 / static_cast<float>(trailLen);
                                const float t = 1.0f - trdist / TR_RADIUS;
                                edgeFalloff += 0.18f * t * t * (0.4f + 0.6f * ageT);
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
                // Wider noise amplitude (0.28 vs 0.14) lets fractal
                // noise carve bays and inlets into land plate edges
                // while the narrowed hard floor still protects deep
                // interiors from becoming inland seas.
                elev = edgeFalloff + noiseCentred * 0.28f + oro;
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
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const float nx = static_cast<float>(col) / static_cast<float>(width);
                const float ny = static_cast<float>(row) / static_cast<float>(height);
                // Match the gentle two-octave warp from the elevation
                // pass so overlay tints / borders trace the actual
                // continent outlines smoothly.
                const float pwX1 =
                    (fractalNoise(nx * 1.3f, ny * 1.3f, 4, 2.0f, 0.55f, plateWarpRng) - 0.5f) * 0.24f;
                const float pwY1 =
                    (fractalNoise(nx * 1.3f + 17.0f, ny * 1.3f + 31.0f, 4, 2.0f, 0.55f, plateWarpRng) - 0.5f) * 0.24f;
                const float pwX2 =
                    (fractalNoise(nx * 3.5f, ny * 3.5f, 2, 2.0f, 0.5f, plateWarpRng) - 0.5f) * 0.06f;
                const float pwY2 =
                    (fractalNoise(nx * 3.5f + 9.0f, ny * 3.5f + 21.0f, 2, 2.0f, 0.5f, plateWarpRng) - 0.5f) * 0.06f;
                const float pwx = nx + pwX1 + pwX2;
                const float pwy = ny + pwY1 + pwY2;
                float d1Sq = 1e9f;
                int32_t nearest = -1;
                for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                    float dx = pwx - plates[pi].cx;
                    float dy = pwy - plates[pi].cy;
                    if (cylSim) {
                        if (dx >  0.5f) { dx -= 1.0f; }
                        if (dx < -0.5f) { dx += 1.0f; }
                    }
                    const float cs = std::cos(plates[pi].rot);
                    const float sn = std::sin(plates[pi].rot);
                    const float lx = (dx * cs + dy * sn) / plates[pi].aspect;
                    const float ly = (-dx * sn + dy * cs) * plates[pi].aspect;
                    const float dsq = lx * lx + ly * ly;
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

        // Plate-id boundary smoothing. The domain warp can flip
        // individual tiles near a Voronoi seam into a different plate,
        // creating noisy 1-2 tile "arms" jutting from a plate's edge.
        // Two majority-vote passes: a tile whose plate id disagrees
        // with ≥ 4 of its 6 neighbours adopts the neighbour majority.
        // Smooths plate edges without dragging in distant plates.
        for (int32_t pass = 0; pass < 2; ++pass) {
            std::vector<uint8_t> nextId(static_cast<std::size_t>(width * height), 0xFFu);
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
                    if (bestId != my && bestCount >= 4) {
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
        motions.reserve(plates.size());
        centers.reserve(plates.size());
        for (const Plate& p : plates) {
            motions.emplace_back(p.vx, p.vy);
            centers.emplace_back(p.cx, p.cy);
        }
        grid.setPlateMotions(std::move(motions));
        grid.setPlateCenters(std::move(centers));
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

            // Mountain assignment: require BOTH the percentile gate
            // (top mountainRatio % of elevation) AND a minimum
            // orogeny floor. Eroded ranges drop below the orogeny
            // floor → fall into the Hills feature pass below, not
            // Mountain. Active boundaries with fresh orogeny still
            // qualify and form sharp ranges.
            const float oroAt = orogeny[static_cast<std::size_t>(index)];
            if (mountainElev[static_cast<std::size_t>(index)] >= mountainThreshold
                && oroAt >= MIN_OROGENY_FOR_MOUNTAIN) {
                grid.setTerrain(index, TerrainType::Mountain);
                grid.setElevation(index, 3);
                continue;
            }

            float nx = static_cast<float>(col) / static_cast<float>(width);
            float ny = static_cast<float>(row) / static_cast<float>(height);

            // ---- Temperature ----
            // latFromEquator ∈ [0,1]: 0 = equator (row = height/2), 1 = pole.
            const float latFromEquator = 2.0f * std::abs(ny - 0.5f); // 0=eq,1=pole
            // Cosine curve: warmer equator, colder poles.
            float temperature = std::cos(latFromEquator * 1.5708f); // cos(π/2 * lat)
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

            temperature = std::clamp(temperature + currentTempDelta, 0.0f, 1.0f);

            // Wind-driven moisture: positive when prevailing wind brings
            // ocean air across few/no mountains; negative inland of a
            // mountain wall (rain shadow). Already accounts for wind
            // direction by latitude band.
            const float windMoistTile = windMoist[static_cast<std::size_t>(index)];

            const float moisture = std::clamp(
                moistureBase - continentalFactor * 0.32f
                + currentMoistDelta
                + windMoistTile * 0.45f
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
    constexpr int32_t SHALLOW_BFS_MAX = 4; // BFS depth limit (max possible threshold)

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

            // Hills
            if (featureRng.chance(config.hillRatio)) {
                grid.setFeature(index, FeatureType::Hills);
                continue;
            }

            // Forest/Jungle based on terrain temperature
            if (terrain == TerrainType::Grassland || terrain == TerrainType::Plains) {
                if (featureRng.chance(config.forestRatio)) {
                    // Latitude determines forest vs jungle
                    float latitudeT = static_cast<float>(row) / static_cast<float>(height);
                    float temperature = 1.0f - 2.0f * std::abs(latitudeT - 0.5f);
                    if (temperature > 0.65f) {
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

    // Rebuild plate and boundary data (lightweight, same seed path guarantees consistency).
    // We re-derive from the same rng sequence used in generate(), but since rng has advanced
    // past terrain generation, we use a fresh sub-rng seeded from the current state.
    aoc::Random plateRng(rng.next());
    const int32_t plateCount = plateRng.nextInt(4, 6);

    struct PlateSeed {
        float x;
        float y;
        float driftX;
        float driftY;
    };

    std::vector<PlateSeed> plates(static_cast<std::size_t>(plateCount));
    for (int32_t p = 0; p < plateCount; ++p) {
        plates[static_cast<std::size_t>(p)].x = plateRng.nextFloat(0.05f, 0.95f);
        plates[static_cast<std::size_t>(p)].y = plateRng.nextFloat(0.05f, 0.95f);
        const float angle = plateRng.nextFloat(0.0f, 6.28318f);
        plates[static_cast<std::size_t>(p)].driftX = std::cos(angle);
        plates[static_cast<std::size_t>(p)].driftY = std::sin(angle);
    }

    std::vector<int32_t> plateId(static_cast<std::size_t>(tileCount));
    std::vector<BoundaryType> boundary(static_cast<std::size_t>(tileCount), BoundaryType::None);

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

                const PlateSeed& myP = plates[static_cast<std::size_t>(myPlate)];
                const PlateSeed& nP  = plates[static_cast<std::size_t>(nPlate)];
                const float towardX = nP.x - myP.x;
                const float towardY = nP.y - myP.y;
                const float dotMy = myP.driftX * towardX + myP.driftY * towardY;
                const float dotN  = nP.driftX * (-towardX) + nP.driftY * (-towardY);

                if (dotMy > 0.0f && dotN > 0.0f) {
                    boundary[static_cast<std::size_t>(index)] = BoundaryType::Convergent;
                } else if (dotMy < 0.0f && dotN < 0.0f) {
                    boundary[static_cast<std::size_t>(index)] = BoundaryType::Divergent;
                } else {
                    boundary[static_cast<std::size_t>(index)] = BoundaryType::Transform;
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

            ResourceId placed{};

            // Volcanic zone: convergent + high elevation.
            // WP-B3: Rare Earth deposit at 2% on these high-elev convergent
            // tiles — fits the "mountain + volcanic" geology (real REE
            // deposits cluster near carbonatites / alkaline intrusives).
            if (bType == BoundaryType::Convergent && elev >= 2) {
                if (resRng.chance(0.05f)) {
                    placed = ResourceId{aoc::sim::goods::GOLD_ORE};
                } else if (resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::SILVER_ORE};
                } else if (resRng.chance(0.05f)) {
                    placed = ResourceId{aoc::sim::goods::ALUMINUM};
                } else if (resRng.chance(0.02f)) {
                    placed = ResourceId{aoc::sim::goods::RARE_EARTH};
                }
            }
            // Convergent boundary (mountain range).
            // WP-C2 cut: GEMS slot redirected to TIN.
            else if (bType == BoundaryType::Convergent) {
                if (resRng.chance(0.06f)) {
                    placed = ResourceId{aoc::sim::goods::COPPER_ORE};
                } else if (resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::GOLD_ORE};
                } else if (resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::SILVER_ORE};
                } else if (resRng.chance(0.05f)) {
                    placed = ResourceId{aoc::sim::goods::TIN};
                }
            }
            // Divergent boundary (rift zone)
            else if (bType == BoundaryType::Divergent) {
                if (resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::COPPER_ORE};
                } else if (elev <= 0 && resRng.chance(0.03f)) {
                    placed = ResourceId{aoc::sim::goods::OIL};
                }
            }
            // Continental interior (no boundary)
            else if (bType == BoundaryType::None) {
                // Sedimentary basin: low elevation interior (oil + gas +
                // coal + niter).  Oil + gas rates bumped so strategic energy
                // resources actually appear on typical continents instead of
                // depending on the rare elev==0 interior subset alone.
                if (elev <= 1) {
                    if (resRng.chance(0.10f)) {
                        placed = ResourceId{aoc::sim::goods::OIL};
                    } else if (resRng.chance(0.06f)) {
                        placed = ResourceId{aoc::sim::goods::NATURAL_GAS};
                    } else if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::COAL};
                    } else if (resRng.chance(0.03f)) {
                        placed = ResourceId{aoc::sim::goods::NITER};
                    }
                }
                // Continental shield (higher elevation interior)
                else {
                    if (resRng.chance(0.08f)) {
                        placed = ResourceId{aoc::sim::goods::IRON_ORE};
                    } else if (resRng.chance(0.06f)) {
                        placed = ResourceId{aoc::sim::goods::STONE};
                    } else if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::COAL};
                    }
                }
            }

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

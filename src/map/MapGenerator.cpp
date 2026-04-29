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

    // Natural fish spots: seed FISH on Coast / ShallowWater tiles so the
    // fishing chain has reachable spots from the start, not only via
    // Fishing Boats improvement on empty coast. Coast tiles 4%, shallows
    // 3%. Skips tiles that already carry a resource.
    {
        aoc::Random fishRng(config.seed ^ 0x46495348u);  // "FISH"
        const int32_t tiles = outGrid.tileCount();
        for (int32_t i = 0; i < tiles; ++i) {
            const TerrainType t = outGrid.terrain(i);
            if (t != TerrainType::Coast && t != TerrainType::ShallowWater) {
                continue;
            }
            if (outGrid.resource(i).isValid()) { continue; }
            const float p = (t == TerrainType::Coast) ? 0.04f : 0.03f;
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
    };
    std::vector<Plate> plates;

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
            constexpr int32_t PLATE_COUNT = 14;
            for (int32_t i = 0; i < PLATE_COUNT; ++i) {
                Plate p;
                p.cx = centerRng.nextFloat(0.04f, 0.96f);
                p.cy = centerRng.nextFloat(0.04f, 0.96f);
                p.isLand = false;
                plates.push_back(p);
            }
            // Mark ~45% of plates as land. Random selection ensures
            // continents don't always sit in the same plate slots.
            std::vector<int32_t> indices;
            indices.reserve(static_cast<std::size_t>(PLATE_COUNT));
            for (int32_t i = 0; i < PLATE_COUNT; ++i) { indices.push_back(i); }
            for (std::size_t i = indices.size(); i > 1; --i) {
                const std::size_t j = static_cast<std::size_t>(
                    centerRng.nextInt(0, static_cast<int32_t>(i) - 1));
                std::swap(indices[i - 1], indices[j]);
            }
            const int32_t landCount = static_cast<int32_t>(PLATE_COUNT * 0.45f);
            for (int32_t i = 0; i < landCount; ++i) {
                plates[static_cast<std::size_t>(indices[static_cast<std::size_t>(i)])]
                    .isLand = true;
            }
            // Effective water ratio is now governed by plate flags +
            // noise. A modest cutoff bump cleans up small lakes inside
            // ocean cells without re-flooding land plates.
            effectiveWaterRatio = std::clamp(config.waterRatio, 0.40f, 0.55f);
            break;
        }
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
                aoc::Random warpRng(noiseRng);
                const float warpX1 =
                    (fractalNoise(nx * 1.7f, ny * 1.7f, 4, 2.0f, 0.5f, warpRng) - 0.5f) * 0.85f;
                const float warpY1 =
                    (fractalNoise(nx * 1.7f + 17.0f, ny * 1.7f + 31.0f, 4, 2.0f, 0.5f, warpRng) - 0.5f) * 0.85f;
                const float warpX2 =
                    (fractalNoise(nx * 5.0f, ny * 5.0f, 2, 2.0f, 0.5f, warpRng) - 0.5f) * 0.20f;
                const float warpY2 =
                    (fractalNoise(nx * 5.0f + 9.0f, ny * 5.0f + 21.0f, 2, 2.0f, 0.5f, warpRng) - 0.5f) * 0.20f;
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
                    for (std::size_t pi = 0; pi < plates.size(); ++pi) {
                        const float dx = wx - plates[pi].cx;
                        const float dy = wy - plates[pi].cy;
                        const float dsq = dx * dx + dy * dy;
                        if (dsq < d1Sq) {
                            d2Sq    = d1Sq;
                            second  = nearest;
                            d1Sq    = dsq;
                            nearest = static_cast<int32_t>(pi);
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
                        const float boundary = (d2 > 0.0001f)
                            ? std::clamp((d1 / d2 - 0.85f) / 0.15f, 0.0f, 1.0f)
                            : 0.0f;
                        const float landBase  = 0.85f;  // strong inland height
                        const float oceanBase = 0.10f;  // deep ocean
                        const bool nearestIsLand = plates[static_cast<std::size_t>(nearest)].isLand;
                        const float nearestHeight = nearestIsLand ? landBase : oceanBase;
                        const bool secondIsLand =
                            (second >= 0) && plates[static_cast<std::size_t>(second)].isLand;
                        const float secondHeight = secondIsLand ? landBase : oceanBase;
                        edgeFalloff = nearestHeight * (1.0f - boundary)
                                    + secondHeight * boundary;
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

            // Blend noise + plate/falloff. Continents now use a
            // plate-dominant blend (35/65) so ocean plates stay deep
            // and land plates form coherent landmasses; otherwise noise
            // would chew holes in continents and bridge gaps between
            // them.  Other land-shaped types keep the older balance.
            if (config.mapType == MapType::Fractal) {
                elev = elev * 0.6f + edgeFalloff * 0.4f;
            } else if (config.mapType == MapType::Continents) {
                elev = elev * 0.35f + edgeFalloff * 0.65f;
            } else {
                elev = elev * 0.55f + edgeFalloff * 0.45f;
            }

            elevationMap[static_cast<std::size_t>(row * width + col)] = elev;
        }
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
    // Peak bonus at dist 3, falls off by dist 8.  Beyond that, slight
    // penalty so the interior is less mountain-dominated.  Water tiles
    // unchanged (they'll be reclassified below).
    std::vector<float> mountainElev = elevationMap;
    for (int32_t i = 0; i < totalTiles; ++i) {
        if (distFromCoast[static_cast<size_t>(i)] <= 0) { continue; }  // water
        const int32_t d = distFromCoast[static_cast<size_t>(i)];
        float bonus = 0.0f;
        if (d >= 2 && d <= 6) {
            // Gaussian-ish hump peaked at 3-4 hexes inland.
            const float peak = 4.0f;
            const float sigma = 2.5f;
            const float x = (static_cast<float>(d) - peak) / sigma;
            bonus = 0.18f * std::exp(-x * x);
        } else if (d > 8) {
            bonus = -0.05f;
        }
        mountainElev[static_cast<size_t>(i)] += bonus;
    }

    // Mountain threshold computed from the coastal-bias-adjusted elevation.
    std::vector<float> sortedMountainElev(mountainElev);
    std::sort(sortedMountainElev.begin(), sortedMountainElev.end());
    std::size_t mountainCutoff = sortedMountainElev.size() -
        static_cast<std::size_t>(config.mountainRatio * static_cast<float>(sortedMountainElev.size()));
    float mountainThreshold = sortedMountainElev[std::min(mountainCutoff, sortedMountainElev.size() - 1)];

    // Temperature map (latitude-based + noise)
    aoc::Random tempRng(rng.next());

    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            int32_t index = row * width + col;
            float elev = elevationMap[static_cast<std::size_t>(index)];

            if (elev < waterThreshold) {
                // Deep vs shallow water
                if (elev < waterThreshold * 0.6f) {
                    grid.setTerrain(index, TerrainType::Ocean);
                } else {
                    grid.setTerrain(index, TerrainType::Coast);
                }
                grid.setElevation(index, -1);
                continue;
            }

            // Use the coastal-bias-adjusted elevation for mountain test so
            // ridges form preferentially in young-margin bands, not center.
            if (mountainElev[static_cast<std::size_t>(index)] >= mountainThreshold) {
                grid.setTerrain(index, TerrainType::Mountain);
                grid.setElevation(index, 3);
                continue;
            }

            // Temperature based on latitude + noise
            float latitudeT = static_cast<float>(row) / static_cast<float>(height);
            // 0 = top (cold), 0.5 = equator (hot), 1.0 = bottom (cold)
            float temperature = 1.0f - 2.0f * std::abs(latitudeT - 0.5f);
            float nx = static_cast<float>(col) / static_cast<float>(width);
            float ny = static_cast<float>(row) / static_cast<float>(height);
            temperature += (fractalNoise(nx, ny, 3, 5.0f, 0.5f, tempRng) - 0.5f) * 0.3f;
            temperature = std::clamp(temperature, 0.0f, 1.0f);

            // Assign terrain based on temperature
            TerrainType terrain;
            // Tightened biome cuts to reduce desert + arctic dominance.
            // User feedback: too much desert, too much unusable terrain.
            if (temperature < 0.10f) {
                terrain = TerrainType::Snow;
            } else if (temperature < 0.22f) {
                terrain = TerrainType::Tundra;
            } else if (temperature > 0.88f) {  // was 0.80 — now narrower desert band
                terrain = TerrainType::Desert;
            } else if (temperature > 0.55f) {
                terrain = TerrainType::Plains;
            } else {
                terrain = TerrainType::Grassland;
            }

            grid.setTerrain(index, terrain);
            grid.setElevation(index, static_cast<int8_t>(
                std::clamp(static_cast<int>(elev * 4.0f), 0, 2)));
        }
    }

    // WP-I rain-shadow + wind pass. Walk 1-2 tiles upwind of each land
    // tile; if any is Mountain, convert the tile to Desert at a seeded
    // probability. Wind direction derives from latitude band:
    //   row < 20% OR row > 80%  -> polar easterly (upwind = +q)
    //   row 20-50%              -> westerlies  (upwind = -q)
    //   row 50-80%              -> westerlies  (upwind = -q)
    //   row 40-60%              -> trade wind easterly (upwind = +q)
    // Net: equator + polar caps pull from east; mid-lats pull from west.
    aoc::Random shadowRng(config.seed ^ 0x52415348u);  // "RASH"
    for (int32_t row = 0; row < height; ++row) {
        const float latT = static_cast<float>(row) / static_cast<float>(height);
        // Equator or polar → trade/polar easterly. Mid-lat → westerly.
        const bool easterly = (latT < 0.20f) || (latT > 0.80f)
                              || (latT >= 0.40f && latT <= 0.60f);
        const int32_t upwindDQ = easterly ? 1 : -1;

        for (int32_t col = 0; col < width; ++col) {
            const int32_t index = row * width + col;
            const TerrainType t = grid.terrain(index);
            if (t != TerrainType::Grassland && t != TerrainType::Plains) {
                continue;
            }
            // Walk 1..2 tiles upwind (same row, shifted column).
            bool mountainUpwind = false;
            for (int32_t step = 1; step <= 2; ++step) {
                const int32_t uCol = col + upwindDQ * step;
                if (uCol < 0 || uCol >= width) { continue; }
                const int32_t uIdx = row * width + uCol;
                if (grid.terrain(uIdx) == TerrainType::Mountain) {
                    mountainUpwind = true;
                    break;
                }
            }
            if (!mountainUpwind) { continue; }
            if (!shadowRng.chance(0.10f)) { continue; }  // was 0.25 — too aggressive
            grid.setTerrain(index, TerrainType::Desert);
        }
    }
}

void MapGenerator::smoothCoastlines(HexGrid& grid) {
    // Convert isolated ocean tiles surrounded by land into coast,
    // and isolated land tiles surrounded by water into coast.
    const int32_t width  = grid.width();
    const int32_t height = grid.height();

    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            int32_t index = row * width + col;
            hex::AxialCoord axial = hex::offsetToAxial({col, row});
            std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(axial);

            int waterCount = 0;
            int validCount = 0;

            for (const hex::AxialCoord& n : nbrs) {
                if (!grid.isValid(n)) {
                    continue;
                }
                ++validCount;
                if (isWater(grid.terrain(grid.toIndex(n)))) {
                    ++waterCount;
                }
            }

            if (validCount == 0) {
                continue;
            }

            // If a land tile is mostly surrounded by water, make it coast
            if (!isWater(grid.terrain(index)) && waterCount >= 4) {
                grid.setTerrain(index, TerrainType::Coast);
            }
            // If an ocean tile has land neighbors, it should be coast
            if (grid.terrain(index) == TerrainType::Ocean) {
                int landCount = validCount - waterCount;
                if (landCount >= 1) {
                    grid.setTerrain(index, TerrainType::Coast);
                }
            }
        }
    }

    // Second pass: create ShallowWater ring around Coast tiles.
    // Ocean tiles adjacent to Coast (but not adjacent to land) become ShallowWater.
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            int32_t index = row * width + col;
            if (grid.terrain(index) != TerrainType::Ocean) {
                continue;
            }

            hex::AxialCoord axial = hex::offsetToAxial({col, row});
            std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(axial);

            bool adjacentToCoast = false;
            for (const hex::AxialCoord& n : nbrs) {
                if (!grid.isValid(n)) { continue; }
                if (grid.terrain(grid.toIndex(n)) == TerrainType::Coast) {
                    adjacentToCoast = true;
                    break;
                }
            }

            if (adjacentToCoast) {
                grid.setTerrain(index, TerrainType::ShallowWater);
            }
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

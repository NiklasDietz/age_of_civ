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

    if (config.mapType == MapType::Realistic) {
        generateRealisticTerrain(config, outGrid, rng);
        smoothCoastlines(outGrid);
        assignFeatures(config, outGrid, rng);
        generateRivers(outGrid, rng);
        placeNaturalWonders(outGrid, rng);
        placeGeologyResources(config, outGrid, rng);
    } else {
        assignTerrain(config, outGrid, rng);
        smoothCoastlines(outGrid);
        assignFeatures(config, outGrid, rng);
        generateRivers(outGrid, rng);
        placeNaturalWonders(outGrid, rng);
        placeBasicResources(config, outGrid, rng);
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

    switch (config.mapType) {
        case MapType::Pangaea: {
            // Single strong center
            landCenters.push_back({0.5f, 0.5f, 1.4f});
            break;
        }
        case MapType::Continents: {
            // 2-3 land mass centers spread across the map
            landCenters.push_back({0.30f, 0.40f, 1.2f});
            landCenters.push_back({0.70f, 0.55f, 1.2f});
            landCenters.push_back({0.50f, 0.25f, 0.8f});
            break;
        }
        case MapType::Archipelago: {
            // Many weak centers for small islands
            constexpr int32_t ISLAND_COUNT = 8;
            for (int32_t i = 0; i < ISLAND_COUNT; ++i) {
                float cx = centerRng.nextFloat(0.1f, 0.9f);
                float cy = centerRng.nextFloat(0.1f, 0.9f);
                landCenters.push_back({cx, cy, 0.5f});
            }
            break;
        }
        case MapType::Fractal: {
            // No gradient centers -- pure noise
            break;
        }
        case MapType::Realistic: {
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
                // Compute falloff as max contribution from any land center
                for (const LandCenter& center : landCenters) {
                    float dx = (nx - center.cx) * 2.0f;
                    float dy = (ny - center.cy) * 2.0f;
                    float distFromCenter = std::sqrt(dx * dx + dy * dy);
                    float falloff = 1.0f - std::clamp(distFromCenter * center.strength, 0.0f, 1.0f);
                    falloff = smoothstep(falloff);
                    edgeFalloff = std::max(edgeFalloff, falloff);
                }
            }

            elev = elev * 0.6f + edgeFalloff * 0.4f;

            elevationMap[static_cast<std::size_t>(row * width + col)] = elev;
        }
    }

    // Sort elevations to find the water threshold
    std::vector<float> sortedElevations(elevationMap);
    std::sort(sortedElevations.begin(), sortedElevations.end());
    std::size_t waterCutoff = static_cast<std::size_t>(
        config.waterRatio * static_cast<float>(sortedElevations.size()));
    float waterThreshold = sortedElevations[std::min(waterCutoff, sortedElevations.size() - 1)];

    // Mountain threshold
    std::size_t mountainCutoff = sortedElevations.size() -
        static_cast<std::size_t>(config.mountainRatio * static_cast<float>(sortedElevations.size()));
    float mountainThreshold = sortedElevations[std::min(mountainCutoff, sortedElevations.size() - 1)];

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

            if (elev >= mountainThreshold) {
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
            if (temperature < 0.15f) {
                terrain = TerrainType::Snow;
            } else if (temperature < 0.30f) {
                terrain = TerrainType::Tundra;
            } else if (temperature > 0.80f) {
                terrain = TerrainType::Desert;
            } else if (temperature > 0.50f) {
                terrain = TerrainType::Plains;
            } else {
                terrain = TerrainType::Grassland;
            }

            grid.setTerrain(index, terrain);
            grid.setElevation(index, static_cast<int8_t>(
                std::clamp(static_cast<int>(elev * 4.0f), 0, 2)));
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

    // Simple river generation: start from random high-elevation tiles,
    // flow toward lowest neighbor until reaching water.
    int32_t riverCount = std::max(1, grid.tileCount() / 200);

    for (int32_t r = 0; r < riverCount; ++r) {
        // Find a random land tile with decent elevation
        int32_t attempts = 50;
        int32_t startIndex = -1;
        while (attempts-- > 0) {
            int32_t col = rng.nextInt(0, width - 1);
            int32_t row = rng.nextInt(0, height - 1);
            int32_t index = row * width + col;
            if (!isWater(grid.terrain(index)) && grid.elevation(index) >= 1) {
                startIndex = index;
                break;
            }
        }
        if (startIndex < 0) {
            continue;
        }

        // Flow downhill
        int32_t currentIndex = startIndex;
        int32_t maxSteps = 30;
        while (maxSteps-- > 0) {
            hex::AxialCoord current = grid.toAxial(currentIndex);
            std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(current);

            int32_t bestNeighborIndex = -1;
            int8_t  bestElevation = grid.elevation(currentIndex);
            int     bestDirection = -1;

            for (int dir = 0; dir < 6; ++dir) {
                if (!grid.isValid(nbrs[static_cast<std::size_t>(dir)])) {
                    continue;
                }
                int32_t nIndex = grid.toIndex(nbrs[static_cast<std::size_t>(dir)]);
                int8_t nElev = grid.elevation(nIndex);

                // Prefer lower elevation, with slight randomness for variety
                if (nElev < bestElevation || (nElev == bestElevation && rng.chance(0.3f))) {
                    bestElevation = nElev;
                    bestNeighborIndex = nIndex;
                    bestDirection = dir;
                }
            }

            if (bestDirection < 0) {
                break;
            }

            // Mark river edge on current tile
            uint8_t edges = grid.riverEdges(currentIndex);
            edges |= static_cast<uint8_t>(1u << bestDirection);
            grid.setRiverEdges(currentIndex, edges);

            // Mark reverse edge on neighbor
            int reverseDir = (bestDirection + 3) % 6;
            uint8_t neighborEdges = grid.riverEdges(bestNeighborIndex);
            neighborEdges |= static_cast<uint8_t>(1u << reverseDir);
            grid.setRiverEdges(bestNeighborIndex, neighborEdges);

            // Stop if we reached water
            if (isWater(grid.terrain(bestNeighborIndex))) {
                break;
            }

            currentIndex = bestNeighborIndex;
        }
    }
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

    // Temperature map (latitude-based + noise)
    aoc::Random tempRng(rng.next());

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
                continue;
            }

            // Temperature based on latitude + noise
            const float latitudeT = static_cast<float>(row) / static_cast<float>(height);
            float temperature = 1.0f - 2.0f * std::abs(latitudeT - 0.5f);
            const float tnx = static_cast<float>(col) / static_cast<float>(width);
            const float tny = static_cast<float>(row) / static_cast<float>(height);
            temperature += (fractalNoise(tnx, tny, 3, 5.0f, 0.5f, tempRng) - 0.5f) * 0.3f;
            temperature = std::clamp(temperature, 0.0f, 1.0f);

            TerrainType terrain;
            if (temperature < 0.15f) {
                terrain = TerrainType::Snow;
            } else if (temperature < 0.30f) {
                terrain = TerrainType::Tundra;
            } else if (temperature > 0.80f) {
                terrain = TerrainType::Desert;
            } else if (temperature > 0.50f) {
                terrain = TerrainType::Plains;
            } else {
                terrain = TerrainType::Grassland;
            }

            grid.setTerrain(index, terrain);
            grid.setElevation(index, static_cast<int8_t>(
                std::clamp(static_cast<int>(elev * 4.0f), 0, 2)));
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

            // Volcanic zone: convergent + high elevation
            if (bType == BoundaryType::Convergent && elev >= 2) {
                if (resRng.chance(0.05f)) {
                    placed = ResourceId{aoc::sim::goods::GOLD_ORE};
                } else if (resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::SILVER_ORE};
                } else if (resRng.chance(0.03f)) {
                    placed = ResourceId{aoc::sim::goods::GEMS};
                } else if (resRng.chance(0.02f)) {
                    placed = ResourceId{aoc::sim::goods::ALUMINUM};
                }
            }
            // Convergent boundary (mountain range)
            else if (bType == BoundaryType::Convergent) {
                if (resRng.chance(0.06f)) {
                    placed = ResourceId{aoc::sim::goods::COPPER_ORE};
                } else if (resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::GOLD_ORE};
                } else if (resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::SILVER_ORE};
                } else if (resRng.chance(0.03f)) {
                    placed = ResourceId{aoc::sim::goods::TIN};
                } else if (resRng.chance(0.02f)) {
                    placed = ResourceId{aoc::sim::goods::GEMS};
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
                // Sedimentary basin: low elevation interior
                if (elev <= 0) {
                    if (resRng.chance(0.06f)) {
                        placed = ResourceId{aoc::sim::goods::OIL};
                    } else if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::COAL};
                    } else if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::NATURAL_GAS};
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

            // Climate-based resources (only if no geology resource was placed)
            if (!placed.isValid()) {
                if (terrain == TerrainType::Desert) {
                    if (elev <= 0 && resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::OIL};
                    } else if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::INCENSE};
                    } else if (resRng.chance(0.03f)) {
                        placed = ResourceId{aoc::sim::goods::IVORY};
                    }
                } else if (temperature > 0.65f && terrain != TerrainType::Desert) {
                    // Tropical: luxuries are more abundant here
                    if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::COTTON};
                    } else if (resRng.chance(0.03f)) {
                        placed = ResourceId{aoc::sim::goods::RUBBER};
                    } else if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::SPICES};
                    } else if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::SUGAR};
                    } else if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::TEA};
                    } else if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::COFFEE};
                    } else if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::TOBACCO};
                    } else if (resRng.chance(0.03f)) {
                        placed = ResourceId{aoc::sim::goods::SILK};
                    }
                } else if (temperature >= 0.30f && temperature <= 0.65f) {
                    // Temperate: bonus + luxury resources
                    if (resRng.chance(0.06f)) {
                        placed = ResourceId{aoc::sim::goods::WHEAT};
                    } else if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::WOOD};
                    } else if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::CATTLE};
                    } else if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::WINE};
                    } else if (resRng.chance(0.03f)) {
                        placed = ResourceId{aoc::sim::goods::SALT};
                    } else if (resRng.chance(0.03f)) {
                        placed = ResourceId{aoc::sim::goods::DYES};
                    } else if (grid.feature(index) == FeatureType::Hills && resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::MARBLE};
                    }
                } else if (temperature < 0.30f) {
                    // Cold: furs and gems are more common
                    if (resRng.chance(0.06f)) {
                        placed = ResourceId{aoc::sim::goods::FURS};
                    } else if (resRng.chance(0.03f)) {
                        placed = ResourceId{aoc::sim::goods::GEMS};
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

            // Coast adjacency resources (only if still nothing placed)
            if (!placed.isValid() && nearCoast) {
                if (resRng.chance(0.06f)) {
                    placed = ResourceId{aoc::sim::goods::FISH};
                } else if (resRng.chance(0.02f)) {
                    placed = ResourceId{aoc::sim::goods::SUGAR};
                } else if (resRng.chance(0.02f)) {
                    placed = ResourceId{aoc::sim::goods::PEARLS};
                }
            }

            if (placed.isValid()) {
                grid.setResource(index, placed);
                grid.setReserves(index, aoc::sim::defaultReserves(placed.value));
                ++totalPlaced;
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
                if      (resRng.chance(0.06f)) { placed = ResourceId{aoc::sim::goods::IRON_ORE}; }
                else if (resRng.chance(0.05f)) { placed = ResourceId{aoc::sim::goods::COPPER_ORE}; }
                else if (resRng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::SILVER_ORE}; }
                else if (resRng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::GOLD_ORE}; }
            } else {
                if      (resRng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::IRON_ORE}; }
                else if (resRng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::COPPER_ORE}; }
                else if (resRng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::SILVER_ORE}; }
                else if (resRng.chance(0.01f)) { placed = ResourceId{aoc::sim::goods::GOLD_ORE}; }
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
            // Desert: oil, incense
            else if (terrain == TerrainType::Desert) {
                if (rng.chance(0.04f))      { placed = ResourceId{aoc::sim::goods::OIL}; }
                else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::INCENSE}; }
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
            // Grassland: food, cotton, horses
            else if (terrain == TerrainType::Grassland) {
                if (rng.chance(0.06f))      { placed = ResourceId{aoc::sim::goods::WHEAT}; }
                else if (rng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::CATTLE}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::COTTON}; }
                else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::HORSES}; }
            }
            // Plains: food, stone, horses, niter
            else if (terrain == TerrainType::Plains) {
                if (rng.chance(0.05f))      { placed = ResourceId{aoc::sim::goods::WHEAT}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::HORSES}; }
                else if (rng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::STONE}; }
                else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::NITER}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::WOOD}; }
            }
            // Tundra: furs, gems
            else if (terrain == TerrainType::Tundra) {
                if (rng.chance(0.04f))      { placed = ResourceId{aoc::sim::goods::FURS}; }
                else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::GEMS}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::COAL}; }
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

    (void)config;
    LOG_INFO("Basic resource placement: %d resources placed (%d on mountains)",
             totalPlaced, mountainMetalsPlaced);
}

} // namespace aoc::map

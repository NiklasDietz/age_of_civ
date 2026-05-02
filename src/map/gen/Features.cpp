/**
 * @file Features.cpp
 * @brief Coastline smoothing, terrain feature assignment, natural-wonder
 *        placement. Extracted 2026-05-02 from MapGenerator.cpp during the
 *        gen/ split.
 */

#include "aoc/map/MapGenerator.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/gen/Noise.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef AOC_HAS_OPENMP
#  include <omp.h>
#  define AOC_PARALLEL_FOR_ROWS _Pragma("omp parallel for schedule(static)")
#else
#  define AOC_PARALLEL_FOR_ROWS
#endif

namespace aoc::map {

using gen::hashNoise;
using gen::smoothstep;
using gen::lerp;

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

} // namespace aoc::map

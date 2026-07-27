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
#include <omp.h>
#define AOC_PARALLEL_FOR_ROWS _Pragma("omp parallel for schedule(static)")
#else
#define AOC_PARALLEL_FOR_ROWS
#endif

namespace aoc::map {

using gen::fractalNoise;
using gen::hashNoise;
using gen::lerp;
using gen::smoothstep;

namespace {

/// Depth of the shelf break, metres. The outer edge of the continental shelf
/// sits near 140 m worldwide -- which is also roughly the Last Glacial Maximum
/// lowstand, because the shelves were exposed land then. Below it the
/// continental slope begins and the sea floor drops to the abyss.
constexpr float SHELF_BREAK_DEPTH_M = 140.0f;

/// One unitless elevation step in metres (surfaceElevationM / 5000).
constexpr float ELEV_UNIT_TO_M = 5000.0f;

/// continentalFraction at or above which a column counts as continental crust
/// rather than oceanic. Matches the 0.5 cut every other pass uses.
constexpr float CONTINENTAL_CRUST_FRACTION = 0.5f;

/// Depth below the sea-level cut at tile `i`, in metres. Falls back to zero
/// depth when the elevation field is absent or short -- "exactly at sea level"
/// is the least-wrong answer for a grid built without a generator pass, and it
/// classifies as shelf rather than abyss.
[[nodiscard]] float depthMAt(const std::vector<float>& elevationMap, float waterThreshold,
                             int32_t i) {
    const std::size_t idx = static_cast<std::size_t>(i);
    if (idx >= elevationMap.size()) {
        return 0.0f;
    }
    return (waterThreshold - elevationMap[idx]) * ELEV_UNIT_TO_M;
}

} // namespace

void MapGenerator::smoothCoastlines(HexGrid& grid, const TerrainFields& fields) {
    // BFS from land tiles outward into the ocean, giving each water tile its
    // ring distance from the nearest coast.
    //
    // The ring distance no longer DECIDES anything -- the shelf test below is on
    // crustal composition. It survives for two reasons: it excludes open ocean
    // from reclassification (so a landlocked sea far from any coast is left
    // alone), and it buckets the AOC_DUMP_SHELF depth histogram, which is how the
    // missing shelf population was measured in the first place.
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
        if (d >= SHALLOW_BFS_MAX) {
            continue;
        }
        const int32_t col           = idx % width;
        const int32_t row           = idx / width;
        const hex::AxialCoord axial = hex::offsetToAxial({col, row});
        for (const hex::AxialCoord& n : hex::neighbors(axial)) {
            if (!grid.isValid(n)) {
                continue;
            }
            const int32_t ni = grid.toIndex(n);
            if (distFromLand[static_cast<std::size_t>(ni)] >= 0) {
                continue;
            }
            distFromLand[static_cast<std::size_t>(ni)] = d + 1;
            queue.push_back(ni);
        }
    }

    // Shelf from BATHYMETRY.
    //
    // 2026-07-27. This used to be a BFS ring 1-4 tiles wide whose width came
    // from `fractalNoise(nx*5, ny*5)`, with no coupling whatsoever to
    // surfaceElevationM, crustThicknessKm or continentalFraction -- the
    // continental shelf was a cosmetic band drawn around the coastline. Two
    // further bugs made it worse than intended: the lattice seed was derived
    // from the grid DIMENSIONS alone, so every world of a given size got the
    // same shelf pattern regardless of seed, and `fractalNoise` redrew its
    // lattice per call, so the "low-frequency" field was per-tile white noise.
    //
    // A continental shelf is the FLOODED PART OF CONTINENTAL CRUST. That is the
    // definition, and it is what the criterion tests: water sitting on crust
    // with continentalFraction >= 0.5 is shelf, everything else is ocean. Shelf
    // extent is therefore a consequence of where the continent-ocean crustal
    // boundary fell relative to the solved sea level, which is the real
    // mechanism -- Earth has ~41 % continental crust but only ~29 % emergent,
    // and that missing third IS the shelf.
    //
    // Why not the textbook 140 m shelf-break depth, which is the more obvious
    // test. Measured with AOC_DUMP_SHELF on seeds 42/7/100: the FIRST water tile
    // adjacent to land is already at a median depth of 2372-3409 m, and the 90th
    // percentile of the first ring is 4357-4863 m. The coast drops straight to
    // the abyss -- this hypsometry contains no shelf population at all, because
    // continental freeboard is several km too high and there is no thinned
    // continental margin between craton and ocean floor. A depth cut is correct
    // physics against a broken hypsometry: it yields 1.0-1.9 % of water area as
    // shelf against Earth's ~7.5 %. Composition asks a question the current
    // state can answer honestly; depth cannot until the elevation law is fixed,
    // at which point the depth test becomes the better one and should replace
    // this. SHELF_BREAK_DEPTH_M and the AOC_DUMP_SHELF ring histogram are kept
    // so that transition is a measurement rather than a guess.
    for (int32_t i = 0; i < total; ++i) {
        if (!isWater(grid.terrain(i))) {
            continue;
        }
        const int32_t d = distFromLand[static_cast<std::size_t>(i)];
        if (d <= 0) {
            continue;
        }
        const std::size_t idx = static_cast<std::size_t>(i);
        const float contFrac =
            (idx < fields.continentalFraction.size()) ? fields.continentalFraction[idx] : 0.0f;
        if (contFrac >= CONTINENTAL_CRUST_FRACTION) {
            grid.setTerrain(i, TerrainType::ShallowWater);
        } else {
            grid.setTerrain(i, TerrainType::Ocean);
        }
    }

    if (std::getenv("AOC_DUMP_SHELF") != nullptr) {
        // Depth distribution of water by ring distance from land, so the
        // SHELF_BREAK_DEPTH_M cut can be checked against the bathymetry the
        // physics actually produces rather than assumed.
        for (int32_t ring = 1; ring <= SHALLOW_BFS_MAX; ++ring) {
            std::vector<float> depths;
            for (int32_t i = 0; i < total; ++i) {
                if (distFromLand[static_cast<std::size_t>(i)] != ring) {
                    continue;
                }
                depths.push_back(depthMAt(fields.elevationMap, fields.waterThreshold, i));
            }
            std::sort(depths.begin(), depths.end());
            if (depths.empty()) {
                continue;
            }
            std::fprintf(stderr, "[shelf] ring %d n=%zu depth m:", ring, depths.size());
            for (const int32_t p : {10, 50, 90}) {
                const std::size_t k =
                    std::min(depths.size() - 1,
                             static_cast<std::size_t>(static_cast<double>(p) / 100.0 *
                                                      static_cast<double>(depths.size() - 1)));
                std::fprintf(stderr, "  p%d=%.0f", p, static_cast<double>(depths[k]));
            }
            std::fprintf(stderr, "\n");
        }
        std::size_t shelf     = 0;
        std::size_t water     = 0;
        std::size_t byDepth   = 0;
        for (int32_t i = 0; i < total; ++i) {
            if (!isWater(grid.terrain(i))) {
                continue;
            }
            ++water;
            if (grid.terrain(i) == TerrainType::ShallowWater) {
                ++shelf;
            }
            // What the textbook shelf-break depth would select. Once the
            // elevation law produces a real shelf population these two numbers
            // converge, and the depth test -- which is the better criterion --
            // can replace the composition test. Until then the gap IS the
            // measurement of the missing hypsometry.
            if (depthMAt(fields.elevationMap, fields.waterThreshold, i)
                < SHELF_BREAK_DEPTH_M) {
                ++byDepth;
            }
        }
        std::fprintf(stderr, "[shelf] by 140 m depth cut it would be %zu (%.1f%%)\n", byDepth,
                     100.0 * static_cast<double>(byDepth)
                         / static_cast<double>(std::max<std::size_t>(1, water)));
        std::fprintf(stderr, "[shelf] shelf=%zu of %zu water tiles (%.1f%%)\n", shelf, water,
                     100.0 * static_cast<double>(shelf) /
                         static_cast<double>(std::max<std::size_t>(1, water)));
    }
}

void MapGenerator::assignFeatures(const Config& config, HexGrid& grid, aoc::Random& rng) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();

    aoc::Random featureRng(rng.next());
    // Lattice seed for the forest-cover field, drawn once (see Noise.hpp). This
    // one used to work by accident: the old code copy-constructed a throwaway
    // Random from `featureRng` INSIDE the loop, so every tile restarted from the
    // same state and the field happened to be coherent. Removing the per-tile
    // copy also removes a per-tile Random construction.
    const uint64_t forestSeed = featureRng.next();

    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            int32_t index       = row * width + col;
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
                const float forestN =
                    fractalNoise(fnx * 4.5f + 11.0f, fny * 4.5f + 7.0f, 3, 2.0f, 0.5f, forestSeed);
                // Threshold tuned so ~30 % of land in Grassland/Plains
                // gets a forest patch. Higher forestRatio = more cover.
                const float thresh = std::clamp(1.0f - config.forestRatio * 1.5f, 0.30f, 0.85f);
                if (forestN > thresh) {
                    // Jungle is the tropical-rainforest variant, so the band is
                    // the tropics: equatorward of the Tropic of Cancer /
                    // Capricorn, which is the axial tilt by definition.
                    //
                    // 2026-07-27: this was another un-migrated pseudo-latitude
                    // site -- `1 - 2*|row/height - 0.5| > 0.70` on a Lambert
                    // grid selected |lat| < 12.5 deg, not the 21 deg it was
                    // written to mean, so jungle stopped roughly a full biome
                    // band short of the tropics.
                    const bool tropical =
                        std::abs(grid.rowLatitudeDeg(row)) < std::max(1.0f, config.axialTilt);
                    if (tropical && terrain == TerrainType::Grassland) {
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
                hex::AxialCoord axial               = hex::offsetToAxial({col, row});
                std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(axial);
                bool nearWater                      = false;
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
        NaturalWonderType wonderType =
            static_cast<NaturalWonderType>((static_cast<uint8_t>(w) % WONDER_TYPE_COUNT) + 1);

        // Try to find a valid tile
        for (int32_t attempt = 0; attempt < 200; ++attempt) {
            const int32_t col         = rng.nextInt(3, width - 4);
            const int32_t row         = rng.nextInt(3, height - 4);
            const int32_t index       = row * width + col;
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
                suitable = (feature == FeatureType::Hills || terrain == TerrainType::Plains ||
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
            bool tooClose                   = false;
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
                     naturalWonderName(wonderType).data(), col, row);
            break;
        }
    }
}

} // namespace aoc::map

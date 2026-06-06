/**
 * @file Session14.cpp
 * @brief SESSION 14 implementation.
 */

#include "aoc/map/gen/StreamRiparian.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/gen/HexNeighbors.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef _OPENMP
#  define AOC_S14_PARALLEL_FOR_ROWS _Pragma("omp parallel for schedule(static)")
#else
#  define AOC_S14_PARALLEL_FOR_ROWS
#endif

namespace aoc::map::gen {

void runStreamRiparian(HexGrid& grid, bool cylindrical,
                  const std::vector<float>& soilFert,
                  const std::vector<float>& orogeny) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    const bool cylSim    = cylindrical;

    auto nb = [&](int32_t col, int32_t row, int32_t dir, int32_t& outIdx) {
        return hexNeighbor(width, height, cylSim, col, row, dir, outIdx);
    };

    std::array<std::vector<uint8_t>, 8> crops;
    for (auto& c : crops) {
        c.assign(static_cast<std::size_t>(totalT), 0);
    }
    std::vector<uint8_t> spRich (static_cast<std::size_t>(totalT), 0);

    // ---- PER-CROP SUITABILITY ----
    AOC_S14_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            const FeatureType f = grid.feature(i);
            const std::size_t si = static_cast<std::size_t>(i);
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater
                || t == TerrainType::Mountain
                || t == TerrainType::Snow) {
                continue;
            }
            const float fert = (si < soilFert.size())
                ? soilFert[si] : 0.5f;
            if ((t == TerrainType::Plains || t == TerrainType::Grassland)
                && lat > 0.30f && lat < 0.55f) {
                crops[0][si] = static_cast<uint8_t>(
                    std::clamp(fert * 200.0f + 40.0f, 0.0f, 255.0f));
            }
            if (lat < 0.40f
                && (f == FeatureType::Floodplains
                    || f == FeatureType::Marsh
                    || (t == TerrainType::Grassland
                        && grid.riverEdges(i) != 0))) {
                crops[1][si] = static_cast<uint8_t>(
                    std::clamp(fert * 220.0f + 30.0f, 0.0f, 255.0f));
            }
            if (t == TerrainType::Grassland
                && lat > 0.20f && lat < 0.45f) {
                crops[2][si] = static_cast<uint8_t>(
                    std::clamp(fert * 200.0f + 30.0f, 0.0f, 255.0f));
            }
            if (f == FeatureType::Hills
                && lat > 0.40f && lat < 0.60f) {
                crops[3][si] = static_cast<uint8_t>(
                    std::clamp(fert * 180.0f + 50.0f, 0.0f, 255.0f));
            }
            if (lat < 0.20f
                && (t == TerrainType::Grassland
                    || f == FeatureType::Jungle)) {
                crops[4][si] = static_cast<uint8_t>(
                    std::clamp(fert * 200.0f + 40.0f, 0.0f, 255.0f));
            }
            if (f == FeatureType::Hills
                && lat > 0.15f && lat < 0.35f) {
                crops[5][si] = static_cast<uint8_t>(
                    std::clamp(fert * 200.0f + 40.0f, 0.0f, 255.0f));
            }
            if (t == TerrainType::Plains
                && lat > 0.30f && lat < 0.45f) {
                crops[6][si] = static_cast<uint8_t>(
                    std::clamp(fert * 200.0f + 30.0f, 0.0f, 255.0f));
            }
            if (t == TerrainType::Plains
                && lat > 0.20f && lat < 0.40f) {
                crops[7][si] = static_cast<uint8_t>(
                    std::clamp(fert * 200.0f + 20.0f, 0.0f, 255.0f));
            }
        }
    }

    // ---- SPECIES RICHNESS ----
    AOC_S14_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const std::size_t si = static_cast<std::size_t>(i);
            const TerrainType t = grid.terrain(i);
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater) {
                continue;
            }
            int32_t rich = 60;
            if (lat < 0.20f) { rich = 200; }
            else if (lat < 0.40f) { rich = 150; }
            else if (lat < 0.60f) { rich = 100; }
            if (grid.feature(i) == FeatureType::Jungle) {
                rich += 40;
            }
            const auto& ref = grid.refugium();
            if (ref.size() > si && ref[si] != 0) { rich += 30; }
            spRich[si] = static_cast<uint8_t>(
                std::clamp(rich, 0, 255));
        }
    }

    for (int32_t k = 0; k < 8; ++k) {
        grid.setCropSuitability(k, std::move(crops[k]));
    }
    grid.setSpeciesRichness(std::move(spRich));
}

} // namespace aoc::map::gen

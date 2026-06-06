/**
 * @file Session16.cpp
 * @brief SESSION 16 implementation.
 */

#include "aoc/map/gen/DrainageLivestock.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/gen/HexNeighbors.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef _OPENMP
#  define AOC_S16_PARALLEL_FOR_ROWS _Pragma("omp parallel for schedule(static)")
#else
#  define AOC_S16_PARALLEL_FOR_ROWS
#endif

namespace aoc::map::gen {

void runDrainageLivestock(HexGrid& grid, bool cylindrical,
                  const std::vector<float>& soilFert,
                  const std::vector<float>& sediment,
                  const std::vector<uint8_t>& lakeFlag) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    (void)cylindrical;
    (void)sediment;
    (void)lakeFlag;

    std::array<std::vector<uint8_t>, 6> livestockS;
    for (auto& v : livestockS) {
        v.assign(static_cast<std::size_t>(totalT), 0);
    }

    // ---- PER-LIVESTOCK SUITABILITY ----
    AOC_S16_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const std::size_t si = static_cast<std::size_t>(i);
            const TerrainType t = grid.terrain(i);
            const FeatureType f = grid.feature(i);
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater) {
                continue;
            }
            const float fert = (si < soilFert.size())
                ? soilFert[si] : 0.5f;
            if ((t == TerrainType::Grassland || t == TerrainType::Plains)
                && lat > 0.20f && lat < 0.55f) {
                livestockS[0][si] = static_cast<uint8_t>(
                    std::clamp(fert * 220.0f + 30.0f, 0.0f, 255.0f));
            }
            if ((f == FeatureType::Forest || t == TerrainType::Grassland)
                && lat < 0.55f) {
                livestockS[1][si] = static_cast<uint8_t>(
                    std::clamp(fert * 180.0f + 50.0f, 0.0f, 255.0f));
            }
            if (f == FeatureType::Hills
                && lat > 0.30f && lat < 0.60f) {
                livestockS[2][si] = static_cast<uint8_t>(
                    std::clamp(fert * 200.0f + 40.0f, 0.0f, 255.0f));
            }
            if (t == TerrainType::Plains
                && lat > 0.30f && lat < 0.55f
                && f == FeatureType::None) {
                livestockS[3][si] = static_cast<uint8_t>(
                    std::clamp(fert * 200.0f + 50.0f, 0.0f, 255.0f));
            }
            if (f == FeatureType::Hills
                || t == TerrainType::Mountain) {
                livestockS[4][si] = static_cast<uint8_t>(
                    std::clamp(fert * 150.0f + 60.0f, 0.0f, 255.0f));
            }
            if (t == TerrainType::Plains
                || t == TerrainType::Grassland) {
                livestockS[5][si] = static_cast<uint8_t>(
                    std::clamp(fert * 180.0f + 50.0f, 0.0f, 255.0f));
            }
        }
    }

    for (int32_t k = 0; k < 6; ++k) {
        grid.setLivestockSuit(k, std::move(livestockS[k]));
    }
}

} // namespace aoc::map::gen

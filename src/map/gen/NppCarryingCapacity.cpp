/**
 * @file Session15.cpp
 * @brief SESSION 15 implementation.
 */

#include "aoc/map/gen/NppCarryingCapacity.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef _OPENMP
#  define AOC_S15_PARALLEL_FOR_ROWS _Pragma("omp parallel for schedule(static)")
#else
#  define AOC_S15_PARALLEL_FOR_ROWS
#endif

namespace aoc::map::gen {

void runNppCarryingCapacity(HexGrid& grid, bool cylindrical,
                  const std::vector<float>& soilFert) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    (void)cylindrical;

    std::vector<uint8_t> npp     (static_cast<std::size_t>(totalT), 0);
    std::vector<uint8_t> silt    (static_cast<std::size_t>(totalT), 0);

    AOC_S15_PARALLEL_FOR_ROWS
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const std::size_t si = static_cast<std::size_t>(i);
            const TerrainType t = grid.terrain(i);
            const FeatureType f = grid.feature(i);

            // ---- NPP ----
            int32_t nppK = 60;
            if (f == FeatureType::Jungle)      { nppK = 240; }
            else if (f == FeatureType::Forest) { nppK = 180; }
            else if (f == FeatureType::Floodplains
                  || f == FeatureType::Marsh)  { nppK = 200; }
            else if (t == TerrainType::Grassland) { nppK = 120; }
            else if (t == TerrainType::Plains)   { nppK = 80;  }
            else if (t == TerrainType::Tundra)   { nppK = 40;  }
            else if (t == TerrainType::Desert
                  || t == TerrainType::Snow)     { nppK = 10;  }
            if (si < soilFert.size()) {
                nppK = static_cast<int32_t>(
                    static_cast<float>(nppK) * (0.5f + soilFert[si]));
            }
            npp[si] = static_cast<uint8_t>(std::clamp(nppK, 0, 255));

            // ---- SOIL TEXTURE ----
            const auto& so = grid.soilOrder();
            uint8_t sP = 0;
            if (so.size() > si) {
                switch (so[si]) {
                    case 4:  sP=40;  break;
                    case 5:  sP=60;  break;
                    case 3:  sP=110; break;
                    case 6:  sP=100; break;
                    case 7:  sP=70;  break;
                    case 8:  sP=60;  break;
                    case 9:  sP=35;  break;
                    case 10: sP=120; break;
                    case 11: sP=85;  break;
                    case 12: sP=90;  break;
                    default: sP=85;  break;
                }
            }
            silt[si] = sP;
        }
    }

    grid.setNetPrimaryProductivity(std::move(npp));
    grid.setSoilSiltPct(std::move(silt));
}

} // namespace aoc::map::gen

/**
 * @file CoastalErosion.cpp
 * @brief Final coastal-arm erosion pass implementation.
 */

#include "aoc/map/gen/CoastalErosion.hpp"

#include "aoc/map/HexCoord.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"

#include <vector>

namespace aoc::map::gen {

void runCoastalErosion(HexGrid& grid) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();

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
                        ++waterCount; ++validCount; // map edge ~ open water
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

} // namespace aoc::map::gen

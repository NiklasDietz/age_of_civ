/**
 * @file CliffCoast.cpp
 * @brief Cliff coast classification implementation.
 */

#include "aoc/map/gen/CliffCoast.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/gen/HexNeighbors.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aoc::map::gen {

void runCliffCoast(HexGrid& grid, bool cylindrical) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t totalT = width * height;
    const bool cylSim    = cylindrical;

    auto nb = [&](int32_t col, int32_t row, int32_t dir, int32_t& outIdx) {
        return hexNeighbor(width, height, cylSim, col, row, dir, outIdx);
    };

    std::vector<uint8_t> cliff(static_cast<std::size_t>(totalT), 0);
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row)
                       / static_cast<float>(height);
        const float lat = 2.0f * std::abs(ny - 0.5f);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t i = row * width + col;
            const TerrainType t = grid.terrain(i);
            if (t == TerrainType::Ocean
                || t == TerrainType::ShallowWater) {
                continue;
            }
            bool nextWater = false;
            bool nextShallow = false;
            for (int32_t d = 0; d < 6; ++d) {
                int32_t nIdx;
                if (!nb(col, row, d, nIdx)) { continue; }
                const TerrainType nt = grid.terrain(nIdx);
                if (nt == TerrainType::ShallowWater) {
                    nextShallow = true; nextWater = true;
                } else if (nt == TerrainType::Ocean) {
                    nextWater = true;
                }
            }
            if (!nextWater) { continue; }
            uint8_t cType = 0;
            if (t == TerrainType::Snow && lat > 0.80f) {
                cType = 4;
            }
            else if (t == TerrainType::Mountain) {
                cType = (lat > 0.55f && nextShallow) ? 2 : 1;
            }
            else if (grid.feature(i) == FeatureType::Hills
                && grid.marginType().size() > static_cast<std::size_t>(i)
                && grid.marginType()[static_cast<std::size_t>(i)] == 1) {
                cType = 3;
            }
            else if (grid.marginType().size()
                    > static_cast<std::size_t>(i)
                && grid.marginType()[static_cast<std::size_t>(i)] == 1
                && grid.elevation(i) >= 1) {
                cType = 1;
            }
            else if (lat > 0.55f) {
                bool nearMtn = false;
                for (int32_t d = 0; d < 6; ++d) {
                    int32_t nIdx;
                    if (!nb(col, row, d, nIdx)) { continue; }
                    if (grid.terrain(nIdx) == TerrainType::Mountain) {
                        nearMtn = true; break;
                    }
                }
                if (nearMtn) { cType = 2; }
            }
            cliff[static_cast<std::size_t>(i)] = cType;
        }
    }
    grid.setCliffCoast(std::move(cliff));
}

} // namespace aoc::map::gen

/**
 * @file IceAndRock.cpp
 * @brief Ice sheet + rock type implementation.
 */

#include "aoc/map/gen/IceAndRock.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace aoc::map::gen {

void runIceSheetExpansion(HexGrid& grid) {
    const int32_t totalT = grid.tileCount();
    const auto& isPolar = grid.plateIsPolar();
    for (int32_t i = 0; i < totalT; ++i) {
        const TerrainType t = grid.terrain(i);
        if (t == TerrainType::Ocean
            || t == TerrainType::ShallowWater) {
            continue;
        }
        const uint8_t pid = grid.plateId(i);
        if (pid == 0xFFu || pid >= isPolar.size()) { continue; }
        if (!isPolar[pid]) { continue; }
        grid.setTerrain(i, TerrainType::Snow);
        if (grid.feature(i) == FeatureType::Hills) {
            grid.setFeature(i, FeatureType::None);
        }
        if (grid.feature(i) == FeatureType::None) {
            grid.setFeature(i, FeatureType::Ice);
        }
    }
}

void runRockTypeAssignment(HexGrid& grid,
                           const std::vector<uint8_t>& ophioliteMask,
                           const std::vector<float>& sediment,
                           std::vector<uint8_t>& rockTypeTile) {
    const int32_t totalT = grid.tileCount();
    const auto& landFrac = grid.plateLandFrac();
    for (int32_t i = 0; i < totalT; ++i) {
        const TerrainType t = grid.terrain(i);
        const FeatureType f = grid.feature(i);
        uint8_t rt = 0;
        if (ophioliteMask[static_cast<std::size_t>(i)] != 0) {
            rt = 3;
        } else if (t == TerrainType::Mountain) {
            rt = 2;
        } else if (f == FeatureType::Hills) {
            rt = 1;
        } else if (t == TerrainType::Ocean
                   || t == TerrainType::ShallowWater) {
            const uint8_t pid = grid.plateId(i);
            if (pid != 0xFFu && pid < landFrac.size()
                && landFrac[pid] < 0.40f
                && sediment[static_cast<std::size_t>(i)] < 0.04f) {
                rt = 1;
            }
        }
        rockTypeTile[static_cast<std::size_t>(i)] = rt;
    }
    grid.setRockType(rockTypeTile);
}

} // namespace aoc::map::gen

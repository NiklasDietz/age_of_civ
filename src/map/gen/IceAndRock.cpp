/**
 * @file IceAndRock.cpp
 * @brief Ice sheet + rock type implementation.
 */

#include "aoc/map/gen/IceAndRock.hpp"

#include <cmath>

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace aoc::map::gen {

// Ice-sheet surface cover.
//
// 2026-07-27: this pass used to DECIDE glaciation itself -- every land tile on
// a polar-centroid plate poleward of 66 deg was overwritten to Snow. That was
// the reason no generated world contained tundra. The gate is 66 deg, but the
// climate pass only reaches its permanent-ice isotherm (-15 C mean annual)
// around 80 deg, so the entire 66-80 deg band -- exactly where tundra belongs
// -- was snowed before the tundra isotherm could apply. Measured across seeds
// 42/7/100: Snow covered 17 % of land in the 60-70 deg band and 74 % in
// 70-80 deg, while Tundra reached 0.3 % of all land.
//
// A latitude line is also the wrong criterion. An ice sheet needs a positive
// annual mass balance, not a parallel: northern Scandinavia, Siberia, Alaska
// and northern Canada all sit poleward of 66 deg and carry tundra and taiga,
// not ice. Greenland and Antarctica are ice because they are cold enough, and
// "cold enough" is what the climate pass now computes -- mean annual
// temperature in Celsius, against a cited isotherm.
//
// So glaciation is decided once, in ClimateBiome, and this pass only dresses
// the result: Snow tiles get the Ice feature. Nothing here overrides a biome.
void runIceSheetExpansion(HexGrid& grid) {
    const int32_t totalT = grid.tileCount();
    for (int32_t i = 0; i < totalT; ++i) {
        if (grid.terrain(i) != TerrainType::Snow) {
            continue;
        }
        if (grid.feature(i) == FeatureType::Hills) {
            grid.setFeature(i, FeatureType::None);
        }
        if (grid.feature(i) == FeatureType::None) {
            grid.setFeature(i, FeatureType::Ice);
        }
    }
}

void runRockTypeAssignment(HexGrid& grid, const std::vector<uint8_t>& ophioliteMask,
                           const std::vector<float>& sediment, std::vector<uint8_t>& rockTypeTile) {
    // Ophiolite (rt=3) classification needs suture stamping pass
    // (deleted; mask zero). Param retained for ABI stability.
    (void)ophioliteMask;
    const int32_t totalT = grid.tileCount();
    const auto& landFrac = grid.plateLandFrac();
    for (int32_t i = 0; i < totalT; ++i) {
        const TerrainType t = grid.terrain(i);
        const FeatureType f = grid.feature(i);
        uint8_t rt          = 0;
        if (t == TerrainType::Mountain) {
            rt = 2;
        } else if (f == FeatureType::Hills) {
            rt = 1;
        } else if (t == TerrainType::Ocean || t == TerrainType::ShallowWater) {
            const uint8_t pid = grid.plateId(i);
            if (pid != 0xFFu && pid < landFrac.size() && landFrac[pid] < 0.40f &&
                sediment[static_cast<std::size_t>(i)] < 0.04f) {
                rt = 1;
            }
            // 2026-05-04: CARBONATE COMPENSATION DEPTH. Below ~4500 m
            // (in our normalised system: deep ocean = elevation
            // < 0.05) seawater dissolves carbonate-shell debris before
            // it can settle. Only insoluble red clay + biogenic
            // silica accumulate -- no chalk/limestone forms. Rock
            // type 4 = abyssal red clay. Detect via tile elevation
            // (deep ocean) AND low sediment (no continental input).
            const int32_t elev = grid.elevation(i);
            if (t == TerrainType::Ocean && elev < -1 &&
                sediment[static_cast<std::size_t>(i)] < 0.02f) {
                rt = 4;
            }
        }
        rockTypeTile[static_cast<std::size_t>(i)] = rt;
    }
    grid.setRockType(rockTypeTile);
}

} // namespace aoc::map::gen

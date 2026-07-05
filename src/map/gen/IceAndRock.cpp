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

void runIceSheetExpansion(HexGrid& grid) {
    const int32_t totalT = grid.tileCount();
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const auto& isPolar = grid.plateIsPolar();
    // Per-TILE latitude gate in addition to the per-plate flag
    // (2026-07-05): plates span tens of degrees, so flagging every
    // tile of a polar-centroid plate snowed mid-latitude land. Only
    // rows poleward of ~66 deg (same row-fraction latitude proxy the
    // climate passes use) glaciate.
    constexpr float POLAR_ROW_BAND = 66.0f / 180.0f; // ~0.3667
    for (int32_t i = 0; i < totalT; ++i) {
        const TerrainType t = grid.terrain(i);
        if (t == TerrainType::Ocean
            || t == TerrainType::ShallowWater) {
            continue;
        }
        const uint8_t pid = grid.plateId(i);
        if (pid == 0xFFu || pid >= isPolar.size()) { continue; }
        if (!isPolar[pid]) { continue; }
        const float ny = (static_cast<float>(i / width) + 0.5f)
            / static_cast<float>(height);
        if (std::fabs(ny - 0.5f) <= POLAR_ROW_BAND) { continue; }
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
    // Ophiolite (rt=3) classification needs suture stamping pass
    // (deleted; mask zero). Param retained for ABI stability.
    (void)ophioliteMask;
    const int32_t totalT = grid.tileCount();
    const auto& landFrac = grid.plateLandFrac();
    for (int32_t i = 0; i < totalT; ++i) {
        const TerrainType t = grid.terrain(i);
        const FeatureType f = grid.feature(i);
        uint8_t rt = 0;
        if (t == TerrainType::Mountain) {
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
            // 2026-05-04: CARBONATE COMPENSATION DEPTH. Below ~4500 m
            // (in our normalised system: deep ocean = elevation
            // < 0.05) seawater dissolves carbonate-shell debris before
            // it can settle. Only insoluble red clay + biogenic
            // silica accumulate -- no chalk/limestone forms. Rock
            // type 4 = abyssal red clay. Detect via tile elevation
            // (deep ocean) AND low sediment (no continental input).
            const int32_t elev = grid.elevation(i);
            if (t == TerrainType::Ocean && elev < -1
                && sediment[static_cast<std::size_t>(i)] < 0.02f) {
                rt = 4;
            }
        }
        rockTypeTile[static_cast<std::size_t>(i)] = rt;
    }
    grid.setRockType(rockTypeTile);
}

} // namespace aoc::map::gen

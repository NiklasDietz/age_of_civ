/**
 * @file Thresholds.cpp
 * @brief Water + mountain threshold computation implementation.
 */

#include "aoc/map/gen/Thresholds.hpp"

#include "aoc/map/HexCoord.hpp"
#include "aoc/map/HexGrid.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aoc::map::gen {

void runThresholdComputation(HexGrid& grid, MapType mapType,
                             float effectiveWaterRatio,
                             const std::vector<float>& elevationMap,
                             ThresholdResult& out) {
    const int32_t width  = grid.width();
    const int32_t totalTiles = grid.tileCount();

    // Direct sea-level cut. elev[i] is the unitless surface elevation
    // produced by MapGenerator from SphereField surfaceElevationM /
    // 5000 m: positive = above sea level, negative = below. With the
    // slope-based erosion law (SphereFieldPhysics) preserving
    // continental shields at their physical steady state and the
    // rigid mantle-datum calibration in PhysicsConstants, sea level
    // is at zero by construction. Cells with elev < 0 are water; all
    // others land. No percentile cutoff (CLAUDE.md rule 3 forbids
    // quota-based shapers); waterRatio config is now informational
    // only -- the physical model produces whatever land/water ratio
    // its constants demand.
    const std::size_t N = elevationMap.size();
    out.isWater.assign(N, 0u);
    std::size_t waterCount = 0;
    for (std::size_t i = 0; i < N; ++i) {
        if (elevationMap[i] < 0.0f) {
            out.isWater[i] = 1u;
            ++waterCount;
        }
    }
    out.waterThreshold = 0.0f;

    (void)effectiveWaterRatio; // kept in signature for legacy callers

    if (std::getenv("AOC_DUMP_THRESHOLD") != nullptr) {
        std::fprintf(stderr,
            "[thresh] sea-level cut: water=%zu/%zu (%.1f%%)\n",
            waterCount, N,
            100.0 * static_cast<double>(waterCount)
                  / static_cast<double>(N));
    }
    out.distFromCoast.assign(static_cast<std::size_t>(totalTiles), -1);
    std::vector<int32_t> coastQ;
    coastQ.reserve(static_cast<std::size_t>(totalTiles));
    for (int32_t i = 0; i < totalTiles; ++i) {
        if (out.isWater[static_cast<std::size_t>(i)]) {
            out.distFromCoast[static_cast<std::size_t>(i)] = 0;
            coastQ.push_back(i);
        }
    }
    for (std::size_t h = 0; h < coastQ.size(); ++h) {
        const int32_t idx = coastQ[h];
        const int32_t d = out.distFromCoast[static_cast<std::size_t>(idx)];
        const int32_t col = idx % width;
        const int32_t row = idx / width;
        const hex::AxialCoord axial = hex::offsetToAxial({col, row});
        for (const hex::AxialCoord& n : hex::neighbors(axial)) {
            if (!grid.isValid(n)) { continue; }
            const int32_t ni = grid.toIndex(n);
            if (out.distFromCoast[static_cast<std::size_t>(ni)] >= 0) {
                continue;
            }
            out.distFromCoast[static_cast<std::size_t>(ni)] = d + 1;
            coastQ.push_back(ni);
        }
    }

    out.mountainElev = elevationMap;
    if (mapType != MapType::Continents) {
        for (int32_t i = 0; i < totalTiles; ++i) {
            if (out.distFromCoast[static_cast<std::size_t>(i)] <= 0) { continue; }
            const int32_t d = out.distFromCoast[static_cast<std::size_t>(i)];
            float bonus = 0.0f;
            if (d >= 2 && d <= 6) {
                const float peak = 4.0f;
                const float sigma = 2.5f;
                const float x = (static_cast<float>(d) - peak) / sigma;
                bonus = 0.18f * std::exp(-x * x);
            } else if (d > 8) {
                bonus = -0.05f;
            }
            out.mountainElev[static_cast<std::size_t>(i)] += bonus;
        }
    } else {
        for (int32_t i = 0; i < totalTiles; ++i) {
            if (out.distFromCoast[static_cast<std::size_t>(i)] <= 0) { continue; }
            const int32_t d = out.distFromCoast[static_cast<std::size_t>(i)];
            if (d > 8) {
                out.mountainElev[static_cast<std::size_t>(i)] -= 0.02f;
            }
        }
    }

    // Mountain status decided downstream by SphereField bilinearSample
    // > 4000 m (MOUNTAIN_THRESHOLD_M); legacy percentile cutoff dead.
    out.mountainThreshold = 0.0f;
}

} // namespace aoc::map::gen

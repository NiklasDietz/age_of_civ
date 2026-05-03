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
                             float effectiveWaterRatio, float mountainRatio,
                             const std::vector<float>& elevationMap,
                             ThresholdResult& out) {
    const int32_t width  = grid.width();
    const int32_t totalTiles = grid.tileCount();

    std::vector<float> sortedElevations(elevationMap);
    std::sort(sortedElevations.begin(), sortedElevations.end());
    std::size_t waterCutoff = static_cast<std::size_t>(
        effectiveWaterRatio * static_cast<float>(sortedElevations.size()));
    out.waterThreshold =
        sortedElevations[std::min(waterCutoff, sortedElevations.size() - 1)];

    out.distFromCoast.assign(static_cast<std::size_t>(totalTiles), -1);
    std::vector<int32_t> coastQ;
    coastQ.reserve(static_cast<std::size_t>(totalTiles));
    for (int32_t i = 0; i < totalTiles; ++i) {
        if (elevationMap[static_cast<std::size_t>(i)] < out.waterThreshold) {
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

    std::vector<float> sortedMountainElev(out.mountainElev);
    std::sort(sortedMountainElev.begin(), sortedMountainElev.end());
    std::size_t mountainCutoff = sortedMountainElev.size() -
        static_cast<std::size_t>(
            mountainRatio * static_cast<float>(sortedMountainElev.size()));
    out.mountainThreshold = sortedMountainElev[
        std::min(mountainCutoff, sortedMountainElev.size() - 1)];
}

} // namespace aoc::map::gen

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

    std::vector<float> sortedElevations(elevationMap);
    std::sort(sortedElevations.begin(), sortedElevations.end());
    std::size_t waterCutoff = static_cast<std::size_t>(
        effectiveWaterRatio * static_cast<float>(sortedElevations.size()));
    waterCutoff = std::min(waterCutoff, sortedElevations.size() - 1);
    const float thresholdAtCutoff = sortedElevations[waterCutoff];

    // 2026-05-07: nudge threshold past any equal-value plateau. After
    // plate-cell advection homogenises wake cells to mid-ocean-ridge
    // basalt elevation (h=7 km Turcotte & Schubert 2014 -> -2701 m via
    // Airy isostasy), thousands of cells share that exact value. The
    // raw percentile lands ON the plateau and downstream strict-less-
    // than tests classify the entire plateau as land -- maps flip to
    // ~87 % land at long simulated times. Advance the threshold to the
    // next strictly-greater elevation so strict-< below catches every
    // plateau cell.
    out.waterThreshold = thresholdAtCutoff;
    for (std::size_t k = waterCutoff + 1; k < sortedElevations.size(); ++k) {
        if (sortedElevations[k] > thresholdAtCutoff) {
            out.waterThreshold = sortedElevations[k];
            break;
        }
    }
    if (std::getenv("AOC_DUMP_THRESHOLD") != nullptr) {
        std::size_t below = 0;
        for (float e : elevationMap) if (e < out.waterThreshold) ++below;
        std::fprintf(stderr,
            "[thresh] cutoff=%zu/%zu raw=%.4f thresh=%.4f below=%zu min=%.4f max=%.4f\n",
            waterCutoff, sortedElevations.size(),
            static_cast<double>(thresholdAtCutoff),
            static_cast<double>(out.waterThreshold),
            below,
            static_cast<double>(sortedElevations.front()),
            static_cast<double>(sortedElevations.back()));
    }
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

    // Mountain status decided downstream by SphereField bilinearSample
    // > 4000 m (MOUNTAIN_THRESHOLD_M); legacy percentile cutoff dead.
    out.mountainThreshold = 0.0f;
}

} // namespace aoc::map::gen

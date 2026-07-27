/**
 * @file Thresholds.cpp
 * @brief Water + mountain threshold computation implementation.
 */

#include "aoc/map/gen/Thresholds.hpp"

#include "aoc/map/HexCoord.hpp"
#include "aoc/map/HexGrid.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace aoc::map::gen {

void runThresholdComputation(HexGrid& grid, float seaLevelDelta,
                             const std::vector<float>& elevationMap, ThresholdResult& out) {
    const int32_t width      = grid.width();
    const int32_t totalTiles = grid.tileCount();

    // Sea-level cut. elev[i] is the unitless surface elevation produced
    // by MapGenerator from SphereField surfaceElevationM / 5000 m:
    // positive = above sea level, negative = below. The slope-based
    // erosion law (SphereFieldPhysics) preserves continental shields at
    // their physical steady state and the rigid mantle-datum
    // calibration in PhysicsConstants pins zero at the modern mean.
    //
    // `seaLevelDelta` (user-controllable creator slider) shifts the cut
    // away from zero: + raises sea level (more water), - lowers (more
    // land). One unit of seaLevelDelta = 0.20 in unitless elevation =
    // 1000 m vertical (since elev = m/5000), bracketing the
    // Cretaceous-to-Pleistocene eustatic envelope (~+200 m to -120 m
    // around modern; user range is wider for creative latitude).
    // No percentile cutoff (CLAUDE.md rule 3 forbids quota-based
    // shapers).
    constexpr float SEA_LEVEL_DELTA_TO_ELEV = 0.20f;
    const float seaLevelCut                 = seaLevelDelta * SEA_LEVEL_DELTA_TO_ELEV;
    const std::size_t N                     = elevationMap.size();
    out.isWater.assign(N, 0u);
    std::size_t waterCount = 0;
    for (std::size_t i = 0; i < N; ++i) {
        if (elevationMap[i] < seaLevelCut) {
            out.isWater[i] = 1u;
            ++waterCount;
        }
    }
    out.waterThreshold = seaLevelCut;

    if (std::getenv("AOC_DUMP_THRESHOLD") != nullptr) {
        std::fprintf(stderr, "[thresh] sea-level cut: water=%zu/%zu (%.1f%%)\n", waterCount, N,
                     100.0 * static_cast<double>(waterCount) / static_cast<double>(N));
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
        const int32_t idx           = coastQ[h];
        const int32_t d             = out.distFromCoast[static_cast<std::size_t>(idx)];
        const int32_t col           = idx % width;
        const int32_t row           = idx / width;
        const hex::AxialCoord axial = hex::offsetToAxial({col, row});
        for (const hex::AxialCoord& n : hex::neighbors(axial)) {
            if (!grid.isValid(n)) {
                continue;
            }
            const int32_t ni = grid.toIndex(n);
            if (out.distFromCoast[static_cast<std::size_t>(ni)] >= 0) {
                continue;
            }
            out.distFromCoast[static_cast<std::size_t>(ni)] = d + 1;
            coastQ.push_back(ni);
        }
    }

    // 2026-07-27: the `mountainElev` array, the `mountainThreshold` scalar and
    // the `mapType` parameter were removed together. Mountains are decided
    // upstream, where the world-frame elevation pass compares each tile's peak
    // SphereField sample against MOUNTAIN_THRESHOLD_M; the percentile cutoff
    // those fields fed has not existed for some time. What was left was a full
    // per-tile copy of `elevationMap` plus a coastal-ridge bias loop that no
    // translation unit read, a scalar hard-assigned 0, and a `mapType` branch
    // that only chose between two ways of writing to the unread array.
}

} // namespace aoc::map::gen

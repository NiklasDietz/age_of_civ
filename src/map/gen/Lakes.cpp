/**
 * @file Lakes.cpp
 * @brief Endorheic-basin detection by priority flood.
 */

#include "aoc/map/gen/Lakes.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/gen/HexNeighbors.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <vector>

namespace aoc::map::gen {

namespace {

/// One unitless elevation step in metres (surfaceElevationM / 5000).
constexpr float ELEV_UNIT_TO_M = 5000.0f;

/// Priority-queue entry: the spill level water reaches this tile at, plus the
/// tile index. Ordered lowest-spill-first.
struct FloodNode {
    float spill;
    int32_t index;
};

/// Strict weak ordering for a min-heap on spill level, tie-broken by tile index.
///
/// The index tie-break is not cosmetic: `std::priority_queue` gives no ordering
/// guarantee among equal elements, and large exactly-flat regions are common in
/// this elevation field (the craton plateau). Without the tie-break, which of
/// several equal-elevation rim tiles spills first would depend on heap internals,
/// and the lake outlines would differ between runs or compilers. This repo has a
/// byte-identical determinism gate.
struct SpillGreater {
    [[nodiscard]] bool operator()(const FloodNode& a, const FloodNode& b) const {
        if (a.spill != b.spill) {
            return a.spill > b.spill;
        }
        return a.index > b.index;
    }
};

} // namespace

LakeResult findEndorheicLakes(const HexGrid& grid, const std::vector<float>& elevationMap,
                              float waterThreshold, float minDepthM, int32_t minTiles) {
    const int32_t width     = grid.width();
    const int32_t height    = grid.height();
    const int32_t total     = width * height;
    const bool cylindrical  = (grid.topology() == MapTopology::Cylindrical);
    const std::size_t count = static_cast<std::size_t>(total);

    LakeResult out;
    out.lakeFlag.assign(count, 0u);
    out.lakeDepthM.assign(count, 0.0f);
    if (elevationMap.size() < count || total <= 0) {
        return out;
    }

    // Spill level per tile: the lowest height water must rise to in order to
    // escape from this tile to the ocean. Filled by the flood below.
    std::vector<float> spill(count, 0.0f);
    std::vector<uint8_t> settled(count, 0u);

    std::priority_queue<FloodNode, std::vector<FloodNode>, SpillGreater> frontier;

    // Seed with the ocean: every water tile drains at its own level, and any
    // path reaching one is finished. Ocean tiles are the algorithm's boundary
    // condition, which is why a world with no ocean produces no lakes rather
    // than flooding everywhere.
    for (int32_t i = 0; i < total; ++i) {
        if (elevationMap[static_cast<std::size_t>(i)] < waterThreshold) {
            settled[static_cast<std::size_t>(i)] = 1u;
            spill[static_cast<std::size_t>(i)]   = waterThreshold;
            frontier.push(FloodNode{waterThreshold, i});
        }
    }

    // Priority flood inward from the ocean. Popping the globally lowest spill
    // level first is what makes one pass sufficient: when a tile is first
    // reached, it is reached by the lowest possible route, so its spill level is
    // final and never needs revisiting.
    while (!frontier.empty()) {
        const FloodNode node = frontier.top();
        frontier.pop();
        const int32_t col = node.index % width;
        const int32_t row = node.index / width;
        for (int32_t dir = 0; dir < 6; ++dir) {
            int32_t nIdx = 0;
            if (!hexNeighbor(width, height, cylindrical, col, row, dir, nIdx)) {
                continue;
            }
            const std::size_t n = static_cast<std::size_t>(nIdx);
            if (settled[n] != 0u) {
                continue;
            }
            settled[n] = 1u;
            // To leave this neighbour you must climb to whichever is higher: the
            // route you came by, or the neighbour's own surface.
            spill[n] = std::max(node.spill, elevationMap[n]);
            frontier.push(FloodNode{spill[n], nIdx});
        }
    }

    // A tile is under water exactly when its spill point is above its surface.
    // The difference is the depth.
    std::vector<uint8_t> flooded(count, 0u);
    for (int32_t i = 0; i < total; ++i) {
        const std::size_t u = static_cast<std::size_t>(i);
        if (elevationMap[u] < waterThreshold) {
            continue; // ocean, not a lake
        }
        const float depthM = (spill[u] - elevationMap[u]) * ELEV_UNIT_TO_M;
        if (depthM >= minDepthM) {
            flooded[u]        = 1u;
            out.lakeDepthM[u] = depthM;
        }
    }

    // Reject basins smaller than `minTiles`: a one-cell pit at this resolution
    // (tiles are ~100-300 km across) is a sampling artefact, not a lake.
    // Connected components over the flooded set, iterative so a continent-sized
    // basin cannot blow the stack.
    std::vector<int32_t> stack;
    std::vector<int32_t> members;
    std::vector<uint8_t> visited(count, 0u);
    for (int32_t seed = 0; seed < total; ++seed) {
        if (flooded[static_cast<std::size_t>(seed)] == 0u ||
            visited[static_cast<std::size_t>(seed)] != 0u) {
            continue;
        }
        stack.clear();
        members.clear();
        stack.push_back(seed);
        visited[static_cast<std::size_t>(seed)] = 1u;
        while (!stack.empty()) {
            const int32_t idx = stack.back();
            stack.pop_back();
            members.push_back(idx);
            const int32_t col = idx % width;
            const int32_t row = idx / width;
            for (int32_t dir = 0; dir < 6; ++dir) {
                int32_t nIdx = 0;
                if (!hexNeighbor(width, height, cylindrical, col, row, dir, nIdx)) {
                    continue;
                }
                const std::size_t n = static_cast<std::size_t>(nIdx);
                if (flooded[n] == 0u || visited[n] != 0u) {
                    continue;
                }
                visited[n] = 1u;
                stack.push_back(nIdx);
            }
        }
        if (static_cast<int32_t>(members.size()) < minTiles) {
            for (const int32_t idx : members) {
                out.lakeDepthM[static_cast<std::size_t>(idx)] = 0.0f;
            }
            continue;
        }
        ++out.lakeCount;
        for (const int32_t idx : members) {
            out.lakeFlag[static_cast<std::size_t>(idx)] = 1u;
            ++out.floodedTiles;
        }
    }

    return out;
}

} // namespace aoc::map::gen

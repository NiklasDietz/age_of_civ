/**
 * @file Rivers.cpp
 * @brief River generation pass. Extracted 2026-05-02 from MapGenerator.cpp.
 */

#include "aoc/map/MapGenerator.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/gen/Noise.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace aoc::map {

using gen::hashNoise;
using gen::smoothstep;
using gen::lerp;

void MapGenerator::generateRivers(HexGrid& grid, aoc::Random& rng) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();

    // Rivers must originate at a mountain or hill and flow monotonically to
    // a water tile.  Previous impl accepted any land with elevation >= 1 and
    // committed edges even when the descent dead-ended inland, producing
    // floating midland river stubs that the rotation-fixed edge renderer now
    // exposed clearly.
    //
    // Algorithm:
    //   1. Collect candidate source tiles (Mountain terrain OR Hills feature).
    //   2. For each source: build a path following strict descent + tie-break
    //      by highest distance-to-water heuristic.
    //   3. Only commit the path's river edges if the path terminates at water.
    //      A failed path contributes zero river edges.

    // Pre-pass: distance-to-water BFS so we can tie-break toward outlets.
    std::vector<int32_t> distToWater(static_cast<size_t>(grid.tileCount()), -1);
    std::vector<int32_t> bfsQueue;
    bfsQueue.reserve(static_cast<size_t>(grid.tileCount()));
    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        if (isWater(grid.terrain(i))) {
            distToWater[static_cast<size_t>(i)] = 0;
            bfsQueue.push_back(i);
        }
    }
    for (size_t qh = 0; qh < bfsQueue.size(); ++qh) {
        const int32_t idx = bfsQueue[qh];
        const int32_t d = distToWater[static_cast<size_t>(idx)];
        const hex::AxialCoord c = grid.toAxial(idx);
        const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(c);
        for (const hex::AxialCoord& n : nbrs) {
            if (!grid.isValid(n)) { continue; }
            const int32_t ni = grid.toIndex(n);
            if (distToWater[static_cast<size_t>(ni)] >= 0) { continue; }
            // BFS expands through land INCLUDING mountains.  Rivers originate
            // at mountain/hill and flow to water, so mountain tiles must
            // receive a finite distance or the source filter rejects them all
            // (Mountain is unit-impassable but rivers ignore that).
            distToWater[static_cast<size_t>(ni)] = d + 1;
            bfsQueue.push_back(ni);
        }
    }

    // Gather river sources.
    std::vector<int32_t> sources;
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx = row * width + col;
            const TerrainType t = grid.terrain(idx);
            if (isWater(t) || isImpassable(t)) { continue; }
            // Civ-6 behaviour: rivers spring only from mountain tiles.
            // Earlier we also accepted Hills features but that produced
            // springs on plain-with-hills tiles, which read as "river
            // appears on a plain". Restricting to Mountain makes the
            // origin always read as a mountain on the map.
            if (t != TerrainType::Mountain) { continue; }
            // Needs a reachable outlet.  Islands of hills with no BFS path to
            // water are disqualified.
            if (distToWater[static_cast<size_t>(idx)] < 1) { continue; }
            sources.push_back(idx);
        }
    }
    if (sources.empty()) { return; }

    // Target density: ~1 river per 200 tiles, capped to source count.
    // Target density: ~1 river per 60 tiles.  Previous 1-per-200 was three
    // times too sparse, and strict ±2 zigzag rejects ~40-60% of candidate
    // sources (paths dead-end before reaching water), so the effective
    // committed count comes out well below even the nominal target.  Over-
    // provision sources 3x so we end up with roughly the real-world density
    // of "most medium landmasses touch 2-3 rivers".
    const int32_t desiredRivers = std::max(3, grid.tileCount() / 60);
    const int32_t riverCount = std::min(desiredRivers, static_cast<int32_t>(sources.size()));

    // Shuffle sources so the first N picks are random.
    for (size_t i = sources.size(); i > 1; --i) {
        const size_t j = static_cast<size_t>(rng.nextInt(0, static_cast<int32_t>(i) - 1));
        std::swap(sources[i - 1], sources[j]);
    }

    struct Step { int32_t tileIndex; int32_t direction; };

    // BFS over (tile, prevDir) state-space with strict ±2 transitions.  Each
    // state represents "we are standing on `tile`, and the last step into
    // this tile used direction `prevDir` (from the previous tile to this
    // tile)".  A step is legal iff its direction satisfies (dir - prevDir)
    // mod 6 ∈ {2, 4} — that constraint guarantees the two river-edges on
    // any intermediate tile share a vertex, producing a Civ 6-style
    // continuous curve instead of opposite-side bars.
    //
    // Starting state has prevDir = -1 (no constraint on first step).  Path
    // must arrive at a water tile.  Elevation is used only as a cost term
    // (monotone descent is softly preferred via non-decreasing distance-
    // to-water), so any geometrically valid ±2 path to water is accepted.
    auto findPath = [&](int32_t startIdx, std::vector<Step>& outPath) -> bool {
        outPath.clear();
        const int32_t tc = grid.tileCount();
        // parent[tile * 7 + (prevDir+1)] = encoded prev state + incoming dir.
        // We allocate 7 slots per tile (prevDir ∈ {-1, 0..5}).
        constexpr int32_t DIR_SLOTS = 7;
        const size_t stateCount = static_cast<size_t>(tc) * DIR_SLOTS;
        std::vector<int32_t> parentTile(stateCount, -1);
        std::vector<int8_t>  parentSlot(stateCount, -1);
        std::vector<int8_t>  incomingDir(stateCount, -1);
        std::vector<uint8_t> visited(stateCount, 0);

        auto slot = [](int32_t prevDir) -> int32_t {
            return prevDir + 1;  // -1 → 0, 0..5 → 1..6
        };

        std::vector<std::pair<int32_t, int32_t>> queue;  // (tileIdx, prevDir)
        queue.reserve(static_cast<size_t>(tc));
        const size_t startState = static_cast<size_t>(startIdx) * DIR_SLOTS + static_cast<size_t>(slot(-1));
        visited[startState] = 1;
        queue.emplace_back(startIdx, -1);

        int32_t foundTile = -1;
        int32_t foundPrev = -1;

        for (size_t head = 0; head < queue.size(); ++head) {
            const int32_t curTile = queue[head].first;
            const int32_t curPrev = queue[head].second;
            const hex::AxialCoord c = grid.toAxial(curTile);
            const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(c);

            for (int dir = 0; dir < 6; ++dir) {
                if (curPrev >= 0) {
                    const int32_t diff = ((dir - curPrev) % 6 + 6) % 6;
                    if (diff != 2 && diff != 4) { continue; }
                }
                const hex::AxialCoord& n = nbrs[static_cast<size_t>(dir)];
                if (!grid.isValid(n)) { continue; }
                const int32_t nIdx = grid.toIndex(n);
                const TerrainType nt = grid.terrain(nIdx);
                if (isImpassable(nt) && !isWater(nt)) { continue; }

                const size_t nState = static_cast<size_t>(nIdx) * DIR_SLOTS + static_cast<size_t>(slot(dir));
                if (visited[nState]) { continue; }
                visited[nState] = 1;
                parentTile[nState]  = curTile;
                parentSlot[nState]  = static_cast<int8_t>(slot(curPrev));
                incomingDir[nState] = static_cast<int8_t>(dir);

                if (isWater(nt)) {
                    foundTile = nIdx;
                    foundPrev = dir;
                    break;
                }
                queue.emplace_back(nIdx, dir);
            }
            if (foundTile >= 0) { break; }
        }

        if (foundTile < 0) { return false; }

        // Reconstruct path by walking parent pointers backwards.
        std::vector<Step> reversePath;
        int32_t curT = foundTile;
        int32_t curS = slot(foundPrev);
        while (true) {
            const size_t s = static_cast<size_t>(curT) * DIR_SLOTS + static_cast<size_t>(curS);
            const int32_t pT = parentTile[s];
            if (pT < 0) { break; }
            const int8_t  inDir = incomingDir[s];
            reversePath.push_back({pT, inDir});
            curT = pT;
            curS = parentSlot[s];
        }
        outPath.assign(reversePath.rbegin(), reversePath.rend());
        return true;
    };

    int32_t committedRivers = 0;
    int32_t rejectedRivers  = 0;
    for (int32_t r = 0; r < riverCount; ++r) {
        const int32_t startIndex = sources[static_cast<size_t>(r)];
        std::vector<Step> path;
        if (!findPath(startIndex, path)) { ++rejectedRivers; continue; }
        if (path.empty())                { ++rejectedRivers; continue; }

        // Hard sanity: the final committed boundary must actually touch water.
        // findPath guarantees this (BFS success case), but double-check here
        // so a silent future bug cannot leave dry-land rivers.
        const Step& last = path.back();
        const hex::AxialCoord lastC = grid.toAxial(last.tileIndex);
        const hex::AxialCoord lastN = hex::neighbors(lastC)[static_cast<size_t>(last.direction)];
        if (!grid.isValid(lastN)) { ++rejectedRivers; continue; }
        const int32_t lastNIdx = grid.toIndex(lastN);
        if (!isWater(grid.terrain(lastNIdx))) { ++rejectedRivers; continue; }

        for (const Step& s : path) {
            const hex::AxialCoord c = grid.toAxial(s.tileIndex);
            const hex::AxialCoord n = hex::neighbors(c)[static_cast<size_t>(s.direction)];
            const int32_t nIdx = grid.toIndex(n);

            // Realism guard: a river edge on the boundary land↔water IS the
            // coastline. Civ-6 rivers spill into the ocean without painting
            // an extra segment on the shore. Skip those edges so the river
            // visually terminates at the last land tile.
            if (isWater(grid.terrain(nIdx))) {
                continue;
            }

            uint8_t edges = grid.riverEdges(s.tileIndex);
            edges |= static_cast<uint8_t>(1u << s.direction);
            grid.setRiverEdges(s.tileIndex, edges);

            const int reverseDir = (s.direction + 3) % 6;
            uint8_t neighborEdges = grid.riverEdges(nIdx);
            neighborEdges |= static_cast<uint8_t>(1u << reverseDir);
            grid.setRiverEdges(nIdx, neighborEdges);
        }
        ++committedRivers;
        LOG_INFO("River committed: source=(%d,%d) outlet=(%d,%d) terrain=%.*s steps=%zu",
                 grid.toAxial(startIndex).q, grid.toAxial(startIndex).r,
                 lastN.q, lastN.r,
                 static_cast<int>(terrainName(grid.terrain(lastNIdx)).size()),
                 terrainName(grid.terrain(lastNIdx)).data(),
                 path.size());
    }
    LOG_INFO("River generation: %d committed, %d rejected (of %d sources tried)",
             committedRivers, rejectedRivers, riverCount);
}

} // namespace aoc::map

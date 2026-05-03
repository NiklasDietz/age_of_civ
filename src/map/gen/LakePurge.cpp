/**
 * @file LakePurge.cpp
 * @brief Inland-lake purge pass implementation.
 */

#include "aoc/map/gen/LakePurge.hpp"

#include "aoc/map/HexCoord.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"

#include <vector>

namespace aoc::map::gen {

void runLakePurge(HexGrid& grid, bool cylindricalTopology) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const bool cylSim    = cylindricalTopology;

    constexpr int32_t MIN_LAKE_SIZE = 60; // tiles smaller than this fill in
    std::vector<int32_t> lakeId(static_cast<std::size_t>(width * height), -1);
    std::vector<int32_t> lakeQueue;
    lakeQueue.reserve(static_cast<std::size_t>(width * height));
    int32_t nextLake = 0;
    std::vector<int32_t> lakeSize;
    std::vector<bool>    lakeTouchesEdge;
    for (int32_t i = 0; i < width * height; ++i) {
        if (lakeId[static_cast<std::size_t>(i)] >= 0) { continue; }
        if (!isWater(grid.terrain(i))) { continue; }
        lakeId[static_cast<std::size_t>(i)] = nextLake;
        int32_t size = 0;
        bool touchesEdge = false;
        lakeQueue.clear();
        lakeQueue.push_back(i);
        while (!lakeQueue.empty()) {
            const int32_t idx = lakeQueue.back();
            lakeQueue.pop_back();
            ++size;
            const int32_t col = idx % width;
            const int32_t row = idx / width;
            if (row == 0 || row == height - 1) { touchesEdge = true; }
            if (!cylSim && (col == 0 || col == width - 1)) {
                touchesEdge = true;
            }
            const hex::AxialCoord ax = hex::offsetToAxial({col, row});
            for (const hex::AxialCoord& n : hex::neighbors(ax)) {
                if (!grid.isValid(n)) { touchesEdge = true; continue; }
                const int32_t ni = grid.toIndex(n);
                if (lakeId[static_cast<std::size_t>(ni)] >= 0) { continue; }
                if (!isWater(grid.terrain(ni))) { continue; }
                lakeId[static_cast<std::size_t>(ni)] = nextLake;
                lakeQueue.push_back(ni);
            }
        }
        lakeSize.push_back(size);
        lakeTouchesEdge.push_back(touchesEdge);
        ++nextLake;
    }
    // Fill in all water components that DON'T touch any map edge and are
    // smaller than the threshold. Uses Plains terrain as a sediment-deposited
    // filled basin.
    for (int32_t i = 0; i < width * height; ++i) {
        const int32_t cid = lakeId[static_cast<std::size_t>(i)];
        if (cid < 0) { continue; }
        if (lakeTouchesEdge[static_cast<std::size_t>(cid)]) { continue; }
        if (lakeSize[static_cast<std::size_t>(cid)] < MIN_LAKE_SIZE) {
            grid.setTerrain(i, TerrainType::Plains);
            grid.setElevation(i, 0);
        }
    }
}

} // namespace aoc::map::gen

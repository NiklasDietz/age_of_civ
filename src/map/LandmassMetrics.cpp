/**
 * @file LandmassMetrics.cpp
 * @brief Connected land-component size map implementation.
 */

#include "aoc/map/LandmassMetrics.hpp"

#include "aoc/map/HexCoord.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"

namespace aoc::map {

std::vector<int32_t> computeLandmassSizes(const HexGrid& grid) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t total  = width * height;
    std::vector<int32_t> compId(static_cast<std::size_t>(total), -1);
    std::vector<int32_t> compSize;
    std::vector<int32_t> stack;
    stack.reserve(static_cast<std::size_t>(total));
    for (int32_t i = 0; i < total; ++i) {
        if (compId[static_cast<std::size_t>(i)] >= 0) { continue; }
        if (isWater(grid.terrain(i))) { continue; }
        const int32_t cid = static_cast<int32_t>(compSize.size());
        compId[static_cast<std::size_t>(i)] = cid;
        int32_t size = 0;
        stack.clear();
        stack.push_back(i);
        while (!stack.empty()) {
            const int32_t idx = stack.back();
            stack.pop_back();
            ++size;
            const hex::AxialCoord ax = hex::offsetToAxial(
                {idx % width, idx / width});
            for (const hex::AxialCoord& n : hex::neighbors(ax)) {
                if (!grid.isValid(n)) { continue; }
                const int32_t ni = grid.toIndex(n);
                if (compId[static_cast<std::size_t>(ni)] >= 0) { continue; }
                if (isWater(grid.terrain(ni))) { continue; }
                compId[static_cast<std::size_t>(ni)] = cid;
                stack.push_back(ni);
            }
        }
        compSize.push_back(size);
    }
    std::vector<int32_t> out(static_cast<std::size_t>(total), 0);
    for (int32_t i = 0; i < total; ++i) {
        const int32_t cid = compId[static_cast<std::size_t>(i)];
        if (cid >= 0) {
            out[static_cast<std::size_t>(i)] =
                compSize[static_cast<std::size_t>(cid)];
        }
    }
    return out;
}

} // namespace aoc::map

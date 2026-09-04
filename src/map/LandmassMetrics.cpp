/**
 * @file LandmassMetrics.cpp
 * @brief Connected land-component labelling.
 */

#include "aoc/map/LandmassMetrics.hpp"

#include "aoc/map/HexCoord.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"

#include <cstddef>

namespace aoc::map {

LandmassMap computeLandmasses(const HexGrid& grid) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t total  = width * height;
    LandmassMap out;
    out.componentId.assign(static_cast<std::size_t>(total), -1);
    std::vector<int32_t> stack;
    stack.reserve(static_cast<std::size_t>(total));
    for (int32_t i = 0; i < total; ++i) {
        if (out.componentId[static_cast<std::size_t>(i)] >= 0) {
            continue;
        }
        if (isWater(grid.terrain(i))) {
            continue;
        }
        const int32_t cid = static_cast<int32_t>(out.componentSize.size());
        out.componentId[static_cast<std::size_t>(i)] = cid;
        int32_t size                                 = 0;
        stack.clear();
        stack.push_back(i);
        while (!stack.empty()) {
            const int32_t idx = stack.back();
            stack.pop_back();
            ++size;
            const hex::AxialCoord ax = hex::offsetToAxial({idx % width, idx / width});
            for (const hex::AxialCoord& n : hex::neighbors(ax)) {
                if (!grid.isValid(n)) {
                    continue;
                }
                const int32_t ni = grid.toIndex(n);
                if (out.componentId[static_cast<std::size_t>(ni)] >= 0) {
                    continue;
                }
                if (isWater(grid.terrain(ni))) {
                    continue;
                }
                out.componentId[static_cast<std::size_t>(ni)] = cid;
                stack.push_back(ni);
            }
        }
        out.componentSize.push_back(size);
    }
    return out;
}

std::vector<int32_t> computeLandmassSizes(const HexGrid& grid) {
    const LandmassMap landmasses = computeLandmasses(grid);
    std::vector<int32_t> out(landmasses.componentId.size(), 0);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int32_t cid = landmasses.componentId[i];
        if (cid >= 0) {
            out[i] = landmasses.componentSize[static_cast<std::size_t>(cid)];
        }
    }
    return out;
}

} // namespace aoc::map

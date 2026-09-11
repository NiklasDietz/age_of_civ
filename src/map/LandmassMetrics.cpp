/**
 * @file LandmassMetrics.cpp
 * @brief Connected land-component labelling.
 */

#include "aoc/map/LandmassMetrics.hpp"

#include "aoc/map/HexCoord.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <set>

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

namespace {

/// Resource ids on the tiles within `radius` of `center`.
[[nodiscard]] std::set<uint16_t> resourcesWithin(const HexGrid& grid, hex::AxialCoord center,
                                                 int32_t radius) {
    std::vector<hex::AxialCoord> tiles;
    hex::spiral(center, radius, std::back_inserter(tiles));
    std::set<uint16_t> found;
    for (const hex::AxialCoord& tile : tiles) {
        if (!grid.isValid(tile)) {
            continue;
        }
        const ResourceId id = grid.resource(grid.toIndex(tile));
        if (id.isValid()) {
            found.insert(id.value);
        }
    }
    return found;
}

[[nodiscard]] std::set<uint16_t> resourcesOnMap(const HexGrid& grid) {
    std::set<uint16_t> found;
    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        const ResourceId id = grid.resource(i);
        if (id.isValid()) {
            found.insert(id.value);
        }
    }
    return found;
}

/// Whether `a` holds a luxury `b` lacks.
[[nodiscard]] bool holdsSomethingTheOtherLacks(const std::set<uint16_t>& a,
                                               const std::set<uint16_t>& b) {
    return std::any_of(a.begin(), a.end(), [&b](uint16_t id) { return b.count(id) == 0; });
}

} // namespace

ResourceGeography measureResourceGeography(const HexGrid& grid,
                                           const std::vector<hex::AxialCoord>& starts,
                                           int32_t radius) {
    ResourceGeography out;
    const std::vector<uint16_t>& luxuries = aoc::sim::luxuryGoodIds();
    out.luxuryTypes                      = static_cast<int32_t>(luxuries.size());
    if (starts.empty() || luxuries.empty()) {
        return out;
    }

    std::vector<std::set<uint16_t>> luxuriesInReach;
    float absentSum   = 0.0f;
    int32_t withMetal = 0;
    int32_t withHorse = 0;
    for (const hex::AxialCoord& start : starts) {
        const std::set<uint16_t> all = resourcesWithin(grid, start, radius);
        std::set<uint16_t> lux;
        for (const uint16_t id : luxuries) {
            if (all.count(id) != 0) {
                lux.insert(id);
            }
        }
        absentSum += 1.0f - static_cast<float>(lux.size()) / static_cast<float>(luxuries.size());
        withMetal += (all.count(aoc::sim::goods::COPPER_ORE) != 0 ||
                      all.count(aoc::sim::goods::IRON_ORE) != 0) ? 1 : 0;
        withHorse += all.count(aoc::sim::goods::HORSES) != 0 ? 1 : 0;
        luxuriesInReach.push_back(std::move(lux));
    }
    const float startCount = static_cast<float>(starts.size());
    out.luxuryTypesAbsent  = absentSum / startCount;
    out.copperOrIronEveryStart = withMetal == static_cast<int32_t>(starts.size());
    out.horsesShare        = static_cast<float>(withHorse) / startCount;
    out.minLuxuryTypes     = static_cast<int32_t>(
        std::min_element(luxuriesInReach.begin(), luxuriesInReach.end(),
                         [](const std::set<uint16_t>& a, const std::set<uint16_t>& b) {
                             return a.size() < b.size();
                         })
            ->size());

    const std::set<uint16_t> onMap = resourcesOnMap(grid);
    out.everyLuxuryOnMap =
        std::all_of(luxuries.begin(), luxuries.end(),
                    [&onMap](uint16_t id) { return onMap.count(id) != 0; });

    int32_t pairs         = 0;
    int32_t complementary = 0;
    for (std::size_t i = 0; i < luxuriesInReach.size(); ++i) {
        for (std::size_t j = i + 1; j < luxuriesInReach.size(); ++j) {
            ++pairs;
            if (holdsSomethingTheOtherLacks(luxuriesInReach[i], luxuriesInReach[j]) &&
                holdsSomethingTheOtherLacks(luxuriesInReach[j], luxuriesInReach[i])) {
                ++complementary;
            }
        }
    }
    out.complementaryPairs =
        pairs > 0 ? static_cast<float>(complementary) / static_cast<float>(pairs) : 0.0f;
    return out;
}

} // namespace aoc::map

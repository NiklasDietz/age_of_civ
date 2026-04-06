/**
 * @file Infrastructure.cpp
 * @brief Infrastructure tier calculations: maintenance, fuel, trade capacity.
 */

#include "aoc/simulation/map/Infrastructure.hpp"

#include <algorithm>

namespace aoc::sim {

int32_t computeInfraMaintenanceCost(const aoc::map::HexGrid& grid, PlayerId player) {
    int32_t totalCost = 0;
    int32_t tileCount = grid.tileCount();

    for (int32_t i = 0; i < tileCount; ++i) {
        if (grid.owner(i) != player) {
            continue;
        }
        int32_t tier = grid.infrastructureTier(i);
        totalCost += infraMaintenanceCost(tier);
    }

    return totalCost;
}

int32_t computeRailwayFuelCost(const aoc::map::HexGrid& grid, PlayerId player) {
    int32_t railwayTiles = 0;
    int32_t tileCount = grid.tileCount();

    for (int32_t i = 0; i < tileCount; ++i) {
        if (grid.owner(i) != player) {
            continue;
        }
        if (grid.improvement(i) == aoc::map::ImprovementType::Railway) {
            ++railwayTiles;
        }
    }

    // 1 coal per 10 railway tiles (rounded up)
    return (railwayTiles + 9) / 10;
}

float tradeRouteCapacity(const aoc::map::HexGrid& grid,
                         int32_t fromIndex, int32_t toIndex) {
    // Bottleneck principle: use the minimum tier of the two endpoints.
    // A more detailed implementation would check the entire path.
    int32_t fromTier = grid.infrastructureTier(fromIndex);
    int32_t toTier = grid.infrastructureTier(toIndex);
    int32_t minTier = std::min(fromTier, toTier);

    return infraTradeCapacityMultiplier(minTier);
}

} // namespace aoc::sim

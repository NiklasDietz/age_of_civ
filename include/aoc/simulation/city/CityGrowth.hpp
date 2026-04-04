#pragma once

/**
 * @file CityGrowth.hpp
 * @brief City population growth model.
 *
 * Growth formula: foodNeeded = 15 + 6*population + population^1.3
 * Each turn, food surplus accumulates. When it reaches foodNeeded,
 * population increases by 1 and surplus resets.
 *
 * Housing limits soft-cap growth. When population exceeds housing,
 * growth rate is halved, then quartered, then stops.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::ecs {
class World;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

/// Food needed to grow to the next population point.
[[nodiscard]] float foodForGrowth(int32_t currentPopulation);

/**
 * @brief Process city growth for all cities of a player.
 *
 * For each city:
 *   1. Calculate food yield from worked tiles.
 *   2. Subtract food consumption (2 per citizen).
 *   3. Accumulate surplus toward next population.
 *   4. If surplus >= threshold, grow. If surplus < 0 and sustained, starve.
 *
 * @param world  ECS world with city components.
 * @param grid   Hex grid for tile yield data.
 * @param player Player whose cities to process.
 */
void processCityGrowth(aoc::ecs::World& world,
                       const aoc::map::HexGrid& grid,
                       PlayerId player);

} // namespace aoc::sim

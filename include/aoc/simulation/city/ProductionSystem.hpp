#pragma once

/**
 * @file ProductionSystem.hpp
 * @brief City production queue processing.
 *
 * Each turn, cities accumulate production from worked tiles and buildings,
 * then apply it to the front item in their production queue. When an item
 * completes, the corresponding entity (unit, building, or district) is created.
 */

#include "aoc/core/Types.hpp"

namespace aoc::ecs {
class World;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

/**
 * @brief Compute production yield for a single city.
 *
 * Sums production from worked tiles and building bonuses, then applies
 * the happiness production multiplier.
 *
 * @param world       ECS world with city/happiness/district components.
 * @param grid        Hex grid for tile yield data.
 * @param cityEntity  The city entity to compute production for.
 * @return Total production points for this turn.
 */
[[nodiscard]] float computeCityProduction(const aoc::ecs::World& world,
                                           const aoc::map::HexGrid& grid,
                                           EntityId cityEntity);

/**
 * @brief Process production queues for all cities of a player.
 *
 * For each city owned by the given player:
 *   1. Compute production yield.
 *   2. Add progress to the front queue item.
 *   3. If completed, spawn the result (unit/building/district) and pop the item.
 *
 * @param world   ECS world with city and production components.
 * @param grid    Hex grid for tile yield data and unit placement.
 * @param player  Player whose cities to process.
 */
void processProductionQueues(aoc::ecs::World& world,
                              const aoc::map::HexGrid& grid,
                              PlayerId player);

} // namespace aoc::sim

#pragma once

/**
 * @file Maintenance.hpp
 * @brief Unit and building maintenance cost processing.
 *
 * Deducts gold per turn for army upkeep and building maintenance.
 * If treasury drops below -20, a random unit is disbanded.
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
 * @brief Deduct unit maintenance costs from a player's treasury.
 *
 * Each player gets a number of free units equal to their city count.
 * Extra units cost 1 gold each per turn. If treasury falls below -20,
 * a random unit is disbanded.
 */
void processUnitMaintenance(aoc::ecs::World& world, PlayerId player);

/**
 * @brief Deduct building maintenance costs from a player's treasury.
 *
 * Sums BuildingDef.maintenanceCost for all buildings in all cities
 * owned by the player, then deducts the total from the treasury.
 */
void processBuildingMaintenance(aoc::ecs::World& world, PlayerId player);

} // namespace aoc::sim

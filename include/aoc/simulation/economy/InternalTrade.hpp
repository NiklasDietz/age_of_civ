#pragma once

/**
 * @file InternalTrade.hpp
 * @brief Internal trade between a player's own cities.
 *
 * Cities with surplus goods share with deficit cities each turn.
 * Transfer efficiency depends on hex distance and road infrastructure.
 * This creates natural trade lanes within an empire and ensures that
 * resource-rich cities feed industrial cities automatically.
 */

#include "aoc/core/Types.hpp"

namespace aoc::ecs { class World; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

/**
 * @brief Process internal trade between a player's cities each turn.
 *
 * For each good, finds cities with surplus (stockpile > 2) and cities
 * with deficit (stockpile == 0 but needing the good for a recipe).
 * Transfers up to half the surplus from each surplus city to the nearest
 * deficit city, penalized by distance (10% lost per 5 hexes; roads reduce this).
 *
 * @param world   ECS world containing all city and stockpile components.
 * @param grid    Hex grid for distance calculation and road checks.
 * @param player  The player whose cities should trade internally.
 */
void processInternalTrade(aoc::ecs::World& world,
                          const aoc::map::HexGrid& grid,
                          PlayerId player);

} // namespace aoc::sim

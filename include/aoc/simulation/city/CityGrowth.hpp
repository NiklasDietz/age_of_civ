#pragma once

/**
 * @file CityGrowth.hpp
 * @brief City population growth model.
 *
 * Growth formula: foodNeeded = 15 + 6*population + population^1.3
 * Each turn, food surplus accumulates. When it reaches foodNeeded,
 * population increases by 1 and surplus resets.
 *
 * City center always yields at least 2 food (Civ 6 guarantee).
 * Food consumption: 2 per citizen per turn.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::map {
class HexGrid;
}

namespace aoc::game {
class Player;
}

namespace aoc::sim {

/// Food needed to grow to the next population point.
[[nodiscard]] float foodForGrowth(int32_t currentPopulation);

/**
 * @brief Process city growth for all cities of a player.
 *
 * For each city:
 *   1. Calculate food yield from worked tiles (center guaranteed 2 food min).
 *   2. Subtract food consumption (2 per citizen).
 *   3. Accumulate surplus toward next population.
 *   4. If surplus >= threshold, grow and auto-assign new citizen.
 *   5. If surplus < -30 and pop > 1, starve.
 */
void processCityGrowth(aoc::game::Player& player, const aoc::map::HexGrid& grid);

} // namespace aoc::sim

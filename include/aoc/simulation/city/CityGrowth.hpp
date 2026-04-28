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
class City;
class Player;
}

namespace aoc::sim {

/// Food needed to grow to the next population point.
[[nodiscard]] float foodForGrowth(int32_t currentPopulation);

/// Effective housing capacity: base 4 + Granary/Hospital/Aqueduct + nearby farms.
/// Shared by CityGrowth (growth gate) and EconomicDepth (migration gate).
[[nodiscard]] int32_t computeCityHousing(const aoc::game::City& city,
                                          const aoc::map::HexGrid& grid);

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
/// Climate food multiplier scales worked-tile food output. Driven by the
/// global CO2 level (see climateFoodMultiplier). Defaults to 1.0 (no
/// penalty) so legacy callers without a climate handle keep current
/// behavior.
void processCityGrowth(aoc::game::Player& player, const aoc::map::HexGrid& grid,
                       float climateFoodMult = 1.0f);

} // namespace aoc::sim

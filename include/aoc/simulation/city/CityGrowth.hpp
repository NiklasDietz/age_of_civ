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
namespace aoc::game { class GameState; }

namespace aoc::sim {

/// Food needed to grow to the next population point.
[[nodiscard]] float foodForGrowth(int32_t currentPopulation);

/// Effective housing capacity: base 4 + buildings (District.hpp buildingHousing:
/// Granary, Hospital, connected Aqueduct, Neighborhood) + nearby farms.
/// Shared by CityGrowth (growth gate) and EconomicDepth (migration gate).
/// Housing a Neighborhood gives, by the appeal of the ground its city stands
/// on. It shipped as a flat +4 because appeal did not exist; a building whose
/// whole point is where people want to live should not be indifferent to that.
[[nodiscard]] int32_t neighborhoodHousing(int32_t appeal);

inline constexpr int32_t NEIGHBORHOOD_SQUALID  = 2;
inline constexpr int32_t NEIGHBORHOOD_ORDINARY = 4;
inline constexpr int32_t NEIGHBORHOOD_PLEASANT = 6;

/// Base housing by where a city stands, before buildings and farms.
/// Fresh water beats salt: a river or a lake will keep a city, the sea only
/// puts it in reach of one. Every city used to start at HOUSING_DRY whatever
/// its ground, so the choice of site said nothing about how far it could grow.
inline constexpr int32_t HOUSING_DRY         = 3;
inline constexpr int32_t HOUSING_COASTAL     = 4;
inline constexpr int32_t HOUSING_FRESH_WATER = 6;

/// `gameState` is optional: appeal counts nearby Industrial and Encampment
/// districts, and those live on cities rather than on a tile layer. Callers
/// without it get appeal from terrain alone, which is the right answer for a
/// readout and close enough for one.
[[nodiscard]] int32_t computeCityHousing(const aoc::game::City& city,
                                          const aoc::map::HexGrid& grid,
                                          const aoc::game::GameState* gameState = nullptr);

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

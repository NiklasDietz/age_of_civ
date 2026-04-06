#pragma once

/**
 * @file CityLoyalty.hpp
 * @brief City loyalty system. Cities with low loyalty can defect.
 *
 * Loyalty starts at 100 and changes each turn based on:
 *   +8 base from being your city
 *   +4 per governor (future, base only for now)
 *   -3 per nearby foreign city (within 5 hexes) with higher population
 *   -5 during Dark Age
 *   +5 during Golden Age
 *   -2 per point of unhappiness (negative happiness)
 *   -3 if city was recently captured
 *
 * If loyalty drops to 0 the city flips to the nearest dominant neighbor.
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

/// ECS component tracking per-city loyalty.
struct CityLoyaltyComponent {
    float loyalty        = 100.0f;  ///< [0, 100]
    float loyaltyPerTurn = 0.0f;    ///< Net change computed each turn.
};

/**
 * @brief Recalculate loyalty for all cities owned by @p player and
 *        flip any city whose loyalty reaches 0.
 */
void computeCityLoyalty(aoc::ecs::World& world, aoc::map::HexGrid& grid,
                        PlayerId player);

} // namespace aoc::sim

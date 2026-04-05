#pragma once

/// @file CityBombardment.hpp
/// @brief City ranged bombardment against adjacent enemy units.

#include "aoc/core/Types.hpp"
#include "aoc/core/Random.hpp"

namespace aoc::ecs {
class World;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

/// Process city bombardment: cities with Walls shoot at adjacent enemies.
/// Called during the unit maintenance turn phase.
void processCityBombardment(aoc::ecs::World& world, const aoc::map::HexGrid& grid,
                             PlayerId player, aoc::Random& rng);

} // namespace aoc::sim

#pragma once

/// @file EnvironmentModifier.hpp
/// @brief Terrain and feature effects on building/production efficiency.

#include "aoc/core/Types.hpp"
#include "aoc/map/HexGrid.hpp"

namespace aoc::ecs {
class World;
}

namespace aoc::sim {

/// Compute terrain-based production modifier for a specific building in a city.
/// Returns a multiplier (1.0 = no effect, >1.0 = bonus, <1.0 = penalty).
[[nodiscard]] float computeEnvironmentModifier(
    const aoc::ecs::World& world,
    const aoc::map::HexGrid& grid,
    EntityId cityEntity,
    BuildingId buildingId);

/// Compute terrain-based yield modifier for a tile improvement.
/// Applied to the improvement's yield bonus.
[[nodiscard]] float computeImprovementEnvironmentModifier(
    const aoc::map::HexGrid& grid,
    int32_t tileIndex,
    aoc::map::ImprovementType improvement);

} // namespace aoc::sim

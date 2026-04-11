#pragma once

/// @file EnvironmentModifier.hpp
/// @brief Terrain and feature effects on building/production efficiency.

#include "aoc/core/Types.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"

namespace aoc::sim {

/// Compute terrain-based production modifier for a specific building at a location.
/// Returns a multiplier (1.0 = no effect, >1.0 = bonus, <1.0 = penalty).
[[nodiscard]] float computeEnvironmentModifier(
    const aoc::map::HexGrid& grid,
    aoc::hex::AxialCoord location,
    BuildingId buildingId);

/// Compute terrain-based yield modifier for a tile improvement.
/// Applied to the improvement's yield bonus.
[[nodiscard]] float computeImprovementEnvironmentModifier(
    const aoc::map::HexGrid& grid,
    int32_t tileIndex,
    aoc::map::ImprovementType improvement);

} // namespace aoc::sim

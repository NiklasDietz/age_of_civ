#pragma once

/**
 * @file PlateBoundary.hpp
 * @brief Plate-boundary classification shared between MapGenerator's
 *        tectonic-sim pass and the geology-driven resource placer.
 */

#include <cstdint>

namespace aoc::map {

/// Boundary type between two tectonic plates. Used to drive resource
/// placement (Convergent → Iron/Copper, Divergent → Oil, Transform →
/// Niter/Uranium) and orogeny effects.
enum class BoundaryType : uint8_t {
    None,
    Convergent,
    Divergent,
    Transform,
};

} // namespace aoc::map

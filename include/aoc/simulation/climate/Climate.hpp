#pragma once

/**
 * @file Climate.hpp
 * @brief Global climate system: CO2 accumulation, temperature rise, and disasters.
 *
 * Industrial production adds CO2. Rising temperature triggers floods on
 * coastal tiles and droughts on grassland/plains.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/Random.hpp"

#include <cstdint>

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

struct GlobalClimateComponent {
    float globalTemperature = 0.0f;   ///< Degrees above baseline
    float co2Level          = 0.0f;   ///< Accumulated from industrial production
    int32_t seaLevelRise    = 0;      ///< Number of coast tiles that have flooded

    /// Add CO2 from industrial activity.
    void addCO2(float amount);

    /// Process climate effects for one turn.
    /// May flood coastal tiles or cause droughts.
    void processTurn(aoc::map::HexGrid& grid, aoc::Random& rng);
};

} // namespace aoc::sim

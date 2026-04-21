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

/// H6.6: cap prevents unbounded warming at late game. At 10000 = 10 degrees —
/// already catastrophic, but finite. Without a cap, 500 turns * 100 pop was
/// hitting 5000+ and growing, with no path back.
inline constexpr float CO2_MAX = 10000.0f;

/// H6.6: per-turn natural sink (forests, oceans). Green-tech may later add
/// extra scrubbing; base decay keeps steady-state possible.
inline constexpr float CO2_DECAY_PER_TURN = 0.5f;

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

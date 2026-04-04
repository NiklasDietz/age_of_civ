#pragma once

/**
 * @file EraProgression.hpp
 * @brief Era tracking and era-change detection.
 */

#include "aoc/core/Types.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace aoc::sim {

inline constexpr uint8_t ERA_COUNT = 7;

[[nodiscard]] constexpr std::string_view eraName(EraId era) {
    constexpr std::array<std::string_view, ERA_COUNT> NAMES = {{
        "Ancient", "Classical", "Medieval", "Renaissance",
        "Industrial", "Modern", "Information"
    }};
    if (era.value < ERA_COUNT) {
        return NAMES[era.value];
    }
    return "Unknown";
}

/// Compute a player's current era based on the most advanced tech/civic completed.
struct PlayerEraComponent {
    PlayerId owner;
    EraId    currentEra = EraId{0};

    /// Call after any tech/civic completes to check for era advancement.
    void updateEra(EraId completedEra) {
        if (completedEra.value > this->currentEra.value) {
            this->currentEra = completedEra;
        }
    }
};

} // namespace aoc::sim

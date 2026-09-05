/**
 * @file PlayerColors.hpp
 * @brief One owner-colour lookup for units, banners, labels and the minimap.
 *
 * Majors take the eight civ colours by seat, barbarians dark red, and
 * city-states a muted palette of their own: seats 200+ used to wrap through
 * `% 8` and paint every city-state in player 0's colour.
 */

#pragma once

#include "aoc/core/Types.hpp"
#include "aoc/simulation/citystate/CityState.hpp"

#include <array>
#include <cstddef>

namespace aoc::render {

inline constexpr std::array<std::array<float, 3>, 8> PLAYER_COLORS = {{
    {0.20f, 0.40f, 0.90f}, // Player 0: blue
    {0.90f, 0.20f, 0.20f}, // Player 1: red
    {0.20f, 0.80f, 0.20f}, // Player 2: green
    {0.90f, 0.80f, 0.10f}, // Player 3: yellow
    {0.70f, 0.30f, 0.80f}, // Player 4: purple
    {0.90f, 0.50f, 0.10f}, // Player 5: orange
    {0.10f, 0.80f, 0.80f}, // Player 6: cyan
    {0.80f, 0.40f, 0.60f}, // Player 7: pink
}};

/// Muted and distinct from every civ colour, so city-states read as minor powers.
inline constexpr std::array<std::array<float, 3>, 8> CITY_STATE_COLORS = {{
    {0.55f, 0.55f, 0.60f}, // Kabul: slate
    {0.60f, 0.62f, 0.45f}, // Valletta: olive
    {0.72f, 0.62f, 0.48f}, // Geneva: tan
    {0.45f, 0.62f, 0.60f}, // Seoul: teal grey
    {0.65f, 0.52f, 0.62f}, // Kumasi: mauve
    {0.70f, 0.66f, 0.50f}, // Zanzibar: khaki
    {0.50f, 0.58f, 0.70f}, // Lisbon: steel
    {0.62f, 0.48f, 0.40f}, // Singapore: umber
}};

[[nodiscard]] inline bool isCityStateOwner(PlayerId owner) {
    return owner != BARBARIAN_PLAYER && owner >= aoc::sim::CITY_STATE_PLAYER_BASE;
}

inline void ownerColor(PlayerId owner, float& r, float& g, float& b) {
    if (owner == BARBARIAN_PLAYER) {
        r = 0.55f; g = 0.08f; b = 0.08f; // dark red, never a civ colour
        return;
    }
    if (isCityStateOwner(owner)) {
        const std::size_t idx =
            static_cast<std::size_t>(owner - aoc::sim::CITY_STATE_PLAYER_BASE) % CITY_STATE_COLORS.size();
        r = CITY_STATE_COLORS[idx][0];
        g = CITY_STATE_COLORS[idx][1];
        b = CITY_STATE_COLORS[idx][2];
        return;
    }
    const std::size_t idx = static_cast<std::size_t>(owner) % PLAYER_COLORS.size();
    r = PLAYER_COLORS[idx][0];
    g = PLAYER_COLORS[idx][1];
    b = PLAYER_COLORS[idx][2];
}

} // namespace aoc::render

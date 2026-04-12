#pragma once

/**
 * @file Governor.hpp
 * @brief City governor system for automated city management.
 *
 * Instead of micromanaging every city every turn, players can assign a
 * "focus" to each city. The governor AI then makes building, district,
 * tile, and production decisions automatically based on the focus.
 *
 * Focus types:
 *   - Growth:     Prioritize food, farms, granary. Grow population fast.
 *   - Production: Prioritize mines, industrial buildings. Maximize hammers.
 *   - Science:    Prioritize campus, library, university. Maximize beakers.
 *   - Gold:       Prioritize commercial, trade, luxury. Maximize income.
 *   - Military:   Prioritize barracks, walls, military units. Defend/attack.
 *   - Balanced:   Default. Equal weight across all categories.
 *   - Manual:     Player controls everything. Governor does nothing.
 *
 * The governor also handles:
 *   - Auto-assigning worked tiles (best tiles for the focus)
 *   - Auto-queueing production when the queue is empty
 *   - Auto-expanding borders toward valuable tiles
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::game { class GameState; }
namespace aoc::game { class City; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

enum class CityFocus : uint8_t {
    Balanced,    ///< Equal priority across all yields
    Growth,      ///< Maximize food and population growth
    Production,  ///< Maximize production (hammers)
    Science,     ///< Maximize science (beakers)
    Gold,        ///< Maximize gold income
    Military,    ///< Prioritize military buildings and units

    Count
};

[[nodiscard]] constexpr const char* cityFocusName(CityFocus focus) {
    switch (focus) {
        case CityFocus::Balanced:   return "Balanced";
        case CityFocus::Growth:     return "Growth";
        case CityFocus::Production: return "Production";
        case CityFocus::Science:    return "Science";
        case CityFocus::Gold:       return "Gold";
        case CityFocus::Military:   return "Military";
        default:                    return "Unknown";
    }
}

/// Per-city governor state (ECS component).
struct CityGovernorComponent {
    CityFocus focus = CityFocus::Balanced;

    /// Whether the governor is active (false = manual control).
    bool isActive = false;

    /// Auto-queue production when queue empties.
    bool autoQueueProduction = true;

    /// Auto-assign best worked tiles for the focus.
    bool autoAssignTiles = true;
};

/**
 * @brief Run the governor for a city: auto-queue production based on focus.
 *
 * Called when a city's production queue is empty and the governor is active.
 * Selects the best item to produce based on the city's focus and build constraints.
 *
 * @param gameState  Full game state (needed for tech/civic checks).
 * @param grid       Hex grid.
 * @param city       The city to manage.
 * @param player     Owning player.
 */
void governorAutoQueue(aoc::game::GameState& gameState,
                        const aoc::map::HexGrid& grid,
                        aoc::game::City& city,
                        PlayerId player);

/**
 * @brief Run all governors for a player's cities.
 *
 * Called once per turn during per-player processing.
 * For each city with an active governor, handles auto-queuing
 * and tile assignment.
 */
void processGovernors(aoc::game::GameState& gameState,
                       const aoc::map::HexGrid& grid,
                       PlayerId player);

} // namespace aoc::sim

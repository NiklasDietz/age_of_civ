#pragma once

/**
 * @file Tourism.hpp
 * @brief Tourism system for cultural victory (Classic victory mode).
 *
 * Tourism is a per-player global output that applies against all other civs.
 * A Cultural Victory is achieved when your total Foreign Tourists exceeds
 * every other civ's Domestic Tourists.
 *
 * Tourism sources:
 *   - Great Works (placed in Theater Square buildings): base 2 each
 *   - Wonders: base 3 each
 *   - Holy Cities: base 4 each
 *   - National Parks: sum of Appeal of 4 tiles
 *   - Open Borders with a civ: +25% tourism against them
 *   - Trade Routes to a civ: +25% tourism against them
 *
 * Domestic Tourists = cumulative culture / 100 (each civ generates its own)
 * Foreign Tourists = cumulative tourism against civ / 150
 *
 * Cultural Victory: your Foreign Tourists > every other civ's Domestic Tourists.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::game { class GameState; class Player; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

/// Per-player tourism state.
struct PlayerTourismComponent {
    PlayerId owner = INVALID_PLAYER;
    float    tourismPerTurn = 0.0f;   ///< Total tourism output per turn
    float    cumulativeTourism = 0.0f; ///< Lifetime tourism generated
    int32_t  foreignTourists = 0;     ///< Computed from cumulative tourism vs others
    int32_t  domesticTourists = 0;    ///< Computed from cumulative culture

    int32_t  greatWorkCount = 0;      ///< Number of Great Works placed
    int32_t  wonderCount = 0;         ///< Number of wonders built
    int32_t  nationalParkCount = 0;   ///< Number of national parks
};

/**
 * @brief Compute tourism output for a player.
 *
 * Sums contributions from Great Works, Wonders, Holy Cities, National Parks.
 * Applies modifiers for open borders and trade routes.
 */
void computeTourism(aoc::game::GameState& gameState, PlayerId player,
                    const aoc::map::HexGrid& grid);

/**
 * @brief Check Cultural Victory condition (Classic mode only).
 *
 * Returns the winning player if their foreign tourists exceed every
 * other civ's domestic tourists. Returns INVALID_PLAYER if no winner.
 */
[[nodiscard]] PlayerId checkCulturalVictory(const aoc::game::GameState& gameState);

} // namespace aoc::sim

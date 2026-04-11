#pragma once

/**
 * @file EraScore.hpp
 * @brief Golden/Dark age system driven by era score accumulation.
 *
 * Players accumulate era score from achievements (combat wins, tech,
 * wonders, etc.). When the era changes, the accumulated score determines
 * whether the player enters a Golden Age, Dark Age, or Normal age.
 *
 * Golden Age: +10% all yields, +2 movement for all units.
 * Dark Age:   -15% all yields, increased foreign loyalty pressure.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <string>

namespace aoc::game {
class Player;
}

namespace aoc::sim {

/// The type of age a player is currently in.
enum class AgeType : uint8_t {
    Normal,
    Golden,
    Dark,
};

/// Per-player era score and age state.
struct PlayerEraScoreComponent {
    PlayerId owner = INVALID_PLAYER;

    int32_t eraScore            = 0;   ///< Accumulated points this era.
    int32_t goldenAgeThreshold  = 20;  ///< Score needed for golden age.
    int32_t darkAgeThreshold    = 5;   ///< Below this triggers dark age.
    AgeType currentAgeType      = AgeType::Normal;
    int32_t turnsRemaining      = 0;   ///< Turns left in current age bonus.

    /// Cumulative victory points from era events.
    int32_t eraVictoryPoints    = 0;
};

/**
 * @brief Add era score points and log the reason.
 */
void addEraScore(aoc::game::Player& player,
                 int32_t points, const std::string& reason);

/**
 * @brief Check whether the player has entered a new era and resolve
 *        golden/dark/normal age accordingly. Call when era changes.
 */
void checkEraTransition(aoc::game::Player& player);

/**
 * @brief Apply per-turn age bonuses/penalties and decrement the timer.
 */
void processAgeEffects(aoc::game::Player& player);

} // namespace aoc::sim

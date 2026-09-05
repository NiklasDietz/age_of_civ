#pragma once

/**
 * @file EraScore.hpp
 * @brief Golden/Dark age system driven by era score accumulation, plus the
 *        Historic Moments timeline that records every award.
 *
 * Players accumulate era score from achievements (combat wins, tech,
 * wonders, etc.). Every age window the accumulated score determines
 * whether the player enters a Golden Age, Dark Age, or Normal age.
 *
 * Golden Age: +10% all yields, +2 movement for all units.
 * Dark Age:   -15% all yields, increased foreign loyalty pressure.
 *
 * Each award is also a Historic Moment: turn, points and text, kept newest
 * last in a bounded timeline the Historic Moments screen reads. Age changes
 * are recorded as zero-point moments so the timeline shows them in place.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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

[[nodiscard]] constexpr std::string_view ageTypeName(AgeType type) {
    switch (type) {
        case AgeType::Golden: return "Golden";
        case AgeType::Dark:   return "Dark";
        case AgeType::Normal: return "Normal";
    }
    return "Normal";
}

/// One entry of the Historic Moments timeline.
struct HistoricMoment {
    int32_t     turn   = 0;
    int32_t     points = 0;  ///< Era score awarded; 0 for age changes.
    std::string text;
};

/// The timeline keeps this many moments; the oldest are dropped first.
inline constexpr std::size_t MAX_HISTORIC_MOMENTS = 64;

/// Per-player era score and age state.
struct PlayerEraScoreComponent {
    PlayerId owner = INVALID_PLAYER;

    int32_t eraScore            = 0;   ///< Accumulated points this age window.
    int32_t goldenAgeThreshold  = 20;  ///< Score needed for golden age.
    int32_t darkAgeThreshold    = 5;   ///< Below this triggers dark age.
    AgeType currentAgeType      = AgeType::Normal;
    int32_t turnsRemaining      = 0;   ///< Turns left in current age bonus.

    /// Cumulative victory points from era events.
    int32_t eraVictoryPoints    = 0;

    /// Every point ever awarded; unlike eraScore it survives the age reset.
    int32_t lifetimeEraScore    = 0;

    /// Newest last, at most MAX_HISTORIC_MOMENTS entries.
    std::vector<HistoricMoment> moments;
};

/**
 * @brief Append a moment to the timeline, dropping the oldest past the cap.
 *        Does not touch the scores; addEraScore does.
 */
void recordHistoricMoment(PlayerEraScoreComponent& score, int32_t turn, int32_t points,
                          std::string text);

/**
 * @brief Add era score points on `turn`, record the moment and log the reason.
 */
void addEraScore(aoc::game::Player& player, int32_t turn, int32_t points,
                 const std::string& reason);

/**
 * @brief Resolve the accumulated score into a Golden/Dark/Normal age, record
 *        the change as a moment on `turn`, reset the score and re-arm the
 *        thresholds. Call once per age window.
 */
void checkEraTransition(aoc::game::Player& player, int32_t turn);

/**
 * @brief Apply per-turn age bonuses/penalties and decrement the timer.
 */
void processAgeEffects(aoc::game::Player& player);

} // namespace aoc::sim

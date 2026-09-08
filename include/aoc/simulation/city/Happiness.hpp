#pragma once

#include "aoc/simulation/religion/Religion.hpp"

/**
 * @file Happiness.hpp
 * @brief City happiness/amenity system.
 *
 * Happiness = amenities - population_demand + modifiers
 * Positive: bonuses to growth and production.
 * Zero: neutral.
 * Negative: penalties to growth, eventually unrest and rebellion.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::game {
class Player;
}

namespace aoc::sim {

/// Ceiling on a single good's amenity contribution, as a multiple of its base.
///
/// Was 4x, which a stockpile of sixteen units reached; every unit past that was
/// worth nothing at all. Raised so a well-supplied city reads differently from
/// a barely-supplied one, but still bounded -- unbounded amenities from a
/// stockpile would turn happiness into a warehousing exercise.
inline constexpr float GOODS_AMENITY_CAP_MULTIPLE = 8.0f;

/// Amenity swing between a city whose consumer demand is fully met and one
/// getting nothing. Half up, half down from neutral, so meeting demand is a
/// reward and failing it is a penalty rather than merely the absence of one.
inline constexpr float CONSUMER_SATISFACTION_AMENITIES = 2.0f;

struct CityHappinessComponent {
    float amenities       = 1.0f;   ///< From luxury resources, buildings, policies
    float demand          = 0.0f;   ///< Based on population (1 per 2 citizens)
    float modifiers       = 0.0f;   ///< From war weariness, inflation, taxes, etc.
    /// WP-A4: persistent unhappiness from climate-driven disasters. Decays
    /// 10%/turn in computeCityHappiness; folded into modifiers each recompute.
    float disasterUnhappiness = 0.0f;
    float happiness       = 1.0f;   ///< Net happiness = amenities - demand + modifiers

    /// How much of last turn's consumer-goods demand the city could actually
    /// meet, 0..1. Recomputed by the consumption drain every turn, so it is
    /// NOT persisted -- a loaded save simply recomputes it on the next turn.
    ///
    /// The drains that consume these goods destroyed them with no reward for
    /// consuming and no penalty for lacking; one drain's own comment said no
    /// downstream effect read its result. This is that result.
    float consumerSatisfaction = 1.0f;

    /// Growth multiplier from happiness. Happy cities grow faster.
    [[nodiscard]] float growthMultiplier() const {
        if (this->happiness >= 3.0f) {
            return 1.2f;  // Ecstatic: +20% growth
        }
        if (this->happiness >= 1.0f) {
            return 1.0f;  // Happy: normal
        }
        if (this->happiness >= 0.0f) {
            return 0.85f; // Content: slightly reduced
        }
        if (this->happiness >= -3.0f) {
            return 0.5f;  // Unhappy: halved growth
        }
        return 0.0f;     // Unrest: no growth
    }

    /// Production multiplier from happiness.
    [[nodiscard]] float productionMultiplier() const {
        if (this->happiness >= 3.0f) {
            return 1.1f;
        }
        if (this->happiness >= 0.0f) {
            return 1.0f;
        }
        return 0.85f;  // Unhappy: -15% production
    }
};

/**
 * @brief Recalculate happiness for all cities of a player.
 *
 * Uses GameState object model (Player/City) directly.
 * Considers: luxury resources, buildings, population size,
 * war weariness, inflation penalty, tax penalty, empire size,
 * military unit unhappiness, specialist entertainers.
 */
/// `tracker` supplies the follower beliefs of whichever religion holds each
/// city. Passing it is what let the belief bonus be applied at all: the
/// religions live on GameState, and this used to see only the Player, so the
/// follower amenity bonus sat behind a comment saying it would be added when
/// the global state was reachable.
void computeCityHappiness(aoc::game::Player& player,
                          const aoc::sim::GlobalReligionTracker* tracker = nullptr);

} // namespace aoc::sim

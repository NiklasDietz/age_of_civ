#pragma once

/**
 * @file CityLoyalty.hpp
 * @brief City loyalty system based on Civ 6 loyalty mechanics.
 *
 * Each city has 0-100 loyalty. Nearby cities exert pressure:
 *   - Own cities within 9 hexes push loyalty UP
 *   - Foreign cities within 9 hexes push loyalty DOWN
 *   - Pressure scales with population (bigger cities exert more)
 *   - Distance reduces pressure (closer = stronger)
 *
 * Loyalty states:
 *   100-76: Loyal      (full yields, green icon)
 *   75-51:  Content    (full yields, no icon)
 *   50-26:  Disloyal   (-25% all yields, yellow warning icon)
 *   25-1:   Unrest     (-50% all yields, red warning icon, may revolt)
 *   0:      Revolt     (city becomes Free City or flips to dominant neighbor)
 *
 * Loyalty modifiers:
 *   +8  base from being your city
 *   +4  if city has a Governor assigned
 *   +2  per Monument building
 *   +3  per Garrison (military unit in city)
 *   +5  during Golden Age, -5 during Dark Age
 *   -2  per point of unhappiness
 *   -3  if recently captured (different from original owner)
 *   +/- Policy card effects
 *
 * Pressure from nearby cities (within 9 hexes):
 *   pressure = sum of (population * 0.5 / distance) for each nearby city
 *   Own cities add to loyalty, foreign cities subtract.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::game { class GameState; }
namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

/// Loyalty status tiers (determines yield penalty and UI icon).
enum class LoyaltyStatus : uint8_t {
    Loyal,      ///< 76-100: Full yields, green icon
    Content,    ///< 51-75:  Full yields, no special icon
    Disloyal,   ///< 26-50:  -25% yields, yellow warning
    Unrest,     ///< 1-25:   -50% yields, red warning, revolt risk
    Revolt,     ///< 0:      City flips to Free City or dominant neighbor
};

/// Get the loyalty status from a raw loyalty value.
[[nodiscard]] constexpr LoyaltyStatus loyaltyToStatus(float loyalty) {
    if (loyalty > 75.0f) { return LoyaltyStatus::Loyal; }
    if (loyalty > 50.0f) { return LoyaltyStatus::Content; }
    if (loyalty > 25.0f) { return LoyaltyStatus::Disloyal; }
    if (loyalty > 0.0f)  { return LoyaltyStatus::Unrest; }
    return LoyaltyStatus::Revolt;
}

/// Yield multiplier for each loyalty status.
[[nodiscard]] constexpr float loyaltyYieldMultiplier(LoyaltyStatus status) {
    switch (status) {
        case LoyaltyStatus::Loyal:    return 1.0f;
        case LoyaltyStatus::Content:  return 1.0f;
        case LoyaltyStatus::Disloyal: return 0.75f;
        case LoyaltyStatus::Unrest:   return 0.50f;
        case LoyaltyStatus::Revolt:   return 0.0f;
    }
    return 1.0f;
}

[[nodiscard]] constexpr const char* loyaltyStatusName(LoyaltyStatus status) {
    switch (status) {
        case LoyaltyStatus::Loyal:    return "Loyal";
        case LoyaltyStatus::Content:  return "Content";
        case LoyaltyStatus::Disloyal: return "Disloyal";
        case LoyaltyStatus::Unrest:   return "Unrest";
        case LoyaltyStatus::Revolt:   return "Revolt";
    }
    return "Unknown";
}

/// ECS component tracking per-city loyalty.
struct CityLoyaltyComponent {
    float loyalty        = 100.0f;  ///< [0, 100]
    float loyaltyPerTurn = 0.0f;    ///< Net change computed each turn.

    /// Breakdown of loyalty sources (for UI display).
    float baseLoyalty         = 0.0f;  ///< +8 base
    float ownCityPressure     = 0.0f;  ///< Positive pressure from own nearby cities
    float foreignCityPressure = 0.0f;  ///< Negative pressure from foreign nearby cities
    float governorBonus       = 0.0f;  ///< +4 if governor assigned
    float garrisonBonus       = 0.0f;  ///< +3 per military unit in city
    float monumentBonus       = 0.0f;  ///< +2 per Monument
    float happinessEffect     = 0.0f;  ///< -2 per unhappiness point
    float ageEffect           = 0.0f;  ///< +/-5 from Golden/Dark Age
    float capturedPenalty     = 0.0f;  ///< -3 if recently captured
    float devotionBonus       = 0.0f;  ///< Religion stabilisation bonus (eras 0-2 only)

    int32_t unrestTurns       = 0;     ///< Consecutive turns with loyalty < 25

    /// Current loyalty status tier.
    [[nodiscard]] LoyaltyStatus status() const {
        return loyaltyToStatus(this->loyalty);
    }

    /// Yield multiplier based on current loyalty.
    [[nodiscard]] float yieldMultiplier() const {
        return loyaltyYieldMultiplier(this->status());
    }
};

/**
 * @brief Recalculate loyalty for all cities owned by a player.
 *
 * Computes pressure from nearby cities, applies modifiers, updates
 * loyalty values, and handles city flipping at 0 loyalty.
 */
void computeCityLoyalty(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                        PlayerId player);

} // namespace aoc::sim

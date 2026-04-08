#pragma once

/**
 * @file GreatPeopleExpanded.hpp
 * @brief 100+ named great people with unique abilities, and spy promotions.
 *
 * Each great person has a historical name and a unique one-time ability:
 *   Scientists:  Free tech boost, eureka, research facility
 *   Engineers:   Instant production, wonder rush, industrial boost
 *   Generals:    Combat aura, movement bonus, formation buff
 *   Artists:     Great work, culture bomb, tourism boost
 *   Merchants:   Gold burst, trade route bonus, market manipulation
 *   Admirals:    Naval combat aura, exploration, fleet movement
 *   Prophets:    Found religion, beliefs, convert cities
 *   Writers:     Great work of writing, culture per turn
 *   Musicians:   Great work of music, tourism burst
 *
 * === Spy Promotions ===
 * Spies gain experience from missions. At thresholds they earn promotions:
 *   Lv1 (3 missions):  +10% success rate
 *   Lv2 (6 missions):  -1 turn mission duration
 *   Lv3 (10 missions): +25% success, immune to counterintelligence detection
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <string_view>

namespace aoc::sim {

// ============================================================================
// Named Great People (20 per type, 9 types = 180 total)
// ============================================================================

enum class GreatPersonCategory : uint8_t {
    Scientist,
    Engineer,
    General,
    Artist,
    Merchant,
    Admiral,
    Prophet,
    Writer,
    Musician,

    Count
};

struct NamedGreatPersonDef {
    uint8_t              id;
    GreatPersonCategory  category;
    std::string_view     name;
    std::string_view     abilityName;
    std::string_view     abilityDescription;
    EraId                era;
};

/// Total named great people.
inline constexpr int32_t NAMED_GP_COUNT = 108;

/// Get a named great person definition by ID.
[[nodiscard]] const NamedGreatPersonDef& namedGreatPersonDef(uint8_t id);

/// Get all named great people.
[[nodiscard]] const NamedGreatPersonDef* allNamedGreatPeople();

// ============================================================================
// Spy Promotions
// ============================================================================

enum class SpyPromotion : uint8_t {
    None      = 0,
    Veteran   = 1,  ///< +10% success rate (3 missions)
    Expert    = 2,  ///< -1 turn mission duration (6 missions)
    Master    = 3,  ///< +25% success, immune to detection (10 missions)
};

/// Mission count thresholds for spy promotions.
[[nodiscard]] constexpr SpyPromotion spyPromotionForMissions(int32_t missionCount) {
    if (missionCount >= 10) { return SpyPromotion::Master; }
    if (missionCount >= 6)  { return SpyPromotion::Expert; }
    if (missionCount >= 3)  { return SpyPromotion::Veteran; }
    return SpyPromotion::None;
}

/// Success rate modifier from spy promotion.
[[nodiscard]] constexpr float spySuccessModifier(SpyPromotion promo) {
    switch (promo) {
        case SpyPromotion::Veteran: return 0.10f;
        case SpyPromotion::Expert:  return 0.10f;
        case SpyPromotion::Master:  return 0.25f;
        default:                    return 0.0f;
    }
}

/// Mission duration reduction from spy promotion.
[[nodiscard]] constexpr int32_t spyDurationReduction(SpyPromotion promo) {
    return (promo >= SpyPromotion::Expert) ? 1 : 0;
}

} // namespace aoc::sim

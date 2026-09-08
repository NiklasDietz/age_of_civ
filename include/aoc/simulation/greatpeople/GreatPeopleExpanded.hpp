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
#include "aoc/simulation/greatpeople/GreatPeople.hpp"

#include <cstdint>
#include <string_view>

namespace aoc::sim {

// ============================================================================
// Named Great People (12 per category, 9 categories = 108 total)
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

    // ---- What this particular figure actually does -------------------------
    //
    // Until 2026-09-08 the three strings above were the whole of a named
    // person: effects came from the 30-entry GreatPersonDef selected by
    // `defId`, and `namedId` drove only the name, Great Work attribution and
    // UI. Monet's "+200 tourism" was text.
    //
    // `magnitudeScale` multiplies the type's own magnitude, so a figure
    // remembered for a bigger contribution gives more of whatever its type
    // gives. The bonus yields below are granted on top, and are what let a
    // description naming a specific yield actually pay it.

    /// Multiplier on the type's magnitude (science, production, gold, faith).
    float   magnitudeScale = 1.0f;

    /// One-off yields this figure grants on activation, over and above its
    /// type's effect.
    float   bonusCulture = 0.0f;
    float   bonusFaith   = 0.0f;
    float   bonusScience = 0.0f;
    int64_t bonusGold    = 0;
};

/// Total named great people.
inline constexpr int32_t NAMED_GP_COUNT = 108;

/// Get a named great person definition by ID.
[[nodiscard]] const NamedGreatPersonDef& namedGreatPersonDef(uint8_t id);

/// Get all named great people.
[[nodiscard]] const NamedGreatPersonDef* allNamedGreatPeople();

/// Display name of a roster category.
[[nodiscard]] const char* greatPersonCategoryName(GreatPersonCategory category);

/// Named people per category. The roster is grouped by category, twelve each.
inline constexpr int32_t NAMED_GP_PER_CATEGORY = NAMED_GP_COUNT / static_cast<int32_t>(GreatPersonCategory::Count);

/// The `nth` named person of `category`, wrapping if `nth` exceeds the roster.
/// `MAX_GP_PER_TYPE` equals the per-category count, so recruitment never wraps.
[[nodiscard]] const NamedGreatPersonDef& namedGreatPersonForCategory(GreatPersonCategory category,
                                                                     int32_t nth);

/// The roster category matching a live `GreatPersonType`. The first five
/// categories are declared in the same order as the five types; Admiral,
/// Prophet, Writer and Musician have no type and are never recruited today.
[[nodiscard]] constexpr GreatPersonCategory categoryForGreatPersonType(GreatPersonType type) {
    return static_cast<GreatPersonCategory>(static_cast<uint8_t>(type));
}

} // namespace aoc::sim

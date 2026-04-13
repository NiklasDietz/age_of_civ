#pragma once

/**
 * @file AllianceTypes.hpp
 * @brief 5 alliance types with leveling system.
 *
 * Alliances level up over time (every 30 turns of active alliance).
 * Higher levels provide stronger bonuses. All alliances provide
 * Open Borders + Defensive Pact as a baseline.
 *
 * Types:
 *   - Research: shared eureka boosts → free techs
 *   - Military: shared visibility → combat bonuses
 *   - Economic: shared market → trade yield bonuses
 *   - Cultural: tourism bonus → shared great works slots
 *   - Religious: shared faith → theological combat bonus
 */

#include "aoc/core/Types.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace aoc::sim {

enum class AllianceType : uint8_t {
    None      = 0,
    Research  = 1,
    Military  = 2,
    Economic  = 3,
    Cultural  = 4,
    Religious = 5,

    Count
};

inline constexpr int32_t ALLIANCE_TYPE_COUNT = 5;

enum class AllianceLevel : uint8_t {
    Level1 = 1,  ///< Basic: Open Borders + Defensive Pact
    Level2 = 2,  ///< Intermediate: type-specific bonus
    Level3 = 3,  ///< Maximum: powerful type-specific bonus
};

struct AllianceLevelBonus {
    std::string_view description;
    float            bonusValue;  ///< Type-specific numeric bonus
};

struct AllianceTypeDef {
    AllianceType     type;
    std::string_view name;
    std::string_view description;
    int32_t          turnsToLevel2;  ///< Turns of active alliance to reach L2
    int32_t          turnsToLevel3;  ///< Turns to reach L3
    AllianceLevelBonus level2Bonus;
    AllianceLevelBonus level3Bonus;
};

inline constexpr std::array<AllianceTypeDef, ALLIANCE_TYPE_COUNT> ALLIANCE_TYPE_DEFS = {{
    {AllianceType::Research, "Research Alliance",
     "Share scientific knowledge. Higher levels grant free tech boosts.",
     30, 60,
     {"Free Eureka boost every 30 turns from ally's researched tech", 0.40f},
     {"Gain a free tech every 50 turns if ally has more techs", 1.0f}},

    {AllianceType::Military, "Military Alliance",
     "Joint military operations. Higher levels grant combat bonuses.",
     30, 60,
     {"Shared visibility: see what your ally sees", 1.0f},
     {"+5 combat strength for your units within 3 tiles of ally units", 5.0f}},

    {AllianceType::Economic, "Economic Alliance",
     "Integrated markets and trade. Higher levels boost trade yields.",
     30, 60,
     {"Shared market prices: buy/sell at ally's better prices", 1.0f},
     {"+15% gold from all trade routes with ally", 0.15f}},

    {AllianceType::Cultural, "Cultural Alliance",
     "Cultural exchange. Higher levels boost tourism and great works.",
     30, 60,
     {"+25% tourism between allied civilizations", 0.25f},
     {"Can share Great Works slots with ally's cities", 1.0f}},

    {AllianceType::Religious, "Religious Alliance",
     "Shared faith and theological support.",
     30, 60,
     {"+25% faith generation from shared holy sites", 0.25f},
     {"+10 theological combat strength for your religious units in ally territory", 10.0f}},
}};

/// Per-pair alliance tracking (stored alongside PairwiseRelation).
struct AllianceState {
    AllianceType  type  = AllianceType::None;
    AllianceLevel level = AllianceLevel::Level1;
    int32_t       turnsActive = 0;

    [[nodiscard]] bool isActive() const { return this->type != AllianceType::None; }

    /// Tick: advance alliance level based on turns active.
    void tick() {
        if (!this->isActive()) { return; }
        ++this->turnsActive;
        const AllianceTypeDef& def = ALLIANCE_TYPE_DEFS[static_cast<std::size_t>(this->type) - 1];
        if (this->turnsActive >= def.turnsToLevel3) {
            this->level = AllianceLevel::Level3;
        } else if (this->turnsActive >= def.turnsToLevel2) {
            this->level = AllianceLevel::Level2;
        }
    }
};

} // namespace aoc::sim

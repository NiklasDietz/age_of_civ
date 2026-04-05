#pragma once

/**
 * @file Government.hpp
 * @brief Government type definitions, policy card definitions, and modifier tables.
 *
 * Governments provide inherent bonuses and a number of policy card slots.
 * Policy cards are unlocked through civics and slotted into matching slot types
 * to provide additional modifiers to production, science, gold, culture, and combat.
 */

#include <array>
#include <cstdint>
#include <string_view>

namespace aoc::sim {

// ============================================================================
// Government types
// ============================================================================

/// Available government forms.
enum class GovernmentType : uint8_t {
    Chiefdom,
    Autocracy,
    Oligarchy,
    Monarchy,
    Democracy,
    Communism,
    Fascism,

    Count
};

/// Aggregate modifiers applied by a government and/or policy cards.
struct GovernmentModifiers {
    float productionMultiplier  = 1.0f;
    float goldMultiplier        = 1.0f;
    float combatStrengthBonus   = 0.0f;
    float scienceMultiplier     = 1.0f;
    float cultureMultiplier     = 1.0f;
};

/// Policy slot classification.
enum class PolicySlotType : uint8_t {
    Military,
    Economic,
    Diplomatic,
    Wildcard,
};

// ============================================================================
// Government definition
// ============================================================================

/// Static definition of a government form.
struct GovernmentDef {
    GovernmentType     type;
    std::string_view   name;
    uint8_t            requiredCivicId;  ///< CivicId value; 255 = no requirement.
    uint8_t            militarySlots;
    uint8_t            economicSlots;
    uint8_t            diplomaticSlots;
    uint8_t            wildcardSlots;
    GovernmentModifiers inherentBonuses;
};

/// Total number of government types.
inline constexpr uint8_t GOVERNMENT_COUNT = static_cast<uint8_t>(GovernmentType::Count);

/// Government definition table (indexed by GovernmentType).
inline constexpr std::array<GovernmentDef, GOVERNMENT_COUNT> GOVERNMENT_DEFS = {{
    {GovernmentType::Chiefdom,  "Chiefdom",  255, 1, 1, 0, 0, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f}},
    {GovernmentType::Autocracy, "Autocracy",   3, 2, 1, 0, 0, {1.10f, 1.0f, 0.0f, 1.0f, 1.0f}},
    {GovernmentType::Oligarchy, "Oligarchy",   3, 1, 1, 1, 0, {1.0f, 1.0f, 4.0f, 1.0f, 1.0f}},
    {GovernmentType::Monarchy,  "Monarchy",    6, 2, 1, 1, 1, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f}},
    {GovernmentType::Democracy, "Democracy",  12, 1, 2, 2, 0, {1.0f, 1.0f, 0.0f, 1.15f, 1.0f}},
    {GovernmentType::Communism, "Communism",  11, 3, 2, 0, 0, {1.15f, 1.0f, 0.0f, 1.0f, 1.0f}},
    {GovernmentType::Fascism,   "Fascism",    11, 4, 1, 0, 0, {1.0f, 1.0f, 5.0f, 1.0f, 0.90f}},
}};

/// Look up a government definition by type.
[[nodiscard]] inline constexpr const GovernmentDef& governmentDef(GovernmentType type) {
    return GOVERNMENT_DEFS[static_cast<uint8_t>(type)];
}

// ============================================================================
// Policy card definitions
// ============================================================================

/// Total number of policy cards.
inline constexpr uint8_t POLICY_CARD_COUNT = 10;

/// Static definition of a policy card.
struct PolicyCardDef {
    uint8_t            id;
    std::string_view   name;
    PolicySlotType     slotType;
    uint8_t            requiredCivicId;  ///< CivicId value; 255 = no requirement.
    GovernmentModifiers modifiers;
};

/// Policy card definition table.
inline constexpr std::array<PolicyCardDef, POLICY_CARD_COUNT> POLICY_CARD_DEFS = {{
    // Military
    {0, "Discipline",    PolicySlotType::Military,   5, {1.0f, 1.0f, 8.0f, 1.0f, 1.0f}},
    {1, "Survey",        PolicySlotType::Military,   1, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f}},  // +1 movement scouts (applied elsewhere)
    {2, "Conscription",  PolicySlotType::Military,  11, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f}},  // -1 unit maintenance (applied elsewhere)

    // Economic
    {3, "Urban Planning",  PolicySlotType::Economic,   0, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f}},  // +1 production per city (applied elsewhere)
    {4, "Caravansaries",   PolicySlotType::Economic,   4, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f}},  // +2 gold trade routes (applied elsewhere)
    {5, "Rationalism",     PolicySlotType::Economic,  10, {1.0f, 1.0f, 0.0f, 1.10f, 1.0f}},

    // Diplomatic
    {6, "Charismatic Leader", PolicySlotType::Diplomatic, 3, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f}},  // +2 influence (applied elsewhere)
    {7, "Diplomatic League", PolicySlotType::Diplomatic, 2, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f}},   // +1 trade route (applied elsewhere)

    // Wildcard
    {8, "Inspiration",  PolicySlotType::Wildcard, 0, {1.0f, 1.0f, 0.0f, 1.0f, 1.10f}},
    {9, "Free Market",  PolicySlotType::Wildcard, 8, {1.0f, 1.10f, 0.0f, 1.0f, 1.0f}},
}};

/// Look up a policy card definition by ID.
[[nodiscard]] inline constexpr const PolicyCardDef& policyCardDef(uint8_t id) {
    return POLICY_CARD_DEFS[id];
}

} // namespace aoc::sim

#pragma once

/**
 * @file Government.hpp
 * @brief Government type definitions, policy card definitions, and modifier tables.
 *
 * Governments provide inherent bonuses, policy card slots, and unique actions.
 * Changing government causes 5 turns of anarchy (no bonuses, -3 amenities).
 *
 * Corruption scales with empire size and is modulated by government type:
 *   - Democracy: lowest corruption (transparency)
 *   - Communism: medium corruption (bureaucracy)
 *   - Fascism: high corruption (cronyism)
 *   - Monarchy: medium (patronage)
 *
 * 22 policy cards across 4 slot types, all with real gameplay effects.
 */

#include "aoc/core/Types.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace aoc::sim {

// ============================================================================
// Government types
// ============================================================================

enum class GovernmentType : uint8_t {
    Chiefdom,
    Autocracy,
    Oligarchy,
    Monarchy,
    Democracy,
    Communism,
    Fascism,
    Theocracy,       ///< Faith-focused government
    MerchantRepublic, ///< Trade-focused government

    Count
};

/// Aggregate modifiers applied by a government and/or policy cards.
struct GovernmentModifiers {
    float productionMultiplier  = 1.0f;
    float goldMultiplier        = 1.0f;
    float combatStrengthBonus   = 0.0f;
    float scienceMultiplier     = 1.0f;
    float cultureMultiplier     = 1.0f;
    // New modifier fields (applied via policies)
    float tradeRouteBonus       = 0.0f;  ///< Extra gold per trade route
    float unitMaintenanceReduction = 0.0f; ///< Flat reduction per unit
    float productionPerCity     = 0.0f;  ///< Flat production bonus per city
    int32_t extraTradeRoutes    = 0;     ///< Bonus trade route slots
    float diplomaticInfluence   = 0.0f;  ///< Bonus to diplomacy interactions
    float corruptionReduction   = 0.0f;  ///< Reduces corruption rate
    float faithMultiplier       = 1.0f;
    float loyaltyBonus          = 0.0f;  ///< Flat loyalty bonus to all cities
    float espionageDefense      = 0.0f;  ///< Defense against spy missions
    float tariffEfficiency      = 0.0f;  ///< Bonus/penalty to tariff collection
    float warWearinessReduction = 0.0f;  ///< Reduces war weariness gain rate
    float growthMultiplier      = 1.0f;  ///< Population growth modifier
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

struct GovernmentDef {
    GovernmentType     type;
    std::string_view   name;
    uint8_t            requiredCivicId;
    uint8_t            militarySlots;
    uint8_t            economicSlots;
    uint8_t            diplomaticSlots;
    uint8_t            wildcardSlots;
    GovernmentModifiers inherentBonuses;
    float              corruptionRate;          ///< Base corruption per city beyond 4 (0.0-0.10)
    int32_t            empireSizeThreshold = 6; ///< Cities before happiness penalty kicks in
    float              militaryUnhappyFactor = 0.0f; ///< Unhappiness per military unit away from cities (0=authoritarian, 1=republic, 2=democracy)
    float              distanceCorruptionRate = 2.0f; ///< Multiplier for distance-based corruption (0=communism)
};

inline constexpr uint8_t GOVERNMENT_COUNT = static_cast<uint8_t>(GovernmentType::Count);

inline constexpr std::array<GovernmentDef, GOVERNMENT_COUNT> GOVERNMENT_DEFS = {{
    //                                                    M  E  D  W  {prod, gold, combat, sci, cul, ...}                        corrupt empireThresh milUnhappy distCorrupt
    {GovernmentType::Chiefdom,         "Chiefdom",         255, 1, 1, 0, 0, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f}, 0.05f,   4, 0.0f, 3.0f},
    {GovernmentType::Autocracy,        "Autocracy",          3, 2, 1, 0, 0, {1.10f, 1.0f, 0.0f, 1.0f, 1.0f}, 0.06f,  6, 0.0f, 4.0f},  // Authoritarian: no mil unhappy, high dist corruption
    {GovernmentType::Oligarchy,        "Oligarchy",          3, 1, 1, 1, 0, {1.0f, 1.0f, 4.0f, 1.0f, 1.0f}, 0.05f,   7, 0.0f, 2.0f},
    {GovernmentType::Monarchy,         "Monarchy",           6, 2, 1, 1, 1, {1.0f, 1.05f, 0.0f, 1.0f, 1.05f}, 0.04f, 8, 0.0f, 2.0f},
    {GovernmentType::Democracy,        "Democracy",         12, 1, 2, 2, 1, {1.0f, 1.0f, 0.0f, 1.0f, 1.15f}, 0.02f, 12, 2.0f, 2.0f},  // Democracy: high mil unhappy, big empire threshold
    {GovernmentType::Communism,        "Communism",         11, 3, 2, 0, 0, {1.15f, 0.90f, 0.0f, 1.05f, 1.0f}, 0.04f,10, 0.0f, 0.0f}, // Communism: no distance corruption
    {GovernmentType::Fascism,          "Fascism",           11, 4, 1, 0, 0, {1.05f, 1.0f, 5.0f, 1.0f, 0.90f}, 0.07f, 7, 0.0f, 3.0f},
    {GovernmentType::Theocracy,        "Theocracy",          8, 1, 1, 1, 2, {1.0f, 1.0f, 0.0f, 0.95f, 1.0f}, 0.03f,  8, 0.0f, 2.0f},
    {GovernmentType::MerchantRepublic, "Merchant Republic",  6, 0, 3, 1, 1, {1.0f, 1.15f, 0.0f, 1.0f, 1.0f}, 0.03f,  9, 1.0f, 2.0f},  // Republic: some mil unhappy
}};

[[nodiscard]] inline constexpr const GovernmentDef& governmentDef(GovernmentType type) {
    return GOVERNMENT_DEFS[static_cast<uint8_t>(type)];
}

// ============================================================================
// Policy card definitions -- ALL 22 with real modifiers
// ============================================================================

inline constexpr uint8_t POLICY_CARD_COUNT = 36;

struct PolicyCardDef {
    uint8_t            id;
    std::string_view   name;
    PolicySlotType     slotType;
    uint8_t            requiredCivicId;
    GovernmentModifiers modifiers;
};

// Helper: default modifiers with overrides
inline constexpr GovernmentModifiers defaultMods() { return {}; }

inline constexpr std::array<PolicyCardDef, POLICY_CARD_COUNT> POLICY_CARD_DEFS = {{
    // === Military (0-5) ===
    { 0, "Discipline",       PolicySlotType::Military,    5, {1.0f, 1.0f, 8.0f, 1.0f, 1.0f}},
    { 1, "Survey",           PolicySlotType::Military,    1, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                                                              0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}}, // Movement handled in unit system
    { 2, "Conscription",     PolicySlotType::Military,   11, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                                                              0.0f, 1.0f, 0.0f, 0, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}}, // -1 unit maintenance
    { 3, "Levee en Masse",   PolicySlotType::Military,   14, {1.10f, 1.0f, 3.0f, 1.0f, 1.0f,
                                                              0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, -0.20f, 1.0f}}, // +10% prod, +3 combat, -20% war weariness
    { 4, "Military Research",PolicySlotType::Military,    8, {1.0f, 1.0f, 0.0f, 1.10f, 1.0f}}, // +10% science
    { 5, "Fortification",    PolicySlotType::Military,    6, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                                                              0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 1.0f, 5.0f, 0.0f, 0.0f, 0.0f, 1.0f}}, // +5 loyalty

    // === Economic (6-11) ===
    { 6, "Urban Planning",   PolicySlotType::Economic,    0, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                                                              0.0f, 0.0f, 1.0f, 0, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}}, // +1 production per city
    { 7, "Caravansaries",    PolicySlotType::Economic,    4, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                                                              2.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}}, // +2 gold per trade route
    { 8, "Rationalism",      PolicySlotType::Economic,   10, {1.0f, 1.0f, 0.0f, 1.10f, 1.0f}}, // +10% science
    { 9, "Free Market",      PolicySlotType::Economic,    8, {1.0f, 1.10f, 0.0f, 1.0f, 1.0f}}, // +10% gold
    {10, "Mercantilism",     PolicySlotType::Economic,    6, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                                                              0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.15f, 0.0f, 1.0f}}, // +15% tariff efficiency
    {11, "Industrial Policy",PolicySlotType::Economic,   11, {1.15f, 1.0f, 0.0f, 1.0f, 1.0f}}, // +15% production

    // === Diplomatic (12-16) ===
    {12, "Charismatic Leader",PolicySlotType::Diplomatic,  3, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                                                               0.0f, 0.0f, 0.0f, 0, 2.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}}, // +2 diplomatic influence
    {13, "Diplomatic League",PolicySlotType::Diplomatic,   2, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                                                               0.0f, 0.0f, 0.0f, 1, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}}, // +1 trade route
    {14, "Foreign Trade",    PolicySlotType::Diplomatic,   4, {1.0f, 1.05f, 0.0f, 1.0f, 1.0f,
                                                               1.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}}, // +5% gold, +1 gold per trade route
    {15, "Espionage Network",PolicySlotType::Diplomatic,  10, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                                                               0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 1.0f, 0.0f, 0.15f, 0.0f, 0.0f, 1.0f}}, // +15% espionage defense
    {16, "Anti-Corruption",  PolicySlotType::Diplomatic,   8, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                                                               0.0f, 0.0f, 0.0f, 0, 0.0f, 0.03f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}}, // -3% corruption

    // === Wildcard (17-21) ===
    {17, "Inspiration",      PolicySlotType::Wildcard,     0, {1.0f, 1.0f, 0.0f, 1.0f, 1.10f}}, // +10% culture
    {18, "Religious Zeal",   PolicySlotType::Wildcard,     3, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                                                               0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 1.25f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}}, // +25% faith
    {19, "Public Works",     PolicySlotType::Wildcard,     6, {1.05f, 1.0f, 0.0f, 1.0f, 1.0f,
                                                               0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 1.0f, 3.0f, 0.0f, 0.0f, 0.0f, 1.05f}}, // +5% prod, +3 loyalty, +5% growth
    {20, "Propaganda",       PolicySlotType::Wildcard,    11, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                                                               0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 1.0f, 10.0f, 0.0f, 0.0f, -0.30f, 1.0f}}, // +10 loyalty, -30% war weariness
    {21, "Laissez-Faire",    PolicySlotType::Wildcard,     8, {1.0f, 1.15f, 0.0f, 1.0f, 1.0f,
                                                               0.0f, 0.0f, 0.0f, 0, 0.0f, -0.02f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.10f}}, // +15% gold, -2% corruption red (more corruption), +10% growth

    // === 2026-05-03: 14 new policy cards tied to expanded civics (14-46) ===
    {22, "Logistics",        PolicySlotType::Economic,    14, {1.05f, 1.0f, 0.0f, 1.0f, 1.0f}}, // State Workforce: +5% prod
    {23, "Imperialism",      PolicySlotType::Military,    15, {1.0f, 1.0f, 4.0f, 1.0f, 1.0f}}, // Early Empire: +4 combat
    {24, "Religious Order",  PolicySlotType::Wildcard,    16, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                                                               0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 1.15f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}}, // Mysticism: +15% faith
    {25, "Drama Festivals",  PolicySlotType::Wildcard,    17, {1.0f, 1.0f, 0.0f, 1.0f, 1.10f}}, // Drama: +10% culture
    {26, "Naval Patrols",    PolicySlotType::Military,    22, {1.0f, 1.0f, 3.0f, 1.0f, 1.0f}}, // Naval Tradition: +3 combat
    {27, "Bureaucracy",      PolicySlotType::Economic,    23, {1.0f, 1.05f, 0.0f, 1.0f, 1.0f}}, // Civil Service: +5% gold
    {28, "Dual Monarchy",    PolicySlotType::Diplomatic,  24, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                                                               0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 1.0f, 2.0f, 0.0f, 0.0f, 0.0f, 1.0f}}, // Divine Right: +2 loyalty
    {29, "Public Schools",   PolicySlotType::Wildcard,    27, {1.0f, 1.0f, 0.0f, 1.10f, 1.0f}}, // Humanism: +10% science
    {30, "International Law",PolicySlotType::Diplomatic,  28, {1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                                                               0.0f, 0.0f, 0.0f, 1, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}}, // Dipl Service: +1 trade route, +1 dipl
    {31, "Total War",        PolicySlotType::Military,    37, {1.05f, 1.0f, 5.0f, 1.0f, 1.0f}}, // Mobilization: +5% prod, +5 combat
    {32, "Five-Year Plan",   PolicySlotType::Economic,    38, {1.15f, 1.0f, 0.0f, 1.0f, 1.0f}}, // Ideology: +15% prod
    {33, "Space Cooperation",PolicySlotType::Diplomatic,  42, {1.0f, 1.0f, 0.0f, 1.05f, 1.0f,
                                                               0.0f, 0.0f, 0.0f, 1, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}}, // Space Race: +5% science, +1 trade route
    {34, "New Deal",         PolicySlotType::Economic,    35, {1.0f, 1.05f, 0.0f, 1.0f, 1.0f}}, // Conservation: +5% gold
    {35, "Information Society",PolicySlotType::Wildcard,  43, {1.0f, 1.0f, 0.0f, 1.05f, 1.05f}}, // Social Media: +5% sci, +5% culture
}};

[[nodiscard]] inline constexpr const PolicyCardDef& policyCardDef(uint8_t id) {
    return POLICY_CARD_DEFS[id];
}

// ============================================================================
// Anarchy
// ============================================================================

/// Turns of anarchy when switching governments.
constexpr int32_t ANARCHY_DURATION = 5;

/// Amenity penalty during anarchy.
constexpr int32_t ANARCHY_AMENITY_PENALTY = 3;

// ============================================================================
// Corruption
// ============================================================================

/**
 * @brief Compute corruption loss for a player.
 *
 * Corruption = corruptionRate * max(0, cityCount - 4)
 * This fraction of gold and production is lost each turn.
 * Democracy has lowest corruption; Fascism highest.
 * Anti-Corruption policy and certain wonders reduce it.
 *
 * @param governmentType  Player's government.
 * @param cityCount       Number of cities owned.
 * @param corruptionReduction  Sum of corruption reduction modifiers from policies.
 * @return Fraction of gold/production lost (0.0 to ~0.30).
 */
[[nodiscard]] constexpr float computeCorruption(GovernmentType governmentType,
                                                 int32_t cityCount,
                                                 float corruptionReduction) {
    float baseRate = governmentDef(governmentType).corruptionRate;
    float adjustedRate = baseRate - corruptionReduction;
    if (adjustedRate < 0.0f) {
        adjustedRate = 0.0f;
    }
    int32_t excessCities = cityCount - 4;
    if (excessCities <= 0) {
        return 0.0f;
    }
    float corruption = adjustedRate * static_cast<float>(excessCities);
    // Cap at 30%
    return (corruption > 0.30f) ? 0.30f : corruption;
}

// ============================================================================
// Government unique actions
// ============================================================================

enum class GovernmentAction : uint8_t {
    None,
    Referendum,      ///< Democracy: +20 loyalty in all cities for 5 turns, costs gold
    FiveYearPlan,    ///< Communism: +30% production for 10 turns, -2 amenities
    Mobilization,    ///< Fascism: instant 3 military units, +war weariness
    RoyalDecree,     ///< Monarchy: +15% gold for 10 turns
    HolyWar,         ///< Theocracy: +4 combat for religious wars, +faith
    TradeFleet,      ///< Merchant Republic: +3 trade routes for 10 turns
};

/// Get the unique action available to a government type.
[[nodiscard]] constexpr GovernmentAction governmentUniqueAction(GovernmentType type) {
    switch (type) {
        case GovernmentType::Democracy:        return GovernmentAction::Referendum;
        case GovernmentType::Communism:        return GovernmentAction::FiveYearPlan;
        case GovernmentType::Fascism:          return GovernmentAction::Mobilization;
        case GovernmentType::Monarchy:         return GovernmentAction::RoyalDecree;
        case GovernmentType::Theocracy:        return GovernmentAction::HolyWar;
        case GovernmentType::MerchantRepublic: return GovernmentAction::TradeFleet;
        default:                               return GovernmentAction::None;
    }
}

} // namespace aoc::sim

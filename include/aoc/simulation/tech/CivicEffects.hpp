#pragma once

/**
 * @file CivicEffects.hpp
 * @brief Direct gameplay effects from civic research (beyond just unlocking governments/policies).
 *
 * Each civic can grant direct bonuses when researched:
 *   - Envoys to city-states
 *   - Extra builder charges
 *   - Unlock trade route slots
 *   - Immediate culture/faith burst
 *   - Unlock specific improvements
 *   - Government type unlocks (already handled)
 *   - Policy card unlocks (already handled)
 *
 * These effects fire once when the civic is completed.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <string_view>

namespace aoc::game { class GameState; }

namespace aoc::sim {

enum class CivicEffectType : uint8_t {
    None,
    GrantEnvoy,           ///< +1 envoy to nearest city-state
    ExtraBuilderCharges,  ///< All new builders get +1 charge
    ExtraTradeRoute,      ///< +1 trade route capacity
    CultureBurst,         ///< Immediate culture points
    FaithBurst,           ///< Immediate faith points
    UnlockImprovement,    ///< Unlock a tile improvement type
    FreeTech,             ///< Grant a eureka on a random tech
    LoyaltyBoost,         ///< +10 loyalty in all cities
};

struct CivicEffect {
    uint8_t         civicId;
    CivicEffectType type;
    int32_t         value;       ///< Depends on type (amount, improvement ID, etc.)
    std::string_view description;
};

/// Hard-coded civic effects. Each civic that grants a direct bonus has an entry.
inline constexpr CivicEffect CIVIC_EFFECTS[] = {
    { 0, CivicEffectType::ExtraBuilderCharges, 1, "+1 builder charge for new builders"},
    { 1, CivicEffectType::GrantEnvoy,          1, "+1 envoy to nearest city-state"},
    { 2, CivicEffectType::CultureBurst,      100, "+100 culture"},
    { 3, CivicEffectType::GrantEnvoy,          1, "+1 envoy"},
    { 4, CivicEffectType::ExtraTradeRoute,     1, "+1 trade route capacity"},
    { 6, CivicEffectType::LoyaltyBoost,       10, "+10 loyalty in all cities"},
    { 8, CivicEffectType::FaithBurst,        100, "+100 faith"},
    {10, CivicEffectType::CultureBurst,      200, "+200 culture"},
    {11, CivicEffectType::ExtraTradeRoute,     1, "+1 trade route capacity"},
    {12, CivicEffectType::GrantEnvoy,          2, "+2 envoys"},
    {14, CivicEffectType::FreeTech,            1, "Free eureka on random tech"},
};

inline constexpr int32_t CIVIC_EFFECT_COUNT = 11;

/**
 * @brief Apply the civic effect when a civic is completed.
 *
 * @param world    ECS world.
 * @param player   Player who completed the civic.
 * @param civicId  ID of the completed civic.
 */
void applyCivicEffect(aoc::game::GameState& gameState, PlayerId player, uint8_t civicId);

} // namespace aoc::sim

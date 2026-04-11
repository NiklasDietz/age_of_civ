/**
 * @file ResourceCurse.cpp
 * @brief Dutch disease calculation and application.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/economy/ResourceCurse.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/ecs/World.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

ResourceCurseModifiers computeResourceCurse(const aoc::game::GameState& gameState,
                                              PlayerId player) {
    aoc::ecs::World& world = gameState.legacyWorld();
    ResourceCurseModifiers result{};
    aoc::ecs::World& world = gameState.legacyWorld();
    result.manufacturingPenalty  = 1.0f;
    result.currencyAppreciation  = 1.0f;
    result.happinessPenalty      = 0.0f;
    result.resourceDependence    = 0.0f;

    const aoc::ecs::ComponentPool<PlayerEconomyComponent>* econPool =
        world.getPool<PlayerEconomyComponent>();
    if (econPool == nullptr) {
        return result;
    }

    // Find this player's economy component
    const PlayerEconomyComponent* playerEcon = nullptr;
    for (uint32_t i = 0; i < econPool->size(); ++i) {
        if (econPool->data()[i].owner == player) {
            playerEcon = &econPool->data()[i];
            break;
        }
    }
    if (playerEcon == nullptr) {
        return result;
    }

    // Calculate resource dependence: fraction of total supply value from raw resources
    float rawResourceValue = 0.0f;
    float totalValue = 0.0f;

    for (const std::pair<const uint16_t, int32_t>& entry : playerEcon->totalSupply) {
        const GoodDef& def = goodDef(entry.first);
        float value = static_cast<float>(entry.second) * static_cast<float>(def.basePrice);
        totalValue += value;

        if (def.category == GoodCategory::RawStrategic ||
            def.category == GoodCategory::RawLuxury) {
            rawResourceValue += value;
        }
    }

    if (totalValue < 1.0f) {
        return result;  // No production, no curse
    }

    result.resourceDependence = rawResourceValue / totalValue;

    // Dutch disease kicks in above 60% resource dependence
    if (result.resourceDependence > 0.6f) {
        float severity = (result.resourceDependence - 0.6f) / 0.4f;  // 0 to 1
        severity = std::clamp(severity, 0.0f, 1.0f);

        // Manufacturing becomes 10-30% slower
        result.manufacturingPenalty = 1.0f - severity * 0.3f;

        // Currency appreciates 5-20% (exports become more expensive for buyers)
        result.currencyAppreciation = 1.0f + severity * 0.2f;

        // Happiness penalty: 0-3 amenities lost
        result.happinessPenalty = severity * 3.0f;
    }

    return result;
}

void applyResourceCurseEffects(aoc::ecs::World& /*world*/,
                                PlayerId /*player*/,
                                const ResourceCurseModifiers& /*modifiers*/) {
    // Applied during ResourceProduction phase:
    // - Multiply processed/advanced good output by manufacturingPenalty
    // - Store currencyAppreciation for trade price calculations
    // - Subtract happinessPenalty from city amenities
    //
    // Full implementation will be wired when city production executes recipes.
    // For now this is a placeholder that other systems can call.
}

} // namespace aoc::sim

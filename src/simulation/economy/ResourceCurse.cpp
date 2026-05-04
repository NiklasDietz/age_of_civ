/**
 * @file ResourceCurse.cpp
 * @brief Dutch disease calculation and application.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/economy/ResourceCurse.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/city/Happiness.hpp"

#include <algorithm>

namespace aoc::sim {

ResourceCurseModifiers computeResourceCurse(const aoc::game::GameState& gameState,
                                              PlayerId player) {
    ResourceCurseModifiers result{};
    result.manufacturingPenalty = 1.0f;
    result.currencyAppreciation = 1.0f;
    result.happinessPenalty     = 0.0f;
    result.resourceDependence   = 0.0f;

    const aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return result;
    }

    const PlayerEconomyComponent& playerEcon = playerObj->economy();

    float rawResourceValue = 0.0f;
    float totalValue       = 0.0f;

    for (const std::pair<const uint16_t, int32_t>& entry : playerEcon.lastTurnProduction) {
        const GoodDef& def = goodDef(entry.first);
        float value        = static_cast<float>(entry.second) * static_cast<float>(def.basePrice);
        totalValue        += value;

        if (def.category == GoodCategory::RawStrategic ||
            def.category == GoodCategory::RawLuxury) {
            rawResourceValue += value;
        }
    }

    if (totalValue < 1.0f) {
        return result;
    }

    result.resourceDependence = rawResourceValue / totalValue;

    if (result.resourceDependence > 0.6f) {
        float severity = (result.resourceDependence - 0.6f) / 0.4f;
        severity = std::clamp(severity, 0.0f, 1.0f);

        result.manufacturingPenalty = 1.0f - severity * 0.3f;
        result.currencyAppreciation = 1.0f + severity * 0.2f;
        result.happinessPenalty     = severity * 3.0f;
    }

    return result;
}

void applyResourceCurseEffects(aoc::game::GameState& gameState,
                                PlayerId player,
                                const ResourceCurseModifiers& modifiers) {
    // C40: stamp modifiers onto the player so downstream systems read them:
    //   - EconomySimulation::executeProduction multiplies processed-good
    //     output by manufacturingPenalty.
    //   - ForexMarket reads currencyAppreciation to lift the fundamental rate.
    //   - Happiness pass subtracts happinessPenalty from city amenities below.
    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) { return; }
    playerObj->resourceCurse() = modifiers;

    if (modifiers.happinessPenalty > 0.0f) {
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerObj->cities()) {
            if (cityPtr == nullptr) { continue; }
            CityHappinessComponent& h = cityPtr->happiness();
            h.amenities = std::max(0.0f, h.amenities - modifiers.happinessPenalty);
            h.happiness = h.amenities - h.demand + h.modifiers;
        }
    }
}

} // namespace aoc::sim

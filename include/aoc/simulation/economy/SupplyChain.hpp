#pragma once

/**
 * @file SupplyChain.hpp
 * @brief Supply chain dependency tracking and disruption mechanics.
 *
 * When a civilization imports critical goods from another player (steel,
 * oil, electronics, etc.), it creates a supply chain dependency. If the
 * supplier cuts off trade (embargo, war, or their own shortage), the
 * dependent civilization suffers production halts in affected industries.
 *
 * This creates realistic geopolitical leverage:
 *   - Control of rare resources gives diplomatic power
 *   - Diversifying supply chains reduces vulnerability
 *   - Stockpiling strategic reserves buffers against cutoffs
 *   - Domestic production is safer but may be more expensive
 *
 * Supply chain health is tracked per player per critical good. A health
 * value of 1.0 means fully supplied; 0.0 means completely cut off.
 */

#include "aoc/core/Types.hpp"

#include <array>
#include <cstdint>

namespace aoc::game { class GameState; }

namespace aoc::sim {

/// Critical goods that create supply chain dependencies when imported.
/// These are the inputs that, if missing, halt specific production chains.
inline constexpr int32_t CRITICAL_GOOD_COUNT = 8;
inline constexpr uint16_t CRITICAL_GOODS[] = {
    0,   // IRON_ORE: needed for steel -> tools, weapons
    2,   // COAL: needed for power, steel smelting
    3,   // OIL: needed for plastics, fuel, military
    7,   // ALUMINUM: needed for aircraft, electronics
    9,   // RUBBER: needed for tires, seals
    64,  // STEEL: needed for construction, military
    61,  // COPPER_WIRE: needed for electronics
    60,  // IRON_INGOTS: needed for tools
};

struct PlayerSupplyChainComponent {
    PlayerId owner = INVALID_PLAYER;

    /// Supply health per critical good (0.0 = completely cut off, 1.0 = fully supplied).
    /// Index matches CRITICAL_GOODS array.
    std::array<float, CRITICAL_GOOD_COUNT> supplyHealth = {
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
    };

    /// Turns of stockpile buffer remaining per good (how many turns can
    /// production continue without imports).
    std::array<int32_t, CRITICAL_GOOD_COUNT> stockpileBuffer = {
        0, 0, 0, 0, 0, 0, 0, 0
    };

    /// True if production was below the crisis threshold last turn.
    /// Used to log the crisis only on the turn it begins, not every subsequent turn.
    bool wasInCrisisLastTurn = false;

    /// Overall production multiplier from supply chain health.
    /// Weighted average: goods used in more recipes have more impact.
    [[nodiscard]] float productionMultiplier() const {
        float totalWeight = 0.0f;
        float weightedHealth = 0.0f;
        // Iron/Steel most critical, oil second, rest equal
        constexpr float weights[] = {1.5f, 1.0f, 1.3f, 0.8f, 0.6f, 1.5f, 0.8f, 1.0f};
        for (int32_t i = 0; i < CRITICAL_GOOD_COUNT; ++i) {
            weightedHealth += this->supplyHealth[static_cast<std::size_t>(i)] * weights[i];
            totalWeight += weights[i];
        }
        float avgHealth = weightedHealth / totalWeight;
        // Production scales: 100% at full health, down to 50% at zero health
        return 0.50f + avgHealth * 0.50f;
    }

    /// Military production multiplier (steel + oil are critical).
    [[nodiscard]] float militaryProductionMultiplier() const {
        float steelHealth = this->supplyHealth[5];   // STEEL
        float ironHealth = this->supplyHealth[0];     // IRON_ORE
        float oilHealth = this->supplyHealth[2];      // OIL
        float avg = (steelHealth * 2.0f + ironHealth + oilHealth * 1.5f) / 4.5f;
        return 0.40f + avg * 0.60f;  // 40% minimum, up to 100%
    }
};

/**
 * @brief Update supply chain health for a player based on stockpiles and imports.
 *
 * Checks each critical good: if the player has domestic production or imports,
 * health stays high. If supply is cut off, health decays (stockpile buffer
 * provides temporary protection).
 *
 * @param world   ECS world.
 * @param player  Player to update.
 */
void updateSupplyChainHealth(aoc::game::GameState& gameState, PlayerId player);

/**
 * @brief Process supply chain effects for all players.
 *
 * Called once per turn in global systems.
 */
void processSupplyChains(aoc::game::GameState& gameState);

} // namespace aoc::sim

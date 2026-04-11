#pragma once

/**
 * @file BlackMarket.hpp
 * @brief Black market smuggling that bypasses embargoes and sanctions.
 *
 * Embargoes don't fully cut trade. 20-30% of goods still flow through
 * illegal channels at 2-3x market price. This makes embargoes impactful
 * but not total, and creates interesting dynamics:
 *
 *   - Embargoing player: goods still leak through (frustrating)
 *   - Embargoed player: can still get critical supplies (expensive)
 *   - Third parties: profit from smuggling (if they don't get caught)
 *   - Anti-smuggling: invest in border control to reduce leakage
 *
 * Black market size scales with:
 *   - Price differential (bigger gap = more profit = more smuggling)
 *   - Border length (more borders = harder to control)
 *   - Corruption level (less developed governments = more smuggling)
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::game { class GameState; }

namespace aoc::sim {

/// Per-player black market state.
struct PlayerBlackMarketComponent {
    PlayerId owner = INVALID_PLAYER;

    /// How effective this player's border control is (0.0-1.0).
    /// Higher = less smuggling gets through. Scales with tech and spending.
    float borderControlEfficiency = 0.20f;

    /// Gold earned from smuggling this turn (passive income from black market).
    CurrencyAmount smugglingIncome = 0;

    /// Gold lost to smuggling this turn (goods bought at premium).
    CurrencyAmount smugglingCost = 0;

    /// Fraction of embargoed goods that get through via black market.
    [[nodiscard]] float smugglingLeakRate() const {
        // Base 30% leaks, reduced by border control
        float baseLeakRate = 0.30f;
        return baseLeakRate * (1.0f - this->borderControlEfficiency);
    }

    /// Price multiplier for black market goods (always expensive).
    [[nodiscard]] float blackMarketPriceMultiplier() const {
        return 2.5f;  // 250% of market price
    }
};

/**
 * @brief Process black market trade for embargoed player pairs.
 *
 * For each active embargo/sanction, a fraction of goods still flows
 * through illegal channels. Smugglers earn profits, buyers pay premium.
 *
 * @param world  ECS world.
 */
void processBlackMarketTrade(aoc::game::GameState& gameState);

/**
 * @brief Update border control efficiency based on tech and spending.
 *
 * @param component  Player's black market component.
 * @param hasWalls   Whether player has Walls buildings (basic border control).
 * @param hasTelecom Whether player has Telecom Hub (surveillance).
 * @param govSpendingRatio Government spending as fraction of GDP.
 */
void updateBorderControl(PlayerBlackMarketComponent& component,
                          bool hasWalls, bool hasTelecom,
                          float govSpendingRatio);

} // namespace aoc::sim

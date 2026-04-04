#pragma once

/**
 * @file ResourceCurse.hpp
 * @brief Dutch disease / resource curse economic mechanics.
 *
 * The resource curse creates natural trade incentives between resource-rich
 * and manufacturing-focused players:
 *
 * 1. Currency appreciation: high resource exports make your currency strong,
 *    which makes your manufactured exports expensive for others.
 * 2. Manufacturing penalty: resource wealth crowds out manufacturing sector
 *    investment (workers prefer easy mining jobs over factory work).
 * 3. Political instability risk: high resource dependence increases corruption
 *    and unrest (simplified as a happiness penalty).
 *
 * Result: resource-rich players are incentivized to SELL raw materials and
 * BUY manufactured goods. Manufacturing players WANT cheap raw imports.
 * This creates the core trade dynamic where both sides benefit.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::ecs {
class World;
}

namespace aoc::sim {

/// Modifiers applied to a player suffering from Dutch disease.
struct ResourceCurseModifiers {
    float manufacturingPenalty;     ///< Multiplier on processed goods production (< 1.0 = slower)
    float currencyAppreciation;     ///< Multiplier on export prices (> 1.0 = more expensive)
    float happinessPenalty;         ///< Flat penalty to city amenities
    float resourceDependence;       ///< 0.0 = no resource income, 1.0 = all income from resources
};

/**
 * @brief Compute the resource curse severity for a player.
 *
 * Severity depends on what fraction of the player's total income comes
 * from raw resource exports vs manufactured goods / services.
 *
 * @param world ECS world with economy components.
 * @param player The player to evaluate.
 * @return Modifiers to apply to this player's economy.
 */
[[nodiscard]] ResourceCurseModifiers computeResourceCurse(
    const aoc::ecs::World& world,
    PlayerId player);

/**
 * @brief Apply resource curse modifiers to a player's production.
 *
 * Called during the ResourceProduction turn phase.
 * Reduces manufacturing output and adjusts export prices.
 */
void applyResourceCurseEffects(aoc::ecs::World& world,
                                PlayerId player,
                                const ResourceCurseModifiers& modifiers);

} // namespace aoc::sim

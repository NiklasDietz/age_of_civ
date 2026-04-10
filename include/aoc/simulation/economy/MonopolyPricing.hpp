#pragma once

/**
 * @file MonopolyPricing.hpp
 * @brief Resource monopoly detection and cartel pricing mechanics.
 *
 * When one player (or an alliance) controls >60% of a strategic resource's
 * global supply, they can set monopoly prices. This gives enormous economic
 * leverage: other players must pay premium prices or find alternatives.
 *
 * Historical parallels: OPEC oil pricing, De Beers diamonds, Dutch VOC spices.
 *
 * Monopoly effects:
 *   - Monopolist earns 2-3x market price on exports
 *   - Dependent buyers pay premium (hurts their production costs)
 *   - Creates diplomatic leverage ("buy from me or suffer")
 *   - Incentivizes prospecting/alternatives by affected players
 *   - Anti-monopoly coalitions form (embargo the monopolist)
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::ecs { class World; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

/// Monopoly status for a specific good.
struct MonopolyInfo {
    uint16_t goodId = 0;
    PlayerId monopolist = INVALID_PLAYER;  ///< INVALID if no monopoly
    float controlShare = 0.0f;             ///< Fraction of global supply (0.0-1.0)
    float priceMultiplier = 1.0f;          ///< How much above market price (1.0-3.0)
    bool isActive = false;
};

/// Global monopoly tracking (one per game).
struct GlobalMonopolyComponent {
    static constexpr int32_t MAX_TRACKED_GOODS = 12;

    /// Tracked strategic goods: iron, copper, coal, oil, horses, niter, uranium, aluminum, rubber, tin, silver, gold
    MonopolyInfo monopolies[MAX_TRACKED_GOODS] = {};
    int32_t trackedCount = 0;

    /// Gold bonus earned by monopolist per turn from price gouging.
    [[nodiscard]] CurrencyAmount monopolyIncome(PlayerId player) const {
        CurrencyAmount total = 0;
        for (int32_t i = 0; i < this->trackedCount; ++i) {
            if (this->monopolies[i].monopolist == player && this->monopolies[i].isActive) {
                // Income scales with price multiplier and control share
                total += static_cast<CurrencyAmount>(
                    (this->monopolies[i].priceMultiplier - 1.0f) * 50.0f
                    * this->monopolies[i].controlShare);
            }
        }
        return total;
    }

    /// Price penalty for a buyer of a monopolized good.
    [[nodiscard]] float buyerPriceMultiplier(uint16_t goodId, PlayerId buyer) const {
        for (int32_t i = 0; i < this->trackedCount; ++i) {
            if (this->monopolies[i].goodId == goodId
                && this->monopolies[i].isActive
                && this->monopolies[i].monopolist != buyer) {
                return this->monopolies[i].priceMultiplier;
            }
        }
        return 1.0f;
    }
};

/**
 * @brief Scan all resource tiles and stockpiles to detect monopolies.
 *
 * For each strategic good, counts total global supply per player.
 * If one player controls >60%, they gain monopoly pricing power.
 * At >80%, price multiplier reaches 3x (dominant monopoly).
 *
 * @param world  ECS world.
 * @param grid   Hex grid (for tile resource scanning).
 */
void detectMonopolies(aoc::ecs::World& world, const aoc::map::HexGrid& grid);

/**
 * @brief Apply monopoly income to monopolists' treasuries.
 */
void applyMonopolyIncome(aoc::ecs::World& world);

} // namespace aoc::sim

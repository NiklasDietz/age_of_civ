#pragma once

/**
 * @file Speculation.hpp
 * @brief Market speculation, commodity hoarding, and currency attacks.
 *
 * Speculation mechanics:
 *
 * 1. Commodity Hoarding:
 *    A player buys up a strategic resource to corner the market.
 *    When they hold >50% of global supply of a good, they can set
 *    an artificially high price. Dependent civs must pay or go without.
 *    Risk: the hoarder ties up capital in inventory that may lose value.
 *
 * 2. Currency Attack:
 *    If espionage reveals a civ is about to debase/default, a player
 *    can "short" their currency: borrow coins from the target, sell
 *    immediately at current price, wait for the crash, buy back cheap.
 *    Requires a spy with sufficient intel.
 *
 * 3. Gold Rush Event:
 *    Discovering a new gold tile while on gold standard creates a
 *    one-time inflation event for all gold-standard civs (increased
 *    gold supply worldwide). Connects map exploration to economics.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"

#include <cstdint>
#include <vector>

namespace aoc::ecs { class World; }

namespace aoc::sim {

class Market;

// ============================================================================
// Commodity hoarding
// ============================================================================

struct CommodityHoardComponent {
    PlayerId owner = INVALID_PLAYER;

    struct HoardPosition {
        uint16_t goodId;
        int32_t  amount;          ///< Units held in hoard (separate from city stockpile)
        int32_t  purchasePrice;   ///< Average price paid (for P/L tracking)
    };

    std::vector<HoardPosition> positions;

    /// Total hoarded amount of a specific good.
    [[nodiscard]] int32_t hoarded(uint16_t goodId) const {
        for (const HoardPosition& pos : this->positions) {
            if (pos.goodId == goodId) {
                return pos.amount;
            }
        }
        return 0;
    }
};

/**
 * @brief Buy goods from the market into a hoard position.
 *
 * Removes goods from the player's city stockpiles and puts them
 * into the hoard (held off-market to restrict supply).
 *
 * @param world    ECS world.
 * @param market   Market (for price info).
 * @param player   Player doing the hoarding.
 * @param goodId   Good to hoard.
 * @param amount   Amount to buy into hoard.
 * @return Ok if successful.
 */
[[nodiscard]] ErrorCode hoardCommodity(aoc::ecs::World& world,
                                       const Market& market,
                                       PlayerId player,
                                       uint16_t goodId, int32_t amount);

/**
 * @brief Release hoarded goods back to the market (sell at current price).
 *
 * Goods re-enter the player's stockpiles and show up as supply next turn.
 *
 * @param world    ECS world.
 * @param market   Market (for price info).
 * @param player   Player releasing the hoard.
 * @param goodId   Good to release.
 * @param amount   Amount to release (0 = all).
 * @return Ok if successful.
 */
[[nodiscard]] ErrorCode releaseCommodity(aoc::ecs::World& world,
                                         const Market& market,
                                         PlayerId player,
                                         uint16_t goodId, int32_t amount);

/**
 * @brief Check if a player has cornered a market (holds >50% of global supply).
 *
 * @param world    ECS world.
 * @param market   Market.
 * @param player   Player to check.
 * @param goodId   Good to check.
 * @return Share of global supply (0.0 to 1.0). Above 0.5 = cornered.
 */
[[nodiscard]] float marketShareOfGood(const aoc::ecs::World& world,
                                      const Market& market,
                                      PlayerId player, uint16_t goodId);

// ============================================================================
// Gold rush event
// ============================================================================

/**
 * @brief Trigger a gold rush inflation event.
 *
 * Called when a new gold resource tile is discovered/worked for the first time.
 * All players on gold standard experience a small inflation bump (increased
 * gold supply means each gold coin is worth slightly less).
 *
 * @param world  ECS world.
 * @param goldAmount  Amount of new gold entering the system.
 */
void triggerGoldRushInflation(aoc::ecs::World& world, int32_t goldAmount);

/**
 * @brief Process speculation effects per turn.
 *
 * Adjusts market prices based on hoarded supply (reduced supply -> higher prices).
 * Reports hoarded goods as reduced supply to the market.
 *
 * @param world   ECS world.
 * @param market  Market to adjust.
 */
void processSpeculation(aoc::ecs::World& world, Market& market);

} // namespace aoc::sim

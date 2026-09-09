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

#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

class DiplomacyManager;

/// Monopoly status for a specific good.
struct MonopolyInfo {
    uint16_t goodId = 0;
    PlayerId monopolist = INVALID_PLAYER;  ///< INVALID if no monopoly
    float controlShare = 0.0f;             ///< Fraction of global supply (0.0-1.0)

    /// The markup the monopolist has CHOSEN to charge. 1.0 = not exploiting.
    ///
    /// Detection is the game's job; charging for it is the player's. This used
    /// to be set automatically from controlShare, so a monopoly gouged on its
    /// holder's behalf whether or not they had noticed they had one -- and
    /// buyerPriceMultiplier, the field that would have made it bite, had no
    /// readers at all, so it did not even gouge. Now the game notices and says
    /// so; the player decides whether to squeeze.
    float priceMultiplier = 1.0f;

    /// The ceiling `controlShare` entitles them to: 1.5x at 60%, 2x at 70%,
    /// 3x at 80%. A request to charge more than this is clamped.
    float maxPriceMultiplier = 1.0f;

    // NOBODY CAN CURRENTLY CHOOSE. `priceMultiplier` starts at 1.0, is reset to
    // 1.0 when a monopoly forms and when it lapses, is clamped DOWN to a fallen
    // ceiling, and is raised in exactly one place: requestSetMonopolyPrice,
    // which has no caller in the tree -- no AI decision, no UI, no REST route,
    // no MCP tool. So it is permanently 1.0, buyerPriceMultiplier always
    // returns 1.0, the markup at the trader delivery is a no-op, and
    // monopolyIncome -- (priceMultiplier - 1) * 50 * controlShare -- is always
    // exactly zero. Detection and notification work; the mechanic pays nothing.
    //
    // Adding an AI caller is NOT sufficient, and would be worse than leaving it
    // alone. Gouging carries no cost anywhere: no grievance type, no reputation
    // modifier, no buyer response. An AI weighing a free benefit always takes
    // the maximum, which reproduces the automatic share-derived markup that was
    // deliberately removed from this struct -- the thing the comment above
    // objects to. A choice needs something on the other side of it.
    //
    // So this needs a design decision before code: what does squeezing cost?
    // Candidates the systems already support -- a grievance against the
    // monopolist from every civ that buys the good, a trade-reputation hit like
    // the one debasement carries, or buyers substituting away and eroding
    // controlShare.

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

    /// Who holds the active monopoly on `goodId`, or INVALID_PLAYER.
    [[nodiscard]] PlayerId monopolistOf(uint16_t goodId) const {
        for (int32_t i = 0; i < this->trackedCount; ++i) {
            if (this->monopolies[i].goodId == goodId && this->monopolies[i].isActive) {
                return this->monopolies[i].monopolist;
            }
        }
        return INVALID_PLAYER;
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

/// Set the markup a monopolist charges for `goodId`, clamped to
/// [1.0, maxPriceMultiplier]. Fails unless `player` actually holds an active
/// monopoly on that good.
///
/// A validated request rather than a UI-only control, so the REST and MCP
/// surfaces and the AI reach the same decision the human does.
[[nodiscard]] ErrorCode requestSetMonopolyPrice(GlobalMonopolyComponent& monopolies,
                                                PlayerId player, uint16_t goodId,
                                                float multiplier);

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
void detectMonopolies(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid);

/**
 * @brief Apply monopoly income to monopolists' treasuries.
 */
void applyMonopolyIncome(aoc::game::GameState& gameState);

/**
 * @brief Let a monopolist choose this turn's markup on every good it corners.
 *
 * The decision the mechanic was missing. Squeezing is no longer free -- each
 * delivery of a marked-up good earns the monopolist a PriceGouged grievance
 * from the buyer -- so the choice is between gold and standing.
 *
 * The rule: squeeze the civs that already resent you and spare the ones you
 * might still win over, so greed is the share of met civs that are already
 * Hostile, Unfriendly or at war. Writes through requestSetMonopolyPrice so the
 * validated path stays the only writer.
 *
 * NOT CALLED PER TURN YET, and the reason is a measurement rather than
 * caution. Wired into aiEconomicStrategy, both seeds keep 0 eliminations and
 * both end on a genuine Culture victory instead of one timing out on Score --
 * seed 42 holds its exact winner and type with revolts 5 -> 7 and wars 15 -> 13,
 * seed 43 halves its revolts, 611 -> 318. But at a common turn 340 the world is
 * far poorer: GDP -47% on seed 42 and -40% on seed 43, with cities -8% and -17%.
 *
 * The grievance is NOT what costs that. Running the markup with the grievance
 * disabled gives GDP -47.3% and -39.7%, against -46.9% and -39.6% with it, so
 * the cost model is nearly free and the MARKUP ITSELF is the expensive part:
 * pricing strategic inputs above cost is a deadweight loss, and the monopolist's
 * individually rational squeeze shrinks the whole economy. That is a defensible
 * and rather good dynamic, but a 40% swing in world GDP is a decision about
 * what kind of game this is, not a tuning detail, so it waits.
 *
 * To enable, restore this line in aiEconomicStrategy after aiCrisisResponse:
 *
 *     aiChooseMonopolyPrices(gameState, player, diplomacy);
 */
void aiChooseMonopolyPrices(aoc::game::GameState& gameState, PlayerId player,
                            const DiplomacyManager& diplomacy);

} // namespace aoc::sim

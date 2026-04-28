#pragma once

/**
 * @file TradeAgreement.hpp
 * @brief Trade agreements, free trade zones, and customs unions.
 *
 * Players can form trade agreements that reduce friction between members:
 *
 *   - Bilateral Trade Deal: -20% tariff between two players
 *   - Free Trade Zone: -50% tariff among 3+ players, +1 trade route each
 *   - Customs Union: zero tariff among members, common external tariff,
 *     shared market prices. Most integrated but requires unanimous consent.
 *
 * Non-members face the full tariff (or higher if the union sets a common
 * external tariff). This creates trade blocs and economic spheres of influence.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

class Market;
class DiplomacyManager;

enum class TradeAgreementType : uint8_t {
    BilateralDeal,   ///< Two-player: -20% tariff
    FreeTradeZone,   ///< Multi-player: -50% tariff, +1 trade route
    CustomsUnion,    ///< Multi-player: 0% tariff, common external tariff
    TransitTreaty,   ///< WP-C3: two-player zero-toll right-of-passage. No
                     ///         tariff relief — purely transit across
                     ///         members' territories.
    Count
};

struct TradeAgreementDef {
    TradeAgreementType type;
    std::vector<PlayerId> members;
    int32_t turnsActive = 0;
    float externalTariff = 0.0f;   ///< Customs Union: tariff applied to non-members
    bool isActive = true;

    /// Turns between auto-spawned Trader units along this agreement's
    /// bilateral standing route. 0 disables the auto-spawn.
    int32_t standingRouteInterval = 0;
    /// Internal counter ticked by `processStandingRoutes`. When it reaches
    /// `standingRouteInterval`, a Trader is spawned and the counter resets.
    int32_t standingRouteCountdown = 0;
};

/// Per-player trade agreement tracking.
struct PlayerTradeAgreementsComponent {
    PlayerId owner = INVALID_PLAYER;
    std::vector<TradeAgreementDef> agreements;

    /// Get the best tariff reduction for trade between this player and another.
    /// Returns 1.0 for no agreement, lower for better agreements.
    [[nodiscard]] float tariffModifier(PlayerId partner) const {
        float bestMod = 1.0f;
        for (const TradeAgreementDef& agreement : this->agreements) {
            if (!agreement.isActive) { continue; }
            bool partnerIsMember = false;
            for (PlayerId member : agreement.members) {
                if (member == partner) { partnerIsMember = true; break; }
            }
            if (!partnerIsMember) { continue; }

            float mod = 1.0f;
            switch (agreement.type) {
                case TradeAgreementType::BilateralDeal: mod = 0.80f; break;
                case TradeAgreementType::FreeTradeZone: mod = 0.50f; break;
                case TradeAgreementType::CustomsUnion:  mod = 0.0f;  break;
                default: break;
            }
            if (mod < bestMod) { bestMod = mod; }
        }
        return bestMod;
    }

    /// Bonus trade routes from agreements.
    [[nodiscard]] int32_t bonusTradeRoutes() const {
        int32_t bonus = 0;
        for (const TradeAgreementDef& agreement : this->agreements) {
            if (!agreement.isActive) { continue; }
            if (agreement.type == TradeAgreementType::FreeTradeZone) { bonus += 1; }
            if (agreement.type == TradeAgreementType::CustomsUnion)  { bonus += 2; }
        }
        return bonus;
    }
};

/**
 * @brief Propose a bilateral trade deal between two players.
 */
/// Optional `diplomacy` parameter: when supplied, the deal is REFUSED if
/// the proposer-partner pair is at war or has Hostile/Unfriendly stance —
/// the partner's leader won't sign with someone they actively dislike.
[[nodiscard]] ErrorCode proposeBilateralDeal(aoc::game::GameState& gameState,
                                               PlayerId proposer, PlayerId partner,
                                               DiplomacyManager* diplomacy = nullptr);

/**
 * @brief Create a free trade zone among multiple players.
 */
[[nodiscard]] ErrorCode createFreeTradeZone(aoc::game::GameState& gameState,
                                              const std::vector<PlayerId>& members);

/**
 * @brief Form a customs union with common external tariff.
 */
[[nodiscard]] ErrorCode formCustomsUnion(aoc::game::GameState& gameState,
                                           const std::vector<PlayerId>& members,
                                           float externalTariff);

/**
 * @brief WP-C3: propose a zero-toll Transit Treaty between two players.
 *
 * Grants traders owned by either member free passage across the other's
 * territory (skips tariff + toll). No tariff relief on delivered goods —
 * purely right-of-passage. Duplicate active treaties between the same pair
 * are rejected with InvalidArgument.
 */
[[nodiscard]] ErrorCode proposeTransitTreaty(aoc::game::GameState& gameState,
                                              PlayerId proposer, PlayerId partner);

/// Exit a trade agreement.  Removes `leaver` from every active agreement of
/// `type` they belong to.  If the agreement drops below the minimum member
/// count (2 for Bilateral, 3 for FTZ, 2 for Customs), it is dissolved.
/// War-driven dissolution uses this same path.
void exitTradeAgreement(aoc::game::GameState& gameState,
                         PlayerId leaver,
                         TradeAgreementType type);

/// Dissolve all trade agreements between two players (called from
/// declareWar so wartime cuts off member-tariff perks immediately).
void breakTradeAgreementsBetween(aoc::game::GameState& gameState,
                                  PlayerId a, PlayerId b);

/**
 * @brief Process trade agreement effects each turn (tick duration, check validity).
 */
void processTradeAgreements(aoc::game::GameState& gameState);

/**
 * @brief Spawn standing-route Trader units for agreements with an active
 *        interval. Ticks each agreement's countdown and, on expiry, spawns a
 *        Trader at the initiating member's capital bound for the partner's
 *        capital. Spawned traders are normal units -- still pillage-able, but
 *        the player did not click to create them.
 *
 * Bilateral deals spawn one trader per turn cycle (one direction). FreeTradeZone
 * and CustomsUnion spawn round-robin across member pairs.
 */
void processStandingRoutes(aoc::game::GameState& gameState,
                           aoc::map::HexGrid& grid,
                           const Market& market,
                           DiplomacyManager* diplomacy);

} // namespace aoc::sim

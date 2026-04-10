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

namespace aoc::ecs { class World; }

namespace aoc::sim {

enum class TradeAgreementType : uint8_t {
    BilateralDeal,   ///< Two-player: -20% tariff
    FreeTradeZone,   ///< Multi-player: -50% tariff, +1 trade route
    CustomsUnion,    ///< Multi-player: 0% tariff, common external tariff

    Count
};

struct TradeAgreementDef {
    TradeAgreementType type;
    std::vector<PlayerId> members;
    int32_t turnsActive = 0;
    float externalTariff = 0.0f;   ///< Customs Union: tariff applied to non-members
    bool isActive = true;
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
[[nodiscard]] ErrorCode proposeBilateralDeal(aoc::ecs::World& world,
                                               PlayerId proposer, PlayerId partner);

/**
 * @brief Create a free trade zone among multiple players.
 */
[[nodiscard]] ErrorCode createFreeTradeZone(aoc::ecs::World& world,
                                              const std::vector<PlayerId>& members);

/**
 * @brief Form a customs union with common external tariff.
 */
[[nodiscard]] ErrorCode formCustomsUnion(aoc::ecs::World& world,
                                           const std::vector<PlayerId>& members,
                                           float externalTariff);

/**
 * @brief Process trade agreement effects each turn (tick duration, check validity).
 */
void processTradeAgreements(aoc::ecs::World& world);

} // namespace aoc::sim

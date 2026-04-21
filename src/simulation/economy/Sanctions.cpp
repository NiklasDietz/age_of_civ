/**
 * @file Sanctions.cpp
 * @brief Economic sanctions with monetary system integration.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/economy/Sanctions.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/Bonds.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

ErrorCode imposeSanction(aoc::game::GameState& gameState,
                         GlobalSanctionTracker& tracker,
                         PlayerId sanctioner, PlayerId target,
                         SanctionType type, bool secondary) {
    if (tracker.hasSanction(sanctioner, target, type)) {
        return ErrorCode::InvalidArgument;
    }

    SanctionEntry entry;
    entry.sanctioner   = sanctioner;
    entry.target       = target;
    entry.type         = type;
    entry.turnsActive  = 0;
    entry.hasSecondary = secondary;
    tracker.activeSanctions.push_back(entry);

    if (type == SanctionType::AssetFreeze) {
        executeAssetFreeze(gameState, sanctioner, target);
    }

    if (type == SanctionType::FinancialSanction) {
        aoc::game::Player* targetPlayer = gameState.player(target);
        if (targetPlayer != nullptr) {
            CurrencyTrustComponent& trust = targetPlayer->currencyTrust();
            trust.trustScore = std::max(0.0f, trust.trustScore - 0.10f);
        }
    }

    LOG_INFO("Player %u imposed %s on player %u%s",
             static_cast<unsigned>(sanctioner),
             type == SanctionType::TradeEmbargo      ? "trade embargo" :
             type == SanctionType::FinancialSanction ? "financial sanctions" :
             "asset freeze",
             static_cast<unsigned>(target),
             secondary ? " (with secondary sanctions)" : "");

    return ErrorCode::Ok;
}

void liftSanction(GlobalSanctionTracker& tracker,
                  PlayerId sanctioner, PlayerId target,
                  SanctionType type) {
    std::vector<SanctionEntry>::iterator it = tracker.activeSanctions.begin();
    while (it != tracker.activeSanctions.end()) {
        if (it->sanctioner == sanctioner && it->target == target && it->type == type) {
            it = tracker.activeSanctions.erase(it);
            LOG_INFO("Player %u lifted sanction on player %u",
                     static_cast<unsigned>(sanctioner), static_cast<unsigned>(target));
            return;
        }
        ++it;
    }
}

void executeAssetFreeze(aoc::game::GameState& gameState,
                        PlayerId sanctioner, PlayerId target) {
    aoc::game::Player* targetPlayer    = gameState.player(target);
    aoc::game::Player* sanctionerPlayer = gameState.player(sanctioner);
    if (targetPlayer == nullptr || sanctionerPlayer == nullptr) {
        return;
    }

    MonetaryStateComponent& targetState    = targetPlayer->monetary();
    MonetaryStateComponent& sanctionerState = sanctionerPlayer->monetary();

    constexpr float SEIZURE_FRACTION = 0.25f;

    int32_t copperSeized = static_cast<int32_t>(
        static_cast<float>(targetState.copperCoinReserves) * SEIZURE_FRACTION);
    int32_t silverSeized = static_cast<int32_t>(
        static_cast<float>(targetState.silverCoinReserves) * SEIZURE_FRACTION);
    int32_t goldSeized   = static_cast<int32_t>(
        static_cast<float>(targetState.goldBarReserves) * SEIZURE_FRACTION);

    targetState.copperCoinReserves -= copperSeized;
    targetState.silverCoinReserves -= silverSeized;
    targetState.goldBarReserves   -= goldSeized;
    targetState.updateCoinTier();

    sanctionerState.copperCoinReserves += copperSeized;
    sanctionerState.silverCoinReserves += silverSeized;
    sanctionerState.goldBarReserves   += goldSeized;
    sanctionerState.updateCoinTier();

    // C28: fiat/digital civs hold wealth in treasury, not physical coin
    // reserves, so coin-only seizure leaves them untouched. Seize the same
    // fraction of treasury across all tiers so upgrading to Fiat doesn't
    // grant sanctions immunity.
    const CurrencyAmount treasurySeized = static_cast<CurrencyAmount>(
        static_cast<float>(targetPlayer->treasury()) * SEIZURE_FRACTION);
    if (treasurySeized > 0) {
        targetPlayer->setTreasury(targetPlayer->treasury() - treasurySeized);
        sanctionerPlayer->addGold(treasurySeized);
    }

    // Cancel bonds held by target that were issued by sanctioner
    PlayerBondComponent& bonds = targetPlayer->bonds();
    bonds.heldBonds.erase(
        std::remove_if(bonds.heldBonds.begin(), bonds.heldBonds.end(),
            [sanctioner](const BondIssue& b) { return b.issuer == sanctioner; }),
        bonds.heldBonds.end());

    LOG_INFO("Asset freeze: player %u seized Cu:%d Ag:%d Au:%d coins from player %u",
             static_cast<unsigned>(sanctioner),
             copperSeized, silverSeized, goldSeized,
             static_cast<unsigned>(target));
}

void processSanctions(aoc::game::GameState& /*gameState*/, GlobalSanctionTracker& tracker) {
    for (SanctionEntry& s : tracker.activeSanctions) {
        ++s.turnsActive;
    }
}

} // namespace aoc::sim

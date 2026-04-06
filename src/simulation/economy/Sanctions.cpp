/**
 * @file Sanctions.cpp
 * @brief Economic sanctions with monetary system integration.
 */

#include "aoc/simulation/economy/Sanctions.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/Bonds.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

ErrorCode imposeSanction(aoc::ecs::World& world,
                         GlobalSanctionTracker& tracker,
                         PlayerId sanctioner, PlayerId target,
                         SanctionType type, bool secondary) {
    // Don't duplicate
    if (tracker.hasSanction(sanctioner, target, type)) {
        return ErrorCode::InvalidArgument;
    }

    SanctionEntry entry;
    entry.sanctioner = sanctioner;
    entry.target = target;
    entry.type = type;
    entry.turnsActive = 0;
    entry.hasSecondary = secondary;
    tracker.activeSanctions.push_back(entry);

    // Asset freeze has an immediate one-time effect
    if (type == SanctionType::AssetFreeze) {
        executeAssetFreeze(world, sanctioner, target);
    }

    // Financial sanctions damage trust
    if (type == SanctionType::FinancialSanction) {
        aoc::ecs::ComponentPool<CurrencyTrustComponent>* trustPool =
            world.getPool<CurrencyTrustComponent>();
        if (trustPool != nullptr) {
            for (uint32_t i = 0; i < trustPool->size(); ++i) {
                if (trustPool->data()[i].owner == target) {
                    trustPool->data()[i].trustScore =
                        std::max(0.0f, trustPool->data()[i].trustScore - 0.10f);
                    break;
                }
            }
        }
    }

    LOG_INFO("Player %u imposed %s on player %u%s",
             static_cast<unsigned>(sanctioner),
             type == SanctionType::TradeEmbargo ? "trade embargo" :
             type == SanctionType::FinancialSanction ? "financial sanctions" :
             "asset freeze",
             static_cast<unsigned>(target),
             secondary ? " (with secondary sanctions)" : "");

    return ErrorCode::Ok;
}

void liftSanction(GlobalSanctionTracker& tracker,
                  PlayerId sanctioner, PlayerId target,
                  SanctionType type) {
    auto it = tracker.activeSanctions.begin();
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

void executeAssetFreeze(aoc::ecs::World& world,
                        PlayerId sanctioner, PlayerId target) {
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool == nullptr) {
        return;
    }

    MonetaryStateComponent* targetState = nullptr;
    MonetaryStateComponent* sanctionerState = nullptr;
    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        if (monetaryPool->data()[i].owner == target) { targetState = &monetaryPool->data()[i]; }
        if (monetaryPool->data()[i].owner == sanctioner) { sanctionerState = &monetaryPool->data()[i]; }
    }
    if (targetState == nullptr || sanctionerState == nullptr) {
        return;
    }

    // Seize a portion of the target's coin reserves (simulating coins held
    // in the sanctioner's territory from trade settlement).
    // Transfer 25% of each coin type from target to sanctioner.
    constexpr float SEIZURE_FRACTION = 0.25f;

    int32_t copperSeized = static_cast<int32_t>(
        static_cast<float>(targetState->copperCoinReserves) * SEIZURE_FRACTION);
    int32_t silverSeized = static_cast<int32_t>(
        static_cast<float>(targetState->silverCoinReserves) * SEIZURE_FRACTION);
    int32_t goldSeized = static_cast<int32_t>(
        static_cast<float>(targetState->goldCoinReserves) * SEIZURE_FRACTION);

    targetState->copperCoinReserves -= copperSeized;
    targetState->silverCoinReserves -= silverSeized;
    targetState->goldCoinReserves   -= goldSeized;
    targetState->updateCoinTier();

    sanctionerState->copperCoinReserves += copperSeized;
    sanctionerState->silverCoinReserves += silverSeized;
    sanctionerState->goldCoinReserves   += goldSeized;
    sanctionerState->updateCoinTier();

    // Also seize bonds held by target issued by sanctioner (cancel them)
    aoc::ecs::ComponentPool<PlayerBondComponent>* bondPool =
        world.getPool<PlayerBondComponent>();
    if (bondPool != nullptr) {
        for (uint32_t i = 0; i < bondPool->size(); ++i) {
            if (bondPool->data()[i].owner == target) {
                auto& held = bondPool->data()[i].heldBonds;
                held.erase(
                    std::remove_if(held.begin(), held.end(),
                        [sanctioner](const BondIssue& b) {
                            return b.issuer == sanctioner;
                        }),
                    held.end());
                break;
            }
        }
    }

    LOG_INFO("Asset freeze: player %u seized Cu:%d Ag:%d Au:%d coins from player %u",
             static_cast<unsigned>(sanctioner),
             copperSeized, silverSeized, goldSeized,
             static_cast<unsigned>(target));
}

void processSanctions(aoc::ecs::World& /*world*/, GlobalSanctionTracker& tracker) {
    for (SanctionEntry& s : tracker.activeSanctions) {
        ++s.turnsActive;
    }
}

} // namespace aoc::sim

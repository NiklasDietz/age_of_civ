/**
 * @file CentralBank.cpp
 * @brief Central bank monetary policy tool implementations.
 */

#include "aoc/simulation/monetary/CentralBank.hpp"

#include <algorithm>

namespace aoc::sim {

void setInterestRate(MonetaryStateComponent& state, Percentage rate) {
    state.interestRate = std::clamp(rate, 0.0f, 0.25f);
}

void setReserveRequirement(MonetaryStateComponent& state, Percentage ratio) {
    state.reserveRequirement = std::clamp(ratio, 0.01f, 0.50f);
}

ErrorCode printMoney(MonetaryStateComponent& state, CurrencyAmount amount) {
    if (state.system != MonetarySystemType::FiatMoney) {
        return ErrorCode::InvalidMonetaryTransition;
    }
    if (amount <= 0) {
        return ErrorCode::InvalidArgument;
    }

    state.moneySupply += amount;
    // Printed money goes to the government treasury
    state.treasury += amount;
    return ErrorCode::Ok;
}

ErrorCode buyGold(MonetaryStateComponent& state,
                   CurrencyAmount goldAmount,
                   CurrencyAmount goldPrice) {
    CurrencyAmount totalCost = goldAmount * goldPrice;
    if (state.treasury < totalCost) {
        return ErrorCode::InsufficientResources;
    }

    state.goldCoinReserves += static_cast<int32_t>(goldAmount);
    state.treasury     -= totalCost;
    state.moneySupply  -= totalCost;  // Currency removed from circulation

    // Recalculate backing ratio if on gold standard
    if (state.system == MonetarySystemType::GoldStandard && state.moneySupply > 0) {
        state.goldBackingRatio = static_cast<float>(state.goldCoinReserves)
                               / static_cast<float>(state.moneySupply);
    }

    state.updateCoinTier();
    return ErrorCode::Ok;
}

ErrorCode sellGold(MonetaryStateComponent& state,
                    CurrencyAmount goldAmount,
                    CurrencyAmount goldPrice) {
    if (state.goldCoinReserves < static_cast<int32_t>(goldAmount)) {
        return ErrorCode::InsufficientResources;
    }

    CurrencyAmount currencyGained = goldAmount * goldPrice;
    state.goldCoinReserves -= static_cast<int32_t>(goldAmount);
    state.treasury     += currencyGained;
    state.moneySupply  += currencyGained;  // Currency enters circulation

    // Recalculate backing ratio
    if (state.system == MonetarySystemType::GoldStandard && state.moneySupply > 0) {
        state.goldBackingRatio = static_cast<float>(state.goldCoinReserves)
                               / static_cast<float>(state.moneySupply);
    }

    state.updateCoinTier();
    return ErrorCode::Ok;
}

float moneyMultiplier(const MonetaryStateComponent& state) {
    if (state.reserveRequirement <= 0.001f) {
        return 100.0f;  // Cap at 100x to prevent infinity
    }
    return 1.0f / state.reserveRequirement;
}

ErrorCode debaseCurrency(MonetaryStateComponent& state, float ratio) {
    if (state.system != MonetarySystemType::CommodityMoney) {
        return ErrorCode::InvalidMonetaryTransition;
    }
    if (ratio <= 0.0f) {
        return ErrorCode::InvalidArgument;
    }

    // Cannot debase beyond 50% total
    constexpr float MAX_DEBASEMENT = 0.50f;
    float newRatio = state.debasement.debasementRatio + ratio;
    if (newRatio > MAX_DEBASEMENT) {
        return ErrorCode::InvalidArgument;
    }

    state.debasement.debasementRatio = newRatio;
    state.debasement.turnsDebased = 0;  // Reset discovery timer

    // Produce extra coins from the debasement (stretch the metal).
    // Bonus coins = ratio * current reserves for the highest tier held.
    if (state.goldCoinReserves > 0) {
        int32_t bonus = std::max(1, static_cast<int32_t>(
            static_cast<float>(state.goldCoinReserves) * ratio));
        state.goldCoinReserves += bonus;
    } else if (state.silverCoinReserves > 0) {
        int32_t bonus = std::max(1, static_cast<int32_t>(
            static_cast<float>(state.silverCoinReserves) * ratio));
        state.silverCoinReserves += bonus;
    } else if (state.copperCoinReserves > 0) {
        int32_t bonus = std::max(1, static_cast<int32_t>(
            static_cast<float>(state.copperCoinReserves) * ratio));
        state.copperCoinReserves += bonus;
    }

    // Update money supply to reflect the new coins
    state.moneySupply = static_cast<CurrencyAmount>(state.totalCoinValue());
    state.updateCoinTier();

    return ErrorCode::Ok;
}

bool tickDebasementDiscovery(MonetaryStateComponent& state) {
    if (state.system != MonetarySystemType::CommodityMoney) {
        return false;
    }
    if (state.debasement.debasementRatio <= 0.001f) {
        return false;
    }
    if (state.debasement.discoveredByPartners) {
        return false;  // Already discovered
    }

    ++state.debasement.turnsDebased;

    // Discovery probability increases each turn and with higher debasement ratio.
    // Base: 10% per turn * debasementRatio * turnsSinceDebasement
    // At 20% debasement, ~40% chance per turn after 2 turns -> discovered by turn 3-5.
    // At 10% debasement, ~20% chance per turn -> discovered by turn 5-8.
    float discoveryChance = 0.10f * state.debasement.debasementRatio
                          * static_cast<float>(state.debasement.turnsDebased);
    discoveryChance = std::clamp(discoveryChance, 0.0f, 0.95f);

    // Deterministic check using a hash of turn count (no RNG dependency)
    // Simple: if turns * ratio exceeds threshold, it's discovered.
    // After 5 turns at any debasement level, guaranteed discovery.
    if (state.debasement.turnsDebased >= 5 || discoveryChance > 0.50f) {
        state.debasement.discoveredByPartners = true;
        return true;
    }

    return false;
}

} // namespace aoc::sim

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
    // The printed money goes to the government treasury
    state.goldReserves += amount;  // In fiat, "goldReserves" acts as government cash
    return ErrorCode::Ok;
}

ErrorCode buyGold(MonetaryStateComponent& state,
                   CurrencyAmount goldAmount,
                   CurrencyAmount goldPrice) {
    CurrencyAmount totalCost = goldAmount * goldPrice;
    if (state.moneySupply < totalCost) {
        return ErrorCode::InsufficientResources;
    }

    state.goldReserves += goldAmount;
    state.moneySupply  -= totalCost;  // Currency removed from circulation

    // Recalculate backing ratio if on gold standard
    if (state.system == MonetarySystemType::GoldStandard && state.moneySupply > 0) {
        state.goldBackingRatio = static_cast<float>(state.goldReserves)
                               / static_cast<float>(state.moneySupply);
    }

    return ErrorCode::Ok;
}

ErrorCode sellGold(MonetaryStateComponent& state,
                    CurrencyAmount goldAmount,
                    CurrencyAmount goldPrice) {
    if (state.goldReserves < goldAmount) {
        return ErrorCode::InsufficientResources;
    }

    CurrencyAmount currencyGained = goldAmount * goldPrice;
    state.goldReserves -= goldAmount;
    state.moneySupply  += currencyGained;  // Currency enters circulation

    // Recalculate backing ratio
    if (state.system == MonetarySystemType::GoldStandard && state.moneySupply > 0) {
        state.goldBackingRatio = static_cast<float>(state.goldReserves)
                               / static_cast<float>(state.moneySupply);
    }

    return ErrorCode::Ok;
}

float moneyMultiplier(const MonetaryStateComponent& state) {
    if (state.reserveRequirement <= 0.001f) {
        return 100.0f;  // Cap at 100x to prevent infinity
    }
    return 1.0f / state.reserveRequirement;
}

} // namespace aoc::sim

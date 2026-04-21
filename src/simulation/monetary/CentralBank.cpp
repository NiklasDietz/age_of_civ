/**
 * @file CentralBank.cpp
 * @brief Central bank monetary policy tool implementations.
 */

#include "aoc/simulation/monetary/CentralBank.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void setInterestRate(MonetaryStateComponent& state, Percentage rate) {
    state.interestRate = std::clamp(rate, 0.0f, 0.25f);
}

void setReserveRequirement(MonetaryStateComponent& state, Percentage ratio) {
    state.reserveRequirement = std::clamp(ratio, 0.01f, 0.50f);
}

ErrorCode printMoney(MonetaryStateComponent& state, CurrencyAmount amount) {
    if (state.system != MonetarySystemType::FiatMoney
        && state.system != MonetarySystemType::Digital) {
        return ErrorCode::InvalidMonetaryTransition;
    }
    if (amount <= 0) {
        return ErrorCode::InvalidArgument;
    }

    adjustMoneySupply(state, amount, "printMoney");
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

    state.goldBarReserves += static_cast<int32_t>(goldAmount);
    state.treasury     -= totalCost;
    adjustMoneySupply(state, -totalCost, "buyGold");  // Currency removed from circulation

    // Recalculate backing ratio if on gold standard
    if (state.system == MonetarySystemType::GoldStandard && state.moneySupply > 0) {
        state.goldBackingRatio = static_cast<float>(state.goldBarReserves)
                               / static_cast<float>(state.moneySupply);
    }

    state.updateCoinTier();
    return ErrorCode::Ok;
}

ErrorCode sellGold(MonetaryStateComponent& state,
                    CurrencyAmount goldAmount,
                    CurrencyAmount goldPrice) {
    if (state.goldBarReserves < static_cast<int32_t>(goldAmount)) {
        return ErrorCode::InsufficientResources;
    }

    CurrencyAmount currencyGained = goldAmount * goldPrice;
    state.goldBarReserves -= static_cast<int32_t>(goldAmount);
    state.treasury     += currencyGained;
    adjustMoneySupply(state, currencyGained, "sellGold");  // Currency enters circulation

    // Recalculate backing ratio
    if (state.system == MonetarySystemType::GoldStandard && state.moneySupply > 0) {
        state.goldBackingRatio = static_cast<float>(state.goldBarReserves)
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

    // G7: debasement produces extra coins in the *active legal tender* tier,
    // not whichever metal happens to be the highest-denomination reserve held.
    // Per Gresham's law, the bad money circulates; a civ on Copper tier
    // holding 3 gold bars shouldn't see its gold reserves grow from debasing
    // its copper coinage. Route the bonus through effectiveCoinTier.
    switch (state.effectiveCoinTier) {
        case CoinTier::Gold:
            if (state.goldBarReserves > 0) {
                const int32_t bonus = std::max(1, static_cast<int32_t>(
                    static_cast<float>(state.goldBarReserves) * ratio));
                state.goldBarReserves += bonus;
            }
            break;
        case CoinTier::Silver:
            if (state.silverCoinReserves > 0) {
                const int32_t bonus = std::max(1, static_cast<int32_t>(
                    static_cast<float>(state.silverCoinReserves) * ratio));
                state.silverCoinReserves += bonus;
            }
            break;
        case CoinTier::Copper:
            if (state.copperCoinReserves > 0) {
                const int32_t bonus = std::max(1, static_cast<int32_t>(
                    static_cast<float>(state.copperCoinReserves) * ratio));
                state.copperCoinReserves += bonus;
            }
            break;
        case CoinTier::None:
        case CoinTier::Count:
            break;
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

ErrorCode remintCurrency(MonetaryStateComponent& state) {
    // G15: escape valve from the permanent-debasement trap. Costs 20% of the
    // current treasury and shaves 0.10 off the debasementRatio. Clears the
    // discovery flag so partners have to catch the civ again on the next round.
    if (state.system != MonetarySystemType::CommodityMoney) {
        return ErrorCode::InvalidMonetaryTransition;
    }
    if (state.debasement.debasementRatio <= 0.001f) {
        return ErrorCode::InvalidArgument;
    }

    const CurrencyAmount cost = state.treasury / 5;  // 20% of treasury
    if (cost <= 0 || state.treasury < cost) {
        return ErrorCode::InsufficientResources;
    }

    state.treasury -= cost;
    state.debasement.debasementRatio =
        std::max(0.0f, state.debasement.debasementRatio - 0.10f);
    state.debasement.discoveredByPartners = false;
    state.debasement.turnsDebased = 0;

    LOG_INFO("Player %u: reminted currency (-0.10 debasement, -%lld treasury)",
             static_cast<unsigned>(state.owner),
             static_cast<long long>(cost));
    return ErrorCode::Ok;
}

} // namespace aoc::sim

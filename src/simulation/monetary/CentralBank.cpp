/**
 * @file CentralBank.cpp
 * @brief Central bank monetary policy tool implementations.
 */

#include "aoc/simulation/monetary/CentralBank.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/monetary/MoneyFlow.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void setInterestRate(MonetaryStateComponent& state, Percentage rate) {
    state.interestRate = std::clamp(rate, 0.0f, 0.25f);
}

void setReserveRequirement(MonetaryStateComponent& state, Percentage ratio) {
    state.reserveRequirement = std::clamp(ratio, 0.01f, 0.50f);
}

float moneyMultiplier(const MonetaryStateComponent& state) {
    if (state.reserveRequirement <= 0.001f) {
        return 100.0f; // Cap at 100x to prevent infinity
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
    float newRatio                 = state.debasement.debasementRatio + ratio;
    if (newRatio > MAX_DEBASEMENT) {
        return ErrorCode::InvalidArgument;
    }

    state.debasement.debasementRatio = newRatio;
    state.debasement.turnsDebased    = 0; // Reset discovery timer

    // Debasement dilutes the private specie pool: ratio of new base-metal
    // coin is added to what people already hold.
    if (state.privateSpecie > 0) {
        const CurrencyAmount bonus = std::max<CurrencyAmount>(
            1, static_cast<CurrencyAmount>(static_cast<float>(state.privateSpecie) * ratio));
        state.privateSpecie += bonus;
    }

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
        return false; // Already discovered
    }

    ++state.debasement.turnsDebased;

    // Discovery probability increases each turn and with higher debasement ratio.
    // Base: 10% per turn * debasementRatio * turnsSinceDebasement
    // At 20% debasement, ~40% chance per turn after 2 turns -> discovered by turn 3-5.
    // At 10% debasement, ~20% chance per turn -> discovered by turn 5-8.
    float discoveryChance = 0.10f * state.debasement.debasementRatio *
                            static_cast<float>(state.debasement.turnsDebased);
    discoveryChance       = std::clamp(discoveryChance, 0.0f, 0.95f);

    // Deterministic check using a hash of turn count (no RNG dependency)
    // Simple: if turns * ratio exceeds threshold, it's discovered.
    // After 5 turns at any debasement level, guaranteed discovery.
    if (state.debasement.turnsDebased >= 5 || discoveryChance > 0.50f) {
        state.debasement.discoveredByPartners = true;
        return true;
    }

    return false;
}

ErrorCode remintCurrency(aoc::game::Player& player) {
    MonetaryStateComponent& state = player.monetary();
    // G15: escape valve from the permanent-debasement trap. Costs 20% of the
    // current treasury and shaves 0.10 off the debasementRatio. Clears the
    // discovery flag so partners have to catch the civ again on the next round.
    if (state.system != MonetarySystemType::CommodityMoney) {
        return ErrorCode::InvalidMonetaryTransition;
    }
    if (state.debasement.debasementRatio <= 0.001f) {
        return ErrorCode::InvalidArgument;
    }

    const CurrencyAmount cost = state.treasury / 5; // 20% of treasury
    if (cost <= 0 || state.treasury < cost) {
        return ErrorCode::InsufficientResources;
    }

    player.addGold(-cost, aoc::sim::MoneyFlow::loss()); // metal lost in the restrike
    state.debasement.debasementRatio = std::max(0.0f, state.debasement.debasementRatio - 0.10f);
    state.debasement.discoveredByPartners = false;
    state.debasement.turnsDebased         = 0;

    LOG_INFO("Player %u: reminted currency (-0.10 debasement, -%lld treasury)",
             static_cast<unsigned>(state.owner), static_cast<long long>(cost));
    return ErrorCode::Ok;
}

} // namespace aoc::sim

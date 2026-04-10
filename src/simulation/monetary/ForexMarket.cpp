/**
 * @file ForexMarket.cpp
 * @brief Foreign exchange market implementation.
 */

#include "aoc/simulation/monetary/ForexMarket.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

float computeFundamentalRate(const MonetaryStateComponent& state,
                              const CurrencyTrustComponent& trust,
                              CurrencyAmount averageGDP) {
    float rate = 1.0f;

    // GDP weight: larger economy = stronger currency
    if (averageGDP > 0 && state.gdp > 0) {
        float gdpRatio = static_cast<float>(state.gdp) / static_cast<float>(averageGDP);
        rate *= std::clamp(gdpRatio, 0.5f, 2.0f);
    }

    // Interest rate differential: higher rates attract capital (stronger currency)
    // Baseline assumed at 5%
    float interestDiff = state.interestRate - 0.05f;
    rate *= (1.0f + interestDiff * 2.0f);  // +/-10% per 5% rate diff

    // Inflation penalty: high inflation weakens currency
    float inflationPenalty = state.inflationRate * 2.0f;
    rate *= std::max(0.5f, 1.0f - inflationPenalty);

    // Trust bonus: high trust strengthens currency
    rate *= (0.7f + trust.trustScore * 0.6f);  // 0.7 at zero trust, 1.3 at full trust

    // Reserve currency premium
    if (trust.isReserveCurrency) {
        rate *= 1.10f;
    }

    return std::clamp(rate, 0.20f, 5.0f);
}

ErrorCode buyCurrency(aoc::ecs::World& world,
                       PlayerId buyer, PlayerId target,
                       CurrencyAmount goldAmount) {
    if (goldAmount <= 0 || buyer == target) {
        return ErrorCode::InvalidArgument;
    }

    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    aoc::ecs::ComponentPool<CurrencyExchangeComponent>* forexPool =
        world.getPool<CurrencyExchangeComponent>();

    if (monetaryPool == nullptr || forexPool == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    // Find buyer's monetary state (needs treasury)
    MonetaryStateComponent* buyerState = nullptr;
    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        if (monetaryPool->data()[i].owner == buyer) {
            buyerState = &monetaryPool->data()[i];
            break;
        }
    }
    if (buyerState == nullptr || buyerState->treasury < goldAmount) {
        return ErrorCode::InsufficientResources;
    }

    // Find target's forex component
    CurrencyExchangeComponent* targetForex = nullptr;
    CurrencyExchangeComponent* buyerForex = nullptr;
    for (uint32_t i = 0; i < forexPool->size(); ++i) {
        if (forexPool->data()[i].owner == target) { targetForex = &forexPool->data()[i]; }
        if (forexPool->data()[i].owner == buyer)  { buyerForex = &forexPool->data()[i]; }
    }
    if (targetForex == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    // Execute: buyer spends gold, gets foreign currency reserves
    buyerState->treasury -= goldAmount;
    if (buyerForex != nullptr) {
        buyerForex->foreignReserves += goldAmount;  // Stored as foreign currency
    }

    // Buy pressure on target currency (normalized by target GDP to prevent tiny amounts
    // from moving massive economies)
    float pressureScale = 1.0f;
    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        if (monetaryPool->data()[i].owner == target && monetaryPool->data()[i].gdp > 0) {
            pressureScale = static_cast<float>(goldAmount)
                          / static_cast<float>(monetaryPool->data()[i].gdp);
            break;
        }
    }
    targetForex->netOrderFlow += pressureScale * 10.0f;

    LOG_INFO("Forex: player %u bought %lld gold of player %u's currency (buy pressure: +%.2f)",
             static_cast<unsigned>(buyer),
             static_cast<long long>(goldAmount),
             static_cast<unsigned>(target),
             static_cast<double>(pressureScale * 10.0f));

    return ErrorCode::Ok;
}

ErrorCode sellCurrency(aoc::ecs::World& world,
                        PlayerId seller, PlayerId target,
                        CurrencyAmount currencyAmount) {
    if (currencyAmount <= 0 || seller == target) {
        return ErrorCode::InvalidArgument;
    }

    aoc::ecs::ComponentPool<CurrencyExchangeComponent>* forexPool =
        world.getPool<CurrencyExchangeComponent>();
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();

    if (forexPool == nullptr || monetaryPool == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    // Find seller's forex (needs foreign reserves of target currency)
    CurrencyExchangeComponent* sellerForex = nullptr;
    CurrencyExchangeComponent* targetForex = nullptr;
    for (uint32_t i = 0; i < forexPool->size(); ++i) {
        if (forexPool->data()[i].owner == seller) { sellerForex = &forexPool->data()[i]; }
        if (forexPool->data()[i].owner == target) { targetForex = &forexPool->data()[i]; }
    }
    if (sellerForex == nullptr || targetForex == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    if (sellerForex->foreignReserves < currencyAmount) {
        return ErrorCode::InsufficientResources;
    }

    // Execute: seller dumps currency, gets gold equivalent
    sellerForex->foreignReserves -= currencyAmount;

    // Gold received = amount * target exchange rate (stronger currency = more gold)
    CurrencyAmount goldReceived = static_cast<CurrencyAmount>(
        static_cast<float>(currencyAmount) * targetForex->exchangeRate);
    MonetaryStateComponent* sellerState = nullptr;
    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        if (monetaryPool->data()[i].owner == seller) {
            sellerState = &monetaryPool->data()[i];
            break;
        }
    }
    if (sellerState != nullptr) {
        sellerState->treasury += goldReceived;
    }

    // Sell pressure on target currency
    float pressureScale = 1.0f;
    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        if (monetaryPool->data()[i].owner == target && monetaryPool->data()[i].gdp > 0) {
            pressureScale = static_cast<float>(currencyAmount)
                          / static_cast<float>(monetaryPool->data()[i].gdp);
            break;
        }
    }
    targetForex->netOrderFlow -= pressureScale * 10.0f;

    LOG_INFO("Forex: player %u sold %lld of player %u's currency (sell pressure: -%.2f)",
             static_cast<unsigned>(seller),
             static_cast<long long>(currencyAmount),
             static_cast<unsigned>(target),
             static_cast<double>(pressureScale * 10.0f));

    return ErrorCode::Ok;
}

ErrorCode defendCurrency(aoc::ecs::World& world,
                          PlayerId player,
                          CurrencyAmount amount) {
    if (amount <= 0) {
        return ErrorCode::InvalidArgument;
    }

    aoc::ecs::ComponentPool<CurrencyExchangeComponent>* forexPool =
        world.getPool<CurrencyExchangeComponent>();
    if (forexPool == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    CurrencyExchangeComponent* forex = nullptr;
    for (uint32_t i = 0; i < forexPool->size(); ++i) {
        if (forexPool->data()[i].owner == player) {
            forex = &forexPool->data()[i];
            break;
        }
    }
    if (forex == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    if (forex->foreignReserves < amount) {
        return ErrorCode::InsufficientResources;
    }

    // Spend reserves to create buy pressure on own currency
    forex->foreignReserves -= amount;
    forex->defenseSpending += amount;
    forex->isDefendingRate = true;
    forex->defenseTarget = forex->exchangeRate;

    // Intervention creates strong buy pressure
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    float pressureScale = 1.0f;
    if (monetaryPool != nullptr) {
        for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
            if (monetaryPool->data()[i].owner == player && monetaryPool->data()[i].gdp > 0) {
                pressureScale = static_cast<float>(amount)
                              / static_cast<float>(monetaryPool->data()[i].gdp);
                break;
            }
        }
    }
    forex->netOrderFlow += pressureScale * 15.0f;  // Central bank is a bigger buyer

    LOG_INFO("Forex: player %u defending currency with %lld foreign reserves",
             static_cast<unsigned>(player), static_cast<long long>(amount));

    return ErrorCode::Ok;
}

void updateExchangeRates(aoc::ecs::World& world) {
    aoc::ecs::ComponentPool<CurrencyExchangeComponent>* forexPool =
        world.getPool<CurrencyExchangeComponent>();
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    aoc::ecs::ComponentPool<CurrencyTrustComponent>* trustPool =
        world.getPool<CurrencyTrustComponent>();

    if (forexPool == nullptr || monetaryPool == nullptr) {
        return;
    }

    // Compute average GDP for fundamental rate calculation
    CurrencyAmount totalGDP = 0;
    int32_t fiatCount = 0;
    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        if (monetaryPool->data()[i].system == MonetarySystemType::FiatMoney) {
            totalGDP += monetaryPool->data()[i].gdp;
            ++fiatCount;
        }
    }
    CurrencyAmount averageGDP = (fiatCount > 0) ? totalGDP / fiatCount : 1;

    for (uint32_t i = 0; i < forexPool->size(); ++i) {
        CurrencyExchangeComponent& forex = forexPool->data()[i];

        // Find matching monetary and trust components
        const MonetaryStateComponent* state = nullptr;
        const CurrencyTrustComponent* trust = nullptr;
        for (uint32_t m = 0; m < monetaryPool->size(); ++m) {
            if (monetaryPool->data()[m].owner == forex.owner) {
                state = &monetaryPool->data()[m];
                break;
            }
        }
        if (trustPool != nullptr) {
            for (uint32_t t = 0; t < trustPool->size(); ++t) {
                if (trustPool->data()[t].owner == forex.owner) {
                    trust = &trustPool->data()[t];
                    break;
                }
            }
        }

        if (state == nullptr || state->system != MonetarySystemType::FiatMoney) {
            continue;
        }

        CurrencyTrustComponent defaultTrust{};
        defaultTrust.owner = forex.owner;
        const CurrencyTrustComponent& effectiveTrust = (trust != nullptr) ? *trust : defaultTrust;

        // 1. Compute fundamental rate
        forex.fundamentalRate = computeFundamentalRate(*state, effectiveTrust, averageGDP);

        // 2. Apply trade balance (surplus strengthens, deficit weakens)
        float tradeBalanceEffect = 0.0f;
        if (state->gdp > 0) {
            tradeBalanceEffect = static_cast<float>(forex.tradeBalance)
                               / static_cast<float>(state->gdp) * 2.0f;
        }

        // 3. Market rate adjusts: 20% drift toward fundamental + order flow + trade balance
        float targetRate = forex.fundamentalRate + tradeBalanceEffect;
        float orderFlowEffect = forex.netOrderFlow * 0.05f;

        float newRate = forex.exchangeRate * 0.80f
                      + targetRate * 0.20f
                      + orderFlowEffect;

        // 4. Clamp to prevent absurd values
        forex.exchangeRate = std::clamp(newRate, 0.20f, 5.0f);

        // Log significant rate changes
        float rateChange = forex.exchangeRate - (forex.exchangeRate - orderFlowEffect
                          - targetRate * 0.20f + forex.exchangeRate * 0.20f);
        if (std::abs(forex.netOrderFlow) > 0.1f) {
            LOG_INFO("Forex: player %u rate %.3f (fundamental: %.3f, order flow: %.2f)",
                     static_cast<unsigned>(forex.owner),
                     static_cast<double>(forex.exchangeRate),
                     static_cast<double>(forex.fundamentalRate),
                     static_cast<double>(forex.netOrderFlow));
        }
        (void)rateChange;

        // 5. Reset for next turn
        forex.netOrderFlow = 0.0f;
        forex.tradeBalance = 0;
        forex.defenseSpending = 0;
        forex.isDefendingRate = false;
    }
}

float bilateralExchangeRate(const CurrencyExchangeComponent& playerA,
                             const CurrencyExchangeComponent& playerB) {
    if (playerB.exchangeRate <= 0.01f) {
        return 1.0f;
    }
    return playerA.exchangeRate / playerB.exchangeRate;
}

} // namespace aoc::sim

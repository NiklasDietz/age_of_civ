/**
 * @file ForexMarket.cpp
 * @brief Foreign exchange market implementation.
 */

#include "aoc/simulation/monetary/ForexMarket.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
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

    // Interest rate differential: higher rates attract capital (stronger currency).
    // Baseline assumed at 5%.
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

ErrorCode buyCurrency(aoc::game::GameState& gameState,
                       PlayerId buyer, PlayerId target,
                       CurrencyAmount goldAmount) {
    if (goldAmount <= 0 || buyer == target) {
        return ErrorCode::InvalidArgument;
    }

    aoc::game::Player* buyerPlayer  = gameState.player(buyer);
    aoc::game::Player* targetPlayer = gameState.player(target);
    if (buyerPlayer == nullptr || targetPlayer == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    MonetaryStateComponent& buyerState = buyerPlayer->monetary();
    if (buyerState.treasury < goldAmount) {
        return ErrorCode::InsufficientResources;
    }

    CurrencyExchangeComponent& targetForex = targetPlayer->currencyExchange();
    CurrencyExchangeComponent& buyerForex  = buyerPlayer->currencyExchange();

    // Execute: buyer spends gold, gets foreign currency reserves
    buyerState.treasury    -= goldAmount;
    buyerForex.foreignReserves += goldAmount;

    // Buy pressure on target currency, normalised by target GDP to prevent tiny amounts
    // from moving massive economies.
    float pressureScale = 1.0f;
    const MonetaryStateComponent& targetState = targetPlayer->monetary();
    if (targetState.gdp > 0) {
        pressureScale = static_cast<float>(goldAmount)
                      / static_cast<float>(targetState.gdp);
    }
    targetForex.netOrderFlow += pressureScale * 10.0f;

    LOG_INFO("Forex: player %u bought %lld gold of player %u's currency (buy pressure: +%.2f)",
             static_cast<unsigned>(buyer),
             static_cast<long long>(goldAmount),
             static_cast<unsigned>(target),
             static_cast<double>(pressureScale * 10.0f));

    return ErrorCode::Ok;
}

ErrorCode sellCurrency(aoc::game::GameState& gameState,
                        PlayerId seller, PlayerId target,
                        CurrencyAmount currencyAmount) {
    if (currencyAmount <= 0 || seller == target) {
        return ErrorCode::InvalidArgument;
    }

    aoc::game::Player* sellerPlayer = gameState.player(seller);
    aoc::game::Player* targetPlayer = gameState.player(target);
    if (sellerPlayer == nullptr || targetPlayer == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    CurrencyExchangeComponent& sellerForex = sellerPlayer->currencyExchange();
    CurrencyExchangeComponent& targetForex = targetPlayer->currencyExchange();

    if (sellerForex.foreignReserves < currencyAmount) {
        return ErrorCode::InsufficientResources;
    }

    // Execute: seller dumps currency, gets gold equivalent
    sellerForex.foreignReserves -= currencyAmount;

    // Gold received = amount * target exchange rate (stronger currency = more gold)
    CurrencyAmount goldReceived = static_cast<CurrencyAmount>(
        static_cast<float>(currencyAmount) * targetForex.exchangeRate);
    sellerPlayer->monetary().treasury += goldReceived;

    // Sell pressure on target currency, normalised by target GDP
    float pressureScale = 1.0f;
    const MonetaryStateComponent& targetState = targetPlayer->monetary();
    if (targetState.gdp > 0) {
        pressureScale = static_cast<float>(currencyAmount)
                      / static_cast<float>(targetState.gdp);
    }
    targetForex.netOrderFlow -= pressureScale * 10.0f;

    LOG_INFO("Forex: player %u sold %lld of player %u's currency (sell pressure: -%.2f)",
             static_cast<unsigned>(seller),
             static_cast<long long>(currencyAmount),
             static_cast<unsigned>(target),
             static_cast<double>(pressureScale * 10.0f));

    return ErrorCode::Ok;
}

ErrorCode defendCurrency(aoc::game::GameState& gameState,
                          PlayerId player,
                          CurrencyAmount amount) {
    if (amount <= 0) {
        return ErrorCode::InvalidArgument;
    }

    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    CurrencyExchangeComponent& forex = playerObj->currencyExchange();
    if (forex.foreignReserves < amount) {
        return ErrorCode::InsufficientResources;
    }

    // Spend reserves to create buy pressure on own currency
    forex.foreignReserves -= amount;
    forex.defenseSpending += amount;
    forex.isDefendingRate  = true;
    forex.defenseTarget    = forex.exchangeRate;

    // Intervention creates strong buy pressure, normalised by own GDP
    float pressureScale = 1.0f;
    const MonetaryStateComponent& state = playerObj->monetary();
    if (state.gdp > 0) {
        pressureScale = static_cast<float>(amount) / static_cast<float>(state.gdp);
    }
    forex.netOrderFlow += pressureScale * 15.0f;  // Central bank is a bigger buyer

    LOG_INFO("Forex: player %u defending currency with %lld foreign reserves",
             static_cast<unsigned>(player), static_cast<long long>(amount));

    return ErrorCode::Ok;
}

void updateExchangeRates(aoc::game::GameState& gameState) {
    // Compute average GDP across fiat-money players for fundamental rate calculation
    CurrencyAmount totalGDP = 0;
    int32_t fiatCount       = 0;
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) {
            continue;
        }
        const MonetaryStateComponent& state = playerPtr->monetary();
        if (state.system == MonetarySystemType::FiatMoney
            || state.system == MonetarySystemType::Digital) {
            totalGDP += state.gdp;
            ++fiatCount;
        }
    }
    CurrencyAmount averageGDP = (fiatCount > 0) ? totalGDP / fiatCount : 1;

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) {
            continue;
        }

        CurrencyExchangeComponent& forex = playerPtr->currencyExchange();
        const MonetaryStateComponent& state = playerPtr->monetary();

        if (state.system != MonetarySystemType::FiatMoney
            && state.system != MonetarySystemType::Digital) {
            continue;
        }

        const CurrencyTrustComponent& trust = playerPtr->currencyTrust();

        // 1. Compute fundamental rate. Resource curse's currencyAppreciation
        // multiplier lifts the fundamental (Dutch disease): raw-commodity
        // exporters see their currency appreciate, hurting manufacturing
        // exports elsewhere in the model (via the manufacturingPenalty).
        forex.fundamentalRate = computeFundamentalRate(state, trust, averageGDP);
        forex.fundamentalRate *= playerPtr->resourceCurse().currencyAppreciation;

        // 2. Apply trade balance (surplus strengthens, deficit weakens)
        float tradeBalanceEffect = 0.0f;
        if (state.gdp > 0) {
            tradeBalanceEffect = static_cast<float>(forex.tradeBalance)
                               / static_cast<float>(state.gdp) * 2.0f;
        }

        // 3. Market rate adjusts: 20% drift toward fundamental + order flow + trade balance
        float targetRate      = forex.fundamentalRate + tradeBalanceEffect;
        float orderFlowEffect = forex.netOrderFlow * 0.05f;

        float newRate = forex.exchangeRate * 0.80f
                      + targetRate * 0.20f
                      + orderFlowEffect;

        // 4. Clamp to prevent absurd values
        forex.exchangeRate = std::clamp(newRate, 0.20f, 5.0f);

        if (std::abs(forex.netOrderFlow) > 0.1f) {
            LOG_INFO("Forex: player %u rate %.3f (fundamental: %.3f, order flow: %.2f)",
                     static_cast<unsigned>(playerPtr->id()),
                     static_cast<double>(forex.exchangeRate),
                     static_cast<double>(forex.fundamentalRate),
                     static_cast<double>(forex.netOrderFlow));
        }

        // 5. Reset order-flow accumulators for next turn
        forex.netOrderFlow    = 0.0f;
        forex.tradeBalance    = 0;
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

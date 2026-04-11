/**
 * @file StockMarket.cpp
 * @brief Inter-player equity investment and stock market.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/economy/StockMarket.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

ErrorCode investInEconomy(aoc::game::GameState& gameState,
                           PlayerId investor, PlayerId target,
                           CurrencyAmount amount) {
    aoc::ecs::World& world = gameState.legacyWorld();
    if (amount <= 0 || investor == target) {
        return ErrorCode::InvalidArgument;
    }

    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    aoc::ecs::ComponentPool<PlayerStockPortfolioComponent>* stockPool =
        world.getPool<PlayerStockPortfolioComponent>();

    if (monetaryPool == nullptr || stockPool == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    MonetaryStateComponent* investorState = nullptr;
    MonetaryStateComponent* targetState = nullptr;
    PlayerStockPortfolioComponent* investorPortfolio = nullptr;
    PlayerStockPortfolioComponent* targetPortfolio = nullptr;

    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        if (monetaryPool->data()[i].owner == investor) { investorState = &monetaryPool->data()[i]; }
        if (monetaryPool->data()[i].owner == target)   { targetState = &monetaryPool->data()[i]; }
    }
    for (uint32_t i = 0; i < stockPool->size(); ++i) {
        if (stockPool->data()[i].owner == investor) { investorPortfolio = &stockPool->data()[i]; }
        if (stockPool->data()[i].owner == target)   { targetPortfolio = &stockPool->data()[i]; }
    }

    if (investorState == nullptr || targetState == nullptr
        || investorPortfolio == nullptr || targetPortfolio == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    // Need at least Gold Standard to invest
    if (investorState->system < MonetarySystemType::GoldStandard) {
        return ErrorCode::InvalidMonetaryTransition;
    }

    // Check investment limit
    int32_t maxInvestments = (investorState->system == MonetarySystemType::FiatMoney) ? 5 : 2;
    if (static_cast<int32_t>(investorPortfolio->investments.size()) >= maxInvestments) {
        return ErrorCode::InvalidArgument;
    }

    if (investorState->treasury < amount) {
        return ErrorCode::InsufficientResources;
    }

    // Transfer capital
    investorState->treasury -= amount;
    targetState->treasury += amount;

    // Create investment
    EquityInvestment inv;
    inv.investor = investor;
    inv.target = target;
    inv.principalInvested = amount;
    inv.currentValue = amount;
    inv.totalDividends = 0;
    inv.turnsHeld = 0;

    investorPortfolio->investments.push_back(inv);
    targetPortfolio->foreignInvestments.push_back(inv);

    LOG_INFO("Stock market: player %u invested %lld in player %u's economy",
             static_cast<unsigned>(investor),
             static_cast<long long>(amount),
             static_cast<unsigned>(target));

    return ErrorCode::Ok;
}

ErrorCode divestFromEconomy(aoc::game::GameState& gameState,
                             PlayerId investor, PlayerId target) {
    aoc::ecs::World& world = gameState.legacyWorld();
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    aoc::ecs::ComponentPool<PlayerStockPortfolioComponent>* stockPool =
        world.getPool<PlayerStockPortfolioComponent>();

    if (monetaryPool == nullptr || stockPool == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    MonetaryStateComponent* investorState = nullptr;
    MonetaryStateComponent* targetState = nullptr;
    PlayerStockPortfolioComponent* investorPortfolio = nullptr;
    PlayerStockPortfolioComponent* targetPortfolio = nullptr;

    for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
        if (monetaryPool->data()[i].owner == investor) { investorState = &monetaryPool->data()[i]; }
        if (monetaryPool->data()[i].owner == target)   { targetState = &monetaryPool->data()[i]; }
    }
    for (uint32_t i = 0; i < stockPool->size(); ++i) {
        if (stockPool->data()[i].owner == investor) { investorPortfolio = &stockPool->data()[i]; }
        if (stockPool->data()[i].owner == target)   { targetPortfolio = &stockPool->data()[i]; }
    }

    if (investorState == nullptr || investorPortfolio == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    // Find and remove the investment
    CurrencyAmount totalValue = 0;
    std::vector<EquityInvestment>::iterator it = investorPortfolio->investments.begin();
    while (it != investorPortfolio->investments.end()) {
        if (it->target == target) {
            totalValue += it->currentValue;
            it = investorPortfolio->investments.erase(it);
        } else {
            ++it;
        }
    }

    if (totalValue <= 0) {
        return ErrorCode::InvalidArgument;
    }

    // Capital outflow from target, inflow to investor
    investorState->treasury += totalValue;
    if (targetState != nullptr) {
        targetState->treasury -= std::min(targetState->treasury, totalValue);
    }

    // Remove from target's foreign investments
    if (targetPortfolio != nullptr) {
        std::vector<EquityInvestment>::iterator fIt = targetPortfolio->foreignInvestments.begin();
        while (fIt != targetPortfolio->foreignInvestments.end()) {
            if (fIt->investor == investor) {
                fIt = targetPortfolio->foreignInvestments.erase(fIt);
            } else {
                ++fIt;
            }
        }
    }

    LOG_INFO("Stock market: player %u divested %lld from player %u",
             static_cast<unsigned>(investor),
             static_cast<long long>(totalValue),
             static_cast<unsigned>(target));

    return ErrorCode::Ok;
}

void processStockMarket(aoc::game::GameState& gameState) {
    aoc::ecs::ComponentPool<PlayerStockPortfolioComponent>* stockPool =
        world.getPool<PlayerStockPortfolioComponent>();
    aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();

    if (stockPool == nullptr || monetaryPool == nullptr) {
        return;
    }

    for (uint32_t p = 0; p < stockPool->size(); ++p) {
        PlayerStockPortfolioComponent& portfolio = stockPool->data()[p];

        for (EquityInvestment& inv : portfolio.investments) {
            ++inv.turnsHeld;

            // Find target's GDP for value adjustment
            CurrencyAmount targetGDP = 0;
            MonetarySystemType targetSystem = MonetarySystemType::Barter;
            for (uint32_t m = 0; m < monetaryPool->size(); ++m) {
                if (monetaryPool->data()[m].owner == inv.target) {
                    targetGDP = monetaryPool->data()[m].gdp;
                    targetSystem = monetaryPool->data()[m].system;
                    break;
                }
            }

            // Value tracks target GDP: if GDP grows, investment grows
            if (targetGDP > 0 && inv.principalInvested > 0) {
                // Simple model: value = principal * (targetGDP / GDP_at_investment)
                // We approximate by adjusting value 5% toward GDP-proportional target
                float gdpRatio = static_cast<float>(targetGDP) / 1000.0f;  // Normalized
                CurrencyAmount targetValue = static_cast<CurrencyAmount>(
                    static_cast<float>(inv.principalInvested) * std::max(0.1f, gdpRatio));
                int64_t diff = static_cast<int64_t>(targetValue) - static_cast<int64_t>(inv.currentValue);
                inv.currentValue += static_cast<CurrencyAmount>(diff / 20);  // 5% drift
                inv.currentValue = std::max(static_cast<CurrencyAmount>(0), inv.currentValue);
            }

            // Pay dividends: percentage of current value
            float dividendRate = (targetSystem == MonetarySystemType::FiatMoney) ? 0.05f : 0.03f;
            CurrencyAmount dividend = static_cast<CurrencyAmount>(
                static_cast<float>(inv.currentValue) * dividendRate);
            dividend = std::max(static_cast<CurrencyAmount>(1), dividend);

            // Pay from target's treasury to investor's treasury
            for (uint32_t m = 0; m < monetaryPool->size(); ++m) {
                if (monetaryPool->data()[m].owner == inv.target) {
                    CurrencyAmount actualDividend = std::min(
                        monetaryPool->data()[m].treasury, dividend);
                    monetaryPool->data()[m].treasury -= actualDividend;

                    // Pay investor
                    for (uint32_t m2 = 0; m2 < monetaryPool->size(); ++m2) {
                        if (monetaryPool->data()[m2].owner == portfolio.owner) {
                            monetaryPool->data()[m2].treasury += actualDividend;
                            break;
                        }
                    }
                    inv.totalDividends += actualDividend;
                    break;
                }
            }
        }
    }
}

void triggerMarketCrash(aoc::game::GameState& gameState, PlayerId crashedPlayer) {
    aoc::ecs::ComponentPool<PlayerStockPortfolioComponent>* stockPool =
        world.getPool<PlayerStockPortfolioComponent>();
    if (stockPool == nullptr) {
        return;
    }

    // All investments in the crashed player lose 40% value
    for (uint32_t p = 0; p < stockPool->size(); ++p) {
        PlayerStockPortfolioComponent& portfolio = stockPool->data()[p];
        for (EquityInvestment& inv : portfolio.investments) {
            if (inv.target == crashedPlayer) {
                CurrencyAmount loss = static_cast<CurrencyAmount>(
                    static_cast<float>(inv.currentValue) * 0.40f);
                inv.currentValue -= loss;
                inv.currentValue = std::max(static_cast<CurrencyAmount>(0), inv.currentValue);
            }
        }

        // Also update foreign investment records
        for (EquityInvestment& inv : portfolio.foreignInvestments) {
            if (inv.investor == crashedPlayer || inv.target == crashedPlayer) {
                CurrencyAmount loss = static_cast<CurrencyAmount>(
                    static_cast<float>(inv.currentValue) * 0.40f);
                inv.currentValue -= loss;
                inv.currentValue = std::max(static_cast<CurrencyAmount>(0), inv.currentValue);
            }
        }
    }

    LOG_INFO("MARKET CRASH: player %u triggered 40%% loss on all connected investments",
             static_cast<unsigned>(crashedPlayer));
}

} // namespace aoc::sim

/**
 * @file StockMarket.cpp
 * @brief Inter-player equity investment and stock market.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/economy/StockMarket.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

ErrorCode investInEconomy(aoc::game::GameState& gameState,
                           PlayerId investor, PlayerId target,
                           CurrencyAmount amount) {
    if (amount <= 0 || investor == target) {
        return ErrorCode::InvalidArgument;
    }

    aoc::game::Player* investorPlayer = gameState.player(investor);
    aoc::game::Player* targetPlayer   = gameState.player(target);
    if (investorPlayer == nullptr || targetPlayer == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    MonetaryStateComponent&        investorState     = investorPlayer->monetary();
    MonetaryStateComponent&        targetState       = targetPlayer->monetary();
    PlayerStockPortfolioComponent& investorPortfolio = investorPlayer->stockPortfolio();
    PlayerStockPortfolioComponent& targetPortfolio   = targetPlayer->stockPortfolio();

    if (investorState.system < MonetarySystemType::GoldStandard) {
        return ErrorCode::InvalidMonetaryTransition;
    }

    int32_t maxInvestments = (investorState.system == MonetarySystemType::FiatMoney) ? 5 : 2;
    if (static_cast<int32_t>(investorPortfolio.investments.size()) >= maxInvestments) {
        return ErrorCode::InvalidArgument;
    }

    if (investorState.treasury < amount) {
        return ErrorCode::InsufficientResources;
    }

    investorState.treasury -= amount;
    targetState.treasury   += amount;

    EquityInvestment inv;
    inv.investor         = investor;
    inv.target           = target;
    inv.principalInvested = amount;
    inv.currentValue     = amount;
    inv.totalDividends   = 0;
    inv.turnsHeld        = 0;

    investorPortfolio.investments.push_back(inv);
    targetPortfolio.foreignInvestments.push_back(inv);

    LOG_INFO("Stock market: player %u invested %lld in player %u's economy",
             static_cast<unsigned>(investor),
             static_cast<long long>(amount),
             static_cast<unsigned>(target));

    return ErrorCode::Ok;
}

ErrorCode divestFromEconomy(aoc::game::GameState& gameState,
                             PlayerId investor, PlayerId target) {
    aoc::game::Player* investorPlayer = gameState.player(investor);
    aoc::game::Player* targetPlayer   = gameState.player(target);
    if (investorPlayer == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    MonetaryStateComponent&        investorState     = investorPlayer->monetary();
    PlayerStockPortfolioComponent& investorPortfolio = investorPlayer->stockPortfolio();

    CurrencyAmount totalValue = 0;
    std::vector<EquityInvestment>::iterator it = investorPortfolio.investments.begin();
    while (it != investorPortfolio.investments.end()) {
        if (it->target == target) {
            totalValue += it->currentValue;
            it = investorPortfolio.investments.erase(it);
        } else {
            ++it;
        }
    }

    if (totalValue <= 0) {
        return ErrorCode::InvalidArgument;
    }

    investorState.treasury += totalValue;
    if (targetPlayer != nullptr) {
        MonetaryStateComponent& targetState = targetPlayer->monetary();
        targetState.treasury -= std::min(targetState.treasury, totalValue);

        std::vector<EquityInvestment>::iterator fIt =
            targetPlayer->stockPortfolio().foreignInvestments.begin();
        while (fIt != targetPlayer->stockPortfolio().foreignInvestments.end()) {
            if (fIt->investor == investor) {
                fIt = targetPlayer->stockPortfolio().foreignInvestments.erase(fIt);
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
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        PlayerStockPortfolioComponent& portfolio = playerPtr->stockPortfolio();

        for (EquityInvestment& inv : portfolio.investments) {
            ++inv.turnsHeld;

            aoc::game::Player* targetPlayer = gameState.player(inv.target);
            CurrencyAmount targetGDP        = 0;
            MonetarySystemType targetSystem = MonetarySystemType::Barter;
            if (targetPlayer != nullptr) {
                targetGDP    = targetPlayer->monetary().gdp;
                targetSystem = targetPlayer->monetary().system;
            }

            if (targetGDP > 0 && inv.principalInvested > 0) {
                float          gdpRatio    = static_cast<float>(targetGDP) / 1000.0f;
                CurrencyAmount targetValue = static_cast<CurrencyAmount>(
                    static_cast<float>(inv.principalInvested) * std::max(0.1f, gdpRatio));
                int64_t diff = static_cast<int64_t>(targetValue)
                             - static_cast<int64_t>(inv.currentValue);
                inv.currentValue += static_cast<CurrencyAmount>(diff / 20);
                inv.currentValue  = std::max(static_cast<CurrencyAmount>(0), inv.currentValue);
            }

            float          dividendRate = (targetSystem == MonetarySystemType::FiatMoney) ? 0.05f : 0.03f;
            CurrencyAmount dividend     = static_cast<CurrencyAmount>(
                static_cast<float>(inv.currentValue) * dividendRate);
            dividend = std::max(static_cast<CurrencyAmount>(1), dividend);

            if (targetPlayer != nullptr) {
                MonetaryStateComponent& targetState = targetPlayer->monetary();
                CurrencyAmount actualDividend = std::min(targetState.treasury, dividend);
                targetState.treasury -= actualDividend;

                aoc::game::Player* invPlayer = gameState.player(playerPtr->id());
                if (invPlayer != nullptr) {
                    invPlayer->monetary().treasury += actualDividend;
                }
                inv.totalDividends += actualDividend;
            }
        }
    }
}

void triggerMarketCrash(aoc::game::GameState& gameState, PlayerId crashedPlayer) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        PlayerStockPortfolioComponent& portfolio = playerPtr->stockPortfolio();

        for (EquityInvestment& inv : portfolio.investments) {
            if (inv.target == crashedPlayer) {
                CurrencyAmount loss = static_cast<CurrencyAmount>(
                    static_cast<float>(inv.currentValue) * 0.40f);
                inv.currentValue -= loss;
                inv.currentValue  = std::max(static_cast<CurrencyAmount>(0), inv.currentValue);
            }
        }

        for (EquityInvestment& inv : portfolio.foreignInvestments) {
            if (inv.investor == crashedPlayer || inv.target == crashedPlayer) {
                CurrencyAmount loss = static_cast<CurrencyAmount>(
                    static_cast<float>(inv.currentValue) * 0.40f);
                inv.currentValue -= loss;
                inv.currentValue  = std::max(static_cast<CurrencyAmount>(0), inv.currentValue);
            }
        }
    }

    LOG_INFO("MARKET CRASH: player %u triggered 40%% loss on all connected investments",
             static_cast<unsigned>(crashedPlayer));
}

} // namespace aoc::sim

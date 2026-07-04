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
#include <atomic>
#include <cstdint>
#include <unordered_map>

namespace aoc::sim {

namespace {

/// Monotonic counter for EquityInvestment ids. Fresh investments get a
/// unique id; saved games reload with id=0 and are reassigned on first
/// processStockMarket pass. Process-local scope is sufficient — ids are
/// not persisted, only used for matching investor record <-> target
/// mirror in memory. atomic_uint32_t makes the mint thread-safe even
/// though the simulation is currently single-threaded.
std::atomic<uint32_t>& nextInvestmentIdCounter() {
    static std::atomic<uint32_t> s_next{1};
    return s_next;
}

uint32_t mintInvestmentId() {
    return nextInvestmentIdCounter().fetch_add(1, std::memory_order_relaxed);
}

} // namespace

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

    int32_t maxInvestments = 2;
    if (investorState.system == MonetarySystemType::FiatMoney) { maxInvestments = 5; }
    if (investorState.system == MonetarySystemType::Digital)   { maxInvestments = 8; }
    if (static_cast<int32_t>(investorPortfolio.investments.size()) >= maxInvestments) {
        return ErrorCode::InvalidArgument;
    }

    if (investorState.treasury < amount) {
        return ErrorCode::InsufficientResources;
    }

    investorState.treasury -= amount;
    targetState.treasury   += amount;

    EquityInvestment inv;
    inv.investor          = investor;
    inv.target            = target;
    inv.principalInvested = amount;
    inv.currentValue      = amount;
    inv.totalDividends    = 0;
    inv.turnsHeld         = 0;
    inv.id                = mintInvestmentId();

    // Both vectors get the SAME id so processStockMarket can pair the
    // investor's authoritative record with the target-side mirror via
    // direct id comparison instead of the prior triple-field linear
    // scan that used the mutable principalInvested as part of the key.
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
        // Deduct only what the target actually has. A negative treasury would
        // otherwise flip the std::min and MINT gold (treasury -= negative).
        targetState.treasury -=
            std::min(std::max<CurrencyAmount>(0, targetState.treasury), totalValue);

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
    // Audit Warning hot-path fix: build a single (target, investmentId)
    // -> EquityInvestment* map across ALL players' foreignInvestments
    // mirrors once per call. The previous code did a linear scan inside
    // the inner loop using `principalInvested` as part of a triple-field
    // identity match — both slow AND broken (principalInvested is
    // mutable in some flows, so the key shifted).
    //
    // Pointer stability: foreignInvestments is a std::vector<EquityInvestment>;
    // we do NOT push_back / erase to it during the loop below (only field
    // mutations on existing entries), so the pointers stored in `mirror`
    // stay valid for the entire processStockMarket call.
    //
    // Backfill: legacy saves load with id=0; assign fresh ids to such
    // mirror entries before keying the map so each gets a unique slot.
    using MirrorKey = uint64_t;
    std::unordered_map<MirrorKey, EquityInvestment*> mirror;
    {
        std::size_t totalMirrors = 0;
        for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
            if (playerPtr == nullptr) { continue; }
            totalMirrors += playerPtr->stockPortfolio().foreignInvestments.size();
        }
        mirror.reserve(totalMirrors * 2);
        for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
            if (playerPtr == nullptr) { continue; }
            std::vector<EquityInvestment>& fi = playerPtr->stockPortfolio().foreignInvestments;
            for (EquityInvestment& m : fi) {
                if (m.id == 0) { m.id = mintInvestmentId(); }
                const MirrorKey key =
                    (static_cast<uint64_t>(playerPtr->id()) << 32)
                  | static_cast<uint64_t>(m.id);
                mirror[key] = &m;
            }
        }
    }

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        PlayerStockPortfolioComponent& portfolio = playerPtr->stockPortfolio();

        for (EquityInvestment& inv : portfolio.investments) {
            ++inv.turnsHeld;

            // Backfill investor-side id so the mirror lookup below can
            // find a paired entry (some legacy saves loaded with id=0
            // both sides).
            if (inv.id == 0) { inv.id = mintInvestmentId(); }

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

            float dividendRate = 0.03f;
            if (targetSystem == MonetarySystemType::FiatMoney) { dividendRate = 0.05f; }
            if (targetSystem == MonetarySystemType::Digital)   { dividendRate = 0.06f; }
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

                // Keep the target's foreignInvestments mirror in sync with
                // the authoritative record held by the investor. Without
                // this the mirror keeps stale principalInvested values and
                // the target's UI (and downstream systems reading the
                // mirror) sees frozen dividends/turnsHeld. O(1) lookup
                // via the precomputed (targetPlayer, id) map.
                const MirrorKey key =
                    (static_cast<uint64_t>(inv.target) << 32)
                  | static_cast<uint64_t>(inv.id);
                std::unordered_map<MirrorKey, EquityInvestment*>::const_iterator mIt =
                    mirror.find(key);
                if (mIt != mirror.end() && mIt->second != nullptr) {
                    EquityInvestment* m = mIt->second;
                    m->currentValue   = inv.currentValue;
                    m->totalDividends = inv.totalDividends;
                    m->turnsHeld      = inv.turnsHeld;
                }
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

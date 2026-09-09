/**
 * @file test_forex_trade_balance.cpp
 * @brief A trade surplus firms the currency; a deficit weakens it.
 *
 *        ForexMarket READ `forex.tradeBalance` to move the exchange rate away
 *        from its fundamental, and reset it to zero at the end of its own
 *        update -- but nothing anywhere WROTE a non-zero value. The field
 *        occurred exactly twice in the entire tree: its declaration and that
 *        reset. So the trade channel contributed identically zero to every
 *        exchange rate in every game.
 *
 *        It is worse than an ordinary dead field, because a comment asserted
 *        the channel worked and I removed a different mechanic on the strength
 *        of that assertion before checking it.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/simulation/monetary/ForexMarket.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"

using aoc::PlayerId;

namespace {

/// Run one forex update for a civ carrying `balance`, and report the rate.
float rateAfterBalance(aoc::CurrencyAmount balance) {
    aoc::test::World w   = aoc::test::makeWorld(2);
    for (aoc::PlayerId id : {PlayerId{0}, PlayerId{1}}) {
        aoc::game::Player& p = *w.gameState.player(id);
        // Only a floating currency has an exchange rate to move: forex skips
        // anything below FiatMoney, which is correct -- a unit defined as a
        // weight of silver cannot drift against another one.
        p.monetary().system  = aoc::sim::MonetarySystemType::FiatMoney;
        p.monetary().gdp     = 1000;
        p.currencyExchange().exchangeRate    = 1.0f;
        p.currencyExchange().fundamentalRate = 1.0f;
        p.currencyExchange().tradeBalance    = 0;
    }
    w.gameState.player(PlayerId{0})->currencyExchange().tradeBalance = balance;
    aoc::sim::updateExchangeRates(w.gameState);
    return w.gameState.player(PlayerId{0})->currencyExchange().exchangeRate;
}

} // namespace

TEST_CASE("a trade surplus strengthens the currency and a deficit weakens it") {
    const float neutral = rateAfterBalance(0);
    const float surplus = rateAfterBalance(400);
    const float deficit = rateAfterBalance(-400);

    CHECK(surplus > neutral);
    CHECK(deficit < neutral);
}

TEST_CASE("the balance is cleared after it has been applied") {
    // It is a per-turn flow, not a stock. Leaving it to accumulate would let one
    // good trading year pin a currency high forever.
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});
    p.monetary().system  = aoc::sim::MonetarySystemType::FiatMoney;
    p.monetary().gdp     = 1000;
    p.currencyExchange().tradeBalance = 500;

    aoc::sim::updateExchangeRates(w.gameState);

    CHECK(p.currencyExchange().tradeBalance == 0);
}

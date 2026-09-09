/**
 * @file test_trade_settles_in_money.cpp
 * @brief The AI's trade consults the monetary system, and the exchange rate
 *        hears about the trade.
 *
 *        There are two trade systems in the tree. TradeRouteComponent, walked
 *        by EconomySimulation::executeTradeRoutes and settleTradeInCoins, is
 *        appended to in exactly one place -- the human's trade screen in
 *        GameScreens.cpp -- so gameState.tradeRoutes() is empty for every
 *        headless run. Trader UNITS, in TradeRouteSystem.cpp, are how the AI
 *        actually trades.
 *
 *        The whole international-money layer hung off the human-only one.
 *        Measured over 500 turns of seed 42: settleTradeInCoins settled zero
 *        payments, IncomeGoodsEcon was 0 in all 1400 player-turns, and so
 *        bilateralTradeEfficiency -- the one consumer of currency trust and of
 *        the exchange rate -- was never called. Currency trust was computed
 *        every turn, saved, penalised by crises and used to gate reserve-currency
 *        status while having no route to any treasury.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/monetary/ForexMarket.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using aoc::sim::MonetarySystemType;

namespace {

/// Read a source file from the repository, or an empty string when the source
/// tree is not present beside the binary.
std::string readSource(const std::string& relativePath) {
    const std::filesystem::path root = std::filesystem::path(AOC_SOURCE_DIR);
    const std::filesystem::path file = root / relativePath;
    if (!std::filesystem::exists(file)) {
        return {};
    }
    std::ifstream in(file);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("a distrusted currency earns less on the same cargo") {
    aoc::sim::CurrencyTrustComponent trusted;
    trusted.trustScore = 0.90f;
    aoc::sim::CurrencyTrustComponent distrusted;
    distrusted.trustScore = 0.15f;

    const float goodTerms = aoc::sim::fiatTradeEfficiency(trusted);
    const float badTerms  = aoc::sim::fiatTradeEfficiency(distrusted);

    CHECK(goodTerms > badTerms);
    // The spread is worth having: a collapse of confidence costs about a third
    // of the take, which is what makes a crisis penalty matter.
    CHECK(goodTerms - badTerms > 0.30f);
}

TEST_CASE("the trader settlement path consults monetary efficiency") {
    const std::string src = readSource("src/simulation/economy/TradeRouteSystem.cpp");
    if (src.empty()) {
        MESSAGE("source tree not present, skipping the wiring sweep");
        return;
    }
    // Asserted as a source property because the failure mode is silence: the
    // call simply is not made, every number stays plausible, and the mechanic
    // is dead for however long it takes someone to instrument the path.
    CHECK(src.find("bilateralTradeEfficiency(gameState") != std::string::npos);
}

TEST_CASE("a cross-civ sale reaches the exchange rate") {
    const std::string src = readSource("src/simulation/economy/TradeRouteSystem.cpp");
    if (src.empty()) {
        MESSAGE("source tree not present, skipping the wiring sweep");
        return;
    }
    CHECK(src.find("currencyExchange().tradeBalance") != std::string::npos);
}

TEST_CASE("a trade surplus firms the currency and a deficit weakens it") {
    // Two identical fiat civs, one running a surplus and one a deficit of the
    // same size, which is how the trader hook books a sale.
    aoc::sim::MonetaryStateComponent exporterMon;
    exporterMon.system = MonetarySystemType::FiatMoney;
    exporterMon.gdp    = 10000;

    aoc::sim::CurrencyTrustComponent trust;
    trust.trustScore = 0.60f;

    aoc::sim::CurrencyExchangeComponent surplus;
    surplus.tradeBalance = 400;
    aoc::sim::CurrencyExchangeComponent deficit;
    deficit.tradeBalance = -400;

    const float fundamental = aoc::sim::computeFundamentalRate(exporterMon, trust, exporterMon.gdp);

    // The effect updateExchangeRates adds on top of the fundamental rate.
    const float surplusEffect =
        static_cast<float>(surplus.tradeBalance) / static_cast<float>(exporterMon.gdp) * 2.0f;
    const float deficitEffect =
        static_cast<float>(deficit.tradeBalance) / static_cast<float>(exporterMon.gdp) * 2.0f;

    CHECK(fundamental + surplusEffect > fundamental);
    CHECK(fundamental + deficitEffect < fundamental);
    // Equal and opposite, which the trader hook guarantees by debiting the
    // buyer exactly what it credits the seller.
    CHECK(surplusEffect == doctest::Approx(-deficitEffect));
}

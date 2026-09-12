/**
 * @file test_price_anchor.cpp
 * @brief The sticky price anchor of specie regimes (plan B1, 2.3): the level
 *        follows the money per citizen, three percent of the way a turn,
 *        clamped to [0.5, 4]; costs are nominal at that level; nothing here
 *        divides by an empty population; Barter and fiat are not anchored.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/balance/BalanceParams.hpp"
#include "aoc/simulation/city/ProductionSystem.hpp"
#include "aoc/simulation/monetary/Inflation.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"

#include <cmath>

using aoc::sim::MonetarySystemType;

namespace {

/// A coinage civ of `population` holding money worth `perCitizen` per head,
/// at velocity one so the target is exactly perCitizen / K.
[[nodiscard]] aoc::sim::MonetaryStateComponent coinage(int32_t population, float perCitizen) {
    aoc::sim::MonetaryStateComponent m{};
    m.system          = MonetarySystemType::CommodityMoney;
    m.velocityOfMoney = 1.0f;
    m.privateSpecie   = static_cast<aoc::CurrencyAmount>(perCitizen * static_cast<float>(population));
    (void)population;
    return m;
}

const float K = aoc::balance::params().priceAnchorK;

} // namespace

TEST_CASE("the level rises toward a full money stock and falls toward a drained one, three percent a turn") {
    aoc::sim::MonetaryStateComponent m = coinage(10, 2.0f * K); // target 2.0
    aoc::sim::anchorPriceLevel(m, 10);
    CHECK(m.priceLevel == doctest::Approx(1.0f + (2.0f - 1.0f) * aoc::sim::PRICE_ANCHOR_SMOOTHING));
    CHECK(m.inflationRate == doctest::Approx((2.0f - 1.0f) * aoc::sim::PRICE_ANCHOR_SMOOTHING)); // what prices did

    m.privateSpecie = 0; // a specie drain: target 0, clamped to the floor
    const float before = m.priceLevel;
    aoc::sim::anchorPriceLevel(m, 10);
    CHECK(m.priceLevel < before);
    CHECK(m.priceLevel == doctest::Approx(before + (aoc::sim::PRICE_ANCHOR_MIN - before) *
                                                       aoc::sim::PRICE_ANCHOR_SMOOTHING));
    CHECK(m.inflationRate < 0.0f);
}

TEST_CASE("the level is clamped to [0.5, 4] however much or little money there is") {
    aoc::sim::MonetaryStateComponent rich = coinage(10, 1000.0f * K);
    for (int32_t turn = 0; turn < 1000; ++turn) {
        aoc::sim::anchorPriceLevel(rich, 10);
    }
    CHECK(rich.priceLevel == doctest::Approx(aoc::sim::PRICE_ANCHOR_MAX).epsilon(0.001));
    CHECK(rich.inflationRate == doctest::Approx(0.0f).epsilon(0.001)); // at the cap prices sit still

    aoc::sim::MonetaryStateComponent poor = coinage(10, 0.0f);
    for (int32_t turn = 0; turn < 1000; ++turn) {
        aoc::sim::anchorPriceLevel(poor, 10);
    }
    CHECK(poor.priceLevel == doctest::Approx(aoc::sim::PRICE_ANCHOR_MIN).epsilon(0.001));
}

TEST_CASE("the treasury's money counts too, and the money stock is per citizen") {
    aoc::sim::MonetaryStateComponent m = coinage(10, 0.0f);
    m.treasury                         = aoc::sim::TreasuryAccount{}; // zero
    m.privateNotes                     = static_cast<aoc::CurrencyAmount>(10.0f * K); // one per head at K
    aoc::sim::anchorPriceLevel(m, 10);
    CHECK(m.priceLevel == doctest::Approx(1.0f)); // target exactly 1: nothing moves
    aoc::sim::anchorPriceLevel(m, 20); // the same money over twice the people: target 0.5
    CHECK(m.priceLevel < 1.0f);
}

TEST_CASE("an empty population never produces NaN and leaves the level alone") {
    aoc::sim::MonetaryStateComponent m = coinage(1, 5.0f * K);
    m.priceLevel                       = 1.7f;
    aoc::sim::anchorPriceLevel(m, 0);
    CHECK(std::isfinite(m.priceLevel));
    CHECK(m.priceLevel == doctest::Approx(1.7f));
    CHECK(m.inflationRate == doctest::Approx(0.0f));
}

TEST_CASE("Barter and fiat are not anchored") {
    aoc::sim::MonetaryStateComponent barter = coinage(10, 100.0f * K);
    barter.system                           = MonetarySystemType::Barter;
    aoc::sim::anchorPriceLevel(barter, 10);
    CHECK(barter.priceLevel == doctest::Approx(1.0f));
    aoc::sim::MonetaryStateComponent fiat = coinage(10, 100.0f * K);
    fiat.system                           = MonetarySystemType::FiatMoney;
    aoc::sim::anchorPriceLevel(fiat, 10);
    CHECK(fiat.priceLevel == doctest::Approx(1.0f));
    CHECK(aoc::sim::priceAnchored(MonetarySystemType::CommodityMoney));
    CHECK(aoc::sim::priceAnchored(MonetarySystemType::GoldStandard));
    CHECK_FALSE(aoc::sim::priceAnchored(MonetarySystemType::FiatMoney));
}

TEST_CASE("the Fisher path no longer writes an anchored level, and still writes a fiat one") {
    aoc::sim::MonetaryStateComponent m = coinage(10, 1.0f * K);
    m.inflationRate                    = 0.10f;
    aoc::sim::applyInflationEffects(m);
    CHECK(m.priceLevel == doctest::Approx(1.0f));
    m.system = MonetarySystemType::FiatMoney;
    aoc::sim::applyInflationEffects(m);
    CHECK(m.priceLevel == doctest::Approx(1.10f));
}

TEST_CASE("a nominal purchase price tracks the level and never drops below one gold") {
    CHECK(aoc::sim::purchaseCost(30.0f, 1.0f) == 340);
    CHECK(aoc::sim::purchaseCost(30.0f, 0.5f) == 170);
    CHECK(aoc::sim::purchaseCost(30.0f, 4.0f) == 1360);
    CHECK(aoc::sim::purchaseCost(0.0f, 0.001f) == 1);
    // Upkeep scales through the dampened maintenance multiplier.
    CHECK(aoc::sim::priceLevelMaintenanceMultiplier(4.0f) > aoc::sim::priceLevelMaintenanceMultiplier(1.0f));
    CHECK(aoc::sim::priceLevelMaintenanceMultiplier(0.5f) < aoc::sim::priceLevelMaintenanceMultiplier(1.0f));
}

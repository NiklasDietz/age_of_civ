/**
 * @file test_civ_route_bonuses.cpp
 * @brief Civ abilities that pay per active trade route. Science and culture
 *        were always read; gold and faith were authored for ten civs and never
 *        consumed until 2026-09-04. All four now count routes through
 *        `Player::activeTradeRouteCount()` and add a flat bonus per route.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/economy/Maintenance.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/religion/Religion.hpp"

using aoc::PlayerId;
using aoc::sim::CivilizationDef;

namespace {

constexpr aoc::UnitTypeId TRADER{30};
constexpr aoc::UnitTypeId WARRIOR{0};

/// First civ whose modifier selected by `pick` is non-zero, plus that value.
template <typename Pick>
[[nodiscard]] std::pair<aoc::sim::CivId, int32_t> firstCivWith(Pick pick) {
    for (uint8_t i = 0; i < aoc::sim::CIV_COUNT; ++i) {
        const int32_t v = pick(aoc::sim::civDef(static_cast<aoc::sim::CivId>(i)));
        if (v > 0) { return {static_cast<aoc::sim::CivId>(i), v}; }
    }
    return {aoc::sim::CivId{0}, 0};
}

/// A trader only counts as a route once its trader component has an owner.
aoc::game::Unit& addActiveTrader(aoc::test::World& w, PlayerId owner, int32_t q, int32_t r) {
    aoc::game::Unit& t = aoc::test::addUnitAt(w, owner, TRADER, q, r);
    t.trader().owner    = owner;
    return t;
}

} // namespace

TEST_CASE("activeTradeRouteCount counts traders with a route, not idle traders or other units") {
    aoc::test::World w = aoc::test::makeWorld(1);
    aoc::game::Player& p = *w.gameState.players()[0];
    CHECK(p.activeTradeRouteCount() == 0);

    aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, 1, 1);
    aoc::test::addUnitAt(w, PlayerId{0}, TRADER, 2, 1);   // idle: no owner on the route
    CHECK(p.activeTradeRouteCount() == 0);

    addActiveTrader(w, PlayerId{0}, 3, 1);
    addActiveTrader(w, PlayerId{0}, 4, 1);
    CHECK(p.activeTradeRouteCount() == 2);
}

TEST_CASE("goldFromTradeRoute adds a flat bonus per active route to income and its breakdown") {
    const std::pair<aoc::sim::CivId, int32_t> civ =
        firstCivWith([](const CivilizationDef& d) { return d.modifiers.goldFromTradeRoute; });
    REQUIRE(civ.second > 0);

    aoc::test::World w = aoc::test::makeWorld(1);
    aoc::game::Player& p = *w.gameState.players()[0];
    p.setCivId(civ.first);
    p.monetary().system = aoc::sim::MonetarySystemType::CommodityMoney;  // past the barter guard

    const aoc::CurrencyAmount incomeNone    = aoc::sim::processGoldIncome(p, w.grid);
    const aoc::CurrencyAmount breakdownNone = aoc::sim::computeEconomicBreakdown(p, w.grid).totalIncome;
    addActiveTrader(w, PlayerId{0}, 3, 1);
    addActiveTrader(w, PlayerId{0}, 4, 1);
    const aoc::CurrencyAmount incomeTwo    = aoc::sim::processGoldIncome(p, w.grid);
    const aoc::CurrencyAmount breakdownTwo = aoc::sim::computeEconomicBreakdown(p, w.grid).totalIncome;

    CHECK(incomeTwo - incomeNone == 2 * civ.second);
    CHECK(breakdownTwo - breakdownNone == 2 * civ.second);   // the HUD and the CSV agree with the credit
}

TEST_CASE("goldFromTradeRoute pays nothing under barter, like every other gold source") {
    const std::pair<aoc::sim::CivId, int32_t> civ =
        firstCivWith([](const CivilizationDef& d) { return d.modifiers.goldFromTradeRoute; });
    aoc::test::World w = aoc::test::makeWorld(1);
    aoc::game::Player& p = *w.gameState.players()[0];
    p.setCivId(civ.first);
    addActiveTrader(w, PlayerId{0}, 3, 1);
    CHECK(aoc::sim::processGoldIncome(p, w.grid) == 0);
}

TEST_CASE("faithFromTradeRoute adds a flat bonus per active route after the multipliers") {
    const std::pair<aoc::sim::CivId, int32_t> civ =
        firstCivWith([](const CivilizationDef& d) { return d.modifiers.faithFromTradeRoute; });
    REQUIRE(civ.second > 0);

    aoc::test::World w = aoc::test::makeWorld(1);
    aoc::game::Player& p = *w.gameState.players()[0];
    p.setCivId(civ.first);   // no cities, so routes are the only faith source

    aoc::sim::accumulateFaith(p, w.grid);
    const float faithNone = p.faith().faith;
    addActiveTrader(w, PlayerId{0}, 3, 1);
    addActiveTrader(w, PlayerId{0}, 4, 1);
    aoc::sim::accumulateFaith(p, w.grid);
    CHECK(p.faith().faith - faithNone == doctest::Approx(2.0f * static_cast<float>(civ.second)));
}

TEST_CASE("a civ without the abilities gains nothing from routes") {
    aoc::test::World w = aoc::test::makeWorld(1);
    aoc::game::Player& p = *w.gameState.players()[0];
    for (uint8_t i = 0; i < aoc::sim::CIV_COUNT; ++i) {
        const CivilizationDef& d = aoc::sim::civDef(static_cast<aoc::sim::CivId>(i));
        if (d.modifiers.goldFromTradeRoute == 0 && d.modifiers.faithFromTradeRoute == 0) {
            p.setCivId(static_cast<aoc::sim::CivId>(i));
            break;
        }
    }
    p.monetary().system = aoc::sim::MonetarySystemType::CommodityMoney;
    const aoc::CurrencyAmount incomeNone = aoc::sim::processGoldIncome(p, w.grid);
    aoc::sim::accumulateFaith(p, w.grid);
    const float faithNone = p.faith().faith;
    addActiveTrader(w, PlayerId{0}, 3, 1);
    CHECK(aoc::sim::processGoldIncome(p, w.grid) == incomeNone);
    aoc::sim::accumulateFaith(p, w.grid);
    CHECK(p.faith().faith == doctest::Approx(faithNone));
}

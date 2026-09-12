/**
 * @file test_income_display.cpp
 * @brief The HUD's (+N) is what the turn did to the treasury, not gross income.
 *
 *        incomePerTurn() is set to gross income before the allocation split,
 *        maintenance and science funding, so the top bar could read "+100"
 *        while the treasury fell every turn.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/turn/TurnProcessor.hpp"

using aoc::PlayerId;

TEST_CASE("net gold last turn equals the treasury delta of the turn") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha").setPopulation(6);
    aoc::test::addCityAt(w, PlayerId{1}, 15, 9, "Beta").setPopulation(2);
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});
    p.monetary().system = aoc::sim::MonetarySystemType::CommodityMoney;
    p.setTreasury(400, aoc::sim::MoneyFlow::external());
    aoc::test::addUnitAt(w, PlayerId{0}, aoc::UnitTypeId{0}, 6, 5);
    aoc::test::addUnitAt(w, PlayerId{0}, aoc::UnitTypeId{0}, 4, 5);

    aoc::sim::EconomySimulation economy;
    aoc::sim::DiplomacyManager diplomacy;
    diplomacy.initialize(2);
    aoc::Random rng{7u};
    aoc::sim::TurnContext ctx;
    ctx.gameState   = &w.gameState;
    ctx.grid        = &w.grid;
    ctx.economy     = &economy;
    ctx.diplomacy   = &diplomacy;
    ctx.rng         = &rng;
    ctx.allPlayers  = {PlayerId{0}, PlayerId{1}};
    ctx.currentTurn = 1;

    const aoc::CurrencyAmount before = p.treasury();
    aoc::sim::processTurn(ctx);
    const aoc::CurrencyAmount after = p.treasury();

    CHECK(p.netGoldLastTurn() == after - before);
    CHECK(w.gameState.player(PlayerId{1})->netGoldLastTurn()
          == w.gameState.player(PlayerId{1})->treasury() - 0);
}

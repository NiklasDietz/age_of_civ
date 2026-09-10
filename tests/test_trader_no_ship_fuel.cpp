/**
 * @file test_trader_no_ship_fuel.cpp
 * @brief A Trader burns no coal just by existing.
 *
 *        The merchant-ship table mapped its Steamer onto UnitTypeId 30, which
 *        is the Trader, so every Trader on the map ate one coal per turn from
 *        its owner's cities (and every Caravan two oil).
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/turn/TurnProcessor.hpp"

using aoc::PlayerId;

TEST_CASE("an idle Trader consumes no coal") {
    aoc::test::World w    = aoc::test::makeWorld(1);
    aoc::game::City& city = aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    city.stockpile().addGoods(aoc::sim::goods::COAL, 5);
    aoc::test::addUnitAt(w, PlayerId{0}, aoc::UnitTypeId{30}, 5, 5);

    aoc::sim::EconomySimulation economy;
    aoc::sim::DiplomacyManager diplomacy;
    diplomacy.initialize(1);
    aoc::Random rng{5u};
    aoc::sim::TurnContext ctx;
    ctx.gameState   = &w.gameState;
    ctx.grid        = &w.grid;
    ctx.economy     = &economy;
    ctx.diplomacy   = &diplomacy;
    ctx.rng         = &rng;
    ctx.allPlayers  = {PlayerId{0}};
    ctx.currentTurn = 1;
    aoc::sim::processTurn(ctx);

    CHECK(city.stockpile().getAmount(aoc::sim::goods::COAL) == 5);
}

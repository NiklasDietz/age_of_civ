/**
 * @file test_trade_measurability.cpp
 * @brief The instruments the money programme reads before it changes anything:
 *        a DealAccepted event when a deal comes into force, an EmbargoDeclared
 *        event for blanket and per-good embargoes, a route-rejection code that
 *        names the rule which refused a Trader, and the per-turn wiring that
 *        gives the diplomacy manager the turn's event log.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/turn/TurnEventLog.hpp"
#include "aoc/simulation/turn/TurnProcessor.hpp"

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::sim::TurnEvent;
using aoc::sim::TurnEventType;

namespace {

constexpr PlayerId P0{0};
constexpr PlayerId P1{1};
constexpr aoc::UnitTypeId TRADER{30};

[[nodiscard]] std::vector<TurnEvent> eventsOf(const aoc::sim::TurnEventLog& log,
                                              TurnEventType type) {
    std::vector<TurnEvent> out;
    for (const TurnEvent& e : log.events()) {
        if (e.type == type) {
            out.push_back(e);
        }
    }
    return out;
}

/// Two civs, a city each, a diplomacy manager fed by an event log.
struct Pair {
    aoc::test::World world = aoc::test::makeWorld(2);
    aoc::sim::DiplomacyManager diplomacy;
    aoc::sim::TurnEventLog log;

    Pair() {
        aoc::test::addCityAt(world, P0, 5, 5, "Alpha");
        aoc::test::addCityAt(world, P1, 14, 8, "Beta");
        diplomacy.initialize(2);
        diplomacy.setEventLog(&log);
    }
};

} // namespace

TEST_CASE("a deal coming into force records DealAccepted with its parties and term count") {
    Pair f;
    aoc::sim::DiplomaticDeal deal{};
    deal.playerA = P0;
    deal.playerB = P1;
    aoc::sim::DealTerm pact{};
    pact.type       = aoc::sim::DealTermType::NonAggression;
    pact.fromPlayer = P0;
    pact.toPlayer   = P1;
    deal.terms.push_back(pact);

    aoc::sim::GlobalDealTracker& tracker = f.world.gameState.deals();
    REQUIRE(aoc::sim::proposeDeal(f.world.gameState, tracker, deal) == ErrorCode::Ok);
    const int32_t index = static_cast<int32_t>(tracker.activeDeals.size()) - 1;
    REQUIRE(aoc::sim::acceptDeal(f.world.gameState, f.world.grid, tracker, index, &f.diplomacy) ==
            ErrorCode::Ok);

    const std::vector<TurnEvent> accepted = eventsOf(f.log, TurnEventType::DealAccepted);
    REQUIRE(accepted.size() == 1);
    CHECK(accepted[0].player == P0);
    CHECK(accepted[0].otherPlayer == P1);
    CHECK(accepted[0].value1 == 1);
}

TEST_CASE("without an event log a deal is accepted silently") {
    Pair f;
    f.diplomacy.setEventLog(nullptr);
    aoc::sim::DiplomaticDeal deal{};
    deal.playerA = P0;
    deal.playerB = P1;
    aoc::sim::DealTerm pact{};
    pact.type       = aoc::sim::DealTermType::NonAggression;
    pact.fromPlayer = P0;
    pact.toPlayer   = P1;
    deal.terms.push_back(pact);
    aoc::sim::GlobalDealTracker& tracker = f.world.gameState.deals();
    REQUIRE(aoc::sim::proposeDeal(f.world.gameState, tracker, deal) == ErrorCode::Ok);
    CHECK(aoc::sim::acceptDeal(f.world.gameState, f.world.grid, tracker, 0, &f.diplomacy) ==
          ErrorCode::Ok);
    CHECK(f.log.eventCount() == 0);
}

TEST_CASE("a blanket embargo records EmbargoDeclared with -1, a resource embargo the good") {
    Pair f;
    f.diplomacy.setEmbargo(P0, P1, true);
    f.diplomacy.setResourceEmbargo(P1, P0, aoc::sim::goods::SILK, true);

    const std::vector<TurnEvent> declared = eventsOf(f.log, TurnEventType::EmbargoDeclared);
    REQUIRE(declared.size() == 2);
    CHECK(declared[0].player == P0);
    CHECK(declared[0].otherPlayer == P1);
    CHECK(declared[0].value1 == -1);
    CHECK(declared[1].player == P1);
    CHECK(declared[1].otherPlayer == P0);
    CHECK(declared[1].value1 == static_cast<int32_t>(aoc::sim::goods::SILK));
}

TEST_CASE("lifting an embargo records nothing") {
    Pair f;
    f.diplomacy.setEmbargo(P0, P1, true);
    f.diplomacy.setResourceEmbargo(P0, P1, aoc::sim::goods::SILK, true);
    f.log.clear();
    f.diplomacy.setEmbargo(P0, P1, false);
    f.diplomacy.setResourceEmbargo(P0, P1, aoc::sim::goods::SILK, false);
    CHECK(f.log.eventCount() == 0);
}

TEST_CASE("processTurn hands the diplomacy manager the turn's event log, or null") {
    aoc::test::World w = aoc::test::makeWorld(1);
    aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::sim::EconomySimulation economy;
    aoc::sim::DiplomacyManager diplomacy;
    diplomacy.initialize(1);
    aoc::sim::TurnEventLog log;
    aoc::Random rng{5u};
    aoc::sim::TurnContext ctx;
    ctx.gameState   = &w.gameState;
    ctx.grid        = &w.grid;
    ctx.economy     = &economy;
    ctx.diplomacy   = &diplomacy;
    ctx.rng         = &rng;
    ctx.allPlayers  = {P0};
    ctx.currentTurn = 1;

    ctx.eventLog = &log;
    aoc::sim::processTurn(ctx);
    CHECK(diplomacy.eventLog() == &log);

    ctx.eventLog    = nullptr;
    ctx.currentTurn = 2;
    aoc::sim::processTurn(ctx);
    CHECK(diplomacy.eventLog() == nullptr);
}

TEST_CASE("a Trader refused for want of a slot is told so, not 'invalid argument'") {
    aoc::test::World w   = aoc::test::makeWorld(1, 30, 16);
    aoc::game::Player& p = *w.gameState.player(P0);
    aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::game::City& beta = aoc::test::addCityAt(w, P0, 9, 5, "Beta");
    aoc::sim::DiplomacyManager diplomacy;
    diplomacy.initialize(1);
    aoc::sim::Market market;

    const int32_t slots = aoc::sim::computeTotalTradeSlots(p, w.grid);
    REQUIRE(slots > 0);
    for (int32_t i = 0; i < slots; ++i) {
        aoc::test::addUnitAt(w, P0, TRADER, 5, 6).trader().owner = P0; // already on a route
    }
    aoc::game::Unit& idle = aoc::test::addUnitAt(w, P0, TRADER, 5, 5);
    CHECK(aoc::sim::establishTradeRoute(w.gameState, w.grid, market, &diplomacy, idle, beta) ==
          ErrorCode::TradeRouteCapReached);
    CHECK(idle.trader().owner == aoc::INVALID_PLAYER);
}

TEST_CASE("a Trader sent to a civ at war with its owner is refused for consent") {
    aoc::test::World w = aoc::test::makeWorld(2, 30, 16);
    aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::game::City& beta = aoc::test::addCityAt(w, P1, 9, 5, "Beta");
    aoc::sim::DiplomacyManager diplomacy;
    diplomacy.initialize(2);
    diplomacy.declareWar(P1, P0);
    aoc::sim::Market market;
    aoc::game::Unit& idle = aoc::test::addUnitAt(w, P0, TRADER, 5, 5);
    CHECK(aoc::sim::establishTradeRoute(w.gameState, w.grid, market, &diplomacy, idle, beta) ==
          ErrorCode::TradeRouteRefusedConsent);
}

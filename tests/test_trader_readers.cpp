/**
 * @file test_trader_readers.cpp
 * @brief The readers that walked the legacy GameState::tradeRoutes() list,
 *        which no headless game ever filled, now read Trader units: the
 *        city-state SendTradeRoute quest and the CSI trade statistics.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/victory/VictoryCondition.hpp"

#include <string>

using aoc::PlayerId;
using aoc::sim::CityStateComponent;
using aoc::sim::CityStateQuestType;
using aoc::sim::CityStateType;

namespace {

constexpr aoc::UnitTypeId TRADER{30};
constexpr PlayerId P0{0};
constexpr PlayerId P1{1};

std::size_t addCityState(aoc::test::World& w, uint8_t defId, CityStateType type, int32_t q,
                         int32_t r) {
    const std::size_t index = w.gameState.cityStates().size();
    CityStateComponent cs{};
    cs.defId    = defId;
    cs.type     = type;
    cs.location = {q, r};
    cs.envoys.fill(0);
    cs.suzerain = aoc::INVALID_PLAYER;
    w.gameState.cityStates().push_back(cs);
    aoc::game::Player* seat =
        w.gameState.player(static_cast<PlayerId>(aoc::sim::CITY_STATE_PLAYER_BASE + index));
    REQUIRE(seat != nullptr);
    seat->addCity({q, r}, std::string(aoc::sim::CITY_STATE_DEFS[defId].name));
    return index;
}

/// A Trader with a live route from `owner` to a city of `destOwner`.
aoc::game::Unit& sendTrader(aoc::test::World& w, PlayerId owner, PlayerId destOwner, int32_t q,
                            int32_t r) {
    aoc::game::Unit& t   = aoc::test::addUnitAt(w, owner, TRADER, q, r);
    t.trader().owner     = owner;
    t.trader().destOwner = destOwner;
    return t;
}

} // namespace

TEST_CASE("a Trader bound for the city-state completes its SendTradeRoute quest") {
    aoc::test::World w = aoc::test::makeWorld(2);
    w.gameState.initializeCityStateSlots(1);
    const std::size_t index = addCityState(w, 0, CityStateType::Militaristic, 10, 8);
    aoc::test::addCityAt(w, P0, 16, 6, "Home");
    const PlayerId csPlayer = static_cast<PlayerId>(aoc::sim::CITY_STATE_PLAYER_BASE + index);
    CityStateComponent& cs  = w.gameState.cityStates()[index];
    cs.setMet(P0);
    cs.activeQuest            = {};
    cs.activeQuest.type       = CityStateQuestType::SendTradeRoute;
    cs.activeQuest.assignedTo = P0;
    cs.activeQuest.isActive   = true;

    aoc::sim::checkCityStateQuests(w.gameState);
    CHECK(cs.activeQuest.isActive); // no Trader yet

    sendTrader(w, P0, P1, 16, 7); // a route to another civ is not this quest
    aoc::sim::checkCityStateQuests(w.gameState);
    CHECK(cs.activeQuest.isActive);

    sendTrader(w, P0, csPlayer, 16, 8);
    aoc::sim::checkCityStateQuests(w.gameState);
    CHECK_FALSE(cs.activeQuest.isActive);
    CHECK(cs.envoys[0] > 0);
}

TEST_CASE("CSI trade statistics come from the Traders on the road") {
    float withoutTrader = 0.0f;
    {
        aoc::test::World w = aoc::test::makeWorld(2);
        aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
        aoc::test::addCityAt(w, P1, 14, 8, "Beta");
        aoc::sim::EconomySimulation economy;
        aoc::sim::DiplomacyManager diplomacy;
        diplomacy.initialize(2);
        aoc::sim::computeCSI(w.gameState, w.grid, economy, &diplomacy);
        withoutTrader = w.gameState.player(P0)->victoryTracker().compositeCSI;
    }
    float withTrader = 0.0f;
    {
        aoc::test::World w = aoc::test::makeWorld(2);
        aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
        aoc::test::addCityAt(w, P1, 14, 8, "Beta");
        aoc::game::Unit& trader = sendTrader(w, P0, P1, 6, 5);
        trader.trader().cargo.push_back({aoc::sim::goods::WHEAT, 10});
        aoc::sim::EconomySimulation economy;
        aoc::sim::DiplomacyManager diplomacy;
        diplomacy.initialize(2);
        aoc::sim::computeCSI(w.gameState, w.grid, economy, &diplomacy);
        withTrader = w.gameState.player(P0)->victoryTracker().compositeCSI;
    }
    // The cargo is trade volume for its owner alone, so the owner's score
    // rises relative to the twin without a route.
    CHECK(withTrader > withoutTrader);
}

TEST_CASE("a Trader bound for a city-state is trade volume but not a CSI partner") {
    aoc::test::World w = aoc::test::makeWorld(2);
    w.gameState.initializeCityStateSlots(1);
    const std::size_t index = addCityState(w, 0, CityStateType::Militaristic, 10, 8);
    aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::test::addCityAt(w, P1, 14, 8, "Beta");
    const PlayerId csPlayer = static_cast<PlayerId>(aoc::sim::CITY_STATE_PLAYER_BASE + index);
    sendTrader(w, P0, csPlayer, 6, 5);
    aoc::sim::EconomySimulation economy;
    aoc::sim::DiplomacyManager diplomacy;
    diplomacy.initialize(2);
    aoc::sim::computeCSI(w.gameState, w.grid, economy, &diplomacy);
    // The city-state seat must not have been scored as a player.
    CHECK(w.gameState.player(csPlayer)->victoryTracker().compositeCSI == 0.0f);
    CHECK(w.gameState.player(P0)->victoryTracker().compositeCSI > 0.0f);
}

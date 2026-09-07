/**
 * @file test_route_cancellation.cpp
 * @brief A trade route to a city that changes hands is ended, so its trader
 *        stops paying the new owner. No route-cancellation existed anywhere in
 *        the repo: `trader.destOwner` was a snapshot from when the route was
 *        established while the arrival path read the destination's live owner,
 *        so after a conquest the loser's trader kept walking to the same tile
 *        and unloaded into the conqueror's stockpile.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

using aoc::PlayerId;
using aoc::UnitTypeId;

namespace {

constexpr UnitTypeId TRADER{30};
constexpr aoc::hex::AxialCoord HOME{5, 5};
constexpr aoc::hex::AxialCoord TARGET{15, 9};

/// Player 0's trader, outbound from HOME to TARGET (owned by player 1).
aoc::game::Unit& outboundTrader(aoc::test::World& w) {
    aoc::game::Unit& t            = aoc::test::addUnitAt(w, PlayerId{0}, TRADER, 8, 6);
    aoc::sim::TraderComponent& tc = t.trader();
    tc.owner                      = PlayerId{0};
    tc.originCityLocation         = HOME;
    tc.destCityLocation           = TARGET;
    tc.destOwner                  = PlayerId{1};
    tc.isReturning                = false;
    tc.path                       = {{8, 6}, {10, 7}, {12, 8}, TARGET};
    tc.pathIndex                  = 1;
    tc.cargo.push_back({aoc::sim::goods::WINE, 5});
    t.autoRenewRoute = true;
    return t;
}

/// A world with player 0 at HOME and player 1 at TARGET.
aoc::test::World twoCityWorld() {
    aoc::test::World w = aoc::test::makeWorld(3);
    aoc::test::addCityAt(w, PlayerId{0}, HOME.q, HOME.r, "Home");
    aoc::test::addCityAt(w, PlayerId{1}, TARGET.q, TARGET.r, "Target");
    return w;
}

} // namespace

TEST_CASE("a trader outbound to a captured city turns around and keeps its cargo") {
    aoc::test::World w            = twoCityWorld();
    aoc::game::Unit& trader       = outboundTrader(w);
    const std::size_t cargoBefore = trader.trader().cargo.size();

    // Player 2 takes the destination.
    REQUIRE(w.gameState.transferCity(TARGET, PlayerId{2}) != nullptr);

    // The route is ended: heading home, not renewing, and its stale destOwner
    // no longer names the old partner.
    CHECK(trader.trader().isReturning);
    CHECK_FALSE(trader.autoRenewRoute);
    CHECK(trader.trader().destOwner == PlayerId{2});
    // The cargo goes home with it rather than to the conqueror.
    CHECK(trader.trader().cargo.size() == cargoBefore);
    // It still exists -- it has a home to walk back to.
    CHECK(w.gameState.player(PlayerId{0})->unitAt({8, 6}) != nullptr);
}

TEST_CASE("a trader whose own home is captured is removed") {
    aoc::test::World w = twoCityWorld();
    outboundTrader(w);
    aoc::game::Player& owner = *w.gameState.player(PlayerId{0});
    REQUIRE(owner.units().size() == 1);

    // Player 2 takes the trader's ORIGIN. There is nowhere to carry cargo back to.
    REQUIRE(w.gameState.transferCity(HOME, PlayerId{2}) != nullptr);

    CHECK(owner.units().empty());
}

TEST_CASE("the new owner's own traders are left alone") {
    // After the capture the route is internal to the conqueror, which is fine.
    aoc::test::World w      = twoCityWorld();
    aoc::game::Unit& trader = outboundTrader(w);

    // Player 0 -- the trader's own owner -- takes the destination.
    REQUIRE(w.gameState.transferCity(TARGET, PlayerId{0}) != nullptr);

    CHECK_FALSE(trader.trader().isReturning); // still outbound
    CHECK(trader.autoRenewRoute);             // still renewing
}

TEST_CASE("routes that touch neither city are untouched") {
    aoc::test::World w = twoCityWorld();
    aoc::test::addCityAt(w, PlayerId{2}, 3, 12, "Elsewhere");
    aoc::game::Unit& trader = outboundTrader(w);

    // Some unrelated city changes hands.
    REQUIRE(w.gameState.transferCity({3, 12}, PlayerId{1}) != nullptr);

    CHECK_FALSE(trader.trader().isReturning);
    CHECK(trader.autoRenewRoute);
    CHECK(trader.trader().destOwner == PlayerId{1}); // unchanged snapshot
}

TEST_CASE("cancelRoutesToCity reports how many routes it ended") {
    aoc::test::World w = twoCityWorld();
    outboundTrader(w);
    outboundTrader(w); // a second trader on the same route

    const int32_t ended = aoc::sim::cancelRoutesToCity(w.gameState, TARGET, PlayerId{2});
    CHECK(ended == 2);

    // Nothing left to cancel the second time.
    CHECK(aoc::sim::cancelRoutesToCity(w.gameState, TARGET, PlayerId{2}) == 0);
}

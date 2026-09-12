/**
 * @file test_trade_interdependence.cpp
 * @brief Trade ties the courts together (plan B6, 4.1): a foreign delivery
 *        refreshes a "Trade partner" modifier on both directions rather than
 *        stacking one per shipment, it decays once the deliveries stop, and
 *        the IsTradePartner agenda, which answered false for every leader and
 *        every target, now reads the routes actually running.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/greatpeople/GreatPeople.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/monetary/MoneyFlow.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

#include <string>

using aoc::PlayerId;
using aoc::sim::TRADE_PARTNER_MAX;
using aoc::sim::TRADE_PARTNER_PER_ROUTE;
using aoc::sim::TRADE_PARTNER_TURNS;

namespace {

constexpr PlayerId P0{0};
constexpr PlayerId P1{1};
constexpr aoc::UnitTypeId TRADER{30};
const std::string REASON = aoc::sim::TRADE_PARTNER_REASON;

/// An idle Trader given a route to `dest` without going through the whole
/// establish path: enough for routesBetween, which reads owner and destOwner.
aoc::game::Unit& routeTo(aoc::test::World& w, PlayerId owner, PlayerId dest, int32_t q, int32_t r) {
    aoc::game::Unit& t   = aoc::test::addUnitAt(w, owner, TRADER, q, r);
    t.trader().owner     = owner;
    t.trader().destOwner = dest;
    return t;
}

/// Two civs, met, with a real cargo to sell and a buyer who can pay.
struct Partners {
    aoc::test::World w = aoc::test::makeWorld(2, 40, 24);
    aoc::sim::DiplomacyManager d;
    aoc::sim::Market market;
    aoc::game::City* home   = nullptr;
    aoc::game::City* abroad = nullptr;

    explicit Partners(int32_t cargo = 40) {
        this->d.initialize(2);
        this->d.meetPlayers(P0, P1, 1);
        this->market.initialize();
        this->home   = &aoc::test::addCityAt(this->w, P0, 5, 5, "Alpha");
        this->abroad = &aoc::test::addCityAt(this->w, P1, 11, 5, "Beta");
        this->abroad->setPopulation(20);
        this->home->stockpile().addGoods(aoc::sim::goods::WHEAT, cargo);
        aoc::game::Player& seller = *this->w.gameState.player(P0);
        aoc::game::Player& buyer  = *this->w.gameState.player(P1);
        seller.monetary().system  = aoc::sim::MonetarySystemType::CommodityMoney;
        buyer.monetary().system   = aoc::sim::MonetarySystemType::CommodityMoney;
        buyer.monetary().privateSpecie = 1000000;
        buyer.tariffs().importTariffRate = 0.0f; // customs are 3.3's subject
    }

    /// Establish a route and run it until the delivery is made.
    aoc::game::Unit& deliver() {
        aoc::game::Unit& unit = aoc::test::addUnitAt(this->w, P0, TRADER, 5, 5);
        REQUIRE(aoc::sim::establishTradeRoute(this->w.gameState, this->w.grid, this->market, &this->d,
                                              unit, *this->abroad) == aoc::ErrorCode::Ok);
        for (int32_t turn = 0; turn < 20 && !unit.trader().isReturning; ++turn) {
            aoc::sim::processTradeRoutes(this->w.gameState, this->w.grid, this->market, &this->d);
        }
        REQUIRE(unit.trader().isReturning); // the sale happened
        return unit;
    }

    [[nodiscard]] int32_t worth(PlayerId a, PlayerId b) const {
        return this->d.modifierAmount(a, b, REASON);
    }
    [[nodiscard]] std::size_t count(PlayerId a, PlayerId b) const {
        std::size_t n = 0;
        for (const aoc::sim::RelationModifier& m : this->d.relation(a, b).modifiers) {
            n += (m.reason == REASON) ? 1 : 0;
        }
        return n;
    }
};

} // namespace

TEST_CASE("routes between two civs are counted either way, and only live ones") {
    aoc::test::World w = aoc::test::makeWorld(3);
    CHECK(aoc::sim::routesBetween(w.gameState, P0, P1) == 0);

    routeTo(w, P0, P1, 5, 5);
    CHECK(aoc::sim::routesBetween(w.gameState, P0, P1) == 1);
    CHECK(aoc::sim::routesBetween(w.gameState, P1, P0) == 1); // the pair, not the direction

    routeTo(w, P1, P0, 9, 9); // their trader coming the other way
    CHECK(aoc::sim::routesBetween(w.gameState, P0, P1) == 2);

    routeTo(w, P0, PlayerId{2}, 6, 5); // a route to a third civ is not this pair's
    CHECK(aoc::sim::routesBetween(w.gameState, P0, P1) == 2);

    aoc::game::Unit& idle = aoc::test::addUnitAt(w, P0, TRADER, 7, 5);
    idle.trader().destOwner = P1; // never dispatched: owner still unset
    CHECK(aoc::sim::routesBetween(w.gameState, P0, P1) == 2);

    CHECK(aoc::sim::routesBetween(w.gameState, P0, P0) == 0);
    CHECK(aoc::sim::routesBetween(w.gameState, P0, aoc::INVALID_PLAYER) == 0);
}

TEST_CASE("a named modifier is refreshed in place on both directions, never stacked") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::sim::DiplomacyManager d;
    d.initialize(2);
    CHECK(d.modifierAmount(P0, P1, REASON) == 0); // absent reads as nothing

    d.refreshModifier(P0, P1, REASON, 4, TRADE_PARTNER_TURNS);
    CHECK(d.modifierAmount(P0, P1, REASON) == 4);
    CHECK(d.modifierAmount(P1, P0, REASON) == 4); // both courts
    CHECK(d.relation(P0, P1).modifiers.size() == 1);

    d.refreshModifier(P0, P1, REASON, 9, TRADE_PARTNER_TURNS);
    CHECK(d.modifierAmount(P0, P1, REASON) == 9);
    CHECK(d.relation(P0, P1).modifiers.size() == 1); // replaced, not appended
    CHECK(d.relation(P1, P0).modifiers.size() == 1);

    // It survives to the last tick and is gone on the one after.
    for (int32_t turn = 0; turn < TRADE_PARTNER_TURNS - 1; ++turn) {
        d.tickModifiers();
    }
    CHECK(d.modifierAmount(P0, P1, REASON) == 9);
    d.tickModifiers();
    CHECK(d.modifierAmount(P0, P1, REASON) == 0);
    CHECK(d.relation(P1, P0).modifiers.empty());
}

TEST_CASE("a delivery makes the two courts partners, and the tie decays when the trade stops") {
    Partners p;
    CHECK(p.worth(P0, P1) == 0);
    p.deliver();

    const int32_t tie = p.worth(P0, P1);
    CHECK(tie > 0);
    CHECK(tie <= TRADE_PARTNER_MAX);
    CHECK(tie >= TRADE_PARTNER_PER_ROUTE); // the route itself is worth something
    CHECK(p.worth(P1, P0) == tie);         // and both sides feel it
    CHECK(p.count(P0, P1) == 1);

    // Twenty quiet turns and the goodwill is spent.
    for (int32_t turn = 0; turn < TRADE_PARTNER_TURNS; ++turn) {
        p.d.tickModifiers();
    }
    CHECK(p.worth(P0, P1) == 0);
}

TEST_CASE("a standing route is a standing bonus, not an accumulating one") {
    Partners p;
    p.deliver();
    const int32_t first = p.worth(P0, P1);
    p.d.tickModifiers(); // a turn passes
    p.deliver();         // a second trader delivers
    CHECK(p.count(P0, P1) == 1);
    CHECK(p.count(P1, P0) == 1);
    CHECK(p.worth(P0, P1) >= first); // more routes, no less goodwill
    CHECK(p.worth(P0, P1) <= TRADE_PARTNER_MAX);
}

TEST_CASE("the goodwill a trade tie buys is capped, however many routes run") {
    // One delivery cannot reach the cap on its own: a wagon carries two slots
    // of twelve units, so a shipment is worth about a point of goodwill
    // whatever the seller has in store. What can reach it is a web of routes,
    // at two points each, which is what the cap exists to bound.
    Partners lone;
    lone.deliver();
    CHECK(lone.worth(P0, P1) < TRADE_PARTNER_MAX);

    Partners busy;
    // Those lanes are live routes and take the civ's slots; a Great Merchant's
    // are what leave room for the delivering one.
    busy.w.gameState.player(P0)->greatPeople().extraTradeSlots = 20;
    for (int32_t i = 0; i < 7; ++i) {
        routeTo(busy.w, P0, P1, 6 + i, 7); // seven more lanes between the pair
    }
    busy.deliver();
    CHECK(aoc::sim::routesBetween(busy.w.gameState, P0, P1) * TRADE_PARTNER_PER_ROUTE >
          TRADE_PARTNER_MAX); // the uncapped figure really does overshoot
    CHECK(busy.worth(P0, P1) == TRADE_PARTNER_MAX);
    CHECK(busy.worth(P1, P0) == TRADE_PARTNER_MAX);
}

TEST_CASE("the trade-partner agenda reads the routes actually running") {
    // The condition answered false for every leader and every target before
    // 4.1, so the agenda existed on paper only.
    aoc::sim::CivId trader{0};
    bool found = false;
    for (uint8_t i = 0; i < aoc::sim::CIV_COUNT && !found; ++i) {
        const aoc::sim::CivId id = static_cast<aoc::sim::CivId>(i);
        if (aoc::sim::leaderPersonality(id).likeCondition == aoc::sim::AgendaCondition::IsTradePartner) {
            trader = id;
            found  = true;
        }
    }
    REQUIRE(found);

    aoc::test::World w = aoc::test::makeWorld(2);
    w.gameState.player(P0)->setCivId(trader);
    aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::test::addCityAt(w, P1, 14, 8, "Beta");
    const int32_t strangers = aoc::sim::evaluateAgenda(w.gameState, P0, P1);

    routeTo(w, P0, P1, 6, 5);
    const int32_t partners = aoc::sim::evaluateAgenda(w.gameState, P0, P1);
    CHECK(partners - strangers == 15); // the like condition, and only it, changed
}

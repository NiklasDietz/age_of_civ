/**
 * @file test_embargo_direction.cpp
 * @brief Embargoes have a direction. setEmbargo used to write both halves of
 *        the pair, and there was no way to express a one-sided refusal, so a
 *        World Congress sanction against one civ fabricated N reciprocal
 *        embargoes: the sanctioned civ automatically embargoed everyone who had
 *        voted against it. FIXLIST H1.8 asked for the split.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/WorldCongress.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

using aoc::PlayerId;

namespace {

constexpr uint8_t SEATS = 4;
constexpr aoc::UnitTypeId TRADER{30};
constexpr uint16_t WHEAT = aoc::sim::goods::WHEAT;

} // namespace

TEST_CASE("an embargo binds only the civ that declared it") {
    aoc::sim::DiplomacyManager dip;
    dip.initialize(SEATS);

    dip.setEmbargo(PlayerId{0}, PlayerId{1}, true);

    CHECK(dip.hasEmbargo(PlayerId{0}, PlayerId{1}));       // 0 refuses to trade with 1
    CHECK_FALSE(dip.hasEmbargo(PlayerId{1}, PlayerId{0})); // 1 has decided nothing
}

TEST_CASE("either side's embargo blocks trade between the two") {
    // A route needs both ends willing, so trade code asks hasAnyEmbargo.
    aoc::sim::DiplomacyManager dip;
    dip.initialize(SEATS);
    CHECK_FALSE(dip.hasAnyEmbargo(PlayerId{0}, PlayerId{1}));

    dip.setEmbargo(PlayerId{1}, PlayerId{0}, true); // only the second civ refuses
    CHECK(dip.hasAnyEmbargo(PlayerId{0}, PlayerId{1}));
    CHECK(dip.hasAnyEmbargo(PlayerId{1}, PlayerId{0})); // symmetric question
}

TEST_CASE("lifting one direction leaves the other standing") {
    aoc::sim::DiplomacyManager dip;
    dip.initialize(SEATS);

    dip.setEmbargo(PlayerId{0}, PlayerId{1}, true);
    dip.setEmbargo(PlayerId{1}, PlayerId{0}, true);
    REQUIRE(dip.hasEmbargo(PlayerId{0}, PlayerId{1}));
    REQUIRE(dip.hasEmbargo(PlayerId{1}, PlayerId{0}));

    dip.setEmbargo(PlayerId{0}, PlayerId{1}, false);
    CHECK_FALSE(dip.hasEmbargo(PlayerId{0}, PlayerId{1}));
    CHECK(dip.hasEmbargo(PlayerId{1}, PlayerId{0})); // still refusing
    CHECK(dip.hasAnyEmbargo(PlayerId{0}, PlayerId{1}));
}

TEST_CASE("a mutual embargo is available when both sides really do refuse") {
    aoc::sim::DiplomacyManager dip;
    dip.initialize(SEATS);

    dip.setMutualEmbargo(PlayerId{0}, PlayerId{1}, true);
    CHECK(dip.hasEmbargo(PlayerId{0}, PlayerId{1}));
    CHECK(dip.hasEmbargo(PlayerId{1}, PlayerId{0}));

    dip.setMutualEmbargo(PlayerId{0}, PlayerId{1}, false);
    CHECK_FALSE(dip.hasAnyEmbargo(PlayerId{0}, PlayerId{1}));
}

TEST_CASE("a Congress sanction does not make the target embargo the world back") {
    // The fabrication this split exists to stop: sanctioning one civ used to
    // create an embargo in BOTH directions for every other seat.
    aoc::test::World w = aoc::test::makeWorld(SEATS);
    aoc::sim::DiplomacyManager dip;
    dip.initialize(SEATS);

    constexpr PlayerId TARGET{2};
    aoc::sim::applySanctionsBegin(&dip, w.gameState, TARGET);

    for (uint8_t p = 0; p < SEATS; ++p) {
        const PlayerId id{p};
        if (id == TARGET) {
            continue;
        }
        // Everyone else refuses to trade with the sanctioned civ...
        CHECK(dip.hasEmbargo(id, TARGET));
        // ...and the sanctioned civ has not thereby refused them.
        CHECK_FALSE(dip.hasEmbargo(TARGET, id));
    }
}

TEST_CASE("lifting a sanction clears what it set") {
    aoc::test::World w = aoc::test::makeWorld(SEATS);
    aoc::sim::DiplomacyManager dip;
    dip.initialize(SEATS);

    constexpr PlayerId TARGET{2};
    aoc::sim::applySanctionsBegin(&dip, w.gameState, TARGET);
    aoc::sim::applySanctionsEnd(&dip, w.gameState, TARGET);

    for (uint8_t p = 0; p < SEATS; ++p) {
        const PlayerId id{p};
        if (id == TARGET) {
            continue;
        }
        CHECK_FALSE(dip.hasEmbargo(id, TARGET));
    }
}

TEST_CASE("a per-good embargo binds only the civ that declared it") {
    // It used to be written into both halves of the pair, so a civ denying a
    // rival its iron also stopped the rival selling iron back: one court's
    // leverage became a boycott neither had chosen.
    aoc::sim::DiplomacyManager dip;
    dip.initialize(SEATS);

    dip.setResourceEmbargo(PlayerId{0}, PlayerId{1}, WHEAT, true);
    CHECK(dip.hasResourceEmbargo(PlayerId{0}, PlayerId{1}, WHEAT));
    CHECK_FALSE(dip.hasResourceEmbargo(PlayerId{1}, PlayerId{0}, WHEAT));

    // Declaring it twice is still one refusal.
    dip.setResourceEmbargo(PlayerId{0}, PlayerId{1}, WHEAT, true);
    CHECK(dip.relation(PlayerId{0}, PlayerId{1}).embargoedGoods.size() == 1);

    // And lifting touches only the side that declared it.
    dip.setResourceEmbargo(PlayerId{1}, PlayerId{0}, WHEAT, true);
    dip.setResourceEmbargo(PlayerId{0}, PlayerId{1}, WHEAT, false);
    CHECK_FALSE(dip.hasResourceEmbargo(PlayerId{0}, PlayerId{1}, WHEAT));
    CHECK(dip.hasResourceEmbargo(PlayerId{1}, PlayerId{0}, WHEAT));
}

TEST_CASE("a blanket embargo answers for every good in that direction") {
    aoc::sim::DiplomacyManager dip;
    dip.initialize(SEATS);
    dip.setEmbargo(PlayerId{0}, PlayerId{1}, true);
    CHECK(dip.hasResourceEmbargo(PlayerId{0}, PlayerId{1}, WHEAT));
    CHECK_FALSE(dip.hasResourceEmbargo(PlayerId{1}, PlayerId{0}, WHEAT));
}

TEST_CASE("an embargo ends the routes between the pair and leaves the rest running") {
    aoc::test::World w = aoc::test::makeWorld(3);
    aoc::game::City& home  = aoc::test::addCityAt(w, PlayerId{0}, 4, 4, "Alpha");
    aoc::game::City& beta  = aoc::test::addCityAt(w, PlayerId{1}, 14, 8, "Beta");
    aoc::game::City& gamma = aoc::test::addCityAt(w, PlayerId{2}, 20, 12, "Gamma");

    // auto required: the helper returns a reference into the unit store.
    auto route = [&](aoc::game::City& from, aoc::game::City& to, int32_t q,
                     int32_t r) -> aoc::game::Unit& {
        aoc::game::Unit& u            = aoc::test::addUnitAt(w, PlayerId{0}, TRADER, q, r);
        u.trader().owner              = PlayerId{0};
        u.trader().destOwner          = to.owner();
        u.trader().originCityLocation = from.location();
        u.trader().destCityLocation   = to.location();
        u.autoRenewRoute              = true;
        return u;
    };
    aoc::game::Unit& banned = route(home, beta, 5, 4);
    aoc::game::Unit& spared = route(home, gamma, 6, 4);

    CHECK(aoc::sim::cancelRoutesBetween(w.gameState, PlayerId{0}, PlayerId{1}) == 1);
    CHECK_FALSE(banned.autoRenewRoute); // turned round, and it will not renew
    CHECK(banned.trader().isReturning);
    CHECK(spared.autoRenewRoute); // a third civ's lane is nobody else's quarrel
    CHECK_FALSE(spared.trader().isReturning);

    CHECK(aoc::sim::cancelRoutesBetween(w.gameState, PlayerId{0}, PlayerId{0}) == 0);
    CHECK(aoc::sim::cancelRoutesBetween(w.gameState, PlayerId{0}, aoc::INVALID_PLAYER) == 0);
}

TEST_CASE("a shipper does not load what the receiving court refuses") {
    // The cargo would only be seized at customs, so loading it means hauling
    // goods across the map to lose them at the gate.
    aoc::test::World w = aoc::test::makeWorld(2, 40, 24);
    aoc::sim::DiplomacyManager dip;
    dip.initialize(2);
    dip.meetPlayers(PlayerId{0}, PlayerId{1}, 1);
    aoc::sim::Market market;
    market.initialize();

    aoc::game::City& home   = aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha");
    aoc::game::City& abroad = aoc::test::addCityAt(w, PlayerId{1}, 11, 5, "Beta");
    abroad.setPopulation(20);
    home.stockpile().addGoods(WHEAT, 40);

    SUBCASE("with no embargo the wagon loads") {
        aoc::game::Unit& unit = aoc::test::addUnitAt(w, PlayerId{0}, TRADER, 5, 5);
        REQUIRE(aoc::sim::establishTradeRoute(w.gameState, w.grid, market, &dip, unit, abroad) ==
                aoc::ErrorCode::Ok);
        CHECK_FALSE(unit.trader().cargo.empty());
    }

    SUBCASE("the buyer's own refusal is read, not just the seller's") {
        dip.setResourceEmbargo(PlayerId{1}, PlayerId{0}, WHEAT, true);
        aoc::game::Unit& unit = aoc::test::addUnitAt(w, PlayerId{0}, TRADER, 5, 5);
        REQUIRE(aoc::sim::establishTradeRoute(w.gameState, w.grid, market, &dip, unit, abroad) ==
                aoc::ErrorCode::Ok);
        CHECK(unit.trader().cargo.empty());
    }

    SUBCASE("and so is the seller's own") {
        dip.setResourceEmbargo(PlayerId{0}, PlayerId{1}, WHEAT, true);
        aoc::game::Unit& unit = aoc::test::addUnitAt(w, PlayerId{0}, TRADER, 5, 5);
        REQUIRE(aoc::sim::establishTradeRoute(w.gameState, w.grid, market, &dip, unit, abroad) ==
                aoc::ErrorCode::Ok);
        CHECK(unit.trader().cargo.empty());
    }
}

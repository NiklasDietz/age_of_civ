/**
 * @file test_money_good.cpp
 * @brief Adoption as a decision (Mengerian plan, Phase C): requestSetMoneyGood
 *        elects the good a civ prices in, every refusal leaves the state
 *        exactly as it was, and a civ can be driven barter -> silver -> fiat
 *        by request alone.
 *
 *        This phase is the MECHANISM only. Nothing here consults the
 *        saleability score and the AI does not call the request yet; that is
 *        Phase D. What is pinned here is who may change the money good, when,
 *        and that a denial costs nothing.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/monetary/MonetaryActions.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::sim::MonetarySystemType;
using aoc::sim::MONEY_GOOD_DWELL_TURNS;
using aoc::sim::NO_MONEY_GOOD;
using aoc::sim::goods::SILK;
using aoc::sim::goods::SILVER_ORE;

namespace {

constexpr PlayerId P0{0};
constexpr aoc::UnitTypeId TRADER{30};
constexpr aoc::TechId BANKING{9};
constexpr aoc::TechId ECONOMICS{13};

/// A civ holding silver and silk, off cooldown, so the request is the only
/// thing under test.
struct Holder {
    aoc::test::World world = aoc::test::makeWorld(2);
    aoc::game::Player& p   = *world.gameState.player(P0);

    Holder() {
        aoc::game::City& c = aoc::test::addCityAt(world, P0, 5, 5, "Alpha");
        c.stockpile().addGoods(SILVER_ORE, 40);
        c.stockpile().addGoods(SILK, 10);
        p.monetary().turnsWithCurrentMoneyGood = MONEY_GOOD_DWELL_TURNS;
    }

    [[nodiscard]] ErrorCode set(uint8_t good) {
        return aoc::sim::requestSetMoneyGood(world.gameState, P0, good);
    }
};

/// Everything a refusal must not disturb.
[[nodiscard]] bool same(const aoc::sim::MonetaryStateComponent& a,
                        const aoc::sim::MonetaryStateComponent& b) {
    return a.moneyGood == b.moneyGood &&
           a.turnsWithCurrentMoneyGood == b.turnsWithCurrentMoneyGood && a.system == b.system &&
           a.privateSpecie == b.privateSpecie && a.bullion == b.bullion;
}

} // namespace

TEST_CASE("a civ elects a good it holds and the dwell clock restarts") {
    Holder h;
    REQUIRE(h.p.monetary().moneyGood == NO_MONEY_GOOD);
    REQUIRE(h.set(static_cast<uint8_t>(SILVER_ORE)) == ErrorCode::Ok);
    CHECK(h.p.monetary().moneyGood == SILVER_ORE);
    CHECK(h.p.monetary().turnsWithCurrentMoneyGood == 0);
}

TEST_CASE("every refusal leaves the state untouched") {
    SUBCASE("a player that is not a civ") {
        Holder h;
        const aoc::sim::MonetaryStateComponent before = h.p.monetary();
        CHECK(aoc::sim::requestSetMoneyGood(h.world.gameState, PlayerId{200},
                                            static_cast<uint8_t>(SILVER_ORE)) ==
              ErrorCode::EntityNotFound);
        CHECK(same(h.p.monetary(), before));
    }

    SUBCASE("a good id past the end of the table") {
        Holder h;
        const aoc::sim::MonetaryStateComponent before = h.p.monetary();
        // 200 is not NO_MONEY_GOOD and not a real good (GOOD_COUNT is 167).
        CHECK(h.set(200) == ErrorCode::InvalidMoneyGood);
        CHECK(same(h.p.monetary(), before));
    }

    SUBCASE("a good the civ holds none of") {
        Holder h;
        const aoc::sim::MonetaryStateComponent before = h.p.monetary();
        CHECK(h.set(static_cast<uint8_t>(aoc::sim::goods::GOLD_ORE)) ==
              ErrorCode::InvalidMoneyGood);
        CHECK(same(h.p.monetary(), before));
    }

    SUBCASE("under paper, where the note is already the money") {
        Holder h;
        h.p.monetary().system                         = MonetarySystemType::FiatMoney;
        const aoc::sim::MonetaryStateComponent before = h.p.monetary();
        CHECK(h.set(static_cast<uint8_t>(SILVER_ORE)) == ErrorCode::InvalidMoneyGood);
        CHECK(same(h.p.monetary(), before));
    }

    SUBCASE("before the dwell has elapsed") {
        Holder h;
        REQUIRE(h.set(static_cast<uint8_t>(SILVER_ORE)) == ErrorCode::Ok);
        const aoc::sim::MonetaryStateComponent before = h.p.monetary();
        CHECK(h.set(static_cast<uint8_t>(SILK)) == ErrorCode::InvalidMoneyGood);
        CHECK(same(h.p.monetary(), before));
        CHECK(h.p.monetary().moneyGood == SILVER_ORE); // still the old money
    }

    SUBCASE("electing the good that is already money") {
        Holder h;
        REQUIRE(h.set(static_cast<uint8_t>(SILVER_ORE)) == ErrorCode::Ok);
        h.p.monetary().turnsWithCurrentMoneyGood      = MONEY_GOOD_DWELL_TURNS;
        const aoc::sim::MonetaryStateComponent before = h.p.monetary();
        CHECK(h.set(static_cast<uint8_t>(SILVER_ORE)) == ErrorCode::InvalidMoneyGood);
        CHECK(same(h.p.monetary(), before));
    }

    SUBCASE("demonetising when nothing is money") {
        Holder h;
        const aoc::sim::MonetaryStateComponent before = h.p.monetary();
        CHECK(h.set(NO_MONEY_GOOD) == ErrorCode::InvalidMoneyGood);
        CHECK(same(h.p.monetary(), before));
    }
}

TEST_CASE("a civ may switch once the dwell has passed, and may demonetise") {
    Holder h;
    REQUIRE(h.set(static_cast<uint8_t>(SILVER_ORE)) == ErrorCode::Ok);

    h.p.monetary().turnsWithCurrentMoneyGood = MONEY_GOOD_DWELL_TURNS;
    REQUIRE(h.set(static_cast<uint8_t>(SILK)) == ErrorCode::Ok);
    CHECK(h.p.monetary().moneyGood == SILK);

    // Demonetising is open even under paper: a people can stop taking a thing
    // in payment without having agreed on its successor.
    h.p.monetary().turnsWithCurrentMoneyGood = MONEY_GOOD_DWELL_TURNS;
    h.p.monetary().system                    = MonetarySystemType::FiatMoney;
    REQUIRE(h.set(NO_MONEY_GOOD) == ErrorCode::Ok);
    CHECK(h.p.monetary().moneyGood == NO_MONEY_GOOD);
}

TEST_CASE("the plan's gate: barter to silver to fiat by request alone") {
    // Its own world: the ladder's own gates want two cities and two distinct
    // partner civs, which the minimal Holder deliberately does not have.
    aoc::test::World w   = aoc::test::makeWorld(3);
    aoc::game::Player& p = *w.gameState.player(P0);
    aoc::game::City& a   = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::test::addCityAt(w, P0, 9, 5, "Gamma"); // the table wants two
    aoc::test::addCityAt(w, PlayerId{1}, 14, 8, "Beta");
    a.stockpile().addGoods(SILVER_ORE, 40);
    a.stockpile().addGoods(SILK, 10);
    p.monetary().turnsWithCurrentMoneyGood = MONEY_GOOD_DWELL_TURNS;

    REQUIRE(p.monetary().system == MonetarySystemType::Barter);
    REQUIRE(p.monetary().moneyGood == NO_MONEY_GOOD);

    // Silver becomes this people's money while they are still bartering: that
    // is the Mengerian claim, that a good is money because it is taken, not
    // because a regime declared it.
    REQUIRE(aoc::sim::requestSetMoneyGood(w.gameState, P0, static_cast<uint8_t>(SILVER_ORE)) ==
            ErrorCode::Ok);
    CHECK(p.monetary().moneyGood == SILVER_ORE);

    // Then up the regime ladder by request, ending on paper.
    p.monetary().bullion = 200; // clears the CommodityMoney strength gate
    REQUIRE(aoc::sim::requestSetMonetaryRegime(
                w.gameState, P0, MonetarySystemType::CommodityMoney) == ErrorCode::Ok);
    CHECK(p.monetary().moneyGood == SILVER_ORE); // the money good survives the regime change

    p.monetary().turnsInCurrentSystem      = 100;
    p.tech().completedTechs[BANKING.value] = true;
    REQUIRE(aoc::sim::requestSetMonetaryRegime(w.gameState, P0, MonetarySystemType::GoldStandard) ==
            ErrorCode::Ok);

    // Fiat wants Economics or a press, calm prices, and two live partners.
    p.monetary().turnsInCurrentSystem        = 100;
    p.monetary().inflationRate               = 0.0f;
    p.tech().completedTechs[ECONOMICS.value] = true;
    aoc::game::Unit& t1                      = aoc::test::addUnitAt(w, P0, TRADER, 6, 5);
    t1.trader().owner                        = P0;
    t1.trader().destOwner                    = PlayerId{1};
    aoc::game::Unit& t2                      = aoc::test::addUnitAt(w, P0, TRADER, 7, 5);
    t2.trader().owner                        = P0;
    t2.trader().destOwner                    = PlayerId{2};
    REQUIRE(aoc::sim::livePartnerCount(w.gameState, P0) == 2);

    REQUIRE(aoc::sim::requestSetMonetaryRegime(w.gameState, P0, MonetarySystemType::FiatMoney) ==
            ErrorCode::Ok);
    CHECK(p.monetary().system == MonetarySystemType::FiatMoney);

    // On paper the commodity may no longer be elected, but what was already
    // money stays recorded until the civ drops it.
    p.monetary().turnsWithCurrentMoneyGood = MONEY_GOOD_DWELL_TURNS;
    CHECK(aoc::sim::requestSetMoneyGood(w.gameState, P0, static_cast<uint8_t>(SILK)) ==
          ErrorCode::InvalidMoneyGood);
    CHECK(p.monetary().moneyGood == SILVER_ORE);
}

/**
 * @file test_money_good.cpp
 * @brief Adoption as a decision (Mengerian plan, Phase C): requestSetMoneyGood
 *        elects the good a civ prices in, every refusal leaves the state
 *        exactly as it was, and a civ can be driven barter -> silver -> fiat
 *        by request alone.
 *
 *        Phase C is the MECHANISM: who may change the money good, when, and
 *        that a denial costs nothing. Phase D adds the WIRING below -- the four
 *        saleability terms read out of the world, and the AI deciding through
 *        the same request rather than a second path of its own.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/monetary/MonetaryActions.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/economy/Market.hpp"
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

    // On paper the note is the money: the commodity is cleared on entry and
    // may not be elected again while the civ stays on paper.
    CHECK(p.monetary().moneyGood == NO_MONEY_GOOD);
    p.monetary().turnsWithCurrentMoneyGood = MONEY_GOOD_DWELL_TURNS;
    CHECK(aoc::sim::requestSetMoneyGood(w.gameState, P0, static_cast<uint8_t>(SILK)) ==
          ErrorCode::InvalidMoneyGood);
    CHECK(p.monetary().moneyGood == NO_MONEY_GOOD);
}

// ---------------------------------------------------------------------------
// Phase D: the score read out of the world, and the AI deciding through the
// same request. What is pinned here is the WIRING -- that each term is drawn
// from the right place -- not the score's arithmetic, which test_money_-
// saleability already covers on explicit inputs.
// ---------------------------------------------------------------------------

TEST_CASE("the world view counts acceptance by trade weight among met civs") {
    aoc::test::World w = aoc::test::makeWorld(3);
    aoc::sim::DiplomacyManager d;
    d.initialize(3);
    d.meetPlayers(P0, PlayerId{1}, 1);
    aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::test::addCityAt(w, PlayerId{1}, 14, 8, "Beta");
    aoc::test::addCityAt(w, PlayerId{2}, 20, 12, "Gamma");

    // P1 (met) prices in silver; P2 (unmet) also does, and must not count.
    w.gameState.player(PlayerId{1})->monetary().moneyGood = static_cast<uint8_t>(SILVER_ORE);
    w.gameState.player(PlayerId{2})->monetary().moneyGood = static_cast<uint8_t>(SILVER_ORE);

    const aoc::sim::MoneyWorldView view = aoc::sim::moneyWorldView(w.gameState, &d, P0);
    CHECK(view.totalWeight == 1);                          // one met civ, no routes
    CHECK(view.acceptingWeight[SILVER_ORE] == 1);          // and it takes silver
    CHECK(view.acceptingWeight[SILK] == 0);

    // With no diplomacy at all every civ is visible, which is what the null
    // overload means elsewhere in this codebase.
    const aoc::sim::MoneyWorldView all = aoc::sim::moneyWorldView(w.gameState, nullptr, P0);
    CHECK(all.totalWeight == 2);
    CHECK(all.acceptingWeight[SILVER_ORE] == 2);
}

TEST_CASE("the world inputs carry the table's durability, price and the incumbent flag") {
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(P0);
    aoc::game::City& a   = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    a.stockpile().addGoods(SILVER_ORE, 7);
    aoc::sim::Market market;
    market.initialize();
    const aoc::sim::MoneyWorldView view = aoc::sim::moneyWorldView(w.gameState, nullptr, P0);

    aoc::sim::SaleabilityInputs silver =
        aoc::sim::saleabilityInputsFor(w.gameState, market, view, P0, SILVER_ORE);
    CHECK(silver.held == 7);
    CHECK(silver.durability == aoc::sim::moneyDurability(SILVER_ORE));
    CHECK(silver.basePrice == aoc::sim::goodDef(SILVER_ORE).basePrice);
    CHECK_FALSE(silver.isIncumbent);

    p.monetary().moneyGood = static_cast<uint8_t>(SILVER_ORE);
    silver = aoc::sim::saleabilityInputsFor(w.gameState, market, view, P0, SILVER_ORE);
    CHECK(silver.isIncumbent);
    CHECK_FALSE(aoc::sim::saleabilityInputsFor(w.gameState, market, view, P0, SILK).isIncumbent);
}

TEST_CASE("industrial draw counts only recipes the civ can actually run") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::City& a = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    static_cast<void>(a);
    // A fresh civ has no production buildings, so nothing it owns is consumed
    // by anything it can run: every draw is zero. That is exactly why an inert
    // good scores well early, and why Phase E's industrial recipes are what
    // later push a civ off a metal.
    const aoc::sim::MoneyWorldView view = aoc::sim::moneyWorldView(w.gameState, nullptr, P0);
    int32_t total = 0;
    for (const int32_t d : view.industrialDraw) {
        total += d;
    }
    CHECK(total == 0);
}

TEST_CASE("the AI adopts through the request, and holds its money once elected") {
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(P0);
    aoc::game::City& a   = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    a.stockpile().addGoods(SILVER_ORE, 60);
    aoc::sim::Market market;
    market.initialize();
    p.monetary().turnsWithCurrentMoneyGood = MONEY_GOOD_DWELL_TURNS;

    REQUIRE(p.monetary().moneyGood == NO_MONEY_GOOD);
    aoc::sim::aiChooseMoneyGood(w.gameState, market, nullptr, P0);
    CHECK(p.monetary().moneyGood != NO_MONEY_GOOD); // something was elected

    // Locked in straight after: the dwell is what stops the money's identity
    // flickering, and the AI must respect it rather than route around it.
    const uint8_t elected = p.monetary().moneyGood;
    a.stockpile().addGoods(SILK, 500);
    aoc::sim::aiChooseMoneyGood(w.gameState, market, nullptr, P0);
    CHECK(p.monetary().moneyGood == elected);
}

TEST_CASE("a civ on paper elects nothing: the note is already the money") {
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(P0);
    aoc::game::City& a   = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    a.stockpile().addGoods(SILVER_ORE, 60);
    aoc::sim::Market market;
    market.initialize();
    p.monetary().system                    = MonetarySystemType::FiatMoney;
    p.monetary().turnsWithCurrentMoneyGood = MONEY_GOOD_DWELL_TURNS;

    aoc::sim::aiChooseMoneyGood(w.gameState, market, nullptr, P0);
    CHECK(p.monetary().moneyGood == NO_MONEY_GOOD);
}

TEST_CASE("late industry wants the money metals: the Phase E recipes exist and are tech-gated") {
    using aoc::sim::goods::GOLD_ORE;
    constexpr aoc::TechId ELECTRICITY{14};
    constexpr aoc::TechId CHEMISTRY{65};
    bool goldRoute = false;
    bool silverRoute = false;
    for (const aoc::sim::ProductionRecipe& r : aoc::sim::allRecipes()) {
        for (const aoc::sim::RecipeInput& in : r.inputs) {
            if (in.goodId == GOLD_ORE && in.consumed && r.requiredTech == ELECTRICITY &&
                r.requiredBuilding == aoc::BuildingId{4}) {
                goldRoute = true;
            }
            if (in.goodId == SILVER_ORE && in.consumed && r.requiredTech == CHEMISTRY &&
                r.requiredBuilding == aoc::BuildingId{3}) {
                silverRoute = true;
            }
        }
    }
    CHECK(goldRoute);
    CHECK(silverRoute);

    // A civ with Electricity and an Electronics Plant now has a draw on gold;
    // without the tech, or without the plant, it has none.
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(P0);
    aoc::game::City& a   = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    CHECK(aoc::sim::industrialDrawFor(w.gameState, P0, GOLD_ORE) == 0);
    a.districts().districts.push_back(
        {aoc::sim::DistrictType::Industrial, a.location(), {aoc::BuildingId{4}}});
    CHECK(aoc::sim::industrialDrawFor(w.gameState, P0, GOLD_ORE) == 0); // no Electricity
    p.tech().completedTechs[ELECTRICITY.value] = true;
    CHECK(aoc::sim::industrialDrawFor(w.gameState, P0, GOLD_ORE) == 1);
    // Ivory has no late route, so the same civ draws none of it.
    CHECK(aoc::sim::industrialDrawFor(w.gameState, P0, aoc::sim::goods::IVORY) == 0);
}

TEST_CASE("the ranking is a total order and the AI's choice is its head") {
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(P0);
    aoc::game::City& a   = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    a.stockpile().addGoods(SILVER_ORE, 10);
    a.stockpile().addGoods(aoc::sim::goods::GOLD_ORE, 10);
    a.stockpile().addGoods(SILK, 10);
    aoc::sim::Market market;
    market.initialize();
    const aoc::sim::MoneyWorldView view = aoc::sim::moneyWorldView(w.gameState, nullptr, P0);
    const std::vector<aoc::sim::MoneyCandidate> ranking =
        aoc::sim::rankMoneyCandidates(w.gameState, market, view, P0);
    REQUIRE(ranking.size() >= 3);
    for (std::size_t i = 1; i < ranking.size(); ++i) {
        const bool ordered = ranking[i - 1].score > ranking[i].score ||
                             (ranking[i - 1].score == ranking[i].score &&
                              ranking[i - 1].goodId < ranking[i].goodId);
        CHECK(ordered);
    }
    CHECK(ranking.front().goodId == aoc::sim::goods::GOLD_ORE); // the table's order, at equal held
    // Ties break on the lower id: two goods forced to the same score rank by id.
    const std::vector<aoc::sim::MoneyCandidate> facade =
        aoc::sim::rankMoneyCandidates(w.gameState, market, nullptr, P0);
    CHECK(facade.size() == ranking.size());
    CHECK(facade.front().goodId == ranking.front().goodId);

    p.monetary().turnsWithCurrentMoneyGood = MONEY_GOOD_DWELL_TURNS;
    aoc::sim::aiChooseMoneyGood(w.gameState, market, nullptr, P0);
    CHECK(p.monetary().moneyGood == ranking.front().goodId);
}

TEST_CASE("the late metals are hidden until their tech, so antiquity cannot mint nickel") {
    using aoc::sim::resourceRevealTech;
    CHECK(resourceRevealTech(aoc::sim::goods::NICKEL) == aoc::TechId{14});
    CHECK(resourceRevealTech(aoc::sim::goods::COBALT) == aoc::TechId{14});
    CHECK(resourceRevealTech(aoc::sim::goods::HELIUM) == aoc::TechId{14});
    CHECK(resourceRevealTech(aoc::sim::goods::PLATINUM) == aoc::TechId{65});
    CHECK(resourceRevealTech(aoc::sim::goods::LITHIUM) == aoc::TechId{65});
    CHECK(resourceRevealTech(aoc::sim::goods::TITANIUM) == aoc::TechId{23});
    CHECK(resourceRevealTech(aoc::sim::goods::RARE_EARTH) == aoc::TechId{23});
    // The ancient money metals stay visible from the first turn.
    CHECK_FALSE(resourceRevealTech(SILVER_ORE).isValid());
    CHECK_FALSE(resourceRevealTech(aoc::sim::goods::GOLD_ORE).isValid());
}

TEST_CASE("a recipe whose other input cannot be seen yet is no draw on the money metal") {
    // Recipe 67, Platinum Jewelry, eats gold ore and platinum at a Textile
    // Mill (building 8) with no tech gate. Platinum is revealed by Chemistry.
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(P0);
    aoc::game::City& a   = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    a.districts().districts.push_back(
        {aoc::sim::DistrictType::Industrial, a.location(), {aoc::BuildingId{8}}});
    CHECK(aoc::sim::industrialDrawFor(w.gameState, P0, aoc::sim::goods::GOLD_ORE) == 0);
    // Platinum in the stockpile (an import) makes the recipe runnable even
    // before the tech that reveals its tiles.
    a.stockpile().addGoods(aoc::sim::goods::PLATINUM, 1);
    CHECK(aoc::sim::industrialDrawFor(w.gameState, P0, aoc::sim::goods::GOLD_ORE) == 1);
    a.stockpile().consumeGoods(aoc::sim::goods::PLATINUM, 1);
    CHECK(aoc::sim::industrialDrawFor(w.gameState, P0, aoc::sim::goods::GOLD_ORE) == 0);
    p.tech().completedTechs[65] = true; // Chemistry: platinum can be mined
    CHECK(aoc::sim::industrialDrawFor(w.gameState, P0, aoc::sim::goods::GOLD_ORE) == 1);
}

TEST_CASE("the incumbent's coin counts as its metal, a challenger's does not") {
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(P0);
    aoc::game::City& a   = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    a.stockpile().addGoods(SILVER_ORE, 2);
    aoc::sim::Market market;
    market.initialize();
    p.monetary().system        = MonetarySystemType::CommodityMoney;
    p.monetary().privateSpecie = 22 * 40; // forty silver's worth, struck at par 22
    const aoc::sim::MoneyWorldView view = aoc::sim::moneyWorldView(w.gameState, nullptr, P0);

    CHECK(aoc::sim::saleabilityInputsFor(w.gameState, market, view, P0, SILVER_ORE).held == 2);
    p.monetary().moneyGood = static_cast<uint8_t>(SILVER_ORE);
    CHECK(aoc::sim::saleabilityInputsFor(w.gameState, market, view, P0, SILVER_ORE).held == 42);
    // Under barter the people hold no coin of their own: the stock is the stock.
    p.monetary().system = MonetarySystemType::Barter;
    CHECK(aoc::sim::saleabilityInputsFor(w.gameState, market, view, P0, SILVER_ORE).held == 2);
}

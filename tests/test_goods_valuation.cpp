/**
 * @file test_goods_valuation.cpp
 * @brief The one valuation seam for goods terms (plan B4, step 1.2): a market
 *        anchor scaled by need, stock, sole-source leverage, war and cash, and
 *        the AI's choice of what to buy first.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/simulation/diplomacy/DealProposals.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

using aoc::PlayerId;
using aoc::sim::GoodsSide;
using aoc::sim::goods::CLOTHING;
using aoc::sim::goods::IRON_ORE;
using aoc::sim::goods::SILK;
using aoc::sim::goods::WHEAT;

namespace {

constexpr PlayerId P0{0};
constexpr PlayerId P1{1};
constexpr PlayerId P2{2};

/// Three met civs; P1 and P2 each hold ten silk and ten iron, P0 holds nothing.
struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(3);
    aoc::sim::DiplomacyManager d;
    aoc::game::City* alpha = nullptr;
    aoc::game::City* beta  = nullptr;
    aoc::game::City* gamma = nullptr;

    Fixture() {
        this->d.initialize(3);
        this->d.meetPlayers(P0, P1, 1);
        this->d.meetPlayers(P0, P2, 1);
        this->d.meetPlayers(P1, P2, 1);
        this->alpha = &aoc::test::addCityAt(this->world, P0, 4, 4, "Alpha");
        this->beta  = &aoc::test::addCityAt(this->world, P1, 14, 8, "Beta");
        this->gamma = &aoc::test::addCityAt(this->world, P2, 20, 12, "Gamma");
        for (const PlayerId id : {P0, P1, P2}) {
            this->world.gameState.player(id)->setTreasury(1000, aoc::sim::MoneyFlow::external());
        }
        this->beta->stockpile().addGoods(SILK, 10);
        this->gamma->stockpile().addGoods(SILK, 10);
        this->beta->stockpile().addGoods(IRON_ORE, 10);
        this->gamma->stockpile().addGoods(IRON_ORE, 10);
    }

    [[nodiscard]] int32_t value(PlayerId me, PlayerId other, uint16_t good, int32_t amount,
                                GoodsSide side, const aoc::sim::Market* market = nullptr) const {
        return aoc::sim::goodsValueFor(this->world.gameState, this->d, market, me, other, good,
                                       amount, side);
    }

    void need(PlayerId who, uint16_t good, int32_t amount) {
        this->world.gameState.player(who)->economy().totalNeeds[good] = amount;
    }
};

[[nodiscard]] int32_t base(uint16_t good, int32_t amount) {
    return amount * static_cast<int32_t>(aoc::sim::goodDef(good).basePrice);
}

/// The value the model computes for `amount` of `good` at `percent` of the anchor.
[[nodiscard]] int32_t worth(uint16_t good, int32_t amount, int32_t percent) {
    return base(good, amount) * percent / 100;
}

} // namespace

TEST_CASE("a null market anchors on the base price, a market on its own price") {
    Fixture f;
    CHECK(f.value(P0, P1, SILK, 4, GoodsSide::Receive) == base(SILK, 4));
    aoc::sim::Market market;
    market.initialize();
    market.setPrice(SILK, 2 * static_cast<int32_t>(aoc::sim::goodDef(SILK).basePrice));
    CHECK(f.value(P0, P1, SILK, 4, GoodsSide::Receive, &market) == 2 * base(SILK, 4));
}

TEST_CASE("receiving: missing and wanted 250, short 175, neither 100, surplus 60") {
    Fixture f;
    CHECK(f.value(P0, P1, SILK, 4, GoodsSide::Receive) ==
          worth(SILK, 4, aoc::sim::GOODS_NEUTRAL_PCT));
    f.need(P0, SILK, 5);
    CHECK(f.value(P0, P1, SILK, 4, GoodsSide::Receive) ==
          worth(SILK, 4, aoc::sim::GOODS_MISSING_PCT));
    f.alpha->stockpile().addGoods(SILK, 3);
    CHECK(f.value(P0, P1, SILK, 4, GoodsSide::Receive) ==
          worth(SILK, 4, aoc::sim::GOODS_SHORT_PCT));
    f.alpha->stockpile().addGoods(SILK, 7);
    CHECK(f.value(P0, P1, SILK, 4, GoodsSide::Receive) ==
          worth(SILK, 4, aoc::sim::GOODS_SURPLUS_PCT));
}

TEST_CASE("giving: what leaves you short 200, a thin surplus 100, a deep one 70") {
    Fixture f;
    CHECK(f.value(P1, P0, SILK, 3, GoodsSide::Give) == worth(SILK, 3, aoc::sim::GIVE_SURPLUS_PCT));
    f.need(P1, SILK, 5); // ten held, three given, seven left: above need, under need plus shipment
    CHECK(f.value(P1, P0, SILK, 3, GoodsSide::Give) == worth(SILK, 3, aoc::sim::GIVE_THIN_PCT));
    f.need(P1, SILK, 8);
    CHECK(f.value(P1, P0, SILK, 3, GoodsSide::Give) ==
          worth(SILK, 3, aoc::sim::GIVE_SHORTFALL_PCT));
}

TEST_CASE("the sole source on the map commands more, on both sides of the table") {
    Fixture f;
    REQUIRE(f.gamma->stockpile().consumeGoods(SILK, 10)); // now only P1 holds silk
    CHECK(f.value(P0, P1, SILK, 4, GoodsSide::Receive) ==
          worth(SILK, 4, aoc::sim::SOLE_SOURCE_PCT));
    CHECK(f.value(P1, P0, SILK, 4, GoodsSide::Give) ==
          worth(SILK, 4, aoc::sim::GIVE_SURPLUS_PCT * aoc::sim::SOLE_SOURCE_PCT / 100));
    // A buyer who is not the source gains no leverage from someone else's.
    CHECK(f.value(P0, P2, IRON_ORE, 4, GoodsSide::Receive) ==
          worth(IRON_ORE, 4, aoc::sim::GOODS_NEUTRAL_PCT));
}

TEST_CASE("at war a strategic good is dearer and a luxury is not") {
    Fixture f;
    REQUIRE(aoc::sim::goodDef(IRON_ORE).isStrategic);
    REQUIRE_FALSE(aoc::sim::goodDef(SILK).isStrategic);
    f.d.declareWar(P0, P2);
    CHECK(f.value(P0, P1, IRON_ORE, 4, GoodsSide::Receive) ==
          worth(IRON_ORE, 4, aoc::sim::WAR_STRATEGIC_PCT));
    CHECK(f.value(P0, P1, SILK, 4, GoodsSide::Receive) ==
          worth(SILK, 4, aoc::sim::GOODS_NEUTRAL_PCT));
    // The belligerent's counterparty at peace prices iron as before.
    CHECK(f.value(P1, P0, IRON_ORE, 3, GoodsSide::Give) ==
          worth(IRON_ORE, 3, aoc::sim::GIVE_SURPLUS_PCT));
}

TEST_CASE("a cash-poor seller sells cheaper") {
    Fixture f;
    f.world.gameState.player(P1)->setTreasury(aoc::sim::CASH_POOR_TREASURY - 1, aoc::sim::MoneyFlow::external());
    CHECK(f.value(P1, P0, SILK, 3, GoodsSide::Give) ==
          worth(SILK, 3, aoc::sim::GIVE_SURPLUS_PCT * aoc::sim::CASH_POOR_PCT / 100));
    // Being poor does not make what you buy cheaper.
    f.world.gameState.player(P0)->setTreasury(0, aoc::sim::MoneyFlow::external());
    CHECK(f.value(P0, P1, SILK, 4, GoodsSide::Receive) ==
          worth(SILK, 4, aoc::sim::GOODS_NEUTRAL_PCT));
}

TEST_CASE("the factors stack and the result is capped at triple the anchor") {
    Fixture f;
    REQUIRE(f.gamma->stockpile().consumeGoods(IRON_ORE, 10)); // P1 is the sole source
    f.need(P0, IRON_ORE, 5);                                  // and P0 has none of it
    f.d.declareWar(P0, P2);                                   // at war
    // 250 x 1.5 x 1.25 = 468, capped.
    CHECK(f.value(P0, P1, IRON_ORE, 4, GoodsSide::Receive) ==
          worth(IRON_ORE, 4, aoc::sim::GOODS_VALUE_MAX_PCT));
}

TEST_CASE("a goods term is valued from each party's own side of the transfer") {
    Fixture f;
    f.need(P0, SILK, 5);
    aoc::sim::DiplomaticDeal deal{};
    deal.playerA = P1;
    deal.playerB = P0;
    aoc::sim::DealTerm shipment{};
    shipment.type       = aoc::sim::DealTermType::GoodsExchange;
    shipment.fromPlayer = P1;
    shipment.toPlayer   = P0;
    shipment.goodId     = SILK;
    shipment.goodAmount = 4;
    deal.terms.push_back(shipment);
    CHECK(aoc::sim::dealValueFor(f.world.gameState, f.d, P0, deal) ==
          f.value(P0, P1, SILK, 4, GoodsSide::Receive));
    CHECK(aoc::sim::dealValueFor(f.world.gameState, f.d, P1, deal) ==
          -f.value(P1, P0, SILK, 4, GoodsSide::Give));
}

TEST_CASE("the AI buys the luxury it lacks before its biggest bulk need") {
    Fixture f;
    aoc::game::Player& buyer = *f.world.gameState.player(P0);
    CHECK_FALSE(aoc::sim::aiPurchaseTarget(buyer).has_value());

    buyer.economy().totalNeeds[WHEAT]            = 40;
    std::optional<aoc::sim::PurchaseTarget> bulk = aoc::sim::aiPurchaseTarget(buyer);
    REQUIRE(bulk.has_value());
    CHECK(bulk->goodId == WHEAT);
    CHECK(bulk->amount == aoc::sim::GOODS_DEAL_MAX_UNITS);

    buyer.economy().totalNeeds[SILK]                = 1;
    std::optional<aoc::sim::PurchaseTarget> variety = aoc::sim::aiPurchaseTarget(buyer);
    REQUIRE(variety.has_value());
    CHECK(variety->goodId == SILK);
    CHECK(variety->amount == aoc::sim::LUXURY_DEAL_UNITS);

    // Ties among bulk needs go to the lowest id, so the pick is reproducible.
    buyer.economy().totalNeeds.erase(SILK);
    buyer.economy().totalNeeds[CLOTHING]        = 40;
    std::optional<aoc::sim::PurchaseTarget> tie = aoc::sim::aiPurchaseTarget(buyer);
    REQUIRE(tie.has_value());
    CHECK(tie->goodId == std::min(WHEAT, CLOTHING));
}

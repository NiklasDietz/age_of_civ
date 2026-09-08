/**
 * @file test_deal_economic_terms.cpp
 * @brief Goods and access can be bargained for, not just shipped.
 *
 *        GoodsExchange and ExclusiveAccess were declared, described in the UI,
 *        and executed by NOBODY -- neither appeared in any switch in
 *        DealTerms.cpp. Goods could only ever move by trade route, so a
 *        negotiated deal had no way to hand over the thing being negotiated
 *        about, and there was no lever at all to bargain over access to a
 *        resource. The AI scored both at exactly zero (a `default: break;`
 *        marked "neutral until the goods valuation lands"), so it would give
 *        either away for free and never ask for either.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/diplomacy/DealProposals.hpp"
#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::sim::DealTerm;
using aoc::sim::DealTermType;
using aoc::sim::DiplomacyManager;
using aoc::sim::DiplomaticDeal;
using aoc::sim::GlobalDealTracker;

namespace {

constexpr uint16_t OIL = aoc::sim::goods::OIL;

struct Table {
    aoc::test::World world = aoc::test::makeWorld(3);
    DiplomacyManager d;
    GlobalDealTracker tracker;

    Table() {
        this->d.initialize(3);
        this->d.meetPlayers(PlayerId{0}, PlayerId{1}, 1);
        this->d.meetPlayers(PlayerId{0}, PlayerId{2}, 1);
        aoc::test::addCityAt(this->world, PlayerId{0}, 5, 5, "Seller");
        aoc::test::addCityAt(this->world, PlayerId{1}, 13, 7, "Buyer");
        aoc::test::addCityAt(this->world, PlayerId{2}, 19, 11, "Rival");
    }

    aoc::game::City& cityOf(PlayerId p) {
        return *this->world.gameState.player(p)->cities()[0];
    }

    /// Propose then accept a one-term deal, returning acceptDeal's verdict.
    ErrorCode settle(const DealTerm& term) {
        DiplomaticDeal deal{};
        deal.playerA = PlayerId{0};
        deal.playerB = PlayerId{1};
        deal.terms.push_back(term);
        const std::size_t idx = this->tracker.activeDeals.size();
        REQUIRE(aoc::sim::proposeDeal(this->world.gameState, this->tracker, deal) == ErrorCode::Ok);
        return aoc::sim::acceptDeal(this->world.gameState, this->world.grid, this->tracker,
                                    static_cast<int32_t>(idx), &this->d);
    }
};

DealTerm goodsTerm(int32_t amount) {
    DealTerm t{};
    t.type       = DealTermType::GoodsExchange;
    t.fromPlayer = PlayerId{0};
    t.toPlayer   = PlayerId{1};
    t.goodId     = OIL;
    t.goodAmount = amount;
    return t;
}

DealTerm accessTerm() {
    DealTerm t{};
    t.type       = DealTermType::ExclusiveAccess;
    t.fromPlayer = PlayerId{0};
    t.toPlayer   = PlayerId{1};
    t.goodId     = OIL;
    return t;
}

} // namespace

TEST_CASE("a negotiated shipment of goods actually changes hands") {
    Table t;
    t.cityOf(PlayerId{0}).stockpile().addGoods(OIL, 50);
    REQUIRE(t.cityOf(PlayerId{1}).stockpile().getAmount(OIL) == 0);

    REQUIRE(t.settle(goodsTerm(20)) == ErrorCode::Ok);

    CHECK(t.cityOf(PlayerId{0}).stockpile().getAmount(OIL) == 30);
    CHECK(t.cityOf(PlayerId{1}).stockpile().getAmount(OIL) == 20);
}

TEST_CASE("a civ cannot promise goods it does not have") {
    // Through the atomicity gate, so the deal is refused whole rather than
    // half-applied -- the same guarantee ceding a city already had.
    Table t;
    t.cityOf(PlayerId{0}).stockpile().addGoods(OIL, 5);

    CHECK(t.settle(goodsTerm(20)) == ErrorCode::InsufficientResources);
    CHECK(t.cityOf(PlayerId{0}).stockpile().getAmount(OIL) == 5); // untouched
    CHECK(t.cityOf(PlayerId{1}).stockpile().getAmount(OIL) == 0);
}

TEST_CASE("exclusive access shuts out the third party, and only the third party") {
    Table t;
    REQUIRE_FALSE(t.d.relation(PlayerId{0}, PlayerId{2}).isGoodEmbargoed(OIL));

    REQUIRE(t.settle(accessTerm()) == ErrorCode::Ok);

    // The rival can no longer buy this good from the seller...
    CHECK(t.d.relation(PlayerId{0}, PlayerId{2}).isGoodEmbargoed(OIL));
    // ...but the civ that bought exclusivity obviously can.
    CHECK_FALSE(t.d.relation(PlayerId{0}, PlayerId{1}).isGoodEmbargoed(OIL));
}

TEST_CASE("exclusivity does not outlive its contract") {
    Table t;
    REQUIRE(t.settle(accessTerm()) == ErrorCode::Ok);
    REQUIRE(t.d.relation(PlayerId{0}, PlayerId{2}).isGoodEmbargoed(OIL));

    // Run the deal out. A standing claim that survived its own deal would be a
    // permanent embargo nobody agreed to.
    t.tracker.activeDeals[0].turnsRemaining = 1;
    aoc::sim::processDeals(t.world.gameState, t.tracker, t.d, t.world.grid);

    CHECK(t.tracker.activeDeals.empty());
    CHECK_FALSE(t.d.relation(PlayerId{0}, PlayerId{2}).isGoodEmbargoed(OIL));
}

TEST_CASE("the AI prices goods and access instead of giving them away") {
    Table t;
    t.cityOf(PlayerId{0}).stockpile().addGoods(OIL, 100);

    // A deal where player 0 hands oil to player 1 for nothing: player 0 should
    // value it negatively and refuse. At zero valuation it accepted anything.
    DiplomaticDeal gift{};
    gift.playerA = PlayerId{0};
    gift.playerB = PlayerId{1};
    gift.terms.push_back(goodsTerm(40));
    CHECK_FALSE(aoc::sim::aiAcceptsDeal(t.world.gameState, t.d, PlayerId{0}, gift));
    // The receiving side is happy to take it.
    CHECK(aoc::sim::aiAcceptsDeal(t.world.gameState, t.d, PlayerId{1}, gift));

    // Exclusivity given away for free is likewise refused by the granter.
    DiplomaticDeal freeAccess{};
    freeAccess.playerA = PlayerId{0};
    freeAccess.playerB = PlayerId{1};
    freeAccess.terms.push_back(accessTerm());
    CHECK_FALSE(aoc::sim::aiAcceptsDeal(t.world.gameState, t.d, PlayerId{0}, freeAccess));
}

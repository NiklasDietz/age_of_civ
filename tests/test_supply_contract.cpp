/**
 * @file test_supply_contract.cpp
 * @brief The standing contract (plan B4, step 1.3): goods per turn for gold
 *        per turn over a term. Delivery and prorated payment each turn, a
 *        shortfall that costs reputation, breach on nothing delivered or
 *        nothing paid, breach at war or under embargo with the right party
 *        blamed, a blameless end when a party has no city, every denial path,
 *        a per-term clock inside a mixed deal, the AI's acceptance threshold,
 *        and the broken-deal leak.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/diplomacy/DealProposals.hpp"
#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

using aoc::CurrencyAmount;
using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::sim::DealTerm;
using aoc::sim::DealTermType;
using aoc::sim::DiplomaticDeal;

namespace {

constexpr PlayerId SELLER{0};
constexpr PlayerId BUYER{1};
constexpr PlayerId RIVAL{2};
constexpr uint16_t SILK = aoc::sim::goods::SILK;

struct Table {
    aoc::test::World world = aoc::test::makeWorld(3);
    aoc::sim::DiplomacyManager d;
    aoc::sim::GlobalDealTracker tracker;
    aoc::game::City* sellerCity = nullptr;
    aoc::game::City* buyerCity  = nullptr;

    Table() {
        this->d.initialize(3);
        this->d.meetPlayers(SELLER, BUYER, 1);
        this->d.meetPlayers(SELLER, RIVAL, 1);
        this->d.meetPlayers(BUYER, RIVAL, 1);
        this->sellerCity = &aoc::test::addCityAt(this->world, SELLER, 5, 5, "Seller");
        this->buyerCity  = &aoc::test::addCityAt(this->world, BUYER, 13, 7, "Buyer");
        aoc::test::addCityAt(this->world, RIVAL, 19, 11, "Rival");
        this->sellerCity->stockpile().addGoods(SILK, 10);
        this->world.gameState.player(BUYER)->setTreasury(100, aoc::sim::MoneyFlow::external());
    }

    [[nodiscard]] aoc::game::Player& player(PlayerId id) {
        return *this->world.gameState.player(id);
    }

    [[nodiscard]] static DealTerm contract(int32_t perTurn, int32_t goldPerTurn, int32_t turns) {
        DealTerm t{};
        t.type        = DealTermType::SupplyContract;
        t.fromPlayer  = SELLER;
        t.toPlayer    = BUYER;
        t.goodId      = SILK;
        t.goodAmount  = perTurn;
        t.goldPerTurn = goldPerTurn;
        t.duration    = turns;
        return t;
    }

    /// Propose then accept a deal, returning acceptDeal's verdict.
    ErrorCode settle(const std::vector<DealTerm>& terms) {
        DiplomaticDeal deal{};
        deal.playerA          = SELLER;
        deal.playerB          = BUYER;
        deal.terms            = terms;
        const std::size_t idx = this->tracker.activeDeals.size();
        REQUIRE(aoc::sim::proposeDeal(this->world.gameState, this->tracker, deal) == ErrorCode::Ok);
        return aoc::sim::acceptDeal(this->world.gameState, this->world.grid, this->tracker,
                                    static_cast<int32_t>(idx), &this->d);
    }

    void turn() {
        aoc::sim::processDeals(this->world.gameState, this->tracker, this->d, this->world.grid);
    }

    /// `who`'s standing with `withWhom`, where breakDeal and the contract
    /// executor record it: relation(who, withWhom).
    [[nodiscard]] int32_t reputation(PlayerId who, PlayerId withWhom) const {
        return this->d.relation(who, withWhom).reputationScore();
    }
};

} // namespace

TEST_CASE("a contract delivers each turn, is paid for what arrives, and ends on time") {
    Table t;
    REQUIRE(t.settle({Table::contract(2, 5, 3)}) == ErrorCode::Ok);
    REQUIRE(t.tracker.activeDeals.size() == 1);
    CHECK(t.tracker.activeDeals.front().turnsRemaining == 3); // a pure contract lives its own term

    t.turn();
    CHECK(t.buyerCity->stockpile().getAmount(SILK) == 2);
    CHECK(t.sellerCity->stockpile().getAmount(SILK) == 8);
    CHECK(t.player(BUYER).treasury() == 95);
    CHECK(t.player(SELLER).treasury() == 5);
    t.turn();
    t.turn();
    CHECK(t.buyerCity->stockpile().getAmount(SILK) == 6);
    CHECK(t.player(BUYER).treasury() == 85);
    CHECK(t.player(SELLER).treasury() == 15);
    CHECK(t.tracker.activeDeals.empty());
    // Nobody lost standing over a contract honoured to the end.
    CHECK(t.reputation(BUYER, SELLER) == 0);
    CHECK(t.reputation(SELLER, BUYER) == 0);
}

TEST_CASE("a shortfall is prorated and costs the seller reputation; nothing at all is a breach") {
    Table t;
    REQUIRE(t.sellerCity->stockpile().consumeGoods(SILK, 7)); // three left
    REQUIRE(t.settle({Table::contract(2, 10, 5)}) == ErrorCode::Ok);

    t.turn(); // 2 of 2
    CHECK(t.player(BUYER).treasury() == 90);
    CHECK(t.reputation(SELLER, BUYER) == 0);
    t.turn(); // 1 of 2: half the gold, and the seller's word is worth less
    CHECK(t.buyerCity->stockpile().getAmount(SILK) == 3);
    CHECK(t.player(BUYER).treasury() == 85);
    CHECK(t.reputation(SELLER, BUYER) == aoc::sim::CONTRACT_SHORTFALL_REPUTATION);
    t.turn(); // 0 of 2: breach by the seller, the deal is gone
    CHECK(t.tracker.activeDeals.empty());
    CHECK(t.reputation(SELLER, BUYER) < aoc::sim::CONTRACT_SHORTFALL_REPUTATION);
    CHECK(t.player(BUYER).treasury() == 85);
}

TEST_CASE("a buyer who cannot pay is in breach before any goods move") {
    Table t;
    REQUIRE(t.settle({Table::contract(2, 5, 3)}) == ErrorCode::Ok);
    t.player(BUYER).setTreasury(0, aoc::sim::MoneyFlow::external());
    t.turn();
    CHECK(t.tracker.activeDeals.empty());
    CHECK(t.buyerCity->stockpile().getAmount(SILK) == 0);
    CHECK(t.sellerCity->stockpile().getAmount(SILK) == 10);
    CHECK(t.reputation(BUYER, SELLER) < 0);
    CHECK(t.reputation(SELLER, BUYER) == 0);
}

TEST_CASE("war breaks a contract with the aggressor to blame") {
    Table t;
    REQUIRE(t.settle({Table::contract(2, 5, 10)}) == ErrorCode::Ok);
    t.d.declareWar(BUYER, SELLER);
    t.turn();
    CHECK(t.tracker.activeDeals.empty());
    CHECK(t.buyerCity->stockpile().getAmount(SILK) == 0);
    CHECK(t.reputation(BUYER, SELLER) < 0);
    CHECK(t.reputation(SELLER, BUYER) == 0);
}

TEST_CASE("an embargo breaks a contract with the embargoer to blame") {
    Table t;
    REQUIRE(t.settle({Table::contract(2, 5, 10)}) == ErrorCode::Ok);
    t.d.setEmbargo(SELLER, BUYER, true);
    t.turn();
    CHECK(t.tracker.activeDeals.empty());
    CHECK(t.reputation(SELLER, BUYER) < 0);
    CHECK(t.reputation(BUYER, SELLER) == 0);
}

TEST_CASE("a party with no city left ends the contract without blame") {
    Table t;
    REQUIRE(t.settle({Table::contract(2, 5, 10)}) == ErrorCode::Ok);
    t.buyerCity->setOwner(RIVAL);
    t.turn();
    CHECK(t.tracker.activeDeals.empty());
    CHECK(t.reputation(SELLER, BUYER) == 0);
    CHECK(t.reputation(BUYER, SELLER) == 0);
    CHECK(t.sellerCity->stockpile().getAmount(SILK) == 10);
}

TEST_CASE("a contract is refused before anyone is bound when it cannot start") {
    Table t;
    const int32_t silkBefore = t.sellerCity->stockpile().getAmount(SILK);
    CHECK(t.settle({Table::contract(11, 5, 3)}) == ErrorCode::InsufficientResources); // no stock
    t.player(BUYER).setTreasury(4, aoc::sim::MoneyFlow::external());
    CHECK(t.settle({Table::contract(2, 5, 3)}) ==
          ErrorCode::InsufficientResources); // first instalment
    t.player(BUYER).setTreasury(100, aoc::sim::MoneyFlow::external());
    CHECK(t.settle({Table::contract(0, 5, 3)}) == ErrorCode::InvalidArgument);
    CHECK(t.settle({Table::contract(2, 5, 0)}) == ErrorCode::InvalidArgument);
    CHECK(t.settle({Table::contract(2, 5, aoc::sim::SUPPLY_CONTRACT_MAX_TURNS + 1)}) ==
          ErrorCode::InvalidArgument);
    CHECK(t.settle({Table::contract(2, -1, 3)}) == ErrorCode::InvalidArgument);
    CHECK(t.tracker.activeDeals.empty());
    CHECK(t.sellerCity->stockpile().getAmount(SILK) == silkBefore);
    CHECK(t.player(BUYER).treasury() == 100);
}

TEST_CASE("a contract cannot be proposed to an enemy or across an embargo") {
    Table t;
    DiplomaticDeal deal{};
    deal.playerA = SELLER;
    deal.playerB = BUYER;
    deal.terms   = {Table::contract(2, 5, 3)};
    t.d.setEmbargo(BUYER, SELLER, true);
    CHECK(aoc::sim::requestProposeDeal(t.world.gameState, t.world.grid, t.tracker, t.d, deal, 1) ==
          ErrorCode::InvalidState);
    t.d.setEmbargo(BUYER, SELLER, false);
    t.d.declareWar(SELLER, BUYER);
    CHECK(aoc::sim::requestProposeDeal(t.world.gameState, t.world.grid, t.tracker, t.d, deal, 1) ==
          ErrorCode::InvalidState);
}

TEST_CASE("a mixed deal keeps each term's own clock") {
    Table t;
    DealTerm pact{};
    pact.type       = DealTermType::NonAggression;
    pact.fromPlayer = SELLER;
    pact.toPlayer   = BUYER;
    pact.duration   = 30;
    REQUIRE(t.settle({Table::contract(1, 1, 2), pact}) == ErrorCode::Ok);
    CHECK(t.tracker.activeDeals.front().turnsRemaining == 30);
    t.turn();
    t.turn();
    t.turn();
    REQUIRE(t.tracker.activeDeals.size() == 1); // the pact outlives the shipments
    CHECK(t.buyerCity->stockpile().getAmount(SILK) == 2);
    CHECK(t.player(BUYER).treasury() == 98);
    CHECK(t.tracker.activeDeals.front().terms.front().duration == 0);
}

TEST_CASE("the AI accepts a contract priced at the stream's worth and refuses a dearer one") {
    Table t;
    t.player(BUYER).economy().totalNeeds[SILK] = 5; // missing and wanted: 250% of the anchor
    const int32_t perTurnWorth = static_cast<int32_t>(aoc::sim::goodDef(SILK).basePrice) *
                                 aoc::sim::GOODS_MISSING_PCT / 100 * aoc::sim::CONTRACT_VALUE_PCT /
                                 100;
    DiplomaticDeal fair{};
    fair.playerA = SELLER;
    fair.playerB = BUYER;
    fair.terms   = {Table::contract(1, perTurnWorth, 3)};
    CHECK(aoc::sim::requestProposeDeal(t.world.gameState, t.world.grid, t.tracker, t.d, fair, 1) ==
          ErrorCode::Ok);
    // Well past the worth: a point of goodwill per four of relation score
    // sweetens every deal, so a one-gold overcharge can still pass.
    DiplomaticDeal dear = fair;
    dear.terms          = {Table::contract(1, perTurnWorth + 20, 3)};
    CHECK(aoc::sim::requestProposeDeal(t.world.gameState, t.world.grid, t.tracker, t.d, dear, 1) ==
          ErrorCode::InvalidState);
}

TEST_CASE("a broken pact no longer lingers in the list") {
    Table t;
    DealTerm pact{};
    pact.type       = DealTermType::NonAggression;
    pact.fromPlayer = SELLER;
    pact.toPlayer   = BUYER;
    REQUIRE(t.settle({pact}) == ErrorCode::Ok);
    t.d.declareWar(SELLER, BUYER);
    t.turn(); // the first pass breaks it, the second pass now removes it
    CHECK(t.tracker.activeDeals.empty());
    CHECK(t.reputation(SELLER, BUYER) < 0);
}

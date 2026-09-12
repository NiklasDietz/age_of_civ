/**
 * @file test_ai_offers_goods.cpp
 * @brief The AI sells (plan B4, step 1.5): its surplus as a standing supply
 *        contract to a met, peaceful civ that needs it, priced between both
 *        sides' break-even, one per buyer and good; and, as the sole source
 *        of a good, exclusive access on a staggered turn. The old sell path
 *        logged a wish and added a relation note.
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

using aoc::PlayerId;
using aoc::sim::DealTerm;
using aoc::sim::DealTermType;
using aoc::sim::goods::SILK;
using aoc::sim::goods::WHEAT;

namespace {

constexpr PlayerId HUMAN{0}; // makeWorld marks seat 0 human
constexpr PlayerId SELLER{1};
constexpr PlayerId BUYER{2};

/// A turn on which (turn + good) is a multiple of the offer period.
[[nodiscard]] constexpr int32_t staggeredTurn(uint16_t good) {
    return 2 * aoc::sim::EXCLUSIVE_OFFER_PERIOD - static_cast<int32_t>(good % aoc::sim::EXCLUSIVE_OFFER_PERIOD);
}

struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(3);
    aoc::sim::DiplomacyManager d;
    aoc::sim::GlobalDealTracker tracker;
    aoc::game::City* alpha = nullptr;
    aoc::game::City* beta  = nullptr;
    aoc::game::City* gamma = nullptr;

    Fixture() {
        this->d.initialize(3);
        this->d.meetPlayers(SELLER, BUYER, 1);
        this->d.meetPlayers(SELLER, HUMAN, 1);
        this->alpha = &aoc::test::addCityAt(this->world, HUMAN, 4, 4, "Alpha");
        this->beta  = &aoc::test::addCityAt(this->world, SELLER, 14, 8, "Beta");
        this->gamma = &aoc::test::addCityAt(this->world, BUYER, 20, 12, "Gamma");
        for (const PlayerId id : {HUMAN, SELLER, BUYER}) {
            this->world.gameState.player(id)->setTreasury(1000, aoc::sim::MoneyFlow::external());
        }
    }

    void needs(PlayerId who, uint16_t good, int32_t amount) {
        this->world.gameState.player(who)->economy().totalNeeds[good] = amount;
    }
    [[nodiscard]] aoc::CurrencyAmount gold(PlayerId who) const {
        return this->world.gameState.player(who)->treasury();
    }
    bool offerGoods(int32_t turn = 10) {
        return aoc::sim::aiOfferGoods(this->world.gameState, this->world.grid, this->tracker, this->d, SELLER,
                                      turn);
    }
    bool offerExclusive(int32_t turn) {
        return aoc::sim::aiOfferExclusiveAccess(this->world.gameState, this->world.grid, this->tracker, this->d,
                                                SELLER, turn);
    }

    /// The seller's loss and the buyer's gain on a bare term, by their own valuations.
    [[nodiscard]] int32_t sellerLoss(const DealTerm& term) const { return -this->valueFor(SELLER, term); }
    [[nodiscard]] int32_t buyerGain(const DealTerm& term) const { return this->valueFor(BUYER, term); }
    [[nodiscard]] int32_t valueFor(PlayerId who, const DealTerm& term) const {
        aoc::sim::DiplomaticDeal deal;
        deal.playerA = SELLER;
        deal.playerB = BUYER;
        deal.terms.push_back(term);
        return aoc::sim::dealValueFor(this->world.gameState, this->d, who, deal);
    }

    [[nodiscard]] static DealTerm bare(DealTermType type, uint16_t good, int32_t amount) {
        DealTerm term{};
        term.type       = type;
        term.fromPlayer = SELLER;
        term.toPlayer   = BUYER;
        term.goodId     = good;
        term.goodAmount = amount;
        term.duration   = aoc::sim::CONTRACT_OFFER_TURNS;
        return term;
    }

    [[nodiscard]] const DealTerm* term(DealTermType type, std::size_t dealIndex = 0) const {
        if (dealIndex >= this->tracker.activeDeals.size()) {
            return nullptr;
        }
        for (const DealTerm& t : this->tracker.activeDeals[dealIndex].terms) {
            if (t.type == type) {
                return &t;
            }
        }
        return nullptr;
    }
};

} // namespace

TEST_CASE("an AI offers its surplus as a supply contract priced between both sides' break-even") {
    Fixture f;
    f.beta->stockpile().addGoods(SILK, 30);
    f.needs(BUYER, SILK, 1);
    const DealTerm bare  = Fixture::bare(DealTermType::SupplyContract, SILK, 1);
    const int32_t loss   = f.sellerLoss(bare);
    const int32_t gain   = f.buyerGain(bare);
    REQUIRE(loss > 0);
    REQUIRE(gain > loss);

    CHECK(f.offerGoods());
    REQUIRE(f.tracker.activeDeals.size() == 1);
    const DealTerm* contract = f.term(DealTermType::SupplyContract);
    REQUIRE(contract != nullptr);
    CHECK(contract->fromPlayer == SELLER);
    CHECK(contract->toPlayer == BUYER);
    CHECK(contract->goodId == SILK);
    CHECK(contract->goodAmount == 1); // a luxury: one a turn covers the upkeep
    CHECK(contract->duration == aoc::sim::CONTRACT_OFFER_TURNS);
    CHECK(contract->goldPerTurn * contract->duration >= loss);
    CHECK(contract->goldPerTurn * contract->duration <= gain);
    CHECK(f.tracker.activeDeals.front().turnsRemaining == aoc::sim::CONTRACT_OFFER_TURNS);

    CHECK_FALSE(f.offerGoods()); // one contract per buyer and good
}

TEST_CASE("bulk ships at the buyer's need, capped, and the deepest surplus is offered first") {
    Fixture f;
    f.beta->stockpile().addGoods(WHEAT, 100);
    f.beta->stockpile().addGoods(SILK, 30);
    f.needs(BUYER, WHEAT, 40);
    f.needs(BUYER, SILK, 1);

    CHECK(f.offerGoods());
    const DealTerm* wheat = f.term(DealTermType::SupplyContract, 0);
    REQUIRE(wheat != nullptr);
    CHECK(wheat->goodId == WHEAT);
    CHECK(wheat->goodAmount == aoc::sim::CONTRACT_BULK_PER_TURN);

    CHECK(f.offerGoods()); // the next good, now that wheat is under contract
    REQUIRE(f.tracker.activeDeals.size() == 2);
    const DealTerm* silk = f.term(DealTermType::SupplyContract, 1);
    REQUIRE(silk != nullptr);
    CHECK(silk->goodId == SILK);

    Fixture g;
    g.beta->stockpile().addGoods(WHEAT, 100);
    g.needs(BUYER, WHEAT, 3);
    CHECK(g.offerGoods());
    const DealTerm* small = g.term(DealTermType::SupplyContract);
    REQUIRE(small != nullptr);
    CHECK(small->goodAmount == 3);
}

TEST_CASE("a human buyer gets the contract offer in the inbox and nothing binds") {
    Fixture f;
    f.beta->stockpile().addGoods(SILK, 30);
    f.needs(HUMAN, SILK, 1);
    CHECK(f.offerGoods());
    REQUIRE(f.world.gameState.pendingProposals().size() == 1);
    const aoc::sim::PendingProposal& p = f.world.gameState.pendingProposals().front();
    CHECK(p.from == SELLER);
    CHECK(p.to == HUMAN);
    REQUIRE(p.deal.terms.size() == 1);
    CHECK(p.deal.terms[0].type == DealTermType::SupplyContract);
    CHECK(f.tracker.activeDeals.empty());
    CHECK(f.gold(HUMAN) == 1000);
}

TEST_CASE("no need, too little to spare, own need, war or embargo: nothing is offered") {
    {
        Fixture f;
        f.beta->stockpile().addGoods(SILK, 30);
        CHECK_FALSE(f.offerGoods()); // nobody needs it
    }
    {
        Fixture f;
        f.beta->stockpile().addGoods(SILK, aoc::sim::CONTRACT_COVER_TURNS - 1);
        f.needs(BUYER, SILK, 1);
        CHECK_FALSE(f.offerGoods()); // cannot cover the contract
    }
    {
        Fixture f;
        f.beta->stockpile().addGoods(SILK, 30);
        f.needs(SELLER, SILK, 25); // keeps what it needs itself
        f.needs(BUYER, SILK, 1);
        CHECK_FALSE(f.offerGoods());
    }
    {
        Fixture f;
        f.beta->stockpile().addGoods(SILK, 30);
        f.needs(BUYER, SILK, 1);
        f.d.declareWar(SELLER, BUYER);
        CHECK_FALSE(f.offerGoods());
    }
    {
        Fixture f;
        f.beta->stockpile().addGoods(SILK, 30);
        f.needs(BUYER, SILK, 1);
        f.d.setEmbargo(BUYER, SELLER, true);
        CHECK_FALSE(f.offerGoods());
    }
    CHECK(Fixture{}.tracker.activeDeals.empty());
}

TEST_CASE("a sole source offers exclusive access on its staggered turn for a lump within both valuations") {
    Fixture f;
    f.beta->stockpile().addGoods(SILK, 30);
    f.needs(BUYER, SILK, 1);
    const int32_t on     = staggeredTurn(SILK);
    const DealTerm bare  = Fixture::bare(DealTermType::ExclusiveAccess, SILK, 0);
    const int32_t loss   = f.sellerLoss(bare);
    const int32_t gain   = f.buyerGain(bare);
    REQUIRE(loss > 0);
    REQUIRE(gain > loss);

    CHECK_FALSE(f.offerExclusive(on + 1)); // not this good's turn
    CHECK(f.offerExclusive(on));
    REQUIRE(f.tracker.activeDeals.size() == 1);
    const DealTerm* access = f.term(DealTermType::ExclusiveAccess);
    const DealTerm* pay    = f.term(DealTermType::GoldLump);
    REQUIRE(access != nullptr);
    REQUIRE(pay != nullptr);
    CHECK(access->fromPlayer == SELLER);
    CHECK(access->toPlayer == BUYER);
    CHECK(access->goodId == SILK);
    CHECK(pay->fromPlayer == BUYER);
    CHECK(pay->goldLump >= loss);
    CHECK(pay->goldLump <= gain);
    CHECK(f.gold(BUYER) == 1000 - pay->goldLump);
    CHECK(f.gold(SELLER) == 1000 + pay->goldLump);
    CHECK(f.tracker.activeDeals.front().turnsRemaining == aoc::sim::CONTRACT_OFFER_TURNS);
    // Everyone else is cut off from the seller's silk; the buyer is not.
    CHECK(f.d.relation(SELLER, HUMAN).isGoodEmbargoed(SILK));
    CHECK_FALSE(f.d.relation(SELLER, BUYER).isGoodEmbargoed(SILK));

    CHECK_FALSE(f.offerExclusive(on)); // not again while it stands
}

TEST_CASE("no exclusive offer from a civ that is not the sole source, and none a poor buyer cannot pay") {
    const int32_t on = staggeredTurn(SILK);
    {
        Fixture f;
        f.beta->stockpile().addGoods(SILK, 30);
        f.alpha->stockpile().addGoods(SILK, 5); // someone else holds it too
        f.needs(BUYER, SILK, 1);
        CHECK_FALSE(f.offerExclusive(on));
    }
    {
        Fixture f;
        f.beta->stockpile().addGoods(SILK, 30);
        f.needs(BUYER, SILK, 1);
        const int32_t loss = f.sellerLoss(Fixture::bare(DealTermType::ExclusiveAccess, SILK, 0));
        REQUIRE(loss > 1);
        f.world.gameState.player(BUYER)->setTreasury(loss - 1, aoc::sim::MoneyFlow::external());
        CHECK_FALSE(f.offerExclusive(on)); // would leave the seller short
        f.world.gameState.player(BUYER)->setTreasury(loss, aoc::sim::MoneyFlow::external());
        CHECK(f.offerExclusive(on)); // pays what it has, which just covers the seller
        const DealTerm* pay = f.term(DealTermType::GoldLump);
        REQUIRE(pay != nullptr);
        CHECK(pay->goldLump == loss);
    }
}

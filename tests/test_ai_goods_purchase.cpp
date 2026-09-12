/**
 * @file test_ai_goods_purchase.cpp
 * @brief An AI buying goods goes through the request layer, so a human seller
 *        answers from the inbox instead of having goods lifted from its cities.
 *
 *        The buy loop used to call proposeDeal and acceptDeal directly, gated
 *        only by the AI valuation of the SELLER's side -- which for a human
 *        seller meant the AI decided on the human's behalf.
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

namespace {

constexpr uint16_t GOOD = aoc::sim::goods::OIL;

struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(3); // player 0 is the human seat
    aoc::sim::DiplomacyManager d;
    aoc::sim::GlobalDealTracker tracker;
    aoc::game::City* alpha = nullptr;
    aoc::game::City* gamma = nullptr;

    Fixture() {
        this->d.initialize(3);
        this->d.meetPlayers(PlayerId{0}, PlayerId{1}, 2);
        this->d.meetPlayers(PlayerId{1}, PlayerId{2}, 2);
        this->alpha = &aoc::test::addCityAt(this->world, PlayerId{0}, 4, 4, "Alpha");
        aoc::test::addCityAt(this->world, PlayerId{1}, 14, 8, "Beta");
        this->gamma = &aoc::test::addCityAt(this->world, PlayerId{2}, 20, 12, "Gamma");
        this->world.gameState.player(PlayerId{1})->setTreasury(5000, aoc::sim::MoneyFlow::external());
    }

    bool offer() {
        return aoc::sim::aiOfferToBuy(this->world.gameState, this->world.grid, this->tracker, this->d,
                                      PlayerId{1}, GOOD, 5, 10);
    }
};

} // namespace

TEST_CASE("an AI buying from a human puts the offer in the inbox and moves nothing") {
    Fixture f;
    f.alpha->stockpile().addGoods(GOOD, 20);
    const auto goldBefore = f.world.gameState.player(PlayerId{1})->treasury();

    CHECK(f.offer());

    REQUIRE(f.world.gameState.pendingProposals().size() == 1);
    const aoc::sim::PendingProposal& p = f.world.gameState.pendingProposals().front();
    CHECK(p.from == PlayerId{1});
    CHECK(p.to == PlayerId{0});
    CHECK(f.alpha->stockpile().getAmount(GOOD) == 20);
    CHECK(f.world.gameState.player(PlayerId{1})->treasury() == goldBefore);
    CHECK(f.tracker.activeDeals.empty());
}

TEST_CASE("an AI buying from an AI settles at once") {
    Fixture f;
    f.gamma->stockpile().addGoods(GOOD, 20);
    const auto buyerBefore  = f.world.gameState.player(PlayerId{1})->treasury();
    const auto sellerBefore = f.world.gameState.player(PlayerId{2})->treasury();

    CHECK(f.offer());

    CHECK(f.world.gameState.pendingProposals().empty());
    CHECK(f.gamma->stockpile().getAmount(GOOD) == 15);
    CHECK(f.world.gameState.player(PlayerId{1})->treasury() < buyerBefore);
    CHECK(f.world.gameState.player(PlayerId{2})->treasury() > sellerBefore);
    REQUIRE(f.tracker.activeDeals.size() == 1);
}

TEST_CASE("a human accepting the inbox offer completes the sale") {
    Fixture f;
    f.alpha->stockpile().addGoods(GOOD, 20);
    REQUIRE(f.offer());
    const auto sellerBefore = f.world.gameState.player(PlayerId{0})->treasury();

    REQUIRE(aoc::sim::requestRespondToProposal(f.world.gameState, f.world.grid, f.tracker, f.d, PlayerId{0}, 0,
                                               true, 11) == aoc::ErrorCode::Ok);

    CHECK(f.world.gameState.pendingProposals().empty());
    CHECK(f.alpha->stockpile().getAmount(GOOD) == 15);
    CHECK(f.world.gameState.player(PlayerId{0})->treasury() > sellerBefore);
}

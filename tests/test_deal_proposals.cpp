/**
 * @file test_deal_proposals.cpp
 * @brief Deal proposals: AI valuation and instant answers, the human inbox, expiry, validation.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/diplomacy/DealProposals.hpp"
#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"

#include <string>

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::sim::DealTerm;
using aoc::sim::DealTermType;
using aoc::sim::DiplomaticDeal;

namespace {

struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(3); // player 0 is the human seat
    aoc::sim::DiplomacyManager d;
    aoc::sim::GlobalDealTracker tracker;

    Fixture() {
        this->d.initialize(3);
        this->d.meetPlayers(PlayerId{0}, PlayerId{1}, 2);
        this->d.meetPlayers(PlayerId{1}, PlayerId{2}, 2);
        aoc::test::addCityAt(this->world, PlayerId{0}, 4, 4, "Alpha").setPopulation(4);
        aoc::test::addCityAt(this->world, PlayerId{1}, 14, 8, "Beta").setPopulation(2);
        aoc::test::addCityAt(this->world, PlayerId{2}, 20, 12, "Gamma");
    }

    static DealTerm gold(PlayerId from, PlayerId to, int32_t amount) {
        DealTerm t{};
        t.type       = DealTermType::GoldLump;
        t.fromPlayer = from;
        t.toPlayer   = to;
        t.goldLump   = amount;
        return t;
    }

    static DealTerm pact(DealTermType type, PlayerId from, PlayerId to) {
        DealTerm t{};
        t.type       = type;
        t.fromPlayer = from;
        t.toPlayer   = to;
        return t;
    }

    static DiplomaticDeal deal(PlayerId a, PlayerId b, std::initializer_list<DealTerm> terms) {
        DiplomaticDeal dl;
        dl.playerA = a;
        dl.playerB = b;
        dl.terms.assign(terms.begin(), terms.end());
        return dl;
    }

    ErrorCode propose(const DiplomaticDeal& dl, int32_t turn = 10) {
        return aoc::sim::requestProposeDeal(this->world.gameState, this->world.grid, this->tracker, this->d, dl,
                                            turn);
    }
};

} // namespace

TEST_CASE("an AI accepts a deal worth at least nothing to it at once and declines a bad one") {
    Fixture f;
    aoc::game::Player& me   = *f.world.gameState.player(PlayerId{0});
    aoc::game::Player& them = *f.world.gameState.player(PlayerId{1});
    me.addGold(300);

    // Gift: +100 for the AI -> applied immediately.
    CHECK(f.propose(Fixture::deal(PlayerId{0}, PlayerId{1}, {Fixture::gold(PlayerId{0}, PlayerId{1}, 100)}))
          == ErrorCode::Ok);
    CHECK(them.treasury() == 100);
    CHECK(me.treasury() == 200);
    REQUIRE(f.tracker.activeDeals.size() == 1);
    CHECK(f.tracker.activeDeals.front().isAccepted);

    // Demand: -100 for the AI -> declined, nothing moves.
    CHECK(f.propose(Fixture::deal(PlayerId{0}, PlayerId{1}, {Fixture::gold(PlayerId{1}, PlayerId{0}, 100)}))
          == ErrorCode::InvalidState);
    CHECK(them.treasury() == 100);
    CHECK(f.tracker.activeDeals.size() == 1);

    // Balanced swap with a sweetener: 60 for 50 -> accepted.
    CHECK(f.propose(Fixture::deal(PlayerId{0}, PlayerId{1},
                                  {Fixture::gold(PlayerId{0}, PlayerId{1}, 60), Fixture::gold(PlayerId{1}, PlayerId{0}, 50)}))
          == ErrorCode::Ok);
    CHECK(them.treasury() == 110);
    CHECK(me.treasury() == 190);
}

TEST_CASE("valuation follows stance for pacts and blame for war guilt") {
    Fixture f;
    const aoc::game::GameState& gs = f.world.gameState;
    const DiplomaticDeal borders =
        Fixture::deal(PlayerId{0}, PlayerId{1}, {Fixture::pact(DealTermType::OpenBorders, PlayerId{0}, PlayerId{1})});
    CHECK(aoc::sim::dealValueFor(gs, f.d, PlayerId{1}, borders) < 0); // neutral: cool
    f.d.addModifier(PlayerId{0}, PlayerId{1}, {"Test goodwill", 20, 0});
    CHECK(aoc::sim::dealValueFor(gs, f.d, PlayerId{1}, borders) >= 0); // friendly: fine
    f.d.relation(PlayerId{1}, PlayerId{0}).modifiers.clear();
    f.d.addModifier(PlayerId{0}, PlayerId{1}, {"Test insult", -50, 0});
    CHECK(aoc::sim::dealValueFor(gs, f.d, PlayerId{1}, borders) < -50); // hostile: cold

    const DiplomaticDeal guilt =
        Fixture::deal(PlayerId{0}, PlayerId{1}, {Fixture::pact(DealTermType::WarGuilt, PlayerId{1}, PlayerId{0})});
    CHECK(aoc::sim::dealValueFor(gs, f.d, PlayerId{1}, guilt) < -100); // player 1 would accept blame
    CHECK(aoc::sim::dealValueFor(gs, f.d, PlayerId{0}, guilt) > 0);

    DealTerm city{};
    city.type       = DealTermType::CedeCity;
    city.fromPlayer = PlayerId{0};
    city.toPlayer   = PlayerId{1};
    city.tileCoord  = {4, 4}; // Alpha, population 4
    const DiplomaticDeal cession = Fixture::deal(PlayerId{0}, PlayerId{1}, {city});
    const int32_t value = aoc::sim::dealValueFor(gs, f.d, PlayerId{1}, cession);
    CHECK(value == 400); // 200 + 4 * 50; hostility adds no goodwill and costs none
    CHECK(aoc::sim::describeDealTerm(gs, city).find("city at (4,4) ceded by") == 0);
    CHECK(aoc::sim::describeDealTerm(gs, Fixture::gold(PlayerId{0}, PlayerId{1}, 25)).find("25 gold (") == 0);
}

TEST_CASE("proposals to the human wait in the inbox until answered or expired") {
    Fixture f;
    aoc::game::GameState& gs = f.world.gameState;
    aoc::game::Player& human = *gs.player(PlayerId{0});
    aoc::game::Player& ai    = *gs.player(PlayerId{1});
    ai.addGold(500);

    const DiplomaticDeal offer =
        Fixture::deal(PlayerId{1}, PlayerId{0}, {Fixture::gold(PlayerId{1}, PlayerId{0}, 50)});
    CHECK(f.propose(offer, 10) == ErrorCode::Ok);
    REQUIRE(gs.pendingProposals().size() == 1);
    CHECK(gs.pendingProposals().front().from == PlayerId{1});
    CHECK(gs.pendingProposals().front().expiresTurn == 10 + aoc::sim::PROPOSAL_TTL_TURNS);
    CHECK(human.treasury() == 0); // nothing applied yet
    CHECK(f.propose(offer, 11) == ErrorCode::InvalidState); // one open proposal per pair

    CHECK(aoc::sim::requestRespondToProposal(gs, f.world.grid, f.tracker, PlayerId{2}, 0, true)
          == ErrorCode::InvalidState); // not addressed to player 2
    CHECK(aoc::sim::requestRespondToProposal(gs, f.world.grid, f.tracker, PlayerId{0}, 4, true)
          == ErrorCode::EntityNotFound);
    CHECK(aoc::sim::requestRespondToProposal(gs, f.world.grid, f.tracker, PlayerId{0}, 0, true) == ErrorCode::Ok);
    CHECK(gs.pendingProposals().empty());
    CHECK(human.treasury() == 50);
    CHECK(ai.treasury() == 450);

    CHECK(f.propose(offer, 12) == ErrorCode::Ok);
    CHECK(aoc::sim::requestRespondToProposal(gs, f.world.grid, f.tracker, PlayerId{0}, 0, false) == ErrorCode::Ok);
    CHECK(gs.pendingProposals().empty());
    CHECK(human.treasury() == 50);

    CHECK(f.propose(offer, 20) == ErrorCode::Ok);
    aoc::sim::expireProposals(gs, 20 + aoc::sim::PROPOSAL_TTL_TURNS - 1);
    CHECK(gs.pendingProposals().size() == 1);
    aoc::sim::expireProposals(gs, 20 + aoc::sim::PROPOSAL_TTL_TURNS);
    CHECK(gs.pendingProposals().empty());
}

TEST_CASE("malformed proposals are refused before anyone is asked") {
    Fixture f;
    aoc::game::GameState& gs = f.world.gameState;
    CHECK(f.propose(Fixture::deal(PlayerId{0}, PlayerId{0}, {Fixture::gold(PlayerId{0}, PlayerId{0}, 1)}))
          == ErrorCode::EntityNotFound);
    CHECK(f.propose(Fixture::deal(PlayerId{0}, PlayerId{200}, {Fixture::gold(PlayerId{0}, PlayerId{200}, 1)}))
          == ErrorCode::EntityNotFound);
    CHECK(f.propose(Fixture::deal(PlayerId{0}, PlayerId{2}, {Fixture::gold(PlayerId{0}, PlayerId{2}, 1)}))
          == ErrorCode::InvalidState); // never met
    CHECK(f.propose(Fixture::deal(PlayerId{0}, PlayerId{1}, {})) == ErrorCode::InvalidArgument);
    CHECK(f.propose(Fixture::deal(PlayerId{0}, PlayerId{1}, {Fixture::gold(PlayerId{2}, PlayerId{1}, 1)}))
          == ErrorCode::InvalidArgument); // a third party's gold
    f.d.declareWar(PlayerId{0}, PlayerId{1}, aoc::sim::CasusBelliType::SurpriseWar, nullptr, &gs, 5);
    CHECK(f.propose(Fixture::deal(PlayerId{0}, PlayerId{1}, {Fixture::pact(DealTermType::OpenBorders, PlayerId{0}, PlayerId{1})}))
          == ErrorCode::InvalidState); // no open borders at war
    // A gold gift during war is still a valid proposal (an AI recipient takes it).
    gs.player(PlayerId{0})->addGold(10);
    CHECK(f.propose(Fixture::deal(PlayerId{0}, PlayerId{1}, {Fixture::gold(PlayerId{0}, PlayerId{1}, 10)}))
          == ErrorCode::Ok);
    // An accepted deal the payer cannot afford is rejected by acceptDeal and leaves no trace.
    CHECK(f.propose(Fixture::deal(PlayerId{0}, PlayerId{1}, {Fixture::gold(PlayerId{0}, PlayerId{1}, 999)}))
          == ErrorCode::InsufficientResources);
    CHECK(f.tracker.activeDeals.size() == 1);
}

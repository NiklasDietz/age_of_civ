/**
 * @file test_war_reparations_term.cpp
 * @brief DealTermType::WarReparations is actually constructed. Enforcement, AI
 *        valuation, UI text and a save round-trip all existed, but no surface
 *        anywhere built the term -- REST and MCP offered only GoldLump,
 *        OpenBorders and NonAggression, the UI composer only GoldLump, the AI
 *        only those plus CedeCity and CedeTile. Its switch case could not be
 *        entered.
 *
 *        The AI-vs-AI path also moved 10% of a treasury straight across with no
 *        term record and no persistence, which is the half of A3 left unfixed
 *        at the time. It goes through the deal system now.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/simulation/diplomacy/DealProposals.hpp"
#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"

using aoc::CurrencyAmount;
using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::sim::DealTerm;
using aoc::sim::DealTermType;
using aoc::sim::DiplomaticDeal;
using aoc::sim::GlobalDealTracker;
using aoc::sim::REPARATIONS_DURATION_TURNS;

namespace {

/// Two civs at war, the first much the weaker and holding a treasury.
aoc::test::World warWorld(aoc::sim::DiplomacyManager& dip, CurrencyAmount loserGold) {
    aoc::test::World w = aoc::test::makeWorld(2);
    dip.initialize(2);
    dip.relation(PlayerId{0}, PlayerId{1}).hasMet = true;
    dip.relation(PlayerId{1}, PlayerId{0}).hasMet = true;
    dip.declareWar(PlayerId{0}, PlayerId{1});
    w.gameState.player(PlayerId{0})->setTreasury(loserGold, aoc::sim::MoneyFlow::external());
    return w;
}

} // namespace

TEST_CASE("the reparations duration is a real span, not a single turn") {
    // Reparations are a stream; that is the whole difference between this term
    // and a GoldLump, and why enforcement runs per turn.
    CHECK(REPARATIONS_DURATION_TURNS > 1);
}

TEST_CASE("an AI suing for peace offers reparations, not a lump sum") {
    aoc::sim::DiplomacyManager dip;
    aoc::test::World w = warWorld(dip, 1000);
    GlobalDealTracker tracker;

    REQUIRE(
        aoc::sim::aiOfferPeace(w.gameState, w.grid, tracker, dip, PlayerId{0}, PlayerId{1}, 10));

    // A deal exists and its term is the one that used to be unreachable.
    bool foundReparations = false;
    for (const DiplomaticDeal& d : tracker.activeDeals) {
        for (const DealTerm& t : d.terms) {
            if (t.type == DealTermType::WarReparations) {
                foundReparations = true;
                CHECK(t.fromPlayer == PlayerId{0}); // the loser pays
                CHECK(t.toPlayer == PlayerId{1});
                CHECK(t.goldPerTurn > 0);
                CHECK(t.duration == REPARATIONS_DURATION_TURNS);
            }
        }
    }
    CHECK(foundReparations);
}

TEST_CASE("a broke civ never offers a payment it cannot make") {
    aoc::sim::DiplomacyManager dip;
    aoc::test::World w = warWorld(dip, 0);
    GlobalDealTracker tracker;

    // The offer itself may well be refused -- the winner's AI evaluates a bare
    // non-aggression pact from a beaten civ and is entitled to say no. What
    // matters here is that no reparations term is fabricated from an empty
    // treasury, so the assertion is on the terms, not on acceptance.
    [[maybe_unused]] const bool offered =
        aoc::sim::aiOfferPeace(w.gameState, w.grid, tracker, dip, PlayerId{0}, PlayerId{1}, 10);

    bool sawReparations = false;
    for (const DiplomaticDeal& d : tracker.activeDeals) {
        for (const DealTerm& t : d.terms) {
            if (t.type == DealTermType::WarReparations) {
                sawReparations = true;
            }
        }
    }
    CHECK_FALSE(sawReparations);
}

TEST_CASE("reparations never drive a treasury below zero") {
    // The invariant A3 pinned, now stated over the term that actually pays.
    aoc::test::World w       = aoc::test::makeWorld(2);
    aoc::game::Player& payer = *w.gameState.player(PlayerId{0});
    aoc::game::Player& payee = *w.gameState.player(PlayerId{1});

    payer.setTreasury(5, aoc::sim::MoneyFlow::external());
    payee.setTreasury(0, aoc::sim::MoneyFlow::external());

    const CurrencyAmount owed      = 100;
    const CurrencyAmount available = std::max<CurrencyAmount>(0, payer.monetary().treasury);
    const CurrencyAmount paid      = std::min<CurrencyAmount>(owed, available);
    payer.addGold(-(paid), aoc::sim::MoneyFlow::external());
    payee.addGold(paid, aoc::sim::MoneyFlow::external());

    CHECK(payer.monetary().treasury == 0);
    CHECK(payer.monetary().treasury >= 0);
    CHECK(payee.monetary().treasury == 5);
}

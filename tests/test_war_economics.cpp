/**
 * @file test_war_economics.cpp
 * @brief Trade is a reason not to fight (plan B6, 4.2). What a civ earns from a
 *        partner is priced into the war decision: an AI declaration is refused
 *        above the veto whichever of the three war paths asks for it, because
 *        they all go through requestDeclareWar, and a partner worth keeping is
 *        granted peace by the side that is winning.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/simulation/diplomacy/CasusBelli.hpp"
#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/diplomacy/DiplomacyActions.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"

#include <cstdint>
#include <string>

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::sim::CasusBelliType;
using aoc::sim::DiplomacyManager;
using aoc::sim::economicCostOfWar;
using aoc::sim::PEACE_TRADE_VALUE;
using aoc::sim::WAR_COST_MAX_POINTS;
using aoc::sim::WAR_COST_VETO_POINTS;
using aoc::sim::warCostRelationPoints;

namespace {

constexpr aoc::UnitTypeId WARRIOR{0};
constexpr PlayerId HUMAN{0}; ///< GameState::humanPlayerId defaults to this seat
constexpr PlayerId TRADER_CIV{1};
constexpr PlayerId NEIGHBOUR{2};
const std::string TIE = aoc::sim::TRADE_PARTNER_REASON;

struct Courts {
    aoc::test::World world = aoc::test::makeWorld(3);
    DiplomacyManager d;

    Courts() {
        this->d.initialize(3);
        this->d.meetPlayers(TRADER_CIV, NEIGHBOUR, 1);
        this->d.meetPlayers(HUMAN, NEIGHBOUR, 1);
        aoc::test::addCityAt(this->world, HUMAN, 4, 4, "Alpha");
        aoc::test::addCityAt(this->world, TRADER_CIV, 14, 8, "Beta");
        aoc::test::addCityAt(this->world, NEIGHBOUR, 20, 12, "Gamma");
    }

    /// A standing contract shipping `goods` a turn and paying `gold` a turn to `to`.
    void pays(PlayerId from, PlayerId to, int32_t gold, int32_t goods = 0) {
        aoc::sim::DealTerm term{};
        term.type        = aoc::sim::DealTermType::SupplyContract;
        term.fromPlayer  = from;
        term.toPlayer    = to;
        term.goldPerTurn = gold;
        term.goodAmount  = goods;
        aoc::sim::DiplomaticDeal deal{};
        deal.playerA    = from;
        deal.playerB    = to;
        deal.isAccepted = true;
        deal.terms.push_back(term);
        this->world.gameState.deals().activeDeals.push_back(deal);
    }
};

} // namespace

TEST_CASE("a war with nothing to lose costs nothing") {
    Courts c;
    CHECK(economicCostOfWar(c.world.gameState, c.d, TRADER_CIV, NEIGHBOUR) == 0);
    CHECK(warCostRelationPoints(c.world.gameState, c.d, TRADER_CIV, NEIGHBOUR) == 0);
    CHECK(economicCostOfWar(c.world.gameState, c.d, TRADER_CIV, TRADER_CIV) == 0);
    CHECK(economicCostOfWar(c.world.gameState, c.d, TRADER_CIV, aoc::INVALID_PLAYER) == 0);
}

TEST_CASE("the cost of a war is the trade tie plus what the standing deals bring in") {
    Courts c;
    c.d.refreshModifier(TRADER_CIV, NEIGHBOUR, TIE, 12, aoc::sim::TRADE_PARTNER_TURNS);
    CHECK(economicCostOfWar(c.world.gameState, c.d, TRADER_CIV, NEIGHBOUR) == 12);

    c.pays(NEIGHBOUR, TRADER_CIV, 20, 5); // twenty gold and five units a turn, to us
    CHECK(economicCostOfWar(c.world.gameState, c.d, TRADER_CIV, NEIGHBOUR) == 37);

    // What we ship THEM is their loss to count, not ours: both sides hold the
    // same tie, only the contract is one-way.
    CHECK(economicCostOfWar(c.world.gameState, c.d, NEIGHBOUR, TRADER_CIV) == 12);

    // A contract already broken pays nobody.
    c.world.gameState.deals().activeDeals.back().isBroken = true;
    CHECK(economicCostOfWar(c.world.gameState, c.d, TRADER_CIV, NEIGHBOUR) == 12);
}

TEST_CASE("the points weigh the leader's interest in commerce, and stop at the cap") {
    aoc::sim::CivId keen{0};
    aoc::sim::CivId cool{0};
    float best  = -1.0f;
    float worst = 1e9f;
    for (uint8_t i = 0; i < aoc::sim::CIV_COUNT; ++i) {
        const aoc::sim::CivId id = static_cast<aoc::sim::CivId>(i);
        const float focus        = aoc::sim::leaderPersonality(id).behavior.economicFocus;
        if (focus > best) {
            best = focus;
            keen = id;
        }
        if (focus < worst) {
            worst = focus;
            cool  = id;
        }
    }
    REQUIRE(best > worst); // the data really does distinguish them

    Courts c;
    c.pays(NEIGHBOUR, TRADER_CIV, 40);
    c.world.gameState.player(TRADER_CIV)->setCivId(keen);
    const int32_t keenPoints = warCostRelationPoints(c.world.gameState, c.d, TRADER_CIV, NEIGHBOUR);
    c.world.gameState.player(TRADER_CIV)->setCivId(cool);
    const int32_t coolPoints = warCostRelationPoints(c.world.gameState, c.d, TRADER_CIV, NEIGHBOUR);
    CHECK(keenPoints > coolPoints);

    Courts rich;
    rich.pays(NEIGHBOUR, TRADER_CIV, 100000);
    CHECK(warCostRelationPoints(rich.world.gameState, rich.d, TRADER_CIV, NEIGHBOUR) ==
          WAR_COST_MAX_POINTS);
}

TEST_CASE("an AI does not declare war on a partner it cannot afford to lose") {
    Courts lone; // two civs who trade nothing
    CHECK(warCostRelationPoints(lone.world.gameState, lone.d, TRADER_CIV, NEIGHBOUR) <=
          WAR_COST_VETO_POINTS);
    CHECK(aoc::sim::requestDeclareWar(lone.world.gameState, lone.d, TRADER_CIV, NEIGHBOUR,
                                      CasusBelliType::SurpriseWar, 10) == ErrorCode::Ok);
    CHECK(lone.d.isAtWar(TRADER_CIV, NEIGHBOUR));

    Courts tied; // the same war, with a partnership behind it
    tied.pays(NEIGHBOUR, TRADER_CIV, 400);
    REQUIRE(warCostRelationPoints(tied.world.gameState, tied.d, TRADER_CIV, NEIGHBOUR) >
            WAR_COST_VETO_POINTS);
    CHECK(aoc::sim::requestDeclareWar(tied.world.gameState, tied.d, TRADER_CIV, NEIGHBOUR,
                                      CasusBelliType::SurpriseWar, 10) == ErrorCode::InvalidState);
    CHECK_FALSE(tied.d.isAtWar(TRADER_CIV, NEIGHBOUR));
}

TEST_CASE("a human is told the cost and decides for itself") {
    Courts c;
    c.pays(NEIGHBOUR, HUMAN, 400);
    REQUIRE(c.world.gameState.humanPlayerId() == HUMAN);
    REQUIRE(warCostRelationPoints(c.world.gameState, c.d, HUMAN, NEIGHBOUR) > WAR_COST_VETO_POINTS);
    CHECK(aoc::sim::requestDeclareWar(c.world.gameState, c.d, HUMAN, NEIGHBOUR,
                                      CasusBelliType::SurpriseWar, 10) == ErrorCode::Ok);
}

TEST_CASE("a partner worth keeping is granted peace by the side that is winning") {
    Courts c;
    aoc::game::GameState& gs = c.world.gameState;
    for (int32_t i = 0; i < 6; ++i) {
        aoc::test::addUnitAt(c.world, TRADER_CIV, WARRIOR, 14 + i, 9);
    }
    aoc::test::addUnitAt(c.world, NEIGHBOUR, WARRIOR, 20, 13);
    REQUIRE(aoc::sim::requestDeclareWar(gs, c.d, TRADER_CIV, NEIGHBOUR, CasusBelliType::SurpriseWar,
                                        10) == ErrorCode::Ok);
    CHECK_FALSE(aoc::sim::aiAcceptsPeace(gs, c.d, TRADER_CIV, NEIGHBOUR)); // winning, and nothing owed

    // The tie decays across a war, so what counts is what is left of it.
    c.d.refreshModifier(TRADER_CIV, NEIGHBOUR, TIE, PEACE_TRADE_VALUE - 1,
                        aoc::sim::TRADE_PARTNER_TURNS);
    CHECK_FALSE(aoc::sim::aiAcceptsPeace(gs, c.d, TRADER_CIV, NEIGHBOUR));

    c.d.refreshModifier(TRADER_CIV, NEIGHBOUR, TIE, PEACE_TRADE_VALUE,
                        aoc::sim::TRADE_PARTNER_TURNS);
    CHECK(aoc::sim::aiAcceptsPeace(gs, c.d, TRADER_CIV, NEIGHBOUR));
    CHECK(aoc::sim::requestMakePeace(gs, c.d, NEIGHBOUR, TRADER_CIV, 30) == ErrorCode::Ok);
}

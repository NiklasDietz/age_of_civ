/**
 * @file test_barbarian_clans.cpp
 * @brief Barbarian clans exist and do something. The whole module was dead:
 *        GameState::m_barbarianClans was never populated, so bribeClan and
 *        hireClan -- both fully implemented -- had no caller anywhere, every
 *        clan field (type, strength, isBribed, hiredBy) was unread, and
 *        convertClanToCityState had no definition at all.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/core/Random.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/barbarian/BarbarianClans.hpp"
#include "aoc/simulation/barbarian/BarbarianController.hpp"

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::sim::BARBARIAN_CLAN_COUNT;
using aoc::sim::BARBARIAN_CLAN_DEFS;
using aoc::sim::BarbarianClanComponent;
using aoc::sim::BarbarianController;

namespace {

/// Run turns until at least one camp exists, or give up.
[[nodiscard]] bool runUntilCamp(aoc::test::World& w, BarbarianController& barbarians,
                                aoc::Random& rng, int32_t maxTurns = 60) {
    for (int32_t t = 0; t < maxTurns; ++t) {
        barbarians.executeTurn(w.gameState, w.grid, rng, nullptr);
        if (!barbarians.encampments().empty()) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("the clan table is self-consistent") {
    for (int32_t i = 0; i < BARBARIAN_CLAN_COUNT; ++i) {
        CHECK(BARBARIAN_CLAN_DEFS[i].id == i);
        CHECK_FALSE(BARBARIAN_CLAN_DEFS[i].name.empty());
    }
}

TEST_CASE("a camp that spawns is given a clan") {
    // The list was empty for the entire game before this.
    aoc::test::World w = aoc::test::makeWorld(2, 40, 30);
    BarbarianController barbarians;
    aoc::Random rng{42u};

    REQUIRE(runUntilCamp(w, barbarians, rng));

    REQUIRE_FALSE(w.gameState.barbarianClans().empty());
    const int32_t idx = barbarians.encampments()[0].clanIndex;
    CHECK(idx >= 0);
    CHECK(static_cast<std::size_t>(idx) < w.gameState.barbarianClans().size());
    CHECK(w.gameState.barbarianClans()[static_cast<std::size_t>(idx)].strength >= 1);
}

TEST_CASE("bribing a clan costs gold and buys quiet") {
    aoc::test::World w = aoc::test::makeWorld(2);
    BarbarianClanComponent clan{};
    clan.strength = 3;
    w.gameState.barbarianClans().push_back(clan);

    aoc::game::Player& payer  = *w.gameState.player(PlayerId{0});
    const int32_t cost        = aoc::sim::bribeCost(3);
    payer.setTreasury(cost, aoc::sim::MoneyFlow::external());

    REQUIRE(aoc::sim::bribeClan(w.gameState, 0, PlayerId{0}) == ErrorCode::Ok);
    CHECK(w.gameState.barbarianClans()[0].isBribed);
    CHECK(w.gameState.barbarianClans()[0].bribeTurnsLeft > 0);
    CHECK(payer.monetary().treasury < cost); // it was paid for
}

TEST_CASE("a clan that cannot be paid for is not bribed") {
    aoc::test::World w = aoc::test::makeWorld(2);
    BarbarianClanComponent clan{};
    clan.strength = 3;
    w.gameState.barbarianClans().push_back(clan);
    w.gameState.player(PlayerId{0})->setTreasury(0, aoc::sim::MoneyFlow::external());

    CHECK(aoc::sim::bribeClan(w.gameState, 0, PlayerId{0}) != ErrorCode::Ok);
    CHECK_FALSE(w.gameState.barbarianClans()[0].isBribed);
}

TEST_CASE("a bribed clan's units stand down") {
    // What the gold actually buys. isBribed was written by nobody and read by
    // nobody, so a bribe bought nothing at all.
    aoc::test::World w = aoc::test::makeWorld(2, 40, 30);
    BarbarianController barbarians;
    aoc::Random rng{42u};
    REQUIRE(runUntilCamp(w, barbarians, rng));

    // Put a victim next to the camp and bribe the clan holding it.
    const aoc::hex::AxialCoord camp = barbarians.encampments()[0].location;
    const int32_t clanIdx           = barbarians.encampments()[0].clanIndex;
    REQUIRE(clanIdx >= 0);
    w.gameState.barbarianClans()[static_cast<std::size_t>(clanIdx)].isBribed       = true;
    w.gameState.barbarianClans()[static_cast<std::size_t>(clanIdx)].bribeTurnsLeft = 20;

    aoc::game::Unit& victim =
        aoc::test::addUnitAt(w, PlayerId{0}, aoc::UnitTypeId{0}, camp.q + 1, camp.r);
    const int32_t hpBefore = victim.hitPoints();

    barbarians.executeTurn(w.gameState, w.grid, rng, nullptr);
    CHECK(victim.hitPoints() == hpBefore); // left alone
}

TEST_CASE("a bribe wears off") {
    aoc::test::World w = aoc::test::makeWorld(2, 40, 30);
    BarbarianController barbarians;
    aoc::Random rng{7u};

    BarbarianClanComponent clan{};
    clan.isBribed       = true;
    clan.bribeTurnsLeft = 2;
    w.gameState.barbarianClans().push_back(clan);

    barbarians.executeTurn(w.gameState, w.grid, rng, nullptr);
    CHECK(w.gameState.barbarianClans()[0].isBribed); // one turn left
    barbarians.executeTurn(w.gameState, w.grid, rng, nullptr);
    CHECK_FALSE(w.gameState.barbarianClans()[0].isBribed);
}

TEST_CASE("a hire wears off too") {
    aoc::test::World w = aoc::test::makeWorld(3, 40, 30);
    BarbarianController barbarians;
    aoc::Random rng{7u};

    BarbarianClanComponent clan{};
    clan.hiredBy       = PlayerId{0};
    clan.hiredTarget   = PlayerId{1};
    clan.hireTurnsLeft = 1;
    w.gameState.barbarianClans().push_back(clan);

    barbarians.executeTurn(w.gameState, w.grid, rng, nullptr);
    CHECK(w.gameState.barbarianClans()[0].hiredBy == aoc::INVALID_PLAYER);
    CHECK(w.gameState.barbarianClans()[0].hiredTarget == aoc::INVALID_PLAYER);
}

TEST_CASE("costs rise with clan strength") {
    // The relationship, not the constants: a balance pass may retune all three,
    // but a stronger clan must never be cheaper to deal with.
    CHECK(aoc::sim::bribeCost(5) > aoc::sim::bribeCost(1));
    CHECK(aoc::sim::hireCost(5) > aoc::sim::hireCost(1));
    CHECK(aoc::sim::convertToCityStateCost(5) > aoc::sim::convertToCityStateCost(1));
    // Settling them is the biggest commitment of the three.
    CHECK(aoc::sim::convertToCityStateCost(3) > aoc::sim::bribeCost(3));
}

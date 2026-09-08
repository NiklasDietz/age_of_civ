/**
 * @file test_ai_war_cadence.cpp
 * @brief The AI's Domination campaign goes through the validated war path.
 *
 *        It used to call DiplomacyManager::declareWar directly, which observes
 *        none of the gates requestDeclareWar enforces -- PEACE_LOCK_TURNS, the
 *        friendship window, casus-belli validity, or even whether the two civs
 *        had met. It also passed currentTurn as a literal 0, so
 *        warDeclaredOnTurn was 0 and the WAR_MIN_TURNS gate on peace could not
 *        bind past turn 10. The peace side bypassed the layer too, IMPOSING
 *        peace via makePeace without the minimum war duration or the other
 *        side's consent.
 *
 *        Together those made a war/peace cycle: measured on seed 42, 64 -> 145
 *        declarations and 63 -> 142 treaties once the era-advantage modifier
 *        made combat decisive enough for campaigns to end quickly.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/ai/AIMilitaryController.hpp"
#include "aoc/simulation/diplomacy/DiplomacyActions.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::sim::DiplomacyManager;

namespace {

constexpr aoc::UnitTypeId WARRIOR{0};

/// Two neighbours: player 0 with an army, player 1 with none, cities close
/// enough to be inside striking range. That is the situation the Domination
/// campaign exists to act on, so the AI declares here unless a gate stops it.
struct Neighbours {
    aoc::test::World world = aoc::test::makeWorld(2);
    DiplomacyManager d;
    aoc::Random rng{1234};
    aoc::sim::ai::AIMilitaryController ai{PlayerId{0}, aoc::ui::AIDifficulty::Normal};

    Neighbours() {
        this->d.initialize(2);
        this->d.meetPlayers(PlayerId{0}, PlayerId{1}, 1);
        aoc::test::addCityAt(this->world, PlayerId{0}, 6, 6, "Alpha");
        aoc::test::addCityAt(this->world, PlayerId{1}, 10, 6, "Beta");
        for (int32_t i = 0; i < 4; ++i) {
            aoc::test::addUnitAt(this->world, PlayerId{0}, WARRIOR, 7 + i, 6);
        }
        this->world.gameState.setCurrentTurn(60);
    }

    void runAI() {
        this->ai.executeMilitaryActions(this->world.gameState, this->world.grid, this->rng,
                                        &this->d);
    }
};

} // namespace

TEST_CASE("the AI does declare on a weak neighbour when nothing forbids it") {
    // Positive control. Without this, the tests below would pass for the wrong
    // reason -- something unrelated blocking the campaign entirely.
    Neighbours n;
    REQUIRE_FALSE(n.d.isAtWar(PlayerId{0}, PlayerId{1}));
    n.runAI();
    CHECK(n.d.isAtWar(PlayerId{0}, PlayerId{1}));
}

TEST_CASE("the AI cannot re-declare inside the peace lock") {
    Neighbours n;
    // A war, then peace: turnsSincePeace is 0, so PEACE_LOCK_TURNS forbids a
    // new war. The direct declareWar call never consulted it.
    n.d.declareWar(PlayerId{0}, PlayerId{1}, aoc::sim::CasusBelliType::SurpriseWar, nullptr,
                   &n.world.gameState, 20);
    n.d.makePeace(PlayerId{0}, PlayerId{1});
    REQUIRE(n.d.relation(PlayerId{0}, PlayerId{1}).turnsSincePeace < aoc::sim::PEACE_LOCK_TURNS);

    n.runAI();
    CHECK_FALSE(n.d.isAtWar(PlayerId{0}, PlayerId{1}));
}

TEST_CASE("a war the AI declares records the turn it began") {
    // currentTurn was passed as a literal 0, which set warDeclaredOnTurn to 0.
    // Every WAR_MIN_TURNS comparison then read `turn - 0`, so from turn 10 on
    // the minimum war duration was satisfied the moment war was declared.
    Neighbours n;
    n.runAI();
    REQUIRE(n.d.isAtWar(PlayerId{0}, PlayerId{1}));
    CHECK(n.d.relation(PlayerId{0}, PlayerId{1}).warDeclaredOnTurn ==
          n.world.gameState.currentTurn());

    // And so the war cannot be ended on the turn it started.
    CHECK(aoc::sim::requestMakePeace(n.world.gameState, n.d, PlayerId{0}, PlayerId{1},
                                     n.world.gameState.currentTurn()) != ErrorCode::Ok);
}

TEST_CASE("the AI does not commit a campaign to a war it failed to declare") {
    // Committing on a refused declaration marched an army at a civ we were not
    // at war with, and burned the 30-turn commitment doing it. Observable
    // consequence: once the lock lifts, the AI is still willing to declare.
    Neighbours n;
    n.d.declareWar(PlayerId{0}, PlayerId{1}, aoc::sim::CasusBelliType::SurpriseWar, nullptr,
                   &n.world.gameState, 20);
    n.d.makePeace(PlayerId{0}, PlayerId{1});
    n.runAI();
    REQUIRE_FALSE(n.d.isAtWar(PlayerId{0}, PlayerId{1}));

    n.d.relation(PlayerId{0}, PlayerId{1}).turnsSincePeace = aoc::sim::PEACE_LOCK_TURNS + 1;
    n.d.relation(PlayerId{1}, PlayerId{0}).turnsSincePeace = aoc::sim::PEACE_LOCK_TURNS + 1;
    n.runAI();
    CHECK(n.d.isAtWar(PlayerId{0}, PlayerId{1}));
}

TEST_CASE("an unmet civ is not a war target") {
    // requestDeclareWar requires hasMet. The direct call did not, so the
    // campaign could declare on a civ it had never encountered.
    Neighbours n;
    n.d.relation(PlayerId{0}, PlayerId{1}).hasMet = false;
    n.d.relation(PlayerId{1}, PlayerId{0}).hasMet = false;
    n.runAI();
    CHECK_FALSE(n.d.isAtWar(PlayerId{0}, PlayerId{1}));
}

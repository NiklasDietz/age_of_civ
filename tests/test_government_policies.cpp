/**
 * @file test_government_policies.cpp
 * @brief The shared policy and government requests behind the Government
 *        screen, the REST routes and the MCP tools: slot-type validation,
 *        unlocks, duplicates, the free swap after a civic, the gold cost,
 *        anarchy, the change cooldown, the 64-bit unlock mask and the two
 *        governments no civic used to unlock. Civ VI plan Phase 2.1, 2026-09-05.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"

using aoc::CivicId;
using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::sim::EMPTY_POLICY_SLOT;
using aoc::sim::GovernmentType;
using aoc::sim::PolicySlotType;

namespace {
constexpr int8_t DISCIPLINE     = 0;   // Military
constexpr int8_t SURVEY         = 1;   // Military, stays locked in the tests
constexpr int8_t URBAN_PLANNING = 6;   // Economic
constexpr int8_t INSPIRATION    = 17;  // Wildcard
} // namespace

TEST_CASE("a card goes into a slot of its type, a wildcard slot takes any card") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});
    aoc::sim::PlayerGovernmentComponent& gov = p.government();
    gov.unlockPolicy(DISCIPLINE);
    gov.unlockPolicy(URBAN_PLANNING);
    gov.unlockPolicy(INSPIRATION);
    gov.policySwapFree = true;

    // Chiefdom: one Military slot, one Economic slot.
    CHECK(aoc::sim::policySlotCount(gov) == 2);
    CHECK(aoc::sim::policySlotType(gov, 0) == PolicySlotType::Military);
    CHECK(aoc::sim::policySlotType(gov, 1) == PolicySlotType::Economic);

    CHECK(aoc::sim::requestSlotPolicy(w.gameState, PlayerId{0}, 1, DISCIPLINE)
          == ErrorCode::InvalidArgument);                       // wrong slot type
    CHECK(aoc::sim::requestSlotPolicy(w.gameState, PlayerId{0}, 0, SURVEY)
          == ErrorCode::InvalidArgument);                       // not unlocked
    CHECK(aoc::sim::requestSlotPolicy(w.gameState, PlayerId{0}, 5, DISCIPLINE)
          == ErrorCode::InvalidArgument);                       // no such slot
    CHECK(aoc::sim::requestSlotPolicy(w.gameState, PlayerId{0}, 0, DISCIPLINE) == ErrorCode::Ok);
    CHECK(gov.activePolicies[0] == DISCIPLINE);
    CHECK(aoc::sim::requestSlotPolicy(w.gameState, PlayerId{0}, 1, URBAN_PLANNING) == ErrorCode::Ok);
    CHECK(aoc::sim::requestSlotPolicy(w.gameState, PlayerId{0}, 0, EMPTY_POLICY_SLOT) == ErrorCode::Ok);
    CHECK(gov.activePolicies[0] == EMPTY_POLICY_SLOT);

    // Monarchy: 2 Military, 1 Economic, 1 Diplomatic, 1 Wildcard.
    gov.unlockGovernment(GovernmentType::Monarchy);
    CHECK(aoc::sim::requestChangeGovernment(w.gameState, PlayerId{0}, GovernmentType::Monarchy)
          == ErrorCode::Ok);
    CHECK(aoc::sim::policySlotCount(gov) == 5);
    CHECK(aoc::sim::policySlotType(gov, 4) == PolicySlotType::Wildcard);
    CHECK(aoc::sim::requestSlotPolicy(w.gameState, PlayerId{0}, 4, DISCIPLINE) == ErrorCode::Ok);
    CHECK(aoc::sim::requestSlotPolicy(w.gameState, PlayerId{0}, 0, DISCIPLINE)
          == ErrorCode::InvalidState);                          // already slotted elsewhere
    CHECK(aoc::sim::requestSlotPolicy(w.gameState, PlayerId{0}, 4, INSPIRATION) == ErrorCode::Ok);
}

TEST_CASE("slotting costs gold unless a civic just completed") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});
    aoc::sim::PlayerGovernmentComponent& gov = p.government();
    gov.unlockPolicy(DISCIPLINE);
    gov.policySwapFree = false;
    p.spendGold(p.treasury());
    REQUIRE(p.treasury() == 0);
    CHECK(aoc::sim::requestSlotPolicy(w.gameState, PlayerId{0}, 0, DISCIPLINE)
          == ErrorCode::InsufficientResources);
    p.addGold(aoc::sim::POLICY_SWAP_GOLD_COST);
    CHECK(aoc::sim::requestSlotPolicy(w.gameState, PlayerId{0}, 0, DISCIPLINE) == ErrorCode::Ok);
    CHECK(p.treasury() == 0);
    CHECK(aoc::sim::requestSlotPolicy(w.gameState, PlayerId{0}, 0, EMPTY_POLICY_SLOT) == ErrorCode::Ok);

    // Completing a civic opens the free window; processing the turn closes it.
    aoc::sim::PlayerCivicComponent& civics = p.civics();
    civics.currentResearch  = CivicId{0};
    civics.researchProgress = 0.0f;
    CHECK(aoc::sim::advanceCivicResearch(civics, 100000.0f, &gov));
    CHECK(gov.policySwapFree);
    CHECK(aoc::sim::requestSlotPolicy(w.gameState, PlayerId{0}, 0, DISCIPLINE) == ErrorCode::Ok);
    CHECK(p.treasury() == 0);
    aoc::sim::processGovernment(p);
    CHECK_FALSE(gov.policySwapFree);
}

TEST_CASE("changing government: free out of Chiefdom, anarchy later, a cooldown between") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});
    aoc::sim::PlayerGovernmentComponent& gov = p.government();
    gov.unlockPolicy(DISCIPLINE);
    gov.policySwapFree = true;
    w.gameState.setCurrentTurn(30);

    CHECK(aoc::sim::requestChangeGovernment(w.gameState, PlayerId{0}, GovernmentType::Oligarchy)
          == ErrorCode::InvalidArgument);                       // not unlocked
    gov.unlockGovernment(GovernmentType::Oligarchy);
    gov.unlockGovernment(GovernmentType::Autocracy);
    CHECK(aoc::sim::requestSlotPolicy(w.gameState, PlayerId{0}, 0, DISCIPLINE) == ErrorCode::Ok);
    CHECK(aoc::sim::requestChangeGovernment(w.gameState, PlayerId{0}, GovernmentType::Oligarchy)
          == ErrorCode::Ok);
    CHECK(gov.government == GovernmentType::Oligarchy);
    CHECK_FALSE(gov.isInAnarchy());                             // leaving Chiefdom is free
    CHECK(gov.activePolicies[0] == EMPTY_POLICY_SLOT);          // slots cleared
    CHECK(gov.lastGovernmentChangeTurn == 30);
    CHECK(aoc::sim::requestChangeGovernment(w.gameState, PlayerId{0}, GovernmentType::Autocracy)
          == ErrorCode::InvalidState);                          // cooldown
    w.gameState.setCurrentTurn(30 + aoc::sim::GOVERNMENT_CHANGE_COOLDOWN_TURNS);
    CHECK(aoc::sim::requestChangeGovernment(w.gameState, PlayerId{0}, GovernmentType::Autocracy)
          == ErrorCode::Ok);
    CHECK(gov.anarchyTurnsRemaining == aoc::sim::ANARCHY_DURATION);
    CHECK(aoc::sim::requestSlotPolicy(w.gameState, PlayerId{0}, 0, DISCIPLINE)
          == ErrorCode::InvalidState);                          // no cards during anarchy
    CHECK(aoc::sim::requestChangeGovernment(w.gameState, PlayerId{0}, GovernmentType::Oligarchy)
          == ErrorCode::InvalidState);                          // nor a change
}

TEST_CASE("the unlock mask holds all 36 cards; Exploration and Reformed Church unlock governments") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});
    aoc::sim::PlayerGovernmentComponent& gov = p.government();
    gov.unlockPolicy(35);
    CHECK(gov.isPolicyUnlocked(35));
    CHECK_FALSE(gov.isPolicyUnlocked(3));   // the u32 mask aliased 35 onto 3
    CHECK_FALSE(gov.isPolicyUnlocked(64));

    aoc::sim::PlayerCivicComponent& civics = p.civics();
    CHECK_FALSE(gov.isGovernmentUnlocked(GovernmentType::MerchantRepublic));
    civics.currentResearch = CivicId{9};    // Exploration
    CHECK(aoc::sim::advanceCivicResearch(civics, 100000.0f, &gov));
    CHECK(gov.isGovernmentUnlocked(GovernmentType::MerchantRepublic));
    civics.currentResearch = CivicId{29};   // Reformed Church
    CHECK(aoc::sim::advanceCivicResearch(civics, 100000.0f, &gov));
    CHECK(gov.isGovernmentUnlocked(GovernmentType::Theocracy));
}

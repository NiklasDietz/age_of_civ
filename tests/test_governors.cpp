/**
 * @file test_governors.cpp
 * @brief Governor titles: one per five completed civics, spent on recruiting a named
 *        governor or buying a promotion; the validated assign / promote requests; the
 *        bonuses the yield passes now apply; the AI spending its titles. Until
 *        2026-09-05 the whole named-governor model had no caller.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/Governor.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"

using aoc::CivicId;
using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::hex::AxialCoord;
using aoc::sim::GovernorPromotion;
using aoc::sim::GovernorType;

namespace {

constexpr PlayerId P0{0};

/// Mark the first `n` civics completed for `player`.
void completeCivics(aoc::game::Player& player, int32_t n) {
    aoc::sim::PlayerCivicComponent& c = player.civics();
    if (c.completedCivics.size() < static_cast<std::size_t>(n)) {
        c.completedCivics.resize(static_cast<std::size_t>(n), false);
    }
    for (int32_t i = 0; i < n; ++i) {
        c.completedCivics[static_cast<std::size_t>(i)] = true;
    }
}

} // namespace

TEST_CASE("titles come one per five civics and are spent on recruits and promotions") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::game::Player& p = *w.gameState.player(P0);
    CHECK(aoc::sim::governorTitlesEarned(p) == 0);
    completeCivics(p, 4);
    CHECK(aoc::sim::governorTitlesEarned(p) == 0);
    completeCivics(p, 5);
    CHECK(aoc::sim::governorTitlesEarned(p) == 1);
    completeCivics(p, 12);
    CHECK(aoc::sim::governorTitlesEarned(p) == 2);
    CHECK(aoc::sim::governorTitlesSpent(p) == 0);
    CHECK(aoc::sim::governorTitlesAvailable(p) == 2);

    CHECK(aoc::sim::governorForPromotion(GovernorPromotion::TaxHaven) == GovernorType::Financier);
    CHECK(aoc::sim::governorForPromotion(GovernorPromotion::ResearchGrant) ==
          GovernorType::Scholar);
    CHECK(aoc::sim::governorForPromotion(GovernorPromotion::CarbonCredit) ==
          GovernorType::Environmentalist);
    CHECK(aoc::sim::governorForPromotion(GovernorPromotion::None) == GovernorType::None);
    CHECK(aoc::sim::governorTypeName(GovernorType::Scholar) == "Scholar");
}

TEST_CASE("assigning recruits with a title, moving is free, promotions follow the tree") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::test::addCityAt(w, P0, 15, 9, "Beta");
    aoc::game::Player& p   = *w.gameState.player(P0);
    aoc::game::City& alpha = *p.cities()[0];
    aoc::game::City& beta  = *p.cities()[1];

    CHECK(aoc::sim::requestAssignGovernor(w.gameState, PlayerId{9}, {5, 5},
                                          GovernorType::Scholar) == ErrorCode::InvalidArgument);
    CHECK(aoc::sim::requestAssignGovernor(w.gameState, P0, {9, 9}, GovernorType::Scholar) ==
          ErrorCode::InvalidArgument);
    CHECK(aoc::sim::requestAssignGovernor(w.gameState, P0, {5, 5}, GovernorType::None) ==
          ErrorCode::InvalidArgument);
    CHECK(aoc::sim::requestAssignGovernor(w.gameState, P0, {5, 5}, GovernorType::Scholar) ==
          ErrorCode::InvalidState); // no title

    completeCivics(p, 5); // one title
    REQUIRE(aoc::sim::requestAssignGovernor(w.gameState, P0, {5, 5}, GovernorType::Scholar) ==
            ErrorCode::Ok);
    CHECK(alpha.governor().assignedGovernor == GovernorType::Scholar);
    CHECK(aoc::sim::governorTitlesAvailable(p) == 0);
    CHECK(aoc::sim::requestAssignGovernor(w.gameState, P0, {15, 9}, GovernorType::Financier) ==
          ErrorCode::InvalidState);

    // Moving the Scholar to Beta is free and clears Alpha.
    REQUIRE(aoc::sim::requestAssignGovernor(w.gameState, P0, {15, 9}, GovernorType::Scholar) ==
            ErrorCode::Ok);
    CHECK(beta.governor().assignedGovernor == GovernorType::Scholar);
    CHECK(alpha.governor().assignedGovernor == GovernorType::None);
    CHECK(aoc::sim::governorTitlesSpent(p) == 1);

    // Promotions: wrong tree, then no title, then bought, then the slots and duplicates.
    CHECK(aoc::sim::requestPromoteGovernor(w.gameState, P0, {15, 9}, GovernorPromotion::TaxHaven) ==
          ErrorCode::InvalidArgument);
    CHECK(aoc::sim::requestPromoteGovernor(w.gameState, P0, {5, 5},
                                           GovernorPromotion::ResearchGrant) ==
          ErrorCode::InvalidArgument); // nobody seated
    CHECK(aoc::sim::requestPromoteGovernor(w.gameState, P0, {15, 9},
                                           GovernorPromotion::ResearchGrant) ==
          ErrorCode::InvalidState);
    completeCivics(p, 20); // four titles earned, one spent
    REQUIRE(aoc::sim::requestPromoteGovernor(w.gameState, P0, {15, 9},
                                             GovernorPromotion::ResearchGrant) == ErrorCode::Ok);
    CHECK(beta.governor().hasPromotion(GovernorPromotion::ResearchGrant));
    CHECK(aoc::sim::requestPromoteGovernor(w.gameState, P0, {15, 9},
                                           GovernorPromotion::ResearchGrant) ==
          ErrorCode::InvalidUnitAction);
    REQUIRE(aoc::sim::requestPromoteGovernor(w.gameState, P0, {15, 9},
                                             GovernorPromotion::EurekaBoost) == ErrorCode::Ok);
    REQUIRE(aoc::sim::requestPromoteGovernor(w.gameState, P0, {15, 9},
                                             GovernorPromotion::Innovation) == ErrorCode::Ok);
    CHECK(aoc::sim::requestPromoteGovernor(w.gameState, P0, {15, 9},
                                           GovernorPromotion::TechTransfer) ==
          ErrorCode::InvalidUnitAction); // three slots
    CHECK(aoc::sim::governorTitlesSpent(p) == 4);
    CHECK(aoc::sim::governorTitlesAvailable(p) == 0);

    // The titles travel with the governor when it moves.
    completeCivics(p, 25);
    REQUIRE(aoc::sim::requestAssignGovernor(w.gameState, P0, {5, 5}, GovernorType::Scholar) ==
            ErrorCode::Ok);
    CHECK(alpha.governor().promotionCount == 3);
    CHECK(beta.governor().promotionCount == 0);
    CHECK(beta.governor().assignedGovernor == GovernorType::None);
}

TEST_CASE("the bonuses stack the first title on the governor's own multiplier") {
    aoc::sim::CityGovernorComponent gov;
    CHECK(gov.goldMultiplier() == doctest::Approx(1.0f));
    gov.assignedGovernor = GovernorType::Financier;
    CHECK(gov.goldMultiplier() == doctest::Approx(1.20f));
    static_cast<void>(gov.addPromotion(GovernorPromotion::TaxHaven));
    CHECK(gov.goldMultiplier() == doctest::Approx(1.30f));

    aoc::sim::CityGovernorComponent scholar;
    scholar.assignedGovernor = GovernorType::Scholar;
    CHECK(scholar.scienceMultiplier() == doctest::Approx(1.15f));
    static_cast<void>(scholar.addPromotion(GovernorPromotion::ResearchGrant));
    CHECK(scholar.scienceMultiplier() == doctest::Approx(1.25f));

    aoc::sim::CityGovernorComponent diplomat;
    diplomat.assignedGovernor = GovernorType::Diplomat;
    CHECK(diplomat.loyaltyBonus() == doctest::Approx(8.0f));
    aoc::sim::CityGovernorComponent general;
    general.assignedGovernor = GovernorType::General;
    CHECK(general.loyaltyBonus() == doctest::Approx(4.0f));
    static_cast<void>(general.addPromotion(GovernorPromotion::Citadel));
    CHECK(general.loyaltyBonus() == doctest::Approx(8.0f));
    CHECK(aoc::sim::governorPromotionHasEffect(GovernorPromotion::Citadel));
    CHECK_FALSE(aoc::sim::governorPromotionHasEffect(GovernorPromotion::Militia));
}

TEST_CASE("Peace Keeper and Carbon Credit add favor per turn across the empire") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::test::addCityAt(w, P0, 15, 9, "Beta");
    aoc::game::Player& p = *w.gameState.player(P0);
    CHECK(aoc::sim::governorFavorPerTurn(p) == 0);
    p.cities()[0]->governor().assignedGovernor = GovernorType::Diplomat;
    static_cast<void>(p.cities()[0]->governor().addPromotion(GovernorPromotion::PeaceKeeper));
    p.cities()[1]->governor().assignedGovernor = GovernorType::Environmentalist;
    static_cast<void>(p.cities()[1]->governor().addPromotion(GovernorPromotion::CarbonCredit));
    CHECK(aoc::sim::governorFavorPerTurn(p) == 15);
}

TEST_CASE(
    "the AI seats the capital's Financier first, then promotes, and stops when titles run out") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, P0, 5, 5, "Capital");
    aoc::test::addCityAt(w, P0, 15, 9, "Campus Town");
    aoc::game::Player& p     = *w.gameState.player(P0);
    aoc::game::City& capital = *p.cities()[0];
    aoc::game::City& town    = *p.cities()[1];
    town.districts().districts.push_back({aoc::sim::DistrictType::Campus, {16, 9}, {}});

    aoc::sim::aiSpendGovernorTitles(w.gameState, P0); // no titles: nothing
    CHECK_FALSE(capital.governor().hasNamedGovernor());

    completeCivics(p, 5); // one title
    aoc::sim::aiSpendGovernorTitles(w.gameState, P0);
    CHECK(capital.governor().assignedGovernor == GovernorType::Financier);
    CHECK_FALSE(town.governor().hasNamedGovernor());
    CHECK(aoc::sim::governorTitlesAvailable(p) == 0);

    completeCivics(p, 15); // three titles: the Campus town gets its Scholar, then promotions
    aoc::sim::aiSpendGovernorTitles(w.gameState, P0);
    CHECK(town.governor().assignedGovernor == GovernorType::Scholar);
    CHECK(capital.governor().hasPromotion(GovernorPromotion::TaxHaven));
    CHECK(aoc::sim::governorTitlesAvailable(p) == 0);
    CHECK(aoc::sim::governorTitlesSpent(p) == 3);
}

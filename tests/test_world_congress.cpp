/**
 * @file test_world_congress.cpp
 * @brief The World Congress component's propose / vote / tally cycle, and the two
 *        player requests (`requestCongressVote`, `requestCongressProposal`) that the
 *        screen, the debug routes and the MCP tools share. Until 2026-09-05 the game
 *        voted for the human and picked the human's proposal by utility.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/core/Random.hpp"
#include "aoc/simulation/diplomacy/DiplomaticFavor.hpp"
#include "aoc/simulation/diplomacy/WorldCongress.hpp"

#include <cstdlib>

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::sim::Resolution;
using aoc::sim::WorldCongressComponent;

namespace {

constexpr PlayerId P0{0};
constexpr PlayerId P1{1};
constexpr PlayerId P2{2};
constexpr PlayerId NOBODY = aoc::INVALID_PLAYER;

aoc::sim::PlayerDiplomaticFavorComponent& favorOf(aoc::test::World& w, PlayerId id) {
    return w.gameState.player(id)->diplomaticFavor();
}

} // namespace

TEST_CASE("proposeResolution clears the votes and resolveVotes tallies signed weights") {
    WorldCongressComponent wc;
    wc.proposeResolution(Resolution::WorldsFair, P1, P1);
    wc.castVote(P0, 2);
    wc.castVote(P1, -1);
    CHECK(wc.resolveVotes());
    REQUIRE(wc.passedResolutions.size() == 1);
    CHECK(wc.passedResolutions[0] == Resolution::WorldsFair);
    CHECK(wc.isResolutionActive(Resolution::WorldsFair));

    wc.castVote(P0, 1); // a stale vote must not survive the next proposal
    wc.proposeResolution(Resolution::ArmsReduction, P0);
    CHECK(wc.votes[0] == 0);
    wc.castVote(P0, -3);
    wc.castVote(P1, 1);
    CHECK_FALSE(wc.resolveVotes());
    CHECK(wc.passedResolutions.size() == 1);

    wc.castVote(NOBODY, 4); // sentinel seats never vote
    int32_t total = 0;
    for (const int16_t v : wc.votes) {
        total += std::abs(v);
    }
    CHECK(total == 4);
}

TEST_CASE(
    "requestCongressVote rejects an unknown seat, an out-of-range weight and a closed floor") {
    aoc::test::World w = aoc::test::makeWorld(2);
    CHECK(aoc::sim::requestCongressVote(w.gameState, PlayerId{9}, 1) == ErrorCode::InvalidArgument);
    CHECK(aoc::sim::requestCongressVote(w.gameState, NOBODY, 1) == ErrorCode::InvalidArgument);
    CHECK(aoc::sim::requestCongressVote(w.gameState, P0, 5) == ErrorCode::InvalidArgument);
    CHECK(aoc::sim::requestCongressVote(w.gameState, P0, -5) == ErrorCode::InvalidArgument);
    CHECK(aoc::sim::requestCongressVote(w.gameState, P0, 1) ==
          ErrorCode::InvalidState); // nothing proposed

    w.gameState.player(P1)->victoryTracker().isEliminated = true;
    w.gameState.worldCongress().proposeResolution(Resolution::BanNuclearWeapons, P0);
    CHECK(aoc::sim::requestCongressVote(w.gameState, P1, 1) == ErrorCode::InvalidArgument);
    CHECK(aoc::sim::requestCongressVote(w.gameState, P0, 1) == ErrorCode::Ok);
}

TEST_CASE("requestCongressVote refunds the automatic extras and charges the new ones") {
    aoc::test::World w         = aoc::test::makeWorld(2);
    WorldCongressComponent& wc = w.gameState.worldCongress();
    wc.isActive                = true;
    wc.proposeResolution(Resolution::ClimateAccord, P1, P1);
    // The automatic vote bought one extra (+2) out of 35 favor.
    wc.castVote(P0, 2);
    favorOf(w, P0).favor = 25;

    CHECK(aoc::sim::requestCongressVote(w.gameState, P0, 4) == ErrorCode::Ok); // two more extras
    CHECK(wc.votes[0] == 4);
    CHECK(favorOf(w, P0).favor == 5);
    CHECK(wc.voteChosen[0]);

    CHECK(aoc::sim::requestCongressVote(w.gameState, P0, -4) == ErrorCode::Ok); // same weight: free
    CHECK(wc.votes[0] == -4);
    CHECK(favorOf(w, P0).favor == 5);

    CHECK(aoc::sim::requestCongressVote(w.gameState, P0, -1) == ErrorCode::Ok); // three extras back
    CHECK(wc.votes[0] == -1);
    CHECK(favorOf(w, P0).favor == 35);

    CHECK(aoc::sim::requestCongressVote(w.gameState, P0, 0) ==
          ErrorCode::Ok); // abstain owes nothing
    CHECK(wc.votes[0] == 0);
    CHECK(favorOf(w, P0).favor == 35);

    favorOf(w, P0).favor = 15;
    CHECK(aoc::sim::requestCongressVote(w.gameState, P0, 3) == ErrorCode::InsufficientResources);
    CHECK(wc.votes[0] == 0); // a rejection changes nothing
    CHECK(favorOf(w, P0).favor == 15);
    CHECK(aoc::sim::requestCongressVote(w.gameState, P0, 2) == ErrorCode::Ok);
    CHECK(favorOf(w, P0).favor == 5);
}

TEST_CASE("requestCongressProposal normalises the target per resolution and can be cleared") {
    aoc::test::World w         = aoc::test::makeWorld(3);
    WorldCongressComponent& wc = w.gameState.worldCongress();

    CHECK(aoc::sim::requestCongressProposal(w.gameState, PlayerId{9}, Resolution::WorldsFair,
                                            NOBODY) == ErrorCode::InvalidArgument);
    CHECK(aoc::sim::requestCongressProposal(w.gameState, P0, static_cast<Resolution>(200),
                                            NOBODY) == ErrorCode::InvalidArgument);

    // Prestige resolutions always boost the proposer, whatever target is passed.
    CHECK(aoc::sim::requestCongressProposal(w.gameState, P0, Resolution::ClimateAccord, P1) ==
          ErrorCode::Ok);
    CHECK(wc.preferredProposal == Resolution::ClimateAccord);
    CHECK(wc.preferredTarget == P0);
    CHECK(wc.preferredBy == P0);

    // Untargeted resolutions drop the target.
    CHECK(aoc::sim::requestCongressProposal(w.gameState, P0, Resolution::ArmsReduction, P1) ==
          ErrorCode::Ok);
    CHECK(wc.preferredTarget == NOBODY);

    // Sanctions need a living rival.
    CHECK(aoc::sim::requestCongressProposal(w.gameState, P0, Resolution::GlobalSanctions, NOBODY) ==
          ErrorCode::InvalidArgument);
    CHECK(aoc::sim::requestCongressProposal(w.gameState, P0, Resolution::GlobalSanctions, P0) ==
          ErrorCode::InvalidArgument);
    w.gameState.player(P2)->victoryTracker().isEliminated = true;
    CHECK(aoc::sim::requestCongressProposal(w.gameState, P0, Resolution::GlobalSanctions, P2) ==
          ErrorCode::InvalidArgument);
    CHECK(wc.preferredProposal ==
          Resolution::ArmsReduction); // rejections leave the registration alone
    CHECK(aoc::sim::requestCongressProposal(w.gameState, P0, Resolution::GlobalSanctions, P1) ==
          ErrorCode::Ok);
    CHECK(wc.preferredProposal == Resolution::GlobalSanctions);
    CHECK(wc.preferredTarget == P1);

    // Another seat's clear is a no-op; the owner's clear empties it.
    CHECK(aoc::sim::requestCongressProposal(w.gameState, P1, Resolution::Count, NOBODY) ==
          ErrorCode::Ok);
    CHECK(wc.preferredProposal == Resolution::GlobalSanctions);
    CHECK(aoc::sim::requestCongressProposal(w.gameState, P0, Resolution::Count, NOBODY) ==
          ErrorCode::Ok);
    CHECK(wc.preferredProposal == Resolution::Count);
    CHECK(wc.preferredBy == NOBODY);
}

TEST_CASE("processWorldCongress proposes the registered resolution and tallies the changed vote") {
    aoc::test::World w         = aoc::test::makeWorld(2);
    WorldCongressComponent& wc = w.gameState.worldCongress();
    favorOf(w, P0).favor       = 40; // the richest seat proposes
    favorOf(w, P1).favor       = 35;   // enough for three extras, not enough to be chosen
    REQUIRE(aoc::sim::requestCongressProposal(w.gameState, P0, Resolution::ClimateAccord, NOBODY) ==
            ErrorCode::Ok);
    wc.isActive              = true;
    wc.turnsUntilNextSession = 1;
    aoc::Random rng(7);

    aoc::sim::processWorldCongress(w.gameState, 1, rng, nullptr);
    CHECK(wc.currentProposal == Resolution::ClimateAccord);
    CHECK(wc.proposer == P0);
    CHECK(wc.proposalTarget == P0);
    CHECK(wc.preferredProposal == Resolution::Count); // consumed
    CHECK(wc.votes[0] >= 1);                          // the proposer backs its own resolution
    CHECK(wc.votes[0] <= 2);                          // utility 30 buys at most one extra
    CHECK_FALSE(wc.voteChosen[0]);
    // 40 + this turn's accrual - 30 to propose - 10 per extra the proposer bought.
    const int32_t extras0 = std::abs(wc.votes[0]) - 1;
    CHECK(favorOf(w, P0).favor == 40 + favorOf(w, P0).favorPerTurn - 30 - 10 * extras0);
    CHECK(wc.votes[1] == 0); // without diplomacy the rival abstains

    // The rival changes its mind before the tally: three extras out of its favor.
    const int32_t favor1 = favorOf(w, P1).favor;
    REQUIRE(aoc::sim::requestCongressVote(w.gameState, P1, -4) == ErrorCode::Ok);
    CHECK(wc.votes[1] == -4);
    CHECK(favorOf(w, P1).favor == favor1 - 30);
    CHECK(wc.voteChosen[1]);

    aoc::sim::processWorldCongress(w.gameState, 2, rng, nullptr);
    CHECK(wc.currentProposal == Resolution::Count);
    CHECK(wc.passedResolutions.empty()); // 4 no against at most 2 yes
    CHECK_FALSE(wc.voteChosen[1]);       // cleared with the votes
    CHECK(wc.votes[1] == 0);
    CHECK(wc.turnsUntilNextSession == aoc::sim::WORLD_CONGRESS_SESSION_INTERVAL);
}

TEST_CASE("a registered proposal whose target died falls back to the automatic pick") {
    aoc::test::World w         = aoc::test::makeWorld(3);
    WorldCongressComponent& wc = w.gameState.worldCongress();
    favorOf(w, P0).favor       = 40;
    REQUIRE(aoc::sim::requestCongressProposal(w.gameState, P0, Resolution::GlobalSanctions, P2) ==
            ErrorCode::Ok);
    w.gameState.player(P2)->victoryTracker().isEliminated = true;
    wc.isActive                                           = true;
    wc.turnsUntilNextSession                              = 1;
    aoc::Random rng(7);

    aoc::sim::processWorldCongress(w.gameState, 1, rng, nullptr);
    CHECK(wc.proposer == P0);
    CHECK(wc.currentProposal !=
          Resolution::GlobalSanctions); // nobody to sanction without diplomacy
    CHECK(wc.currentProposal != Resolution::Count);
    CHECK(wc.preferredProposal == Resolution::Count);
}

/**
 * @file test_diplomatic_favor.cpp
 * @brief Pins the severity-weighting of computeDiplomaticFavor (aoc::sim,
 *        DiplomaticFavor.hpp).
 *
 * Diplomatic-favor accrual used to dock a flat -1 per grievance held against a
 * player, regardless of how serious each grievance was. It now docks -1 per 10
 * points of accumulated grievance SEVERITY (a positive magnitude), still capped
 * at -10/turn. These cases pin that scaling and cap, and that two players with
 * the same grievance COUNT but different total severity get different penalties.
 *
 * Expected favor is expressed relative to `base` (the government/alliance/suze
 * contribution) so the test does not depend on the default government's exact
 * base value.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/core/Random.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/diplomacy/DiplomaticFavor.hpp"
#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/simulation/diplomacy/WorldCongress.hpp"

namespace {

const aoc::game::Player& soloPlayer(aoc::game::GameState& gameState) {
    gameState.initialize(1);
    return *gameState.players()[0];
}

} // namespace

TEST_CASE("computeDiplomaticFavor docks -1 favor per 10 severity points, capped at -10") {
    aoc::game::GameState gameState;
    const aoc::game::Player& p = soloPlayer(gameState);

    // No grievances -> baseline (government + no alliances/suzerainties).
    const int32_t base = aoc::sim::computeDiplomaticFavor(p, 0, 0, 0);

    CHECK(aoc::sim::computeDiplomaticFavor(p, 0, 0, 10) == base - 1);
    CHECK(aoc::sim::computeDiplomaticFavor(p, 0, 0, 50) == base - 5);
    CHECK(aoc::sim::computeDiplomaticFavor(p, 0, 0, 95) == base - 9);   // 95/10 == 9
    CHECK(aoc::sim::computeDiplomaticFavor(p, 0, 0, 100) == base - 10); // hits the cap
    CHECK(aoc::sim::computeDiplomaticFavor(p, 0, 0, 500) == base - 10); // stays capped
}

TEST_CASE("computeDiplomaticFavor weighs a harsh grievance above several mild ones") {
    aoc::game::GameState gameState;
    const aoc::game::Player& p = soloPlayer(gameState);
    const int32_t base = aoc::sim::computeDiplomaticFavor(p, 0, 0, 0);

    // Two grievances either way, but different total severity:
    const int32_t twoMild  = aoc::sim::computeDiplomaticFavor(p, 0, 0, 20);   // 2x -10
    const int32_t twoHarsh = aoc::sim::computeDiplomaticFavor(p, 0, 0, 100);  // 2x -50

    CHECK(twoMild == base - 2);
    CHECK(twoHarsh == base - 10);
    CHECK(twoHarsh < twoMild);  // severity, not count, drives the penalty
}

TEST_CASE("computeDiplomaticFavor treats zero or negative severity as no penalty") {
    aoc::game::GameState gameState;
    const aoc::game::Player& p = soloPlayer(gameState);
    const int32_t base = aoc::sim::computeDiplomaticFavor(p, 0, 0, 0);

    CHECK(aoc::sim::computeDiplomaticFavor(p, 0, 0, 0) == base);
    CHECK(aoc::sim::computeDiplomaticFavor(p, 0, 0, -30) == base);
    CHECK(aoc::sim::computeDiplomaticFavor(p, 0, 0, 5) == base);  // 5/10 == 0
}

TEST_CASE("computeDiplomaticFavor still adds alliance and suzerainty bonuses") {
    aoc::game::GameState gameState;
    const aoc::game::Player& p = soloPlayer(gameState);
    const int32_t base = aoc::sim::computeDiplomaticFavor(p, 0, 0, 0);

    CHECK(aoc::sim::computeDiplomaticFavor(p, 2, 0, 0) == base + 4);   // +2 each alliance
    CHECK(aoc::sim::computeDiplomaticFavor(p, 0, 3, 0) == base + 6);   // +2 each suzerainty
    CHECK(aoc::sim::computeDiplomaticFavor(p, 2, 3, 100) == base + 4 + 6 - 10);
    // Negative counts are floored at zero, not subtracted.
    CHECK(aoc::sim::computeDiplomaticFavor(p, -5, -5, 0) == base);
}

TEST_CASE("per-turn favor accrual reflects grievance severity, not count") {
    // Drives the real accrual path (processWorldCongress -> accruePerPlayerFavor
    // -> grievanceSeverityAgainst -> computeDiplomaticFavor), so a regression of
    // the summation back to counting grievances is caught here even though the
    // seed-42 golden run happens to carry no such grievances.
    aoc::game::GameState gameState;
    gameState.initialize(3);  // p0 & p1 are grieved against; p2 holds + is clean
    aoc::Random rng{1u};

    aoc::game::Player& holder = *gameState.players()[2];
    const aoc::PlayerId p0 = gameState.players()[0]->id();
    const aoc::PlayerId p1 = gameState.players()[1]->id();

    // Same grievance COUNT against each target (one apiece), different severity:
    holder.grievances().addGrievance(aoc::sim::GrievanceType::SettledNearBorders, p0);  // -10
    holder.grievances().addGrievance(aoc::sim::GrievanceType::DeclaredWarOnAlly, p1);    // -30

    aoc::sim::processWorldCongress(gameState, 1, rng, nullptr);

    // p2 has no grievance against it -> its favorPerTurn is the shared base
    // (all players default to the same government, no alliances/suzerainties).
    const int32_t base = gameState.players()[2]->diplomaticFavor().favorPerTurn;
    const int32_t favor0 = gameState.players()[0]->diplomaticFavor().favorPerTurn;
    const int32_t favor1 = gameState.players()[1]->diplomaticFavor().favorPerTurn;

    CHECK(favor0 == base - 1);   // 10 severity / 10 == 1 penalty
    CHECK(favor1 == base - 3);   // 30 severity / 10 == 3 penalty
    // The harsher single grievance costs more, despite equal grievance counts.
    CHECK(favor1 < favor0);
}

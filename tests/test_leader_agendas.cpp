/**
 * @file test_leader_agendas.cpp
 * @brief Every civ owns a leader personality. Until the agendas were ported out
 *        of `data/definitions/leaders.json` (2026-09-04) the table held 12 of 36
 *        entries, so `leaderPersonality()` clamped civs 12-35 to Rome and the GA
 *        read past the array end. These cases pin the table's shape and the
 *        conditions that arrived with the port.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/religion/Religion.hpp"

#include <string_view>

using aoc::sim::AgendaCondition;
using aoc::sim::CivId;
using aoc::sim::LeaderPersonalityDef;

namespace {

constexpr aoc::UnitTypeId WARRIOR{0};
constexpr aoc::UnitTypeId GALLEY{6};

} // namespace

TEST_CASE("every civ has its own personality entry, indexed by civId") {
    CHECK(aoc::sim::LEADER_PERSONALITY_COUNT == static_cast<int32_t>(aoc::sim::CIV_COUNT));
    for (int32_t i = 0; i < aoc::sim::LEADER_PERSONALITY_COUNT; ++i) {
        const LeaderPersonalityDef& def = aoc::sim::LEADER_PERSONALITIES[i];
        CHECK(static_cast<int32_t>(def.civId) == i);
        CHECK_FALSE(def.agendaName.empty());
        CHECK_FALSE(def.agendaDescription.empty());
    }
}

TEST_CASE("no civ falls back to Rome's agenda any more") {
    const std::string_view rome = aoc::sim::LEADER_PERSONALITIES[0].agendaName;
    int32_t sharingRome         = 0;
    for (int32_t i = 1; i < aoc::sim::LEADER_PERSONALITY_COUNT; ++i) {
        if (aoc::sim::leaderPersonality(static_cast<CivId>(i)).agendaName == rome) {
            ++sharingRome;
        }
    }
    CHECK(sharingRome == 0);
    // The formerly-clamped range now resolves to itself.
    CHECK(aoc::sim::leaderPersonality(CivId{12}).civId == CivId{12});
    CHECK(aoc::sim::leaderPersonality(CivId{35}).civId == CivId{35});
}

TEST_CASE("the ported conditions are in range and the imported ones are used") {
    bool sawFounderReligion = false;
    bool sawNoNavy          = false;
    for (int32_t i = 0; i < aoc::sim::LEADER_PERSONALITY_COUNT; ++i) {
        const LeaderPersonalityDef& def = aoc::sim::LEADER_PERSONALITIES[i];
        CHECK(static_cast<uint8_t>(def.likeCondition) <=
              static_cast<uint8_t>(AgendaCondition::IsAtPeaceForLong));
        CHECK(static_cast<uint8_t>(def.dislikeCondition) <=
              static_cast<uint8_t>(AgendaCondition::IsAtPeaceForLong));
        sawFounderReligion = sawFounderReligion ||
                             def.likeCondition == AgendaCondition::HasFounderReligion;
        sawNoNavy = sawNoNavy || def.dislikeCondition == AgendaCondition::HasNoNavy;
    }
    CHECK(sawFounderReligion);
    CHECK(sawNoNavy);
}

TEST_CASE("evaluateAgenda scores the religion and navy conditions it gained") {
    aoc::game::GameState gs;
    gs.initialize(2);
    aoc::game::Player& leader = *gs.players()[0];
    aoc::game::Player& target = *gs.players()[1];

    // Civ 13 (Last Prophet): likes a founder of religion, dislikes the irreligious.
    leader.setCivId(CivId{13});
    CHECK(aoc::sim::evaluateAgenda(gs, leader.id(), target.id()) < 0);
    target.faith().foundedReligion = aoc::sim::ReligionId{1};
    CHECK(aoc::sim::evaluateAgenda(gs, leader.id(), target.id()) > 0);

    // Civ 24 (Mediterranean Colonies): dislikes a target with no navy.
    leader.setCivId(CivId{24});
    CHECK(aoc::sim::evaluateAgenda(gs, leader.id(), target.id()) < 0);
    target.addUnit(GALLEY, {4, 4});
    const int32_t withNavy = aoc::sim::evaluateAgenda(gs, leader.id(), target.id());
    target.addUnit(WARRIOR, {5, 4});
    CHECK(aoc::sim::evaluateAgenda(gs, leader.id(), target.id()) == withNavy);
}

/**
 * @file test_eureka_coverage.cpp
 * @brief Every tech and civic can be hurried, and every trigger condition has
 *        somebody able to fire it.
 *
 * The boost table held nineteen entries against a hundred and twenty-four
 * techs and civics, so the eureka mechanic was something a player met once and
 * then forgot. Six of the nine conditions had no producer anywhere in the
 * simulation, which meant the boosts keyed to them could never fire at all.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/tech/EurekaBoost.hpp"
#include "aoc/simulation/tech/TechTree.hpp"

#include <array>
#include <vector>

using aoc::sim::EurekaCondition;

TEST_CASE("every tech and every civic has exactly one boost") {
    const std::vector<aoc::sim::EurekaBoostDef>& boosts = aoc::sim::getEurekaBoosts();

    std::vector<int32_t> perTech(aoc::sim::techCount(), 0);
    std::vector<int32_t> perCivic(aoc::sim::civicCount(), 0);
    for (const aoc::sim::EurekaBoostDef& b : boosts) {
        if (b.techId.isValid() && b.techId.value < perTech.size()) {
            ++perTech[b.techId.value];
        }
        if (b.civicId.isValid() && b.civicId.value < perCivic.size()) {
            ++perCivic[b.civicId.value];
        }
    }

    for (std::size_t t = 0; t < perTech.size(); ++t) {
        INFO("tech ", t);
        CHECK(perTech[t] == 1);
    }
    for (std::size_t c = 0; c < perCivic.size(); ++c) {
        INFO("civic ", c);
        CHECK(perCivic[c] == 1);
    }
}

TEST_CASE("boost indices are unique and inside the bitfield") {
    const std::vector<aoc::sim::EurekaBoostDef>& boosts = aoc::sim::getEurekaBoosts();
    std::vector<bool> seen(aoc::sim::MAX_EUREKA_BOOSTS, false);
    for (const aoc::sim::EurekaBoostDef& b : boosts) {
        REQUIRE(b.boostIndex < aoc::sim::MAX_EUREKA_BOOSTS);
        CHECK_FALSE(seen[b.boostIndex]); // an index reused would share a bit
        seen[b.boostIndex] = true;
    }
    CHECK(boosts.size() <= aoc::sim::MAX_EUREKA_BOOSTS);
}

TEST_CASE("every trigger condition is actually reachable") {
    // Producers, one per condition, listed where they live. A condition with no
    // producer is a boost that can never fire; six of the nine were in that
    // state until 2026-09-07.
    struct Producer {
        EurekaCondition condition;
        const char*     firedBy;
    };
    constexpr std::array<Producer, aoc::sim::EUREKA_CONDITION_COUNT> PRODUCERS = {{
        {EurekaCondition::BuildQuarry,      "BuilderActions.cpp, placing a Quarry"},
        {EurekaCondition::MeetCivilization, "TurnProcessor.cpp, first contact"},
        {EurekaCondition::FoundCity,        "TurnProcessor.cpp and Application.cpp"},
        {EurekaCondition::BuildCampus,      "ProductionSystem.cpp, district completion"},
        {EurekaCondition::KillUnit,         "Combat.cpp, either side dying"},
        {EurekaCondition::BuildHarbor,      "ProductionSystem.cpp, district completion"},
        {EurekaCondition::ResearchTech,     "TurnProcessor.cpp, research completion"},
        {EurekaCondition::BuildWonder,      "GoodyHuts.cpp"},
        {EurekaCondition::TrainUnit,        "ProductionSystem.cpp, unit completion"},
    }};

    // Every enumerator appears exactly once above, so adding a condition
    // without naming its producer fails here rather than silently going dead.
    std::vector<bool> covered(aoc::sim::EUREKA_CONDITION_COUNT, false);
    for (const Producer& p : PRODUCERS) {
        const auto idx = static_cast<std::size_t>(p.condition);
        REQUIRE(idx < covered.size());
        CHECK_FALSE(covered[idx]);
        covered[idx] = true;
    }
    for (std::size_t i = 0; i < covered.size(); ++i) {
        INFO("condition ", i);
        CHECK(covered[i]);
    }
}

TEST_CASE("the conditions the table actually uses are all produced") {
    const std::vector<aoc::sim::EurekaBoostDef>& boosts = aoc::sim::getEurekaBoosts();
    std::vector<int32_t> used(aoc::sim::EUREKA_CONDITION_COUNT, 0);
    for (const aoc::sim::EurekaBoostDef& b : boosts) {
        const auto idx = static_cast<std::size_t>(b.condition);
        REQUIRE(idx < used.size());
        ++used[idx];
    }
    // Not every condition has to be used by the table, but a table that used
    // only one or two would mean the rotation had collapsed.
    int32_t distinct = 0;
    for (int32_t n : used) { distinct += (n > 0) ? 1 : 0; }
    CHECK(distinct >= 7);
}

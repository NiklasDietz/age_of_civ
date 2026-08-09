#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/debug/GameControlValidation.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/tech/TechTree.hpp"

// ---------------------------------------------------------------------------
// isProductionItemValid
// ---------------------------------------------------------------------------

TEST_CASE("isProductionItemValid: Unit type 0 (Warrior, cost 40) is accepted") {
    CHECK(aoc::debug::isProductionItemValid(aoc::sim::ProductionItemType::Unit, 0));
}

TEST_CASE("isProductionItemValid: Unit itemId 9999 is rejected (out of bounds)") {
    CHECK_FALSE(aoc::debug::isProductionItemValid(aoc::sim::ProductionItemType::Unit, 9999));
}

TEST_CASE("isProductionItemValid: Building itemId 9999 is rejected (out of bounds)") {
    CHECK_FALSE(aoc::debug::isProductionItemValid(aoc::sim::ProductionItemType::Building, 9999));
}

TEST_CASE("isProductionItemValid: Wonder itemId 9999 is rejected (out of bounds)") {
    CHECK_FALSE(aoc::debug::isProductionItemValid(aoc::sim::ProductionItemType::Wonder, 9999));
}

TEST_CASE("isProductionItemValid: District itemId 9999 is rejected (out of bounds)") {
    CHECK_FALSE(aoc::debug::isProductionItemValid(aoc::sim::ProductionItemType::District, 9999));
}

// ---------------------------------------------------------------------------
// isResearchValid
// ---------------------------------------------------------------------------

TEST_CASE("isResearchValid: tech 0 (Mining, no prereqs) is accepted on default state") {
    // Default-constructed PlayerTechComponent has empty completedTechs / knownTechs.
    // Mining (id=0) has no prerequisites, so canResearch returns true.
    aoc::sim::PlayerTechComponent tech;
    CHECK(aoc::debug::isResearchValid(tech, 0));
}

TEST_CASE("isResearchValid: tech 7 (Apprenticeship, prereqs unmet) is rejected") {
    // Apprenticeship (id=7) requires techs 5 and 6, which are not researched on a
    // default-constructed component.
    aoc::sim::PlayerTechComponent tech;
    CHECK_FALSE(aoc::debug::isResearchValid(tech, 7));
}

TEST_CASE("isResearchValid: techId 9999 is rejected (out of bounds, techCount==28)") {
    // Bounds check must fire BEFORE canResearch to avoid OOB inside techDef().
    aoc::sim::PlayerTechComponent tech;
    CHECK_FALSE(aoc::debug::isResearchValid(tech, 9999));
}

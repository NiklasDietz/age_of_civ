#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/debug/GameControlValidation.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/diplomacy/EspionageSystem.hpp"

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

// ---------------------------------------------------------------------------
// requestSpyMission (the action behind the espionage screen and /game/spy/mission)
// ---------------------------------------------------------------------------

namespace {

constexpr aoc::UnitTypeId SPY_UNIT{101};

/// Human (0) owns a spy inside the rival capital Thebes and one at home.
struct SpyWorld {
    aoc::game::GameState gs;
    aoc::map::HexGrid grid;
    aoc::game::Unit* spyInThebes = nullptr;
    aoc::game::Unit* spyAtHome   = nullptr;

    SpyWorld() {
        this->grid.initialize(24, 16);
        for (int32_t i = 0; i < this->grid.tileCount(); ++i) {
            this->grid.setTerrain(i, aoc::map::TerrainType::Grassland);
        }
        this->gs.initialize(2);
        this->gs.players()[0]->addCity({3, 3}, "Home").setOriginalCapital(true);
        this->gs.players()[1]->addCity({12, 9}, "Thebes").setOriginalCapital(true);
        this->spyInThebes = &this->gs.players()[0]->addUnit(SPY_UNIT, {12, 9});
        this->spyAtHome   = &this->gs.players()[0]->addUnit(SPY_UNIT, {3, 3});
    }
};

} // namespace

TEST_CASE("requestSpyMission: a mission id past Count is InvalidArgument") {
    SpyWorld w;
    CHECK(aoc::sim::requestSpyMission(w.gs, aoc::PlayerId{0}, {12, 9},
                                      static_cast<aoc::sim::SpyMission>(99)) ==
          aoc::ErrorCode::InvalidArgument);
}

TEST_CASE("requestSpyMission: no spy on the tile is InvalidUnitAction") {
    SpyWorld w;
    CHECK(aoc::sim::requestSpyMission(w.gs, aoc::PlayerId{0}, {5, 5},
                                      aoc::sim::SpyMission::CounterIntelligence) ==
          aoc::ErrorCode::InvalidUnitAction);
}

TEST_CASE("requestSpyMission: an offensive mission with no rival city under the spy is InvalidState") {
    SpyWorld w;
    CHECK(aoc::sim::requestSpyMission(w.gs, aoc::PlayerId{0}, {3, 3},
                                      aoc::sim::SpyMission::StealTechnology) ==
          aoc::ErrorCode::InvalidState);
    CHECK(w.spyAtHome->spy().currentMission == aoc::sim::SpyMission::GatherIntelligence);
    CHECK(w.spyAtHome->spy().turnsRemaining == 0);
}

TEST_CASE("requestSpyMission: Counter-Intelligence is accepted anywhere") {
    SpyWorld w;
    CHECK(aoc::sim::requestSpyMission(w.gs, aoc::PlayerId{0}, {3, 3},
                                      aoc::sim::SpyMission::CounterIntelligence) ==
          aoc::ErrorCode::Ok);
    CHECK(w.spyAtHome->spy().currentMission == aoc::sim::SpyMission::CounterIntelligence);
}

TEST_CASE("requestSpyMission: a valid offensive mission binds the spy to its tile and busies it") {
    SpyWorld w;
    CHECK(aoc::sim::requestSpyMission(w.gs, aoc::PlayerId{0}, {12, 9},
                                      aoc::sim::SpyMission::StealTechnology) ==
          aoc::ErrorCode::Ok);
    CHECK(w.spyInThebes->spy().currentMission == aoc::sim::SpyMission::StealTechnology);
    CHECK(w.spyInThebes->spy().turnsRemaining > 0);
    CHECK(w.spyInThebes->spy().location == aoc::hex::AxialCoord{12, 9});
    CHECK(aoc::sim::requestSpyMission(w.gs, aoc::PlayerId{0}, {12, 9},
                                      aoc::sim::SpyMission::SiphonFunds) ==
          aoc::ErrorCode::InvalidUnitAction);
}

TEST_CASE("processSpyMissions records one outcome per resolved mission, capped at 32") {
    SpyWorld w;
    REQUIRE(aoc::sim::requestSpyMission(w.gs, aoc::PlayerId{0}, {12, 9},
                                        aoc::sim::SpyMission::StealTechnology) ==
            aoc::ErrorCode::Ok);
    w.spyInThebes->spy().turnsRemaining = 1;
    aoc::Random rng{42u};
    aoc::sim::processSpyMissions(w.gs, w.grid, rng);

    REQUIRE(w.gs.spyMissionRecords().size() == 1);
    const aoc::game::GameState::SpyMissionRecord rec = w.gs.spyMissionRecords().front();
    CHECK(rec.turn == 0);
    CHECK(rec.spyOwner == aoc::PlayerId{0});
    CHECK(rec.targetOwner == aoc::PlayerId{1});
    CHECK(rec.mission == aoc::sim::SpyMission::StealTechnology);
    CHECK(rec.location == aoc::hex::AxialCoord{12, 9});
    if (rec.success) {
        CHECK(rec.outcome == aoc::sim::SpyFailureOutcome::EscapedUndetected);
    }

    for (int32_t i = 0; i < 40; ++i) {
        w.gs.recordSpyMission(rec);
    }
    CHECK(w.gs.spyMissionRecords().size() == aoc::game::GameState::MAX_SPY_MISSION_RECORDS);
}

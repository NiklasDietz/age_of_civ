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
#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/diplomacy/EspionageSystem.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

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

// ---------------------------------------------------------------------------
// regimeCommandError (POST /game/monetary/regime)
// ---------------------------------------------------------------------------

TEST_CASE("regimeCommandError: coinage with a metal, and any later stage without one, pass") {
    aoc::debug::MonetaryRegimeCommand cmd{};
    cmd.player = aoc::PlayerId{0};
    cmd.target = 1; // Commodity Money
    cmd.tier   = 2; // Silver
    CHECK(aoc::debug::regimeCommandError(cmd).empty());
    cmd.target = 3; // Fiat
    cmd.tier   = 0;
    CHECK(aoc::debug::regimeCommandError(cmd).empty());
}

TEST_CASE("regimeCommandError: one refusal per parameter") {
    aoc::debug::MonetaryRegimeCommand cmd{};
    cmd.player = aoc::PlayerId{0};
    cmd.target = 1;
    cmd.tier   = 1;
    cmd.player = aoc::MAX_PLAYERS;
    CHECK(aoc::debug::regimeCommandError(cmd) == "player out of range");
    cmd.player = aoc::PlayerId{0};
    cmd.target = 0; // Barter is not a target
    CHECK(aoc::debug::regimeCommandError(cmd) == "target must be a monetary system above Barter");
    cmd.target = 9;
    CHECK(aoc::debug::regimeCommandError(cmd) == "target must be a monetary system above Barter");
    cmd.target = 1;
    cmd.tier   = 7;
    CHECK(aoc::debug::regimeCommandError(cmd) == "tier must be None, Copper, Silver or Gold");
    cmd.tier = 0;
    CHECK(aoc::debug::regimeCommandError(cmd) == "coinage needs a metal");
    cmd.target = 2;
    cmd.tier   = 1;
    CHECK(aoc::debug::regimeCommandError(cmd) == "only coinage takes a metal");
}

// ---------------------------------------------------------------------------
// dealCommandError (POST /game/deal/propose)
// ---------------------------------------------------------------------------

namespace {

/// A sound command: five silk sold and a contract for one iron a turn.
aoc::debug::ProposeDealCommand soundDeal() {
    aoc::debug::ProposeDealCommand cmd{};
    cmd.player          = aoc::PlayerId{0};
    cmd.target          = aoc::PlayerId{1};
    cmd.giveGold        = 0;
    cmd.askGold         = 50;
    cmd.goodId          = aoc::sim::goods::SILK;
    cmd.goodAmount      = 5;
    cmd.contractGood    = aoc::sim::goods::IRON_ORE;
    cmd.contractPerTurn = 1;
    cmd.contractGold    = 3;
    cmd.contractTurns   = aoc::sim::SUPPLY_CONTRACT_MAX_TURNS;
    cmd.exclusiveGood   = aoc::sim::goods::SILK;
    return cmd;
}

} // namespace

TEST_CASE("dealCommandError: a sound command, gold-only and with every goods leg, passes") {
    CHECK(aoc::debug::dealCommandError(soundDeal()).empty());
    aoc::debug::ProposeDealCommand goldOnly{};
    goldOnly.player   = aoc::PlayerId{0};
    goldOnly.target   = aoc::PlayerId{2};
    goldOnly.giveGold = 100;
    CHECK(aoc::debug::dealCommandError(goldOnly).empty());
}

TEST_CASE("dealCommandError: the parties must differ and gold must not be negative") {
    aoc::debug::ProposeDealCommand cmd = soundDeal();
    cmd.target                         = cmd.player;
    CHECK(aoc::debug::dealCommandError(cmd) == "player and target must differ");
    cmd          = soundDeal();
    cmd.giveGold = -1;
    CHECK(aoc::debug::dealCommandError(cmd) == "gold must not be negative");
    cmd         = soundDeal();
    cmd.askGold = -1;
    CHECK(aoc::debug::dealCommandError(cmd) == "gold must not be negative");
}

TEST_CASE("dealCommandError: every good id must be below GOOD_COUNT or -1") {
    for (const int32_t bad : {static_cast<int32_t>(aoc::sim::goods::GOOD_COUNT), -2}) {
        aoc::debug::ProposeDealCommand cmd = soundDeal();
        cmd.goodId                         = bad;
        CHECK(aoc::debug::dealCommandError(cmd) == "good id out of range");
        cmd              = soundDeal();
        cmd.contractGood = bad;
        CHECK(aoc::debug::dealCommandError(cmd) == "good id out of range");
        cmd               = soundDeal();
        cmd.exclusiveGood = bad;
        CHECK(aoc::debug::dealCommandError(cmd) == "good id out of range");
    }
}

TEST_CASE("dealCommandError: a shipment needs a positive amount") {
    aoc::debug::ProposeDealCommand cmd = soundDeal();
    cmd.goodAmount                     = 0;
    CHECK(aoc::debug::dealCommandError(cmd) == "goodAmount must be positive");
    cmd.goodId = -1; // no shipment: the amount is ignored
    CHECK(aoc::debug::dealCommandError(cmd).empty());
}

TEST_CASE("dealCommandError: a contract needs a positive rate, non-negative gold and a length within the cap") {
    aoc::debug::ProposeDealCommand cmd = soundDeal();
    cmd.contractPerTurn                = 0;
    CHECK(aoc::debug::dealCommandError(cmd) == "contractPerTurn must be positive");
    cmd              = soundDeal();
    cmd.contractGold = -5;
    CHECK(aoc::debug::dealCommandError(cmd) == "contractGold must not be negative");
    cmd               = soundDeal();
    cmd.contractTurns = 0;
    CHECK(aoc::debug::dealCommandError(cmd) == "contractTurns out of range");
    cmd.contractTurns = aoc::sim::SUPPLY_CONTRACT_MAX_TURNS + 1;
    CHECK(aoc::debug::dealCommandError(cmd) == "contractTurns out of range");
    cmd.contractGood = -1; // no contract: its fields are ignored
    CHECK(aoc::debug::dealCommandError(cmd).empty());
}

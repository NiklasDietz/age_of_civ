/**
 * @file test_save_roundtrip.cpp
 * @brief Save/load/save characterization: the serializer must reproduce an
 *        identical byte stream after a load, and the loaded state must match
 *        the original on spot-checked fields.
 *
 * Byte-compare is only valid because Serializer writes unordered_map
 * sections in sorted key order (see sortedEntries in Serializer.cpp); the
 * maps here are deliberately populated in scrambled insertion order to keep
 * that guarantee pinned.
 *
 * With AOC_WRITE_CORPUS=<path> the test additionally copies the first save
 * to <path> -- used to (re)generate the known-good corpus under
 * tests/data/saves/ when the save format version changes.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/core/Random.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/culture/GreatWorks.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/save/MapFile.hpp"
#include "aoc/save/Serializer.hpp"
#include "GridLayerCompare.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/diplomacy/Espionage.hpp"
#include "aoc/simulation/city/CityBombardment.hpp"
#include "aoc/simulation/tech/EraScore.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/diplomacy/WorldCongress.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/turn/TurnManager.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

/// Forwards only the layers the save carries (aoc::save::isGameGridLayer).
struct GameLayersOnly {
    aoc::test::LayerCompare& inner;

    template <class Container> void operator()(std::string_view name, const Container& c) {
        if (aoc::save::isGameGridLayer(name)) {
            this->inner(name, c);
        }
    }
};

struct World {
    aoc::game::GameState gameState;
    aoc::map::HexGrid grid;
    aoc::sim::TurnManager turnManager;
    aoc::sim::EconomySimulation economy;
    aoc::sim::DiplomacyManager diplomacy;
    aoc::map::FogOfWar fogOfWar;
    aoc::Random rng{12345u};
};

[[nodiscard]] aoc::game::Player& p2ref(World& w) { return *w.gameState.players()[2]; }

/// Populate a small but non-trivial state. Every unordered_map the
/// serializer touches gets entries, inserted in scrambled key order.
void buildWorld(World& w) {
    w.grid.initialize(24, 16);
    for (int32_t i = 0; i < w.grid.tileCount(); i += 3) {
        w.grid.setTerrain(i, aoc::map::TerrainType::Grassland);
    }
    // v12: game layers outside the six-field MapGrid section must survive.
    w.grid.setNaturalWonder(17, static_cast<aoc::map::NaturalWonderType>(2));
    w.grid.setReserves(17, 300);
    w.grid.setProspectCooldown(18, 4);
    w.grid.setImprovement(19, static_cast<aoc::map::ImprovementType>(1));
    w.grid.setChokepoint(20, static_cast<aoc::map::ChokepointType>(1));
    w.grid.setGreenhouseCrop(21, 7);
    w.grid.applyFallout(22, 5);
    w.grid.setAqueduct(23, true);
    // Worldgen-only layers are deliberately NOT saved (v12); populate them so
    // the load below can prove they come back in their fresh, empty state.
    w.grid.setRowLatitudes(std::vector<float>(16, 12.5f));
    std::vector<float> fertility(static_cast<std::size_t>(w.grid.tileCount()), 0.4f);
    fertility[17] = 0.95f;
    w.grid.setSoilFertility(std::move(fertility));
    w.grid.setCropSuitability(2, std::vector<uint8_t>(static_cast<std::size_t>(w.grid.tileCount()), 9));

    w.gameState.initialize(3);
    w.gameState.setHumanPlayerId(aoc::PlayerId{2}); // v11: a takeover moved the seat
    w.diplomacy.initialize(3);
    w.economy.initialize();
    w.turnManager.setPlayerCount(0, 3);
    w.fogOfWar.initialize(w.grid.tileCount(), 3);

    aoc::game::Player& p0 = *w.gameState.players()[0];
    aoc::game::Player& p1 = *w.gameState.players()[1];

    // NOTE: Player::m_treasury (the setTreasury/treasury() account) is NOT
    // serialized -- only the monetary/economy component treasuries are; the
    // app resyncs the spending account after load. Pin the component field.
    p0.monetary().treasury = 1234;
    p1.monetary().treasury = 87;

    aoc::game::City& alpha = p0.addCity({5, 5}, "Alpha");
    // v14: a housed great work (Amphitheater slot) and an antiquity site.
    alpha.districts().districts.push_back(
        {aoc::sim::DistrictType::Theatre, {4, 5}, {aoc::BuildingId{39}}});
    static_cast<void>(aoc::sim::placeGreatWork(
        alpha, {aoc::sim::GreatWorkType::Writing, aoc::PlayerId{0}, 7, 12}));
    w.grid.setAntiquitySite(24, 1);
    // v16: a seated governor with a title.
    alpha.governor().focus            = aoc::sim::CityFocus::Science;
    alpha.governor().isActive         = true;
    alpha.governor().assignedGovernor = aoc::sim::GovernorType::Scholar;
    static_cast<void>(alpha.governor().addPromotion(aoc::sim::GovernorPromotion::ResearchGrant));
    alpha.governor().turnsActive      = 9;
    alpha.stockpile().goods[42]  = 10;   // scrambled insertion order on
    alpha.stockpile().goods[7]   = 3;    // purpose -- pins the sorted-write
    alpha.stockpile().goods[199] = 25;   // guarantee.
    alpha.stockpile().goods[13]  = 1;
    alpha.stockpile().exportBuffer[9] = 4;
    alpha.stockpile().exportBuffer[2] = 6;
    alpha.productionExperience().recipeExperience[11] = 40;
    alpha.productionExperience().recipeExperience[3]  = 7;
    alpha.buildingLevels().levels[6] = 2;
    alpha.buildingLevels().levels[1] = 3;

    aoc::game::City& beta = p1.addCity({12, 9}, "Beta");
    {
        // v23: a queued district remembers the tile the human picked for it.
        aoc::sim::ProductionQueueItem district{};
        district.type          = aoc::sim::ProductionItemType::District;
        district.itemId        = static_cast<uint16_t>(aoc::sim::DistrictType::Campus);
        district.name          = "Campus";
        district.totalCost     = 60.0f;
        district.progress      = 12.0f;
        district.targetTile    = {13, 10};
        district.hasTargetTile = true;
        beta.production().queue.push_back(std::move(district));
    }
    beta.stockpile().goods[199] = 5;
    beta.stockpile().goods[42]  = 1;

    p0.warWeariness().turnsAtWar[1] = 5;
    p0.warWeariness().turnsAtWar[2] = 12;
    p1.warWeariness().turnsAtWar[0] = 5;

    aoc::game::Unit& veteran         = p0.addUnit(aoc::UnitTypeId{0}, {6, 5});
    veteran.experience().experience = 40;   // experience records ride in MiscEntities
    veteran.experience().level      = 1;
    veteran.experience().promotions = {aoc::PromotionId{0}};
    veteran.setFormationLevel(aoc::sim::FormationLevel::Corps);   // v15
    // v13: air state rides on the unit record.
    aoc::game::Unit& fighter          = p0.addUnit(aoc::UnitTypeId{18}, {7, 5});
    fighter.airUnit().sortiesRemaining = 0;
    fighter.airUnit().maxSorties       = 2;
    fighter.airUnit().operationalRange = 11;
    fighter.airUnit().isIntercepting   = true;
    p1.addUnit(aoc::UnitTypeId{0}, {12, 10});
    // Player 2 needs at least one unit: loadGame derives the player count
    // from the highest player index that owns a city or unit, so a player
    // with neither is silently dropped on load (pre-existing behaviour).
    w.gameState.players()[2]->addUnit(aoc::UnitTypeId{0}, {2, 2});

    // v17: an idle Master Spy with two promotions, a named great person, walls
    // under siege, loyalty in unrest, happiness, stage, aqueduct link, a locked
    // tile and religious pressure on Alpha.
    aoc::game::Unit& spy        = p0.addUnit(aoc::UnitTypeId{101}, {8, 5});
    spy.spy().owner             = aoc::PlayerId{0};
    spy.spy().location          = {12, 9};
    spy.spy().level             = aoc::sim::SpyLevel::MasterSpy;
    spy.spy().currentMission    = aoc::sim::SpyMission::SiphonFunds;
    spy.spy().turnsRemaining    = 0;
    spy.spy().experience        = 7;
    spy.spy().promotion1        = aoc::sim::SpyPromotion::Financier;
    spy.spy().promotion2        = aoc::sim::SpyPromotion::Seduction;
    aoc::game::Unit& sage        = p0.addUnit(aoc::UnitTypeId{102}, {5, 6});
    sage.greatPerson().owner     = aoc::PlayerId{0};
    sage.greatPerson().defId     = 2;
    sage.greatPerson().namedId   = 31;
    sage.greatPerson().position  = {5, 6};
    alpha.walls().setTier(aoc::sim::WallTier::Medieval);
    static_cast<void>(alpha.walls().takeDamage(30));
    alpha.loyalty().loyalty     = 63.5f;
    alpha.loyalty().unrestTurns = 2;
    alpha.loyalty().revoltOriginalOwner = aoc::PlayerId{1};
    alpha.happiness().amenities = 4.5f;
    alpha.happiness().happiness = 1.25f;
    alpha.setStage(aoc::game::CitySize::Town);
    alpha.setAqueductConnected(true);
    alpha.toggleTileLock({6, 5});
    alpha.religion().addPressure(1, 42.0f);
    // v17: a founded religion, faith, a congress mid-session with favor,
    // historic moments, a research queue, a known tech, auto policies, contact,
    // an embargo with goods and intel, and two city-states with a city and a unit.
    aoc::sim::GlobalReligionTracker& religions = w.gameState.religionTracker();
    static_cast<void>(religions.foundReligion("Testism", aoc::PlayerId{1}));
    religions.religions[0].founderBelief  = 0;
    religions.religions[0].enhancerBelief = 13;
    p1.faith().faith           = 88.5f;
    p1.faith().foundedReligion = 0;
    p1.faith().hasPantheon     = true;
    p1.faith().pantheonBelief  = 4;
    aoc::sim::WorldCongressComponent& congress = w.gameState.worldCongress();
    congress.isActive              = true;
    congress.turnsUntilNextSession = 12;
    congress.currentProposal       = aoc::sim::Resolution::GlobalSanctions;
    congress.proposer              = aoc::PlayerId{1};
    congress.proposalTarget        = aoc::PlayerId{0};
    congress.votes[0]              = -2;
    congress.votes[2]              = 3;
    congress.voteChosen[2]         = true;
    congress.passedResolutions.push_back(aoc::sim::Resolution::BanNuclearWeapons);
    congress.activeEffects.push_back({aoc::sim::Resolution::WorldsFair, aoc::PlayerId{2}, 6});
    congress.preferredProposal     = aoc::sim::Resolution::ArmsReduction;
    congress.preferredBy           = aoc::PlayerId{2};
    p2ref(w).diplomaticFavor().favor = 41;
    p2ref(w).diplomaticFavor().favorPerTurn = 3;
    aoc::sim::addEraScore(p0, 12, 3, "Completed the Pyramids");
    aoc::sim::addEraScore(p0, 15, 2, "Researched Mining");
    p0.eraScore().currentAgeType     = aoc::sim::AgeType::Golden;
    p0.eraScore().turnsRemaining     = 7;
    p0.eraScore().goldenAgeThreshold = 25;
    p0.researchQueue().researchQueue = {aoc::TechId{3}, aoc::TechId{9}};
    p0.tech().knownTechs[5]          = true;
    p1.government().autoPolicies     = true;
    p1.government().unlockPolicy(35);            // v18: bit 35 used to alias bit 3
    w.grid.setPillaged(w.grid.toIndex(aoc::hex::AxialCoord{7, 7}), true);   // v19 layer
    p1.government().policySwapFree          = true;
    p1.government().lastGovernmentChangeTurn = 12;
    p1.envoys().available                    = 3;
    p1.envoys().lifetime                     = 7;
    w.diplomacy.meetPlayers(aoc::PlayerId{0}, aoc::PlayerId{1}, 21);
    w.diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).turnsSincePeace = 4;
    w.diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).passiveBonus    = 6;
    w.diplomacy.relation(aoc::PlayerId{1}, aoc::PlayerId{0}).passiveBonus    = 6;
    w.diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).intelLevel      = 3;
    w.diplomacy.relation(aoc::PlayerId{1}, aoc::PlayerId{0}).intelLevel      = 1;
    w.diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).hasEmbargo      = true;
    w.diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).embargoedGoods  = {44, 7};
    w.diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).warDeclaredOnTurn    = 9;
    w.diplomacy.relation(aoc::PlayerId{1}, aoc::PlayerId{0}).warDeclaredOnTurn    = 9;
    w.diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).friendshipUntilTurn  = 55;
    w.diplomacy.relation(aoc::PlayerId{1}, aoc::PlayerId{0}).friendshipUntilTurn  = 55;
    w.diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).openBordersUntilTurn = 40;
    w.diplomacy.relation(aoc::PlayerId{1}, aoc::PlayerId{0}).openBordersUntilTurn = 40;
    w.diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).denouncedOnTurn      = 12;
    w.diplomacy.relation(aoc::PlayerId{1}, aoc::PlayerId{0}).hasDelegation        = true;
    w.diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).hasEmbassy           = true;
    {
        aoc::sim::PendingProposal offer;
        offer.from         = aoc::PlayerId{1};
        offer.to           = aoc::PlayerId{0};
        offer.proposedTurn = 7;
        offer.expiresTurn  = 12;
        offer.deal.playerA = aoc::PlayerId{1};
        offer.deal.playerB = aoc::PlayerId{0};
        aoc::sim::DealTerm gold{};
        gold.type       = aoc::sim::DealTermType::GoldLump;
        gold.fromPlayer = aoc::PlayerId{1};
        gold.toPlayer   = aoc::PlayerId{0};
        gold.goldLump   = 50;
        aoc::sim::DealTerm borders{};
        borders.type       = aoc::sim::DealTermType::OpenBorders;
        borders.fromPlayer = aoc::PlayerId{0};
        borders.toPlayer   = aoc::PlayerId{1};
        borders.duration   = 30;
        offer.deal.terms   = {gold, borders};
        w.gameState.pendingProposals().push_back(offer);
    }
    w.gameState.initializeCityStateSlots(2);
    aoc::sim::CityStateComponent cs0{};
    cs0.defId    = 3;
    cs0.type     = aoc::sim::CityStateType::Scientific;
    cs0.location = {18, 3};
    cs0.envoys[0] = 4;
    cs0.envoys[2] = 1;
    cs0.suzerain  = aoc::PlayerId{0};
    cs0.setMet(aoc::PlayerId{0});
    cs0.activeQuest.type           = aoc::sim::CityStateQuestType::ResearchTech;
    cs0.activeQuest.assignedTo     = aoc::PlayerId{0};
    cs0.activeQuest.isActive       = true;
    cs0.activeQuest.turnsRemaining = 17;
    cs0.questStreak.player         = aoc::PlayerId{0};
    cs0.questStreak.streak         = 2;
    cs0.levyPlayer                 = aoc::PlayerId{0};
    cs0.levyTurnsLeft              = 9;
    cs0.turnsSinceBully            = 3;
    aoc::sim::CityStateComponent cs1{};
    cs1.defId    = 5;
    cs1.type     = aoc::sim::CityStateType::Militaristic;
    cs1.location = {2, 13};
    w.gameState.cityStates() = {cs0, cs1};
    aoc::game::Player& seat0 = *w.gameState.cityStatePlayers()[0];
    seat0.setCivId(static_cast<aoc::sim::CivId>(30));
    aoc::game::City& csCity = seat0.addCity({18, 3}, "Geneva");
    csCity.setPopulation(4);
    csCity.workedTiles().push_back({18, 3});
    aoc::game::Unit& csGuard = seat0.addUnit(aoc::UnitTypeId{9}, {19, 3});
    csGuard.setHitPoints(77);
}

[[nodiscard]] std::vector<char> readAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return std::vector<char>(std::istreambuf_iterator<char>(in),
                             std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("save -> load -> save reproduces identical bytes") {
    const std::string fileA = "roundtrip_a.sav";
    const std::string fileB = "roundtrip_b.sav";

    World original;
    buildWorld(original);
    REQUIRE(aoc::save::saveGame(fileA, original.gameState, original.grid,
                                original.turnManager, original.economy,
                                original.diplomacy, original.fogOfWar,
                                original.rng) == aoc::ErrorCode::Ok);

    World loaded;
    REQUIRE(aoc::save::loadGame(fileA, loaded.gameState, loaded.grid,
                                loaded.turnManager, loaded.economy,
                                loaded.diplomacy, loaded.fogOfWar,
                                loaded.rng) == aoc::ErrorCode::Ok);

    // Spot-check the loaded state against the original.
    REQUIRE(loaded.gameState.players().size() == 3);
    const aoc::game::Player& lp0 = *loaded.gameState.players()[0];
    const aoc::game::Player& lp1 = *loaded.gameState.players()[1];
    CHECK(lp0.monetary().treasury == 1234);
    CHECK(lp1.monetary().treasury == 87);
    REQUIRE(lp0.cities().size() == 1);
    const aoc::game::City& lAlpha = *lp0.cities()[0];
    CHECK(lAlpha.name() == "Alpha");
    CHECK(lAlpha.location() == aoc::hex::AxialCoord{5, 5});
    // v14: the housed work and the antiquity site.
    REQUIRE(lAlpha.greatWorks().works.size() == 1);
    CHECK(lAlpha.greatWorks().works[0].type == aoc::sim::GreatWorkType::Writing);
    CHECK(lAlpha.greatWorks().works[0].creator == aoc::PlayerId{0});
    CHECK(lAlpha.greatWorks().works[0].namedId == 7);
    CHECK(lAlpha.greatWorks().works[0].createdTurn == 12);
    CHECK(loaded.grid.antiquitySite(24) == 1);
    // v16: the governor came back whole.
    CHECK(lAlpha.governor().focus == aoc::sim::CityFocus::Science);
    CHECK(lAlpha.governor().isActive);
    CHECK(lAlpha.governor().assignedGovernor == aoc::sim::GovernorType::Scholar);
    CHECK(lAlpha.governor().promotionCount == 1);
    CHECK(lAlpha.governor().hasPromotion(aoc::sim::GovernorPromotion::ResearchGrant));
    CHECK(lAlpha.governor().turnsActive == 9);
    // Experience and promotions survive the load (they were dropped until 2026-09-05).
    const aoc::game::Unit* lVeteran = nullptr;
    for (const std::unique_ptr<aoc::game::Unit>& u : lp0.units()) {
        if (u->position() == aoc::hex::AxialCoord{6, 5}) { lVeteran = u.get(); }
    }
    REQUIRE(lVeteran != nullptr);
    CHECK(lVeteran->experience().experience == 40);
    CHECK(lVeteran->experience().level == 1);
    REQUIRE(lVeteran->experience().promotions.size() == 1);
    CHECK(lVeteran->experience().promotions[0] == aoc::PromotionId{0});
    CHECK(lVeteran->formationLevel() == aoc::sim::FormationLevel::Corps);   // v15
    CHECK(lAlpha.stockpile().goods.at(42) == 10);
    CHECK(lAlpha.stockpile().goods.at(199) == 25);
    CHECK(lAlpha.stockpile().exportBuffer.at(2) == 6);
    CHECK(lAlpha.productionExperience().recipeExperience.at(11) == 40);
    CHECK(lAlpha.buildingLevels().levels.at(1) == 3);
    CHECK(lp0.warWeariness().turnsAtWar.at(2) == 12);
    CHECK(loaded.grid.width() == 24);
    CHECK(loaded.grid.height() == 16);
    // v11: the human seat. v12: every game layer, and only those.
    CHECK(loaded.gameState.humanPlayerId() == aoc::PlayerId{2});
    CHECK(loaded.gameState.players()[2]->isHuman());
    CHECK_FALSE(loaded.gameState.players()[0]->isHuman());
    CHECK(loaded.grid.naturalWonder(17) == static_cast<aoc::map::NaturalWonderType>(2));
    CHECK(loaded.grid.reserves(17) == 300);
    CHECK(loaded.grid.greenhouseCrop(21) == 7);
    CHECK(loaded.grid.hasFallout(22));
    CHECK(loaded.grid.hasAqueduct(23));
    // v13: the fighter's air state.
    const aoc::game::Unit* lFighter = nullptr;
    for (const std::unique_ptr<aoc::game::Unit>& u : lp0.units()) {
        if (u->typeId() == aoc::UnitTypeId{18}) { lFighter = u.get(); }
    }
    REQUIRE(lFighter != nullptr);
    CHECK(lFighter->airUnit().sortiesRemaining == 0);
    CHECK(lFighter->airUnit().maxSorties == 2);
    CHECK(lFighter->airUnit().operationalRange == 11);
    CHECK(lFighter->airUnit().isIntercepting);
    // v17: spy and great person on the unit record.
    const aoc::game::Unit* lSpy  = nullptr;
    const aoc::game::Unit* lSage = nullptr;
    for (const std::unique_ptr<aoc::game::Unit>& u : lp0.units()) {
        if (u->typeId() == aoc::UnitTypeId{101}) { lSpy = u.get(); }
        if (u->typeId() == aoc::UnitTypeId{102}) { lSage = u.get(); }
    }
    REQUIRE(lSpy != nullptr);
    CHECK(lSpy->spy().level == aoc::sim::SpyLevel::MasterSpy);
    CHECK(lSpy->spy().currentMission == aoc::sim::SpyMission::SiphonFunds);
    CHECK(lSpy->spy().turnsRemaining == 0);
    CHECK(lSpy->spy().experience == 7);
    CHECK(lSpy->spy().location == aoc::hex::AxialCoord{12, 9});
    CHECK(lSpy->spy().promotion1 == aoc::sim::SpyPromotion::Financier);
    CHECK(lSpy->spy().promotion2 == aoc::sim::SpyPromotion::Seduction);
    REQUIRE(lSage != nullptr);
    CHECK(lSage->greatPerson().owner == aoc::PlayerId{0});
    CHECK(lSage->greatPerson().defId == 2);
    CHECK(lSage->greatPerson().namedId == 31);
    CHECK(lSage->greatPerson().position == aoc::hex::AxialCoord{5, 6});
    // v17: the city record.
    CHECK(lAlpha.walls().tier == aoc::sim::WallTier::Medieval);
    CHECK(lAlpha.walls().currentHP == 170);
    CHECK(lAlpha.walls().maxHP == 200);
    CHECK(lAlpha.loyalty().loyalty == doctest::Approx(63.5f));
    CHECK(lAlpha.loyalty().unrestTurns == 2);
    CHECK(lAlpha.loyalty().revoltOriginalOwner == aoc::PlayerId{1});
    CHECK(lAlpha.happiness().amenities == doctest::Approx(4.5f));
    CHECK(lAlpha.happiness().happiness == doctest::Approx(1.25f));
    CHECK(lAlpha.stage() == aoc::game::CitySize::Town);
    CHECK(lAlpha.aqueductConnected());
    CHECK(lAlpha.isTileLocked({6, 5}));
    CHECK(lAlpha.religion().pressure[1] == doctest::Approx(42.0f));
    // v17: religion, congress, favor, moments, queue, known tech, auto policies.
    CHECK(loaded.gameState.religionTracker().religionsFoundedCount == 1);
    CHECK(loaded.gameState.religionTracker().religions[0].name == "Testism");
    CHECK(loaded.gameState.religionTracker().religions[0].founder == aoc::PlayerId{1});
    CHECK(loaded.gameState.religionTracker().religions[0].enhancerBelief == 13);
    CHECK(lp1.faith().faith == doctest::Approx(88.5f));
    CHECK(lp1.faith().foundedReligion == 0);
    CHECK(lp1.faith().hasPantheon);
    CHECK(lp1.faith().pantheonBelief == 4);
    const aoc::sim::WorldCongressComponent& lCongress = loaded.gameState.worldCongress();
    CHECK(lCongress.isActive);
    CHECK(lCongress.turnsUntilNextSession == 12);
    CHECK(lCongress.currentProposal == aoc::sim::Resolution::GlobalSanctions);
    CHECK(lCongress.proposer == aoc::PlayerId{1});
    CHECK(lCongress.votes[0] == -2);
    CHECK(lCongress.votes[2] == 3);
    CHECK(lCongress.voteChosen[2]);
    REQUIRE(lCongress.passedResolutions.size() == 1);
    CHECK(lCongress.passedResolutions[0] == aoc::sim::Resolution::BanNuclearWeapons);
    REQUIRE(lCongress.activeEffects.size() == 1);
    CHECK(lCongress.activeEffects[0].type == aoc::sim::Resolution::WorldsFair);
    CHECK(lCongress.activeEffects[0].turnsRemaining == 6);
    CHECK(lCongress.preferredProposal == aoc::sim::Resolution::ArmsReduction);
    CHECK(lCongress.preferredBy == aoc::PlayerId{2});
    CHECK(loaded.gameState.players()[2]->diplomaticFavor().favor == 41);
    REQUIRE(lp0.eraScore().moments.size() == 2);
    CHECK(lp0.eraScore().moments[1].text == "Researched Mining");
    CHECK(lp0.eraScore().moments[0].turn == 12);
    CHECK(lp0.eraScore().lifetimeEraScore == 5);
    CHECK(lp0.eraScore().eraScore == 5);
    CHECK(lp0.eraScore().currentAgeType == aoc::sim::AgeType::Golden);
    CHECK(lp0.eraScore().turnsRemaining == 7);
    CHECK(lp0.eraScore().goldenAgeThreshold == 25);
    REQUIRE(lp0.researchQueue().researchQueue.size() == 2);
    CHECK(lp0.researchQueue().researchQueue[1] == aoc::TechId{9});
    CHECK(lp0.tech().knownTechs[5]);
    CHECK(lp1.government().autoPolicies);
    CHECK(lp1.government().isPolicyUnlocked(35));
    CHECK(loaded.grid.isPillaged(loaded.grid.toIndex(aoc::hex::AxialCoord{7, 7})));
    CHECK_FALSE(loaded.grid.isPillaged(loaded.grid.toIndex(aoc::hex::AxialCoord{8, 8})));
    CHECK_FALSE(lp1.government().isPolicyUnlocked(3));
    CHECK(lp1.government().policySwapFree);
    CHECK(lp1.government().lastGovernmentChangeTurn == 12);
    CHECK(lp1.envoys().available == 3);
    CHECK(lp1.envoys().lifetime == 7);
    // v17: contact and the directional relation fields.
    CHECK(loaded.diplomacy.haveMet(aoc::PlayerId{0}, aoc::PlayerId{1}));
    CHECK(loaded.diplomacy.relation(aoc::PlayerId{1}, aoc::PlayerId{0}).metOnTurn == 21);
    CHECK(loaded.diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).turnsSincePeace == 4);
    {
        const aoc::game::City& lBeta = *lp1.cities()[0];
        REQUIRE_FALSE(lBeta.production().queue.empty());
        const aoc::sim::ProductionQueueItem& lDistrict = lBeta.production().queue.front();
        CHECK(lDistrict.type == aoc::sim::ProductionItemType::District);
        CHECK(lDistrict.hasTargetTile);
        CHECK(lDistrict.targetTile == aoc::hex::AxialCoord{13, 10});
        CHECK(lDistrict.progress == doctest::Approx(12.0f));
    }
    CHECK(loaded.diplomacy.relation(aoc::PlayerId{1}, aoc::PlayerId{0}).warDeclaredOnTurn == 9);
    CHECK(loaded.diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).friendshipUntilTurn == 55);
    CHECK(loaded.diplomacy.relation(aoc::PlayerId{1}, aoc::PlayerId{0}).openBordersUntilTurn == 40);
    CHECK(loaded.diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).denouncedOnTurn == 12);
    CHECK(loaded.diplomacy.relation(aoc::PlayerId{1}, aoc::PlayerId{0}).denouncedOnTurn == -1);
    CHECK(loaded.diplomacy.relation(aoc::PlayerId{1}, aoc::PlayerId{0}).hasDelegation);
    CHECK_FALSE(loaded.diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).hasDelegation);
    CHECK(loaded.diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).hasEmbassy);
    CHECK(loaded.diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).passiveBonus == 6);
    CHECK(loaded.diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).intelLevel == 3);
    CHECK(loaded.diplomacy.relation(aoc::PlayerId{1}, aoc::PlayerId{0}).intelLevel == 1);
    CHECK(loaded.diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).hasEmbargo);
    CHECK_FALSE(loaded.diplomacy.relation(aoc::PlayerId{1}, aoc::PlayerId{0}).hasEmbargo);
    REQUIRE(loaded.diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).embargoedGoods.size() == 2);
    CHECK(loaded.diplomacy.relation(aoc::PlayerId{0}, aoc::PlayerId{1}).embargoedGoods[0] == 44);
    // v17: city-states and their seats.
    REQUIRE(loaded.gameState.pendingProposals().size() == 1);
    const aoc::sim::PendingProposal& lOffer = loaded.gameState.pendingProposals().front();
    CHECK(lOffer.from == aoc::PlayerId{1});
    CHECK(lOffer.to == aoc::PlayerId{0});
    CHECK(lOffer.expiresTurn == 12);
    REQUIRE(lOffer.deal.terms.size() == 2);
    CHECK(lOffer.deal.terms[0].type == aoc::sim::DealTermType::GoldLump);
    CHECK(lOffer.deal.terms[0].goldLump == 50);
    CHECK(lOffer.deal.terms[1].type == aoc::sim::DealTermType::OpenBorders);
    CHECK(lOffer.deal.terms[1].fromPlayer == aoc::PlayerId{0});
    REQUIRE(loaded.gameState.cityStates().size() == 2);
    const aoc::sim::CityStateComponent& lCs0 = loaded.gameState.cityStates()[0];
    CHECK(lCs0.defId == 3);
    CHECK(lCs0.type == aoc::sim::CityStateType::Scientific);
    CHECK(lCs0.location == aoc::hex::AxialCoord{18, 3});
    CHECK(lCs0.envoys[0] == 4);
    CHECK(lCs0.envoys[2] == 1);
    CHECK(lCs0.suzerain == aoc::PlayerId{0});
    CHECK(lCs0.hasMet(aoc::PlayerId{0}));
    CHECK_FALSE(lCs0.hasMet(aoc::PlayerId{1}));
    CHECK(lCs0.activeQuest.type == aoc::sim::CityStateQuestType::ResearchTech);
    CHECK(lCs0.activeQuest.turnsRemaining == 17);
    CHECK(lCs0.questStreak.streak == 2);
    CHECK(lCs0.levyTurnsLeft == 9);
    CHECK(lCs0.turnsSinceBully == 3);
    CHECK(loaded.gameState.cityStates()[1].type == aoc::sim::CityStateType::Militaristic);
    REQUIRE(loaded.gameState.cityStatePlayers().size() == 2);
    const aoc::game::Player& lSeat0 = *loaded.gameState.cityStatePlayers()[0];
    CHECK(lSeat0.id() == aoc::PlayerId{200});
    CHECK(lSeat0.civId() == static_cast<aoc::sim::CivId>(30));
    REQUIRE(lSeat0.cities().size() == 1);
    CHECK(lSeat0.cities()[0]->name() == "Geneva");
    CHECK(lSeat0.cities()[0]->population() == 4);
    REQUIRE(lSeat0.units().size() == 1);
    CHECK(lSeat0.units()[0]->typeId() == aoc::UnitTypeId{9});
    CHECK(lSeat0.units()[0]->hitPoints() == 77);
    CHECK(loaded.gameState.player(aoc::PlayerId{201}) != nullptr);

    aoc::test::LayerSnapshot layers;
    original.grid.visitLayers(layers);
    aoc::test::LayerCompare layerCompare{layers};
    GameLayersOnly gameLayers{layerCompare};
    loaded.grid.visitLayers(gameLayers);
    CHECK(layerCompare.seen == 18);   // v14 added antiquitySite
    CHECK_MESSAGE(layerCompare.mismatched == 0,
                  "first game layer lost by save/load: " << layerCompare.firstMismatch);
    // v12: worldgen-only layers are not in the save; the loaded grid holds them
    // in the fresh state HexGrid::initialize() leaves (empty), not stale data.
    CHECK(loaded.grid.soilFertility().empty());
    CHECK(loaded.grid.rowLatitudes().empty());

    // Resave the loaded state: byte-identical to the first save.
    REQUIRE(aoc::save::saveGame(fileB, loaded.gameState, loaded.grid,
                                loaded.turnManager, loaded.economy,
                                loaded.diplomacy, loaded.fogOfWar,
                                loaded.rng) == aoc::ErrorCode::Ok);

    std::vector<char> bytesA = readAll(fileA);
    std::vector<char> bytesB = readAll(fileB);
    REQUIRE(!bytesA.empty());
    CHECK(bytesA == bytesB);

    // Optional corpus (re)generation hook.
    const char* corpusPath = std::getenv("AOC_WRITE_CORPUS");
    if (corpusPath != nullptr) {
        std::error_code ec;
        std::filesystem::copy_file(
            fileA, corpusPath,
            std::filesystem::copy_options::overwrite_existing, ec);
        CHECK(!ec);
        std::printf("corpus save written to %s\n", corpusPath);
    }
}

#ifdef AOC_TEST_CORPUS_DIR
TEST_CASE("known-good save corpus still loads") {
    // Guards the load path: hardening changes to Serializer must keep
    // accepting saves produced by earlier good builds of the same
    // SAVE_VERSION. Regenerate via AOC_WRITE_CORPUS when the format
    // version is deliberately bumped.
    int corpusFiles = 0;
    for (const std::filesystem::directory_entry& entry
         : std::filesystem::directory_iterator(AOC_TEST_CORPUS_DIR)) {
        if (entry.path().extension() != ".sav") { continue; }
        ++corpusFiles;
        World w;
        CHECK_MESSAGE(
            aoc::save::loadGame(entry.path().string(), w.gameState, w.grid,
                                w.turnManager, w.economy, w.diplomacy,
                                w.fogOfWar, w.rng) == aoc::ErrorCode::Ok,
            "corpus file rejected: ", entry.path().string());
    }
    // An empty corpus would make this test pass vacuously.
    CHECK(corpusFiles >= 1);
}
#endif

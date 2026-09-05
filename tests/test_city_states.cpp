/**
 * @file test_city_states.cpp
 * @brief City-state rules: strict suzerainty, the envoy pool, bonus tiers, levy, bully, quests.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/citystate/Envoys.hpp"
#include "aoc/render/PlayerColors.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/city/CityScience.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/turn/GameLength.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/turn/TurnProcessor.hpp"
#include "aoc/core/Random.hpp"

#include <array>
#include <string>

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::sim::CityStateComponent;
using aoc::sim::CityStateQuestType;
using aoc::sim::CityStateType;

namespace {

constexpr aoc::UnitTypeId WARRIOR{0};
constexpr aoc::UnitTypeId SETTLER{3};

/// Seats `count` city-state players; call once per world before adding components.
void seatCityStates(aoc::test::World& w, int32_t count) {
    w.gameState.initializeCityStateSlots(count);
}

std::size_t addCityState(aoc::test::World& w, uint8_t defId, CityStateType type, int32_t q, int32_t r) {
    const std::size_t index = w.gameState.cityStates().size();
    CityStateComponent cs{};
    cs.defId    = defId;
    cs.type     = type;
    cs.location = {q, r};
    cs.envoys.fill(0);
    cs.suzerain = aoc::INVALID_PLAYER;
    w.gameState.cityStates().push_back(cs);
    aoc::game::Player* seat =
        w.gameState.player(static_cast<PlayerId>(aoc::sim::CITY_STATE_PLAYER_BASE + index));
    REQUIRE(seat != nullptr);
    seat->addCity({q, r}, std::string(aoc::sim::CITY_STATE_DEFS[defId].name));
    return index;
}

} // namespace

TEST_CASE("suzerainty needs three envoys and a strict lead; a tie leaves the seat empty") {
    CityStateComponent cs{};
    cs.envoys.fill(0);
    cs.envoys[0] = 2;
    CHECK(cs.computeSuzerain() == aoc::INVALID_PLAYER);
    cs.envoys[0] = 3;
    CHECK(cs.computeSuzerain() == PlayerId{0});
    cs.envoys[1] = 3;
    CHECK(cs.computeSuzerain() == aoc::INVALID_PLAYER);
    cs.envoys[1] = 4;
    CHECK(cs.computeSuzerain() == PlayerId{1});
}

TEST_CASE("envoys come from the pool and go one at a time to met city-states") {
    aoc::test::World w = aoc::test::makeWorld(2);
    seatCityStates(w, 1);
    const std::size_t idx = addCityState(w, 2, CityStateType::Scientific, 10, 8);
    w.gameState.cityStates()[idx].setMet(PlayerId{0});
    aoc::game::Player& me = *w.gameState.player(PlayerId{0});

    CHECK(aoc::sim::requestSendEnvoy(w.gameState, PlayerId{0}, idx) == ErrorCode::InsufficientResources);
    me.envoys().grant(2);
    CHECK(me.envoys().available == 2);
    CHECK(me.envoys().lifetime == 2);
    CHECK(aoc::sim::requestSendEnvoy(w.gameState, PlayerId{1}, idx) == ErrorCode::InvalidState); // unmet
    CHECK(aoc::sim::requestSendEnvoy(w.gameState, PlayerId{0}, 5) == ErrorCode::EntityNotFound);
    CHECK(aoc::sim::requestSendEnvoy(w.gameState, PlayerId{200}, idx) == ErrorCode::EntityNotFound); // a seat, not a major

    CHECK(aoc::sim::requestSendEnvoy(w.gameState, PlayerId{0}, idx) == ErrorCode::Ok);
    CHECK(aoc::sim::requestSendEnvoy(w.gameState, PlayerId{0}, idx) == ErrorCode::Ok);
    CHECK(w.gameState.cityStates()[idx].envoys[0] == 2);
    CHECK(w.gameState.cityStates()[idx].suzerain == aoc::INVALID_PLAYER);
    CHECK(me.envoys().available == 0);
    CHECK(aoc::sim::requestSendEnvoy(w.gameState, PlayerId{0}, idx) == ErrorCode::InsufficientResources);

    me.envoys().grant(1);
    CHECK(aoc::sim::requestSendEnvoy(w.gameState, PlayerId{0}, idx) == ErrorCode::Ok);
    CHECK(w.gameState.cityStates()[idx].suzerain == PlayerId{0}); // the third envoy seats you at once
}

TEST_CASE("meeting a city-state gives one envoy and nothing accrues afterwards") {
    aoc::test::World w = aoc::test::makeWorld(2);
    seatCityStates(w, 1);
    const std::size_t idx = addCityState(w, 0, CityStateType::Militaristic, 10, 8);
    aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, 12, 8); // within the 6-tile meet radius

    aoc::sim::processCityStateDiplomacy(w.gameState, w.grid, 1);
    CHECK(w.gameState.cityStates()[idx].hasMet(PlayerId{0}));
    CHECK(w.gameState.cityStates()[idx].envoys[0] == 1);
    for (int32_t turn = 2; turn <= 40; ++turn) {
        aoc::sim::processCityStateDiplomacy(w.gameState, w.grid, turn);
    }
    CHECK(w.gameState.cityStates()[idx].envoys[0] == 1);
    CHECK(w.gameState.cityStates()[idx].suzerain == aoc::INVALID_PLAYER);
}

TEST_CASE("the envoy bonus has three tiers at one, three and six envoys, suzerain or not") {
    aoc::test::World w = aoc::test::makeWorld(2);
    seatCityStates(w, 1);
    const std::size_t idx = addCityState(w, 2, CityStateType::Scientific, 10, 8);
    aoc::game::Player& me = *w.gameState.player(PlayerId{0});
    const auto researchGain = [&](int8_t envoys) {
        w.gameState.cityStates()[idx].envoys[0] = envoys;
        me.tech().researchProgress                = 0.0f;
        aoc::sim::processCityStateBonuses(w.gameState, PlayerId{0});
        return me.tech().researchProgress;
    };
    CHECK(researchGain(0) == doctest::Approx(0.0f));
    CHECK(researchGain(1) == doctest::Approx(4.0f));
    CHECK(researchGain(3) == doctest::Approx(8.0f));
    CHECK(researchGain(6) == doctest::Approx(12.0f));
}

TEST_CASE("levy needs suzerainty and 200 gold; bully needs no rival suzerain and a cooldown") {
    aoc::test::World w = aoc::test::makeWorld(2);
    seatCityStates(w, 1);
    const std::size_t idx = addCityState(w, 1, CityStateType::Militaristic, 10, 8);
    CityStateComponent& cs = w.gameState.cityStates()[idx];
    cs.setMet(PlayerId{0});
    cs.setMet(PlayerId{1});
    cs.envoys[0] = 3;
    cs.suzerain  = cs.computeSuzerain();
    REQUIRE(cs.suzerain == PlayerId{0});
    aoc::game::Player& me = *w.gameState.player(PlayerId{0});

    CHECK(aoc::sim::requestLevyCityState(w.gameState, PlayerId{1}, idx) == ErrorCode::InvalidState);
    CHECK(aoc::sim::requestLevyCityState(w.gameState, PlayerId{0}, idx) == ErrorCode::InsufficientResources);
    me.addGold(250);
    CHECK(aoc::sim::requestLevyCityState(w.gameState, PlayerId{0}, idx) == ErrorCode::Ok);
    CHECK(cs.levyPlayer == PlayerId{0});
    CHECK(cs.levyTurnsLeft == aoc::sim::CS_LEVY_TURNS);
    CHECK(me.treasury() == 50);
    CHECK(aoc::sim::requestLevyCityState(w.gameState, PlayerId{0}, idx) == ErrorCode::InvalidState);

    // Cooldown first (the fixture starts at turnsSinceBully 0), then the rival seat
    // with the cooldown expired, so each rejection is pinned to its own reason.
    CHECK(aoc::sim::requestBullyCityState(w.gameState, PlayerId{0}, idx) == ErrorCode::InvalidState); // cooldown
    cs.turnsSinceBully = aoc::sim::CS_BULLY_COOLDOWN;
    CHECK(aoc::sim::requestBullyCityState(w.gameState, PlayerId{1}, idx) == ErrorCode::InvalidState); // rival seat
    CHECK(cs.envoys[1] == 0);
    cs.envoys[1] = 1; // player 1 has a stake, so the bully aggrieves them
    CHECK(aoc::sim::requestBullyCityState(w.gameState, PlayerId{0}, idx) == ErrorCode::Ok);
    CHECK(me.treasury() == 50 + aoc::sim::CS_BULLY_GOLD);
    CHECK(cs.envoys[0] == 1);
    CHECK(cs.suzerain == aoc::INVALID_PLAYER); // one envoy left: the seat is gone at once
    CHECK(cs.turnsSinceBully == 0);
    CHECK(w.gameState.player(PlayerId{1})->grievances().totalGrievanceAgainst(PlayerId{0}) != 0);
    CHECK(aoc::sim::requestBullyCityState(w.gameState, PlayerId{0}, idx) == ErrorCode::InvalidState); // cooldown again
    CHECK(aoc::sim::requestBullyCityState(w.gameState, PlayerId{0}, 9) == ErrorCode::EntityNotFound);
    CHECK(aoc::sim::requestBullyCityState(w.gameState, PlayerId{200}, idx) == ErrorCode::EntityNotFound);
}

TEST_CASE("a military quest ignores civilians and a conversion quest needs the city to follow") {
    aoc::test::World w = aoc::test::makeWorld(2);
    seatCityStates(w, 2);
    const std::size_t military = addCityState(w, 0, CityStateType::Militaristic, 10, 8);
    const std::size_t religious = addCityState(w, 4, CityStateType::Religious, 4, 12);
    aoc::test::addCityAt(w, PlayerId{0}, 16, 6, "Home");
    w.gameState.cityStates()[military].setMet(PlayerId{0});
    w.gameState.cityStates()[religious].setMet(PlayerId{0});

    aoc::sim::generateCityStateQuest(w.gameState, military, PlayerId{0});
    REQUIRE(w.gameState.cityStates()[military].activeQuest.type == CityStateQuestType::TrainUnit);
    aoc::test::addUnitAt(w, PlayerId{0}, SETTLER, 16, 7);
    aoc::sim::checkCityStateQuests(w.gameState);
    CHECK(w.gameState.cityStates()[military].activeQuest.isActive); // a Settler is not a soldier
    CHECK(w.gameState.cityStates()[military].envoys[0] == 0);
    aoc::test::addUnitAt(w, PlayerId{0}, WARRIOR, 17, 6);
    aoc::sim::checkCityStateQuests(w.gameState);
    CHECK(w.gameState.cityStates()[military].envoys[0] == 2);

    aoc::sim::generateCityStateQuest(w.gameState, religious, PlayerId{0});
    REQUIRE(w.gameState.cityStates()[religious].activeQuest.type == CityStateQuestType::ConvertToReligion);
    w.gameState.player(PlayerId{0})->faith().foundedReligion = aoc::sim::ReligionId{0};
    aoc::sim::checkCityStateQuests(w.gameState);
    CHECK(w.gameState.cityStates()[religious].activeQuest.isActive); // founding alone used to complete it
    aoc::game::Player* seat =
        w.gameState.player(static_cast<PlayerId>(aoc::sim::CITY_STATE_PLAYER_BASE + religious));
    REQUIRE(seat != nullptr);
    seat->cities().front()->religion().pressure[0] = 10.0f;
    aoc::sim::checkCityStateQuests(w.gameState);
    CHECK(w.gameState.cityStates()[religious].envoys[0] == 2);
}

TEST_CASE("type and quest names are printable") {
    CHECK(aoc::sim::cityStateTypeName(CityStateType::Trade) == "Trade");
    CHECK(aoc::sim::cityStateTypeName(CityStateType::Count) == "Unknown");
    CHECK(aoc::sim::cityStateQuestName(CityStateQuestType::TrainUnit) == "Train a military unit");
}

TEST_CASE("owner colours: majors by seat, barbarians dark red, city-states their own palette") {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    aoc::render::ownerColor(PlayerId{0}, r, g, b);
    CHECK(r == doctest::Approx(aoc::render::PLAYER_COLORS[0][0]));
    aoc::render::ownerColor(PlayerId{9}, r, g, b); // wraps through the eight civ colours
    CHECK(g == doctest::Approx(aoc::render::PLAYER_COLORS[1][1]));
    aoc::render::ownerColor(aoc::BARBARIAN_PLAYER, r, g, b);
    CHECK(r == doctest::Approx(0.55f));
    CHECK(g == doctest::Approx(0.08f));
    CHECK_FALSE(aoc::render::isCityStateOwner(aoc::BARBARIAN_PLAYER));
    const PlayerId firstSeat = static_cast<PlayerId>(aoc::sim::CITY_STATE_PLAYER_BASE);
    CHECK(aoc::render::isCityStateOwner(firstSeat));
    aoc::render::ownerColor(firstSeat, r, g, b);
    CHECK(b == doctest::Approx(aoc::render::CITY_STATE_COLORS[0][2]));
    aoc::render::ownerColor(static_cast<PlayerId>(aoc::sim::CITY_STATE_PLAYER_BASE + 8), r, g, b);
    CHECK(b == doctest::Approx(aoc::render::CITY_STATE_COLORS[0][2])); // the ninth seat wraps
    for (const std::array<float, 3>& cs : aoc::render::CITY_STATE_COLORS) {
        for (const std::array<float, 3>& major : aoc::render::PLAYER_COLORS) {
            CHECK((cs[0] != major[0] || cs[1] != major[1] || cs[2] != major[2]));
        }
    }
}

TEST_CASE("the AI spends its whole pool where it is closest to a seat it does not hold") {
    aoc::test::World w = aoc::test::makeWorld(2);
    seatCityStates(w, 3);
    const std::size_t near  = addCityState(w, 0, CityStateType::Militaristic, 10, 8);
    const std::size_t cold  = addCityState(w, 2, CityStateType::Scientific, 4, 12);
    const std::size_t unmet = addCityState(w, 6, CityStateType::Trade, 18, 3);
    w.gameState.cityStates()[near].setMet(PlayerId{0});
    w.gameState.cityStates()[cold].setMet(PlayerId{0});
    w.gameState.cityStates()[near].envoys[0] = 2;
    aoc::game::Player& ai = *w.gameState.player(PlayerId{0});

    aoc::sim::aiSpendEnvoys(w.gameState, PlayerId{0}); // empty pool: nothing happens
    CHECK(w.gameState.cityStates()[near].envoys[0] == 2);

    ai.envoys().grant(3);
    aoc::sim::aiSpendEnvoys(w.gameState, PlayerId{0});
    CHECK(ai.envoys().available == 0);
    CHECK(w.gameState.cityStates()[near].envoys[0] == 3);   // the third envoy took the seat
    CHECK(w.gameState.cityStates()[near].suzerain == PlayerId{0});
    CHECK(w.gameState.cityStates()[cold].envoys[0] == 2);   // held seats are skipped afterwards
    CHECK(w.gameState.cityStates()[unmet].envoys[0] == 0);  // never the unmet one

    aoc::sim::aiSpendEnvoys(w.gameState, PlayerId{200});    // a seat has no pool: no crash, no change
    CHECK(w.gameState.cityStates()[cold].envoys[0] == 2);
}

TEST_CASE("completing a civic during a turn grants one envoy to the pool") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::test::addCityAt(w, PlayerId{0}, 5, 5, "Alpha").setPopulation(3);
    aoc::test::addCityAt(w, PlayerId{1}, 15, 9, "Beta");
    aoc::game::Player& p = *w.gameState.player(PlayerId{0});
    const float culture  = aoc::sim::computePlayerCulture(p, w.grid);
    REQUIRE(culture > 0.0f); // the fixture must produce culture, or this test proves nothing
    p.civics().currentResearch = aoc::CivicId{0};
    const float cost = static_cast<float>(aoc::sim::civicDef(aoc::CivicId{0}).cultureCost)
                     * aoc::sim::GamePace::instance().costMultiplier;
    p.civics().researchProgress = cost - 0.25f;

    aoc::sim::EconomySimulation economy;
    aoc::sim::DiplomacyManager diplomacy;
    diplomacy.initialize(2);
    aoc::Random rng{3u};
    aoc::sim::TurnContext ctx;
    ctx.gameState   = &w.gameState;
    ctx.grid        = &w.grid;
    ctx.economy     = &economy;
    ctx.diplomacy   = &diplomacy;
    ctx.rng         = &rng;
    ctx.allPlayers  = {PlayerId{0}, PlayerId{1}};
    ctx.currentTurn = 1;
    aoc::sim::processTurn(ctx);

    CHECK(p.civics().hasCompleted(aoc::CivicId{0}));
    CHECK(p.envoys().lifetime == aoc::sim::ENVOYS_PER_CIVIC);
    CHECK(w.gameState.player(PlayerId{1})->envoys().lifetime == 0);
}

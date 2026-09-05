/**
 * @file test_city_actions.cpp
 * @brief The validated city actions behind the City Detail screen, the REST
 *        routes and the MCP tools: gold and faith purchase, faith rush, citizen
 *        focus, tile lock, worked-tile toggle with the citizen count, queue
 *        removal, and city projects that complete through production.
 *        Civ VI plan Phase 2.2, 2026-09-05.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/debug/GameControlValidation.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/city/CityActions.hpp"
#include "aoc/simulation/city/ProductionSystem.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

using aoc::BuildingId;
using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::UnitTypeId;
using aoc::hex::AxialCoord;
using aoc::sim::CityFocus;
using aoc::sim::CityProjectType;
using aoc::sim::ProductionItemType;

namespace {
const AxialCoord HOME{5, 5};
constexpr BuildingId MONUMENT{16};   // ungated
constexpr BuildingId TEMPLE{37};     // needs Theology and a Shrine
} // namespace

TEST_CASE("gold purchase pays the price and honours the build gates") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::City& city = aoc::test::addCityAt(w, PlayerId{0}, HOME.q, HOME.r, "Home");
    aoc::game::Player& p  = *w.gameState.player(PlayerId{0});
    aoc::test::addCityAt(w, PlayerId{1}, 15, 15, "Theirs");
    city.setStage(aoc::game::CitySize::Town);   // a Hamlet builds nothing
    for (std::size_t t = 0; t < p.tech().completedTechs.size(); ++t) {
        p.tech().completedTechs[t] = true;       // every tech gate open; civic gates stay
    }
    p.setTreasury(0);

    CHECK(aoc::sim::requestPurchase(w.gameState, &w.grid, PlayerId{0}, HOME, ProductionItemType::Unit, 0)
          == ErrorCode::InsufficientResources);
    p.setTreasury(10000);
    const std::size_t unitsBefore = p.units().size();
    CHECK(aoc::sim::requestPurchase(w.gameState, &w.grid, PlayerId{0}, HOME, ProductionItemType::Unit, 0)
          == ErrorCode::Ok);
    CHECK(p.units().size() == unitsBefore + 1);
    CHECK(p.treasury() == 10000 - aoc::sim::purchaseCost(static_cast<float>(
                              aoc::sim::unitTypeDef(UnitTypeId{0}).productionCost)));

    CHECK(aoc::sim::requestPurchase(w.gameState, &w.grid, PlayerId{0}, HOME, ProductionItemType::Building,
                                    TEMPLE.value) == ErrorCode::TechPrerequisiteNotMet);   // civic + Shrine
    CHECK(aoc::sim::requestPurchase(w.gameState, &w.grid, PlayerId{0}, HOME, ProductionItemType::Building,
                                    MONUMENT.value) == ErrorCode::Ok);
    CHECK(city.hasBuilding(MONUMENT));
    CHECK(aoc::sim::requestPurchase(w.gameState, &w.grid, PlayerId{0}, HOME, ProductionItemType::District, 1)
          == ErrorCode::InvalidArgument);
    CHECK(aoc::sim::requestPurchase(w.gameState, &w.grid, PlayerId{0}, AxialCoord{15, 15},
                                    ProductionItemType::Unit, 0) == ErrorCode::InvalidArgument);
}

TEST_CASE("faith buys religious units once a religion exists, and finishes a building once per turn") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::City& city = aoc::test::addCityAt(w, PlayerId{0}, HOME.q, HOME.r, "Home");
    aoc::game::Player& p  = *w.gameState.player(PlayerId{0});
    p.faith().faith = 1000.0f;

    CHECK(aoc::sim::requestFaithPurchase(w.gameState, PlayerId{0}, HOME, UnitTypeId{19})
          == ErrorCode::InvalidState);          // no religion founded
    CHECK(aoc::sim::requestFaithPurchase(w.gameState, PlayerId{0}, HOME, UnitTypeId{0})
          == ErrorCode::InvalidArgument);       // a Warrior is not a religious unit
    p.faith().foundedReligion = 0;
    const std::size_t before = p.units().size();
    CHECK(aoc::sim::requestFaithPurchase(w.gameState, PlayerId{0}, HOME, UnitTypeId{19}) == ErrorCode::Ok);
    CHECK(p.units().size() == before + 1);
    CHECK(p.faith().faith == doctest::Approx(1000.0f - aoc::sim::religiousUnitFaithCost(UnitTypeId{19})));
    CHECK(p.units().back()->spreadCharges == 3);

    aoc::sim::ProductionQueueItem item{};
    item.type      = ProductionItemType::Building;
    item.itemId    = MONUMENT.value;
    item.name      = "Monument";
    item.totalCost = 60.0f;
    city.production().queue.push_back(item);
    CHECK(aoc::sim::requestFaithRush(w.gameState, PlayerId{0}, HOME) == ErrorCode::Ok);
    CHECK(city.production().queue.front().progress == doctest::Approx(60.0f));
    CHECK(aoc::sim::requestFaithRush(w.gameState, PlayerId{0}, HOME) == ErrorCode::InvalidArgument);
}

TEST_CASE("focus, tile locks and worked tiles respect the citizen count") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::City& city = aoc::test::addCityAt(w, PlayerId{0}, HOME.q, HOME.r, "Home");
    city.setPopulation(2);
    const AxialCoord t1{6, 5};
    const AxialCoord t2{5, 6};
    const AxialCoord t3{4, 6};
    for (const AxialCoord& t : {t1, t2, t3}) {
        w.grid.setOwner(w.grid.toIndex(t), PlayerId{0});
    }
    city.workedTiles().clear();
    city.workedTiles().push_back(HOME);   // the centre is always worked and free

    CHECK(aoc::sim::requestSetCityFocus(w.gameState, w.grid, PlayerId{0}, HOME, CityFocus::Growth) == ErrorCode::Ok);
    CHECK(city.governor().focus == CityFocus::Growth);
    CHECK(aoc::sim::requestSetCityFocus(w.gameState, w.grid, PlayerId{0}, HOME, CityFocus::Count)
          == ErrorCode::InvalidArgument);

    city.workedTiles().clear();
    city.workedTiles().push_back(HOME);
    CHECK(aoc::sim::requestToggleWorkedTile(w.gameState, w.grid, PlayerId{0}, HOME, t1) == ErrorCode::Ok);
    CHECK(aoc::sim::requestToggleWorkedTile(w.gameState, w.grid, PlayerId{0}, HOME, t2) == ErrorCode::Ok);
    CHECK(city.availableCitizens() == 0);
    CHECK(aoc::sim::requestToggleWorkedTile(w.gameState, w.grid, PlayerId{0}, HOME, t3)
          == ErrorCode::InvalidState);          // both citizens are busy
    CHECK(aoc::sim::requestToggleWorkedTile(w.gameState, w.grid, PlayerId{0}, HOME, t1) == ErrorCode::Ok);
    CHECK_FALSE(city.isTileWorked(t1));
    CHECK(aoc::sim::requestToggleWorkedTile(w.gameState, w.grid, PlayerId{0}, HOME, t3) == ErrorCode::Ok);
    CHECK(aoc::sim::requestToggleWorkedTile(w.gameState, w.grid, PlayerId{0}, HOME, HOME)
          == ErrorCode::InvalidArgument);       // the centre cannot be toggled

    CHECK(aoc::sim::requestToggleTileLock(w.gameState, PlayerId{0}, HOME, t3) == ErrorCode::Ok);
    CHECK(city.isTileLocked(t3));
    CHECK(aoc::sim::requestToggleTileLock(w.gameState, PlayerId{0}, HOME, t3) == ErrorCode::Ok);
    CHECK_FALSE(city.isTileLocked(t3));
    CHECK(aoc::sim::requestToggleTileLock(w.gameState, PlayerId{0}, HOME, HOME) == ErrorCode::InvalidArgument);
}

TEST_CASE("queue entries can be dropped and city projects complete through production") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::City& city = aoc::test::addCityAt(w, PlayerId{0}, HOME.q, HOME.r, "Home");

    CHECK(aoc::sim::requestQueueProject(w.gameState, PlayerId{0}, HOME, CityProjectType::CampusResearch)
          == ErrorCode::InvalidArgument);       // no Campus
    CHECK(aoc::sim::cityProjectAvailable(city, CityProjectType::BreadAndCircuses));
    CHECK(aoc::sim::requestQueueProject(w.gameState, PlayerId{0}, HOME, CityProjectType::BreadAndCircuses)
          == ErrorCode::Ok);
    CHECK(aoc::sim::requestQueueProject(w.gameState, PlayerId{0}, HOME, CityProjectType::BreadAndCircuses)
          == ErrorCode::Ok);                    // repeatable
    REQUIRE(city.production().queue.size() == 2);
    CHECK(city.production().queue.front().type == ProductionItemType::Project);
    CHECK(aoc::sim::requestRemoveQueueItem(w.gameState, PlayerId{0}, HOME, 5) == ErrorCode::InvalidArgument);
    CHECK(aoc::sim::requestRemoveQueueItem(w.gameState, PlayerId{0}, HOME, 1) == ErrorCode::Ok);
    REQUIRE(city.production().queue.size() == 1);

    city.loyalty().loyalty = 50.0f;
    city.production().queue.front().progress = city.production().queue.front().totalCost;
    aoc::sim::processProductionQueues(w.gameState, w.grid, PlayerId{0});
    CHECK(city.production().queue.empty());
    CHECK(city.loyalty().loyalty == doctest::Approx(70.0f));

    CHECK(aoc::debug::isProductionItemValid(ProductionItemType::Project, 5));
    CHECK_FALSE(aoc::debug::isProductionItemValid(ProductionItemType::Project, 6));
    CHECK(aoc::sim::amenityTierName(3.5f) == "Ecstatic");
    CHECK(aoc::sim::amenityTierName(-1.0f) == "Displeased");
    CHECK(aoc::sim::turnsToComplete(city, 5.0f) == -1);   // empty queue
}

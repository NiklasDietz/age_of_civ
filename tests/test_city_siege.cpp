/**
 * @file test_city_siege.cpp
 * @brief A city has hit points behind its walls: ranged fire grinds both down,
 *        melee is repelled while the masonry stands, and only a city at zero
 *        changes hands.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/core/Random.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/city/CityBombardment.hpp"
#include "aoc/simulation/city/CitySiege.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/unit/AttackRequest.hpp"

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::UnitTypeId;
using aoc::hex::AxialCoord;

namespace {

constexpr UnitTypeId WARRIOR{0}; ///< Melee, no ranged strength.
constexpr UnitTypeId ARCHER{36}; ///< Ranged, strength 25 at range 2.

/// Player 0 besieges player 1's city at (12, 9); the attacker stands next door.
struct Siege {
    aoc::test::World world = aoc::test::makeWorld(2);
    aoc::Random rng{4242u};
    aoc::game::City* city     = nullptr;
    aoc::game::Unit* attacker = nullptr;

    explicit Siege(UnitTypeId attackerType = WARRIOR) {
        aoc::test::addCityAt(this->world, PlayerId{0}, 4, 4, "Alpha");
        this->city     = &aoc::test::addCityAt(this->world, PlayerId{1}, 12, 9, "Beta");
        this->attacker = &aoc::test::addUnitAt(this->world, PlayerId{0}, attackerType, 11, 9);
    }
};

} // namespace

TEST_CASE("a fresh city starts at full hit points and defends itself") {
    Siege s;
    CHECK(s.city->combat().hp == aoc::sim::CITY_BASE_HP);
    CHECK(s.city->combat().isAlive());
    // No walls, no garrison, era 0: still not free to take.
    CHECK(aoc::sim::cityDefenceStrength(s.world.gameState, *s.city) > 0);
}

TEST_CASE("ranged fire grinds the walls first and then the city") {
    Siege s(ARCHER);
    s.city->walls().setTier(aoc::sim::WallTier::Ancient);
    const int32_t wallsBefore = s.city->walls().currentHP;

    const aoc::sim::CityAttackResult first = aoc::sim::resolveAttackOnCity(
        s.world.gameState, s.rng, s.world.grid, *s.attacker, *s.city, 10);
    CHECK(first.wallDamage > 0);
    CHECK(s.city->walls().currentHP < wallsBefore);
    CHECK_FALSE(first.captured);
    CHECK(s.city->combat().lastAttackedTurn == 10);

    // Flatten the walls, then the same fire reaches the city itself.
    s.city->walls().currentHP           = 0;
    const int32_t hpBefore              = s.city->combat().hp;
    const aoc::sim::CityAttackResult in = aoc::sim::resolveAttackOnCity(
        s.world.gameState, s.rng, s.world.grid, *s.attacker, *s.city, 11);
    CHECK(in.cityDamage > 0);
    CHECK(s.city->combat().hp < hpBefore);
}

TEST_CASE("a ranged attacker is never hurt by the city it shells") {
    Siege s(ARCHER);
    const int32_t healthBefore            = s.attacker->hitPoints();
    const aoc::sim::CityAttackResult shot = aoc::sim::resolveAttackOnCity(
        s.world.gameState, s.rng, s.world.grid, *s.attacker, *s.city, 5);
    CHECK(shot.attackerDamage == 0);
    CHECK(s.attacker->hitPoints() == healthBefore);
}

TEST_CASE("melee is repelled while the walls stand, and pays for the attempt") {
    Siege s;
    s.city->walls().setTier(aoc::sim::WallTier::Ancient);
    const int32_t healthBefore = s.attacker->hitPoints();

    const aoc::sim::CityAttackResult charge = aoc::sim::resolveAttackOnCity(
        s.world.gameState, s.rng, s.world.grid, *s.attacker, *s.city, 7);
    CHECK(charge.repelled);
    CHECK_FALSE(charge.captured);
    CHECK(charge.wallDamage > 0);
    CHECK(charge.cityDamage == 0);
    CHECK(s.attacker->hitPoints() < healthBefore);
    CHECK(s.attacker->hitPoints() > 0); // a city repels, it does not kill
    CHECK(s.city->owner() == PlayerId{1});
}

TEST_CASE("melee takes the city only when its hit points are gone") {
    Siege s;
    s.city->walls().currentHP = 0;
    s.city->combat().hp       = 1;

    const aoc::sim::CityAttackResult storm = aoc::sim::resolveAttackOnCity(
        s.world.gameState, s.rng, s.world.grid, *s.attacker, *s.city, 20);
    CHECK(storm.captured);

    aoc::game::Player* winner = s.world.gameState.player(PlayerId{0});
    aoc::game::Player* loser  = s.world.gameState.player(PlayerId{1});
    REQUIRE(winner != nullptr);
    REQUIRE(loser != nullptr);
    const aoc::game::City* taken = winner->cityAt({12, 9});
    REQUIRE(taken != nullptr);
    CHECK(taken->owner() == PlayerId{0});
    CHECK(loser->cityAt({12, 9}) == nullptr);
    // A captured city is handed over rebuilt: full health behind new walls.
    CHECK(taken->combat().hp == taken->combat().maxHP);
    CHECK(taken->walls().isIntact());
    CHECK(s.attacker->position() == AxialCoord{12, 9});
}

TEST_CASE("a city heals only after a quiet turn") {
    Siege s;
    s.city->combat().hp               = 100;
    s.city->combat().lastAttackedTurn = 30;

    aoc::sim::healCities(s.world.gameState, PlayerId{1}, 30);
    CHECK(s.city->combat().hp == 100); // shelled this turn

    aoc::sim::healCities(s.world.gameState, PlayerId{1}, 30 + aoc::sim::CITY_HEAL_DELAY_TURNS);
    CHECK(s.city->combat().hp == 100 + aoc::sim::CITY_HEAL_PER_TURN);

    s.city->combat().hp = s.city->combat().maxHP;
    aoc::sim::healCities(s.world.gameState, PlayerId{1}, 100);
    CHECK(s.city->combat().hp == s.city->combat().maxHP); // never overheals
}

TEST_CASE("requestAttack accepts a city tile, and needs a war to do it") {
    Siege s(ARCHER);
    aoc::sim::DiplomacyManager diplomacy;
    diplomacy.initialize(2);
    diplomacy.meetPlayers(PlayerId{0}, PlayerId{1}, 1);

    CHECK(aoc::sim::requestAttack(s.world.gameState, s.rng, s.world.grid, PlayerId{0}, {11, 9},
                                  {12, 9}, &diplomacy) == ErrorCode::InvalidState);
    CHECK(s.city->combat().hp == aoc::sim::CITY_BASE_HP);

    diplomacy.declareWar(PlayerId{0}, PlayerId{1}, aoc::sim::CasusBelliType::SurpriseWar, nullptr,
                         &s.world.gameState, 2);
    s.attacker->setMovementRemaining(2);
    CHECK(aoc::sim::requestAttack(s.world.gameState, s.rng, s.world.grid, PlayerId{0}, {11, 9},
                                  {12, 9}, &diplomacy) == ErrorCode::Ok);
    CHECK(s.city->combat().hp < aoc::sim::CITY_BASE_HP);
    CHECK(s.attacker->movementRemaining() == 0);
}

TEST_CASE("walk-in is blocked while the city has hit points") {
    // pressIntoCity must NOT capture a wall-less city at full HP.
    Siege s;
    // No walls -- would have been an instant capture before Phase 3.2.
    CHECK(s.city->combat().hp == aoc::sim::CITY_BASE_HP);
    const aoc::sim::CityAttackResult r =
        aoc::sim::pressIntoCity(s.world.gameState, s.world.grid, *s.attacker, *s.city, 1);
    CHECK_FALSE(r.captured);
    CHECK(r.repelled);
    CHECK(r.cityDamage > 0);
    CHECK(s.city->combat().hp < aoc::sim::CITY_BASE_HP); // damage was applied
    CHECK(s.city->owner() == PlayerId{1});               // city still belongs to the defender
    CHECK(s.attacker->hitPoints() < 100);                // attacker pays counter-damage
    CHECK(s.attacker->hitPoints() >= 1);                 // a city never kills outright
}

TEST_CASE("walk-in captures when hp reaches zero") {
    // pressIntoCity captures immediately if a single blow zeroes the city.
    Siege s;
    s.city->combat().hp = 1; // one touch away from falling
    const aoc::sim::CityAttackResult r =
        aoc::sim::pressIntoCity(s.world.gameState, s.world.grid, *s.attacker, *s.city, 2);
    CHECK(r.captured);
    CHECK_FALSE(r.repelled);
    CHECK(s.attacker->position() == AxialCoord{12, 9}); // captor moves onto the tile
    aoc::game::Player* winner = s.world.gameState.player(PlayerId{0});
    REQUIRE(winner != nullptr);
    CHECK(winner->cityAt({12, 9}) != nullptr);
    CHECK(winner->cityAt({12, 9})->owner() == PlayerId{0});
}

TEST_CASE("a wall-less city without an encampment still fires back at its neighbours") {
    Siege s;
    CHECK_FALSE(s.city->walls().hasWalls());
    aoc::game::Unit& farOff      = aoc::test::addUnitAt(s.world, PlayerId{0}, WARRIOR, 10, 9);
    const int32_t adjacentBefore = s.attacker->hitPoints();
    const int32_t farBefore      = farOff.hitPoints();

    aoc::sim::processCityBombardment(s.world.gameState, s.world.grid, PlayerId{1}, s.rng);

    CHECK(s.attacker->hitPoints() < adjacentBefore); // chip damage on the besieger
    CHECK(s.attacker->hitPoints() > 0);              // a deterrent, not a wall
    CHECK(farOff.hitPoints() == farBefore);          // the base strike reaches one tile
}

TEST_CASE("a city under assault does not also sortie against its besiegers") {
    Siege s;
    // The city was attacked this very turn: the assault roll already answered.
    s.city->combat().lastAttackedTurn = s.world.gameState.currentTurn();
    const int32_t before              = s.attacker->hitPoints();

    aoc::sim::processCityBombardment(s.world.gameState, s.world.grid, PlayerId{1}, s.rng);

    CHECK(s.attacker->hitPoints() == before); // no third damage source
}

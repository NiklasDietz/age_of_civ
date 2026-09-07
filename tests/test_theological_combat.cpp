/**
 * @file test_theological_combat.cpp
 * @brief Religious units contest each other, and the winner's faith gains ground.
 *
 * `requestAttack` required a military attacker, so religious units were barred
 * from combat entirely: the Apostle's combat strength sat in the unit table
 * unusable and two faiths could walk straight through one another.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/core/Random.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/unit/AttackRequest.hpp"

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::UnitTypeId;
using aoc::hex::AxialCoord;

namespace {

constexpr UnitTypeId APOSTLE{20};
constexpr UnitTypeId MISSIONARY{19};
constexpr UnitTypeId WARRIOR{0};

/// Two rival apostles standing next to each other, each of its own faith.
struct Contest {
    aoc::test::World world = aoc::test::makeWorld(2);
    aoc::Random rng{99u};
    aoc::game::Unit* mine   = nullptr;
    aoc::game::Unit* theirs = nullptr;
    aoc::sim::ReligionId myFaith    = aoc::sim::NO_RELIGION;
    aoc::sim::ReligionId theirFaith = aoc::sim::NO_RELIGION;

    explicit Contest(UnitTypeId theirType = APOSTLE) {
        aoc::test::addCityAt(this->world, PlayerId{0}, 5, 5, "Mine");
        aoc::test::addCityAt(this->world, PlayerId{1}, 12, 5, "Theirs");
        this->myFaith =
            this->world.gameState.religionTracker().foundReligion("Ours", PlayerId{0});
        this->theirFaith =
            this->world.gameState.religionTracker().foundReligion("Theirs", PlayerId{1});

        this->mine   = &aoc::test::addUnitAt(this->world, PlayerId{0}, APOSTLE, 8, 5);
        this->theirs = &aoc::test::addUnitAt(this->world, PlayerId{1}, theirType, 9, 5);
        this->mine->spreadingReligion   = this->myFaith;
        this->theirs->spreadingReligion = this->theirFaith;
        this->mine->setMovementRemaining(2);
    }

    ErrorCode attack() {
        return aoc::sim::requestAttack(this->world.gameState, this->rng, this->world.grid,
                                       PlayerId{0}, {8, 5}, {9, 5}, nullptr);
    }
};

} // namespace

TEST_CASE("an apostle can contest a rival apostle, and one of them loses") {
    Contest c;
    const int32_t before = c.world.gameState.player(PlayerId{0})->unitCount()
                           + c.world.gameState.player(PlayerId{1})->unitCount();

    CHECK(c.attack() == ErrorCode::Ok);

    const int32_t after = c.world.gameState.player(PlayerId{0})->unitCount()
                          + c.world.gameState.player(PlayerId{1})->unitCount();
    CHECK(after == before - 1); // exactly one of them is gone
}

TEST_CASE("the winner is spent, not untouched, and has used its turn") {
    Contest c;
    REQUIRE(c.attack() == ErrorCode::Ok);

    aoc::game::Unit* survivor = c.world.gameState.player(PlayerId{0})->unitAt({8, 5});
    if (survivor != nullptr) {
        CHECK(survivor->hitPoints() < survivor->typeDef().maxHitPoints);
        CHECK(survivor->movementRemaining() == 0);
    }
}

TEST_CASE("winning an argument gains ground in the nearest city") {
    Contest c;
    aoc::game::City* nearest = c.world.gameState.player(PlayerId{0})->cityAt({5, 5});
    REQUIRE(nearest != nullptr);
    const float mineBefore  = nearest->religion().pressure[c.myFaith];
    const float theirBefore = nearest->religion().pressure[c.theirFaith];

    REQUIRE(c.attack() == ErrorCode::Ok);

    const float mineAfter  = nearest->religion().pressure[c.myFaith];
    const float theirAfter = nearest->religion().pressure[c.theirFaith];
    // Whoever won, exactly one faith gained; the argument is never a draw.
    CHECK(((mineAfter > mineBefore) != (theirAfter > theirBefore)));
}

TEST_CASE("theology is only contested between rival faiths and adjacent units") {
    SUBCASE("two units of the same faith have nothing to argue about") {
        Contest c;
        c.theirs->spreadingReligion = c.myFaith;
        CHECK(c.attack() == ErrorCode::InvalidState);
    }

    SUBCASE("a soldier is not a theological target") {
        Contest c;
        c.world.gameState.player(PlayerId{1})->removeUnit(c.theirs);
        aoc::test::addUnitAt(c.world, PlayerId{1}, WARRIOR, 9, 5);
        CHECK(c.attack() == ErrorCode::InvalidArgument);
    }

    SUBCASE("an argument cannot be had at a distance") {
        Contest c;
        c.mine->setPosition({6, 5});
        CHECK(aoc::sim::requestAttack(c.world.gameState, c.rng, c.world.grid, PlayerId{0},
                                      {6, 5}, {9, 5}, nullptr)
              == ErrorCode::InvalidUnitAction);
    }

    SUBCASE("a spent unit cannot start one") {
        Contest c;
        c.mine->setMovementRemaining(0);
        CHECK(c.attack() == ErrorCode::InvalidState);
    }
}

TEST_CASE("an apostle outmatches a missionary, who was never built to argue") {
    aoc::test::World w = aoc::test::makeWorld(2);
    aoc::game::Unit& apostle    = aoc::test::addUnitAt(w, PlayerId{0}, APOSTLE, 8, 5);
    aoc::game::Unit& missionary = aoc::test::addUnitAt(w, PlayerId{1}, MISSIONARY, 9, 5);
    CHECK(aoc::sim::theologicalStrength(apostle)
          > aoc::sim::theologicalStrength(missionary));
    CHECK(aoc::sim::theologicalStrength(missionary) > 0.0f); // feeble, not defenceless
}

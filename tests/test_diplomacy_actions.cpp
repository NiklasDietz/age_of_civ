/**
 * @file test_diplomacy_actions.cpp
 * @brief Diplomacy requests: war with a casus belli, the peace lock, denounce, friendship,
 *        delegation, embassy, open borders, and the AI's peace rule.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/diplomacy/DiplomacyActions.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/religion/Religion.hpp"

#include <algorithm>
#include <string>
#include <vector>

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::sim::CasusBelliType;
using aoc::sim::DiplomacyManager;

namespace {

constexpr aoc::UnitTypeId WARRIOR{0};

struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(3);
    DiplomacyManager d;

    Fixture() {
        this->d.initialize(3);
        this->d.meetPlayers(PlayerId{0}, PlayerId{1}, 5);
        aoc::test::addCityAt(this->world, PlayerId{0}, 4, 4, "Alpha");
        aoc::test::addCityAt(this->world, PlayerId{1}, 14, 8, "Beta");
        aoc::test::addCityAt(this->world, PlayerId{2}, 20, 12, "Gamma");
    }

    bool hasModifier(PlayerId a, PlayerId b, const std::string& reason) const {
        for (const aoc::sim::RelationModifier& m : this->d.relation(a, b).modifiers) {
            if (m.reason == reason) {
                return true;
            }
        }
        return false;
    }

    static bool contains(const std::vector<CasusBelliType>& list, CasusBelliType cb) {
        return std::find(list.begin(), list.end(), cb) != list.end();
    }
};

} // namespace

TEST_CASE("war needs contact and no lock; peace waits ten turns and re-arms the lock") {
    Fixture f;
    CHECK(aoc::sim::requestDeclareWar(f.world.gameState, f.d, PlayerId{0}, PlayerId{2},
                                      CasusBelliType::SurpriseWar,
                                      10) == ErrorCode::InvalidState); // unmet
    CHECK(aoc::sim::requestDeclareWar(f.world.gameState, f.d, PlayerId{0}, PlayerId{0},
                                      CasusBelliType::SurpriseWar,
                                      10) == ErrorCode::EntityNotFound);
    CHECK(aoc::sim::requestDeclareWar(f.world.gameState, f.d, PlayerId{0}, PlayerId{200},
                                      CasusBelliType::SurpriseWar,
                                      10) == ErrorCode::EntityNotFound);

    CHECK(aoc::sim::requestDeclareWar(f.world.gameState, f.d, PlayerId{0}, PlayerId{1},
                                      CasusBelliType::SurpriseWar, 10) == ErrorCode::Ok);
    CHECK(f.d.isAtWar(PlayerId{0}, PlayerId{1}));
    CHECK(f.d.relation(PlayerId{1}, PlayerId{0}).warDeclaredOnTurn == 10);
    CHECK(aoc::sim::requestDeclareWar(f.world.gameState, f.d, PlayerId{0}, PlayerId{1},
                                      CasusBelliType::SurpriseWar,
                                      11) == ErrorCode::InvalidState); // already

    CHECK(aoc::sim::requestMakePeace(f.world.gameState, f.d, PlayerId{0}, PlayerId{1}, 15) ==
          ErrorCode::InvalidState); // too early
    CHECK(aoc::sim::requestMakePeace(f.world.gameState, f.d, PlayerId{0}, PlayerId{1}, 20) ==
          ErrorCode::Ok);
    CHECK_FALSE(f.d.isAtWar(PlayerId{0}, PlayerId{1}));
    CHECK(f.d.relation(PlayerId{0}, PlayerId{1}).turnsSincePeace == 0);
    CHECK(aoc::sim::requestMakePeace(f.world.gameState, f.d, PlayerId{0}, PlayerId{1}, 21) ==
          ErrorCode::InvalidState); // not at war

    CHECK(aoc::sim::requestDeclareWar(f.world.gameState, f.d, PlayerId{0}, PlayerId{1},
                                      CasusBelliType::SurpriseWar,
                                      25) == ErrorCode::InvalidState); // peace lock
    f.d.relation(PlayerId{0}, PlayerId{1}).turnsSincePeace = aoc::sim::PEACE_LOCK_TURNS;
    CHECK(aoc::sim::requestDeclareWar(f.world.gameState, f.d, PlayerId{0}, PlayerId{1},
                                      CasusBelliType::SurpriseWar, 30) == ErrorCode::Ok);
}

TEST_CASE("a border casus belli belongs to the owner, not to the trespasser") {
    Fixture f;
    aoc::game::GameState& gs = f.world.gameState;

    // BorderViolation writes the flag on relation(violator, owner): player 1
    // parked units in player 0's land long enough for player 0 to earn a CB.
    f.d.relation(PlayerId{1}, PlayerId{0}).casusBelliLand = true;

    CHECK(f.d.holdsCasusBelli(PlayerId{0}, PlayerId{1}));
    CHECK_FALSE(f.d.holdsCasusBelli(PlayerId{1}, PlayerId{0}));

    // The wronged owner may declare a Formal War; the trespasser may not.
    CHECK(aoc::sim::casusBelliUsable(gs, f.d, PlayerId{0}, PlayerId{1}, CasusBelliType::FormalWar,
                                     20) == ErrorCode::Ok);
    CHECK(aoc::sim::casusBelliUsable(gs, f.d, PlayerId{1}, PlayerId{0}, CasusBelliType::FormalWar,
                                     20) == ErrorCode::InvalidArgument);
    CHECK(Fixture::contains(aoc::sim::availableCasusBelli(gs, f.d, PlayerId{0}, PlayerId{1}, 20),
                            CasusBelliType::FormalWar));
    CHECK_FALSE(
        Fixture::contains(aoc::sim::availableCasusBelli(gs, f.d, PlayerId{1}, PlayerId{0}, 20),
                          CasusBelliType::FormalWar));

    // Naval trespass grants it the same way round.
    Fixture g;
    g.d.relation(PlayerId{1}, PlayerId{0}).casusBelliNaval = true;
    CHECK(g.d.holdsCasusBelli(PlayerId{0}, PlayerId{1}));
    CHECK_FALSE(g.d.holdsCasusBelli(PlayerId{1}, PlayerId{0}));
}

TEST_CASE("each casus belli needs its justification") {
    Fixture f;
    aoc::game::GameState& gs = f.world.gameState;
    std::vector<CasusBelliType> cbs =
        aoc::sim::availableCasusBelli(gs, f.d, PlayerId{0}, PlayerId{1}, 10);
    CHECK(cbs.size() == 1);
    CHECK(Fixture::contains(cbs, CasusBelliType::SurpriseWar));
    CHECK(aoc::sim::requestDeclareWar(gs, f.d, PlayerId{0}, PlayerId{1}, CasusBelliType::FormalWar,
                                      10) == ErrorCode::InvalidArgument);

    // Formal: a denouncement within DENOUNCE_TURNS.
    CHECK(aoc::sim::requestDenounce(gs, f.d, PlayerId{0}, PlayerId{1}, 10) == ErrorCode::Ok);
    CHECK(aoc::sim::casusBelliUsable(gs, f.d, PlayerId{0}, PlayerId{1}, CasusBelliType::FormalWar,
                                     20) == ErrorCode::Ok);
    CHECK(aoc::sim::casusBelliUsable(gs, f.d, PlayerId{0}, PlayerId{1}, CasusBelliType::FormalWar,
                                     41) == ErrorCode::InvalidArgument);
    // Colonial: two eras ahead.
    gs.player(PlayerId{0})->era().currentEra = aoc::EraId{2};
    CHECK(aoc::sim::casusBelliUsable(gs, f.d, PlayerId{0}, PlayerId{1}, CasusBelliType::ColonialWar,
                                     20) == ErrorCode::Ok);
    CHECK(aoc::sim::casusBelliUsable(gs, f.d, PlayerId{1}, PlayerId{0}, CasusBelliType::ColonialWar,
                                     20) == ErrorCode::InvalidArgument);
    // Economic: the target embargoes the actor.
    f.d.setEmbargo(PlayerId{1}, PlayerId{0}, true);
    CHECK(aoc::sim::casusBelliUsable(gs, f.d, PlayerId{0}, PlayerId{1}, CasusBelliType::EconomicWar,
                                     20) == ErrorCode::Ok);
    // Reconquest: the target holds a city the actor founded; Liberation: one an ally founded.
    aoc::game::City& taken = aoc::test::addCityAt(f.world, PlayerId{1}, 16, 8, "Taken");
    taken.setOriginalOwner(PlayerId{0});
    CHECK(aoc::sim::casusBelliUsable(gs, f.d, PlayerId{0}, PlayerId{1},
                                     CasusBelliType::ReconquestWar, 20) == ErrorCode::Ok);
    aoc::game::City& allied = aoc::test::addCityAt(f.world, PlayerId{1}, 18, 6, "Allied");
    allied.setOriginalOwner(PlayerId{2});
    CHECK(aoc::sim::casusBelliUsable(gs, f.d, PlayerId{0}, PlayerId{1},
                                     CasusBelliType::LiberationWar, 20) ==
          ErrorCode::InvalidArgument); // no alliance with player 2 yet
    f.d.relation(PlayerId{0}, PlayerId{2}).hasDefensiveAlliance = true;
    CHECK(aoc::sim::casusBelliUsable(gs, f.d, PlayerId{0}, PlayerId{1},
                                     CasusBelliType::LiberationWar, 20) == ErrorCode::Ok);
    // Holy: the target's religion holds one of the actor's cities.
    gs.player(PlayerId{1})->faith().foundedReligion = aoc::sim::ReligionId{0};
    CHECK(aoc::sim::casusBelliUsable(gs, f.d, PlayerId{0}, PlayerId{1}, CasusBelliType::HolyWar,
                                     20) == ErrorCode::InvalidArgument);
    gs.player(PlayerId{0})->cities().front()->religion().pressure[0] = 10.0f;
    CHECK(aoc::sim::casusBelliUsable(gs, f.d, PlayerId{0}, PlayerId{1}, CasusBelliType::HolyWar,
                                     20) == ErrorCode::Ok);
    CHECK(aoc::sim::casusBelliUsable(gs, f.d, PlayerId{0}, PlayerId{1},
                                     CasusBelliType::ProtectorateWar,
                                     20) == ErrorCode::InvalidArgument);

    cbs = aoc::sim::availableCasusBelli(gs, f.d, PlayerId{0}, PlayerId{1}, 20);
    CHECK(cbs.size() == 7); // everything but Protectorate
    // A justified war carries the casus belli and the smaller grievance penalty.
    CHECK(aoc::sim::requestDeclareWar(gs, f.d, PlayerId{0}, PlayerId{1},
                                      CasusBelliType::ReconquestWar, 20) == ErrorCode::Ok);
    CHECK(f.d.relation(PlayerId{1}, PlayerId{0}).lastCasusBelli == CasusBelliType::ReconquestWar);
}

TEST_CASE("denouncement and friendship exclude each other and expire") {
    Fixture f;
    aoc::game::GameState& gs = f.world.gameState;
    CHECK(aoc::sim::requestDeclareFriendship(gs, f.d, PlayerId{0}, PlayerId{1}, 10) ==
          ErrorCode::InvalidState); // neutral: they decline
    f.d.addModifier(PlayerId{0}, PlayerId{1}, {"Test goodwill", 15, 0});
    CHECK(aoc::sim::requestDeclareFriendship(gs, f.d, PlayerId{0}, PlayerId{1}, 10) ==
          ErrorCode::Ok);
    CHECK(f.d.relation(PlayerId{1}, PlayerId{0}).friendshipUntilTurn ==
          10 + aoc::sim::FRIENDSHIP_TURNS);
    CHECK(f.hasModifier(PlayerId{1}, PlayerId{0}, "Declaration of Friendship"));
    CHECK(aoc::sim::requestDeclareWar(gs, f.d, PlayerId{0}, PlayerId{1},
                                      CasusBelliType::SurpriseWar,
                                      20) == ErrorCode::InvalidState); // friends do not fight
    CHECK(aoc::sim::requestDeclareWar(gs, f.d, PlayerId{1}, PlayerId{0},
                                      CasusBelliType::SurpriseWar, 20) == ErrorCode::InvalidState);
    CHECK(aoc::sim::requestDenounce(gs, f.d, PlayerId{0}, PlayerId{1}, 20) ==
          ErrorCode::InvalidState);
    CHECK(aoc::sim::requestDeclareFriendship(gs, f.d, PlayerId{0}, PlayerId{1}, 20) ==
          ErrorCode::InvalidState); // already friends

    f.d.expireAgreements(10 + aoc::sim::FRIENDSHIP_TURNS);
    CHECK(f.d.relation(PlayerId{0}, PlayerId{1}).friendshipUntilTurn == -1);
    CHECK(aoc::sim::requestDenounce(gs, f.d, PlayerId{0}, PlayerId{1}, 40) == ErrorCode::Ok);
    CHECK(f.d.relation(PlayerId{0}, PlayerId{1}).denouncedOnTurn == 40);
    CHECK(f.d.relation(PlayerId{1}, PlayerId{0}).denouncedOnTurn == -1); // per direction
    CHECK(f.hasModifier(PlayerId{1}, PlayerId{0}, "Denounced"));
    CHECK(aoc::sim::requestDenounce(gs, f.d, PlayerId{0}, PlayerId{1}, 41) ==
          ErrorCode::InvalidState);
    CHECK(aoc::sim::requestDeclareFriendship(gs, f.d, PlayerId{1}, PlayerId{0}, 41) ==
          ErrorCode::InvalidState); // the denounced side cannot befriend the denouncer either
}

TEST_CASE("delegation and embassy cost gold, raise intelligence and cannot repeat") {
    Fixture f;
    aoc::game::GameState& gs = f.world.gameState;
    aoc::game::Player& me    = *gs.player(PlayerId{0});
    CHECK(aoc::sim::requestSendDelegation(gs, f.d, PlayerId{0}, PlayerId{1}) ==
          ErrorCode::InsufficientResources);
    me.addGold(100, aoc::sim::MoneyFlow::external());
    const aoc::CurrencyAmount theirsBefore = gs.player(PlayerId{1})->treasury();
    CHECK(aoc::sim::requestSendDelegation(gs, f.d, PlayerId{0}, PlayerId{1}) == ErrorCode::Ok);
    CHECK(me.treasury() == 100 - aoc::sim::DELEGATION_GOLD);
    CHECK(gs.player(PlayerId{1})->treasury() == theirsBefore + aoc::sim::DELEGATION_GOLD); // it is a transfer
    CHECK(f.d.relation(PlayerId{0}, PlayerId{1}).hasDelegation);
    CHECK_FALSE(f.d.relation(PlayerId{1}, PlayerId{0}).hasDelegation);
    CHECK(f.d.relation(PlayerId{0}, PlayerId{1}).intelLevel == 1);
    CHECK(aoc::sim::requestSendDelegation(gs, f.d, PlayerId{0}, PlayerId{1}) ==
          ErrorCode::InvalidState);

    f.d.addModifier(PlayerId{0}, PlayerId{1}, {"Test insult", -60, 0});
    CHECK(aoc::sim::requestEstablishEmbassy(gs, f.d, PlayerId{0}, PlayerId{1}) ==
          ErrorCode::InvalidState); // hostile
    f.d.relation(PlayerId{0}, PlayerId{1}).modifiers.clear();
    f.d.relation(PlayerId{1}, PlayerId{0}).modifiers.clear();
    CHECK(aoc::sim::requestEstablishEmbassy(gs, f.d, PlayerId{0}, PlayerId{1}) == ErrorCode::Ok);
    CHECK(me.treasury() == 100 - aoc::sim::DELEGATION_GOLD - aoc::sim::EMBASSY_GOLD);
    CHECK(f.d.relation(PlayerId{0}, PlayerId{1}).hasEmbassy);
    CHECK(f.d.relation(PlayerId{0}, PlayerId{1}).intelLevel == 2);
    CHECK(aoc::sim::requestEstablishEmbassy(gs, f.d, PlayerId{0}, PlayerId{1}) ==
          ErrorCode::InvalidState);
}

TEST_CASE("open borders need consent, last thirty turns and expire on schedule") {
    Fixture f;
    aoc::game::GameState& gs = f.world.gameState;
    CHECK(aoc::sim::requestOpenBorders(gs, f.d, PlayerId{0}, PlayerId{1}, 10) ==
          ErrorCode::InvalidState);
    f.d.addModifier(PlayerId{0}, PlayerId{1}, {"Test goodwill", 15, 0});
    CHECK(aoc::sim::requestOpenBorders(gs, f.d, PlayerId{0}, PlayerId{1}, 10) == ErrorCode::Ok);
    CHECK(f.d.relation(PlayerId{1}, PlayerId{0}).hasOpenBorders);
    CHECK(f.d.relation(PlayerId{1}, PlayerId{0}).openBordersUntilTurn ==
          10 + aoc::sim::OPEN_BORDERS_TURNS);
    CHECK(aoc::sim::requestOpenBorders(gs, f.d, PlayerId{0}, PlayerId{1}, 11) ==
          ErrorCode::InvalidState);
    f.d.expireAgreements(10 + aoc::sim::OPEN_BORDERS_TURNS - 1);
    CHECK(f.d.relation(PlayerId{0}, PlayerId{1}).hasOpenBorders);
    f.d.expireAgreements(10 + aoc::sim::OPEN_BORDERS_TURNS);
    CHECK_FALSE(f.d.relation(PlayerId{0}, PlayerId{1}).hasOpenBorders);
    CHECK(f.d.relation(PlayerId{0}, PlayerId{1}).openBordersUntilTurn == -1);

    // Borders the AI grants without a duration are not touched.
    f.d.grantOpenBorders(PlayerId{1}, PlayerId{2});
    f.d.expireAgreements(500);
    CHECK(f.d.relation(PlayerId{1}, PlayerId{2}).hasOpenBorders);
}

TEST_CASE("an AI refuses peace while it is winning and accepts when outnumbered") {
    Fixture f;
    aoc::game::GameState& gs = f.world.gameState;
    for (int32_t i = 0; i < 4; ++i) {
        aoc::test::addUnitAt(f.world, PlayerId{1}, WARRIOR, 14 + i, 9);
    }
    aoc::test::addUnitAt(f.world, PlayerId{0}, WARRIOR, 5, 4);
    REQUIRE(aoc::sim::requestDeclareWar(gs, f.d, PlayerId{1}, PlayerId{0},
                                        CasusBelliType::SurpriseWar, 10) == ErrorCode::Ok);
    CHECK_FALSE(aoc::sim::aiAcceptsPeace(gs, PlayerId{1}, PlayerId{0}));
    CHECK(aoc::sim::requestMakePeace(gs, f.d, PlayerId{0}, PlayerId{1}, 30) ==
          ErrorCode::InvalidState);

    for (int32_t i = 0; i < 12; ++i) {
        aoc::test::addUnitAt(f.world, PlayerId{0}, WARRIOR, 3 + (i % 6), 2 + i / 6);
    }
    CHECK(aoc::sim::aiAcceptsPeace(gs, PlayerId{1}, PlayerId{0}));
    CHECK(aoc::sim::requestMakePeace(gs, f.d, PlayerId{0}, PlayerId{1}, 30) == ErrorCode::Ok);
}

TEST_CASE("a declared war clears friendship and timed borders and records its turn") {
    Fixture f;
    aoc::game::GameState& gs = f.world.gameState;
    f.d.addModifier(PlayerId{0}, PlayerId{1}, {"Test goodwill", 15, 0});
    REQUIRE(aoc::sim::requestOpenBorders(gs, f.d, PlayerId{0}, PlayerId{1}, 10) == ErrorCode::Ok);
    f.d.declareWar(PlayerId{1}, PlayerId{0}, CasusBelliType::SurpriseWar, nullptr, &gs, 12);
    CHECK(f.d.relation(PlayerId{0}, PlayerId{1}).warDeclaredOnTurn == 12);
    CHECK(f.d.relation(PlayerId{0}, PlayerId{1}).openBordersUntilTurn == -1);
    CHECK_FALSE(f.d.relation(PlayerId{0}, PlayerId{1}).hasOpenBorders);
}

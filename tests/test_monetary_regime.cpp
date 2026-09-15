/**
 * @file test_monetary_regime.cpp
 * @brief The monetary regime is a decision (plan 2.5): one request with a
 *        gate for every prerequisite, each refusal leaving the state
 *        untouched; adoption turns bullion into the people's coin; the live
 *        partner count; and the property that no other code but the request
 *        and the crisis suspension changes the regime.
 *
 *        Phase B: CoinTier and the Mint gate are removed. The CommodityMoney
 *        gate now relies solely on canTransition (privateSpecie threshold).
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/monetary/MoneyFlow.hpp"
#include "aoc/simulation/monetary/MonetaryActions.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"

#include <filesystem>
#include <fstream>
#include <string>

using aoc::ErrorCode;
using aoc::PlayerId;
using aoc::sim::MonetarySystemType;

namespace {

constexpr PlayerId P0{0};
constexpr PlayerId P1{1};
constexpr aoc::UnitTypeId TRADER{30};
constexpr aoc::TechId BANKING{9};
constexpr aoc::TechId ECONOMICS{13};
constexpr aoc::TechId PRINTING{55};

/// A Barter civ ready for coinage: 120 bullion in hand (meets the strength
/// threshold) and the canTransition table satisfied.
struct Ready {
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(P0);
    Ready() {
        aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
        aoc::test::addCityAt(w, P1, 14, 8, "Beta");
        p.monetary().bullion = 120;
    }
    [[nodiscard]] ErrorCode adopt(MonetarySystemType target) {
        return aoc::sim::requestSetMonetaryRegime(w.gameState, P0, target);
    }
    [[nodiscard]] aoc::sim::MonetaryStateComponent snapshot() const { return p.monetary(); }
};

[[nodiscard]] bool same(const aoc::sim::MonetaryStateComponent& a,
                        const aoc::sim::MonetaryStateComponent& b) {
    return a.system == b.system && a.bullion == b.bullion &&
           a.privateSpecie == b.privateSpecie && a.moneyGood == b.moneyGood &&
           a.turnsInCurrentSystem == b.turnsInCurrentSystem;
}

} // namespace

TEST_CASE("adopting coinage turns the bullion into the people's coin") {
    Ready r;
    CHECK(aoc::sim::coinageWithinReach(r.w.gameState, P0));
    REQUIRE(r.adopt(MonetarySystemType::CommodityMoney) == ErrorCode::Ok);
    CHECK(r.p.monetary().system == MonetarySystemType::CommodityMoney);
    CHECK(r.p.monetary().bullion == 0);
    CHECK(r.p.monetary().privateSpecie == 120);
    CHECK(r.p.monetary().turnsInCurrentSystem == 0);
    CHECK_FALSE(aoc::sim::coinageWithinReach(r.w.gameState, P0)); // done
}

TEST_CASE("every coinage gate refuses and leaves the state untouched") {
    SUBCASE("unknown player") {
        Ready r;
        CHECK(aoc::sim::requestSetMonetaryRegime(r.w.gameState, PlayerId{200},
                                                 MonetarySystemType::CommodityMoney) ==
              ErrorCode::EntityNotFound);
    }
    SUBCASE("too little bullion (strength threshold)") {
        Ready r;
        r.p.monetary().bullion = 1; // far below the canTransition threshold
        const aoc::sim::MonetaryStateComponent before = r.snapshot();
        CHECK(r.adopt(MonetarySystemType::CommodityMoney) == ErrorCode::InvalidMonetaryTransition);
        CHECK(same(before, r.snapshot()));
        CHECK_FALSE(aoc::sim::coinageWithinReach(r.w.gameState, P0));
    }
    SUBCASE("already adopted, or a stage skipped") {
        Ready r;
        REQUIRE(r.adopt(MonetarySystemType::CommodityMoney) == ErrorCode::Ok);
        const aoc::sim::MonetaryStateComponent before = r.snapshot();
        CHECK(r.adopt(MonetarySystemType::CommodityMoney) == ErrorCode::InvalidMonetaryTransition);
        CHECK(r.adopt(MonetarySystemType::FiatMoney) == ErrorCode::InvalidMonetaryTransition);
        CHECK(r.adopt(MonetarySystemType::Barter) == ErrorCode::InvalidMonetaryTransition);
        CHECK(same(before, r.snapshot()));
    }
}

TEST_CASE("the Gold Standard needs Banking; Fiat needs a press or the theory, partners and calm prices") {
    Ready r;
    REQUIRE(r.adopt(MonetarySystemType::CommodityMoney) == ErrorCode::Ok);
    aoc::test::addCityAt(r.w, P0, 9, 5, "Gamma"); // the table wants two cities
    CHECK(r.adopt(MonetarySystemType::GoldStandard) ==
          ErrorCode::InvalidMonetaryTransition); // no Banking
    r.p.tech().completedTechs[BANKING.value] = true;
    aoc::sim::MoneyLedger ledger;
    r.p.setMoneyLedger(&ledger);
    const int64_t worldBefore = aoc::sim::worldMoney(r.w.gameState);
    REQUIRE(r.adopt(MonetarySystemType::GoldStandard) == ErrorCode::Ok);
    CHECK(r.p.monetary().system == MonetarySystemType::GoldStandard);
    // Notes issued one for one against the people's coin, booked as printed.
    CHECK(r.p.monetary().privateNotes == 120);
    CHECK(r.p.monetary().privateSpecie == 120);
    CHECK(ledger.civs[0].printed == 120);
    CHECK(r.p.monetary().goldBackingRatio == doctest::Approx(1.0f));
    CHECK(r.p.monetary().moneySupply == 240);
    CHECK(aoc::sim::moneyConserved(worldBefore, aoc::sim::worldMoney(r.w.gameState), ledger));

    // Fiat: the row wants 75 face, 5 turns in, 2 partners, inflation under 5%.
    r.p.monetary().turnsInCurrentSystem = 5;
    r.p.monetary().inflationRate        = 0.0f;
    const aoc::sim::MonetaryStateComponent before = r.snapshot();
    CHECK(r.adopt(MonetarySystemType::FiatMoney) ==
          ErrorCode::InvalidMonetaryTransition); // no Printing or Economics
    r.p.tech().completedTechs[PRINTING.value] = true;
    CHECK(r.adopt(MonetarySystemType::FiatMoney) ==
          ErrorCode::InvalidMonetaryTransition); // one partner short of two
    CHECK(same(before, r.snapshot()));

    // A live Trader route to P1 is one partner.
    aoc::game::Unit& trader = aoc::test::addUnitAt(r.w, P0, TRADER, 6, 5);
    trader.trader().owner     = P0;
    trader.trader().destOwner = P1;
    CHECK(aoc::sim::livePartnerCount(r.w.gameState, P0) == 1);
    CHECK(r.adopt(MonetarySystemType::FiatMoney) == ErrorCode::InvalidMonetaryTransition);
    aoc::sim::DiplomaticDeal deal{};
    deal.playerA    = PlayerId{1};
    deal.playerB    = P0;
    deal.isAccepted = true;
    aoc::sim::DealTerm term{};
    term.type       = aoc::sim::DealTermType::SupplyContract;
    term.fromPlayer = PlayerId{1};
    term.toPlayer   = P0;
    term.duration   = 10;
    deal.terms.push_back(term);
    r.w.gameState.deals().activeDeals.push_back(deal);
    CHECK(aoc::sim::livePartnerCount(r.w.gameState, P0) == 1); // the same civ twice is one partner
    // Use a second trader bound for a civ id the world does not seat: it must not count.
    aoc::game::Unit& ghost = aoc::test::addUnitAt(r.w, P0, TRADER, 7, 5);
    ghost.trader().owner     = P0;
    ghost.trader().destOwner = static_cast<PlayerId>(aoc::sim::CITY_STATE_PLAYER_BASE);
    CHECK(aoc::sim::livePartnerCount(r.w.gameState, P0) == 1);

    // Inflation at the limit refuses too.
    aoc::test::World w3 = aoc::test::makeWorld(3);
    static_cast<void>(w3);
    r.p.monetary().inflationRate = 0.05f;
    CHECK(r.adopt(MonetarySystemType::FiatMoney) == ErrorCode::InvalidMonetaryTransition);
}

TEST_CASE("with three civs, two live partners and Economics, Fiat is adopted, and Digital needs Computers") {
    aoc::test::World w   = aoc::test::makeWorld(3);
    aoc::game::Player& p = *w.gameState.player(P0);
    aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::test::addCityAt(w, P0, 9, 5, "Gamma");
    aoc::test::addCityAt(w, P1, 14, 8, "Beta");
    aoc::test::addCityAt(w, PlayerId{2}, 18, 12, "Delta");
    p.monetary().system               = MonetarySystemType::GoldStandard;
    p.monetary().privateSpecie        = 4;
    p.monetary().moneySupply          = 400;
    p.monetary().turnsInCurrentSystem = 5;
    p.monetary().inflationRate        = 0.01f;
    p.monetary().gdp                  = 1000;
    p.tech().completedTechs[BANKING.value]   = true;
    p.tech().completedTechs[ECONOMICS.value] = true;
    for (const PlayerId other : {P1, PlayerId{2}}) {
        aoc::game::Unit& t = aoc::test::addUnitAt(w, P0, TRADER, 6 + other, 5);
        t.trader().owner     = P0;
        t.trader().destOwner = other;
    }
    CHECK(aoc::sim::livePartnerCount(w.gameState, P0) == 2);
    CHECK(aoc::sim::requestSetMonetaryRegime(w.gameState, P0, MonetarySystemType::FiatMoney) ==
          ErrorCode::Ok);
    CHECK(p.monetary().system == MonetarySystemType::FiatMoney);
    CHECK(aoc::sim::requestSetMonetaryRegime(w.gameState, P0, MonetarySystemType::Digital) ==
          ErrorCode::InvalidMonetaryTransition); // no Computers, no ten turns in
}

TEST_CASE("the poorest civ may still issue fiat; whether it is accepted is another matter") {
    aoc::test::World w   = aoc::test::makeWorld(3);
    aoc::game::Player& p = *w.gameState.player(P0);
    aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::test::addCityAt(w, P0, 9, 5, "Gamma");
    aoc::test::addCityAt(w, P1, 14, 8, "Beta");
    aoc::test::addCityAt(w, PlayerId{2}, 18, 12, "Delta");
    p.monetary().system               = MonetarySystemType::GoldStandard;
    p.monetary().privateSpecie        = 4;
    p.monetary().moneySupply          = 400;
    p.monetary().turnsInCurrentSystem = 5;
    p.monetary().inflationRate        = 0.01f;
    p.tech().completedTechs[BANKING.value]   = true;
    p.tech().completedTechs[ECONOMICS.value] = true;
    for (const PlayerId other : {P1, PlayerId{2}}) {
        aoc::game::Unit& t = aoc::test::addUnitAt(w, P0, TRADER, 6 + other, 5);
        t.trader().owner     = P0;
        t.trader().destOwner = other;
    }

    // Dead last by GDP, by a wide margin.
    p.monetary().gdp                                = 1;
    w.gameState.player(P1)->monetary().gdp          = 100000;
    w.gameState.player(PlayerId{2})->monetary().gdp = 100000;

    CHECK(aoc::sim::requestSetMonetaryRegime(w.gameState, P0, MonetarySystemType::FiatMoney) ==
          ErrorCode::Ok);
    CHECK(p.monetary().system == MonetarySystemType::FiatMoney);
}

TEST_CASE("a transition is judged by the measure of the regime it moves to") {
    aoc::sim::MonetaryStateComponent m{};
    m.privateSpecie = 30; // the people's coin

    // Under a gold standard the private specie pool is the strength measure.
    CHECK(m.strengthUnder(MonetarySystemType::GoldStandard) == 30);

    // Fiat runs on money supply, so that is what a fiat candidate is measured
    // on. Asking it for gold reserves it will not use gates the wrong quantity.
    m.moneySupply = 500;
    CHECK(m.strengthUnder(MonetarySystemType::FiatMoney) == 500);

    // currencyStrength() still reports the system the civ is actually running.
    m.system = MonetarySystemType::CommodityMoney;
    CHECK(m.currencyStrength() == 30);
}

TEST_CASE("nothing but the request and the crisis suspension changes the regime") {
    const std::filesystem::path root(AOC_SOURCE_DIR);
    if (!std::filesystem::exists(root / "src")) {
        MESSAGE("source tree not present, skipping the sweep");
        return;
    }
    int32_t writers = 0;
    for (const std::string dir : {"src", "include"}) {
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::recursive_directory_iterator(root / dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string ext = entry.path().extension().string();
            if (ext != ".cpp" && ext != ".hpp") {
                continue;
            }
            std::ifstream in(entry.path());
            const std::string text((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            const bool writes = text.find(".transitionTo(") != std::string::npos ||
                                text.find("->transitionTo(") != std::string::npos;
            if (!writes) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            const bool allowed = name == "MonetaryActions.cpp" || name == "CurrencyCrisis.cpp";
            if (!allowed) {
                MESSAGE("unexpected regime writer: " << entry.path().string());
            }
            CHECK(allowed);
            ++writers;
        }
    }
    CHECK(writers == 2);
}

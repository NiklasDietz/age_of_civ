/**
 * @file test_trade_policy_hooks.cpp
 * @brief The declared-but-dead trade hooks, wired (plan 3.3): route slots
 *        from policies, the civ's trait, agreements and civics; the
 *        Caravansaries bonus as collection reach; tariffs at delivery into
 *        the importer's treasury, with the agreement discount and the
 *        Mercantilism premium; and Conscription's cut of the unit bill.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/economy/AdvancedEconomics.hpp"
#include "aoc/simulation/economy/Maintenance.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/economy/TradeAgreement.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/tech/CivicEffects.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

using aoc::PlayerId;

namespace {

constexpr PlayerId P0{0};
constexpr PlayerId P1{1};
constexpr aoc::UnitTypeId TRADER{30};
constexpr aoc::UnitTypeId WARRIOR{0};
constexpr aoc::UnitTypeId FRIGATE{55}; ///< Renaissance: upkeep 2, above the cut.

/// The first policy card carrying the given modifier, or -1.
template <typename Pick>
[[nodiscard]] int32_t policyWith(Pick pick) {
    for (uint8_t i = 0; i < aoc::sim::POLICY_CARD_COUNT; ++i) {
        if (pick(aoc::sim::policyCardDef(i).modifiers)) {
            return i;
        }
    }
    return -1;
}

/// Slot `policy` into the first slot of the player's government, whatever
/// the slot's type: computeGovernmentModifiers sums what is slotted.
void slot(aoc::game::Player& p, int32_t policy) {
    p.government().activePolicies[0] = static_cast<int8_t>(policy);
}

} // namespace

TEST_CASE("route slots add the government's, the civ's, the agreements' and the civics' extras") {
    aoc::test::World w   = aoc::test::makeWorld(2);
    aoc::game::Player& p = *w.gameState.player(P0);
    aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    const int32_t base = aoc::sim::computeTotalTradeSlots(p, w.grid);

    const int32_t routesPolicy = policyWith([](const aoc::sim::GovernmentModifiers& m) { return m.extraTradeRoutes > 0; });
    REQUIRE(routesPolicy >= 0);
    slot(p, routesPolicy);
    const int32_t extra = aoc::sim::policyCardDef(static_cast<uint8_t>(routesPolicy)).modifiers.extraTradeRoutes;
    CHECK(aoc::sim::computeTotalTradeSlots(p, w.grid) == base + extra);

    aoc::sim::TradeAgreementDef zone{};
    zone.type    = aoc::sim::TradeAgreementType::FreeTradeZone;
    zone.members = {P0, P1};
    p.tradeAgreements().agreements.push_back(zone);
    CHECK(aoc::sim::computeTotalTradeSlots(p, w.grid) == base + extra + 1);

    // A civic with an ExtraTradeRoute effect raises the standing count.
    int32_t civicId = -1;
    for (int32_t i = 0; i < aoc::sim::CIVIC_EFFECT_COUNT; ++i) {
        if (aoc::sim::CIVIC_EFFECTS[i].type == aoc::sim::CivicEffectType::ExtraTradeRoute) {
            civicId = aoc::sim::CIVIC_EFFECTS[i].civicId;
            break;
        }
    }
    REQUIRE(civicId >= 0);
    aoc::sim::applyCivicEffect(w.gameState, P0, static_cast<uint8_t>(civicId));
    CHECK(aoc::sim::computeTotalTradeSlots(p, w.grid) == base + extra + 2);

    // The civ's own trait counts too: the slots differ between a civ with it and one without.
    aoc::sim::CivId with    = aoc::sim::CivId{0};
    aoc::sim::CivId without = aoc::sim::CivId{0};
    bool foundWith = false, foundWithout = false;
    for (uint8_t i = 0; i < aoc::sim::CIV_COUNT && !(foundWith && foundWithout); ++i) {
        const aoc::sim::CivId id = static_cast<aoc::sim::CivId>(i);
        if (aoc::sim::civDef(id).modifiers.extraTradeRoutes > 0 && !foundWith) { with = id; foundWith = true; }
        if (aoc::sim::civDef(id).modifiers.extraTradeRoutes == 0 && !foundWithout) { without = id; foundWithout = true; }
    }
    REQUIRE(foundWith);
    REQUIRE(foundWithout);
    p.setCivId(without);
    const int32_t plain = aoc::sim::computeTotalTradeSlots(p, w.grid);
    p.setCivId(with);
    CHECK(aoc::sim::computeTotalTradeSlots(p, w.grid) == plain + aoc::sim::civDef(with).modifiers.extraTradeRoutes);
}

TEST_CASE("Caravansaries raise the collection reach per active route, like a civ's route ability") {
    aoc::test::World w   = aoc::test::makeWorld(1);
    aoc::game::Player& p = *w.gameState.player(P0);
    aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    p.monetary().system = aoc::sim::MonetarySystemType::CommodityMoney;
    for (const int32_t q : {6, 7}) {
        aoc::game::Unit& t = aoc::test::addUnitAt(w, P0, TRADER, q, 5);
        t.trader().owner   = P0;
    }
    REQUIRE(p.activeTradeRouteCount() == 2);
    const float before = aoc::sim::collectionEfficiency(p, w.grid);
    const int32_t bonusPolicy = policyWith([](const aoc::sim::GovernmentModifiers& m) { return m.tradeRouteBonus > 0.0f; });
    REQUIRE(bonusPolicy >= 0);
    slot(p, bonusPolicy);
    const aoc::sim::GovernmentModifiers gov = aoc::sim::computeGovernmentModifiers(p.government());
    const float expected = before / 1.0f * 1.0f; // the multiplier is the same policy set less the bonus
    (void)expected;
    CHECK(aoc::sim::collectionEfficiency(p, w.grid) ==
          doctest::Approx((before + 0.01f * gov.tradeRouteBonus * 2.0f * 1.0f) * 1.0f).epsilon(0.02));
}

/// One foreign delivery, start to finish: what customs the importer took.
[[nodiscard]] aoc::CurrencyAmount customsOnOneDelivery(bool mercantilism) {
    aoc::test::World w      = aoc::test::makeWorld(2, 40, 24);
    aoc::game::City& home   = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::game::City& abroad = aoc::test::addCityAt(w, P1, 11, 5, "Beta");
    abroad.setPopulation(20);
    home.stockpile().addGoods(aoc::sim::goods::WHEAT, 40);
    aoc::game::Player& seller      = *w.gameState.player(P0);
    aoc::game::Player& buyer       = *w.gameState.player(P1);
    seller.monetary().system       = aoc::sim::MonetarySystemType::CommodityMoney;
    buyer.monetary().system        = aoc::sim::MonetarySystemType::CommodityMoney;
    buyer.monetary().privateSpecie = 100000;
    if (mercantilism) {
        slot(buyer, policyWith([](const aoc::sim::GovernmentModifiers& m) {
                 return m.tariffEfficiency > 0.0f;
             }));
    }
    aoc::sim::Market market;
    market.initialize();
    aoc::sim::DiplomacyManager d;
    d.initialize(2);
    d.meetPlayers(P0, P1, 1);
    aoc::game::Unit& unit = aoc::test::addUnitAt(w, P0, TRADER, 5, 5);
    if (aoc::sim::establishTradeRoute(w.gameState, w.grid, market, &d, unit, abroad) !=
        aoc::ErrorCode::Ok) {
        return -1;
    }
    const aoc::CurrencyAmount before = buyer.treasury();
    for (int32_t turn = 0; turn < 20 && !unit.trader().isReturning; ++turn) {
        aoc::sim::processTradeRoutes(w.gameState, w.grid, market, &d);
    }
    return buyer.treasury() - before;
}

TEST_CASE("Mercantilism raises the customs the importer takes, where they are actually taken") {
    const aoc::CurrencyAmount plain      = customsOnOneDelivery(false);
    const aoc::CurrencyAmount mercantile = customsOnOneDelivery(true);
    REQUIRE(plain > 0); // the delivery happened at all
    CHECK(mercantile > plain);
}

TEST_CASE("customs come out of the purse into the importer's treasury, and a union waives them") {
    aoc::test::World w      = aoc::test::makeWorld(2, 40, 24);
    aoc::game::City& home   = aoc::test::addCityAt(w, P0, 5, 5, "Alpha");
    aoc::game::City& abroad = aoc::test::addCityAt(w, P1, 11, 5, "Beta");
    abroad.setPopulation(20);
    home.stockpile().addGoods(aoc::sim::goods::WHEAT, 40);
    aoc::game::Player& seller = *w.gameState.player(P0);
    aoc::game::Player& buyer  = *w.gameState.player(P1);
    seller.monetary().system  = aoc::sim::MonetarySystemType::CommodityMoney;
    buyer.monetary().system   = aoc::sim::MonetarySystemType::CommodityMoney;
    buyer.monetary().privateSpecie = 100000;
    aoc::sim::Market market;
    market.initialize();
    aoc::sim::DiplomacyManager d;
    d.initialize(2);
    d.meetPlayers(P0, P1, 1);
    CHECK(aoc::sim::importTariffRate(buyer, P0) == doctest::Approx(0.10f)); // the default customs

    aoc::game::Unit& unit = aoc::test::addUnitAt(w, P0, TRADER, 5, 5);
    const aoc::sim::TradeRouteEstimate preview =
        aoc::sim::estimateTradeRouteIncome(w.gameState, w.grid, market, unit, abroad, &d);
    REQUIRE(aoc::sim::establishTradeRoute(w.gameState, w.grid, market, &d, unit, abroad) == aoc::ErrorCode::Ok);
    const aoc::CurrencyAmount treasuryBefore = buyer.treasury();
    const int64_t worldBefore                = aoc::sim::worldMoney(w.gameState);
    for (int32_t turn = 0; turn < 20 && !unit.trader().isReturning; ++turn) {
        aoc::sim::processTradeRoutes(w.gameState, w.grid, market, &d);
    }
    REQUIRE(unit.trader().isReturning);
    const aoc::CurrencyAmount tariff = buyer.treasury() - treasuryBefore;
    CHECK(tariff > 0);
    CHECK(buyer.tariffsLastTurn() == tariff);
    CHECK(unit.trader().carriedGold == preview.estimatedGoldPerTrip); // the preview nets the customs
    CHECK(aoc::sim::worldMoney(w.gameState) == worldBefore);        // a transfer, nothing made
    CHECK(aoc::sim::computeEconomicBreakdown(buyer, w.grid).incomeTariffs == tariff);

    aoc::sim::TradeAgreementDef unionDef{};
    unionDef.type    = aoc::sim::TradeAgreementType::CustomsUnion;
    unionDef.members = {P0, P1};
    buyer.tradeAgreements().agreements.push_back(unionDef);
    CHECK(aoc::sim::importTariffRate(buyer, P0) == doctest::Approx(0.0f));
    buyer.tradeAgreements().agreements.clear();

    // The rate itself is clamped. The Mercantilism premium multiplies it at
    // the delivery rather than here, and has its own case above.
    buyer.tariffs().importTariffRate = 0.9f;
    CHECK(aoc::sim::importTariffRate(buyer, P0) == doctest::Approx(0.5f));
}

TEST_CASE("Conscription takes its flat cut off every paid unit, never below zero") {
    aoc::test::World w   = aoc::test::makeWorld(1);
    aoc::game::Player& p = *w.gameState.player(P0);
    // A Warrior costs 1 and a Frigate 2, so the cut of 1 leaves one of them
    // paying and floors the other: an implementation that wiped the bill
    // outright, or took a multiple of the cut, fails here.
    aoc::game::Unit& cheap = aoc::test::addUnitAt(w, P0, WARRIOR, 5, 5);
    aoc::game::Unit& dear  = aoc::test::addUnitAt(w, P0, FRIGATE, 6, 5);
    const int32_t cheapBase = aoc::sim::unitTypeDef(WARRIOR).maintenanceGold();
    const int32_t dearBase  = aoc::sim::unitTypeDef(FRIGATE).maintenanceGold();
    REQUIRE(dearBase > cheapBase);
    CHECK(aoc::sim::unitUpkeep(p, cheap) == cheapBase);
    CHECK(aoc::sim::unitUpkeep(p, dear) == dearBase);

    const int32_t conscription =
        policyWith([](const aoc::sim::GovernmentModifiers& m) { return m.unitMaintenanceReduction > 0.0f; });
    REQUIRE(conscription >= 0);
    slot(p, conscription);
    const int32_t cut = static_cast<int32_t>(aoc::sim::computeGovernmentModifiers(p.government()).unitMaintenanceReduction);
    REQUIRE(cut == cheapBase); // the cut exactly floors the cheap one
    CHECK(aoc::sim::unitUpkeep(p, cheap) == 0);
    CHECK(aoc::sim::unitUpkeep(p, dear) == dearBase - cut);
    CHECK(aoc::sim::unitUpkeep(p, dear) > 0);

    // And the bill the treasury pays follows.
    p.monetary().system = aoc::sim::MonetarySystemType::CommodityMoney;
    p.setTreasury(1000, aoc::sim::MoneyFlow::external());
    aoc::sim::processUnitMaintenance(w.gameState, w.grid, p);
    CHECK(p.treasury() == 1000 - (dearBase - cut));
}

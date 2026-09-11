/**
 * @file test_world_market.cpp
 * @brief The world market view (plan B4, step 1.4): for every good some met
 *        civ holds or needs, who holds how much and who has an unmet need.
 *        The deal composer and GET /game/market read the same rows.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/simulation/diplomacy/DealProposals.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <algorithm>
#include <vector>

using aoc::PlayerId;
using aoc::sim::WorldMarketRow;
using aoc::sim::goods::COPPER_COINS;
using aoc::sim::goods::IRON_ORE;
using aoc::sim::goods::SILK;

namespace {

constexpr PlayerId P0{0};
constexpr PlayerId P1{1};
constexpr PlayerId P2{2};

/// P0 has met P1 only. P1 and P2 each hold ten silk; P1 also holds coins and
/// needs iron; P0 needs silk.
struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(3);
    aoc::sim::DiplomacyManager d;

    Fixture() {
        this->d.initialize(3);
        this->d.meetPlayers(P0, P1, 1);
        aoc::test::addCityAt(this->world, P0, 4, 4, "Alpha");
        aoc::game::City& beta  = aoc::test::addCityAt(this->world, P1, 14, 8, "Beta");
        aoc::game::City& gamma = aoc::test::addCityAt(this->world, P2, 20, 12, "Gamma");
        beta.stockpile().addGoods(SILK, 10);
        beta.stockpile().addGoods(COPPER_COINS, 3);
        gamma.stockpile().addGoods(SILK, 10);
        this->world.gameState.player(P1)->economy().totalNeeds[IRON_ORE] = 4;
        this->world.gameState.player(P0)->economy().totalNeeds[SILK]     = 2;
    }

    [[nodiscard]] const WorldMarketRow* row(const std::vector<WorldMarketRow>& rows,
                                            uint16_t good) const {
        const auto it = std::find_if(rows.begin(), rows.end(),
                                     [good](const WorldMarketRow& r) { return r.goodId == good; });
        return it == rows.end() ? nullptr : &*it;
    }
};

} // namespace

TEST_CASE("a viewer sees the holders and seekers among the civs it has met, coins left out") {
    Fixture f;
    const std::vector<WorldMarketRow> rows = aoc::sim::worldMarketRows(f.world.gameState, &f.d, P0);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].goodId < rows[1].goodId);

    const WorldMarketRow* silk = f.row(rows, SILK);
    REQUIRE(silk != nullptr);
    REQUIRE(silk->holders.size() == 1); // P2's silk is unknown to P0
    CHECK(silk->holders[0].first == P1);
    CHECK(silk->holders[0].second == 10);
    CHECK(silk->seekers == std::vector<PlayerId>{P0});

    const WorldMarketRow* iron = f.row(rows, IRON_ORE);
    REQUIRE(iron != nullptr);
    CHECK(iron->holders.empty());
    CHECK(iron->seekers == std::vector<PlayerId>{P1});

    CHECK(f.row(rows, COPPER_COINS) == nullptr);
}

TEST_CASE("a civ that has met nobody sees only itself; a null diplomacy sees everyone") {
    Fixture f;
    const std::vector<WorldMarketRow> alone = aoc::sim::worldMarketRows(f.world.gameState, &f.d, P2);
    REQUIRE(alone.size() == 1);
    CHECK(alone[0].goodId == SILK);
    REQUIRE(alone[0].holders.size() == 1);
    CHECK(alone[0].holders[0].first == P2);
    CHECK(alone[0].seekers.empty()); // P0's need is out of sight

    const std::vector<WorldMarketRow> all = aoc::sim::worldMarketRows(f.world.gameState, nullptr, P0);
    const WorldMarketRow* silk             = f.row(all, SILK);
    REQUIRE(silk != nullptr);
    REQUIRE(silk->holders.size() == 2);
    CHECK(silk->holders[0].first == P1); // ascending by player
    CHECK(silk->holders[1].first == P2);
}

TEST_CASE("stock is summed across a civ's cities and a satisfied need is no need") {
    Fixture f;
    aoc::game::City& delta = aoc::test::addCityAt(f.world, P1, 18, 6, "Delta");
    delta.stockpile().addGoods(SILK, 5);
    f.world.gameState.player(P1)->economy().totalNeeds[IRON_ORE] = 0;
    const std::vector<WorldMarketRow> rows = aoc::sim::worldMarketRows(f.world.gameState, &f.d, P0);
    const WorldMarketRow* silk             = f.row(rows, SILK);
    REQUIRE(silk != nullptr);
    REQUIRE(silk->holders.size() == 1);
    CHECK(silk->holders[0].second == 15);
    CHECK(f.row(rows, IRON_ORE) == nullptr);
}

TEST_CASE("deal terms name the good, not its id") {
    Fixture f;
    aoc::sim::DealTerm shipment{};
    shipment.type       = aoc::sim::DealTermType::GoodsExchange;
    shipment.fromPlayer = P1;
    shipment.toPlayer   = P0;
    shipment.goodId     = SILK;
    shipment.goodAmount = 5;
    CHECK(aoc::sim::describeDealTerm(f.world.gameState, shipment).find("5 Silk (") == 0);
    shipment.type = aoc::sim::DealTermType::ExclusiveAccess;
    CHECK(aoc::sim::describeDealTerm(f.world.gameState, shipment).find("Exclusive access to Silk (") == 0);
    shipment.type        = aoc::sim::DealTermType::SupplyContract;
    shipment.duration    = 30;
    shipment.goldPerTurn = 7;
    CHECK(aoc::sim::describeDealTerm(f.world.gameState, shipment)
              .find("5 Silk per turn for 30 turns at 7 gold per turn (") == 0);
}

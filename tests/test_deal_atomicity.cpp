/**
 * @file test_deal_atomicity.cpp
 * @brief Pins acceptDeal (aoc::sim, DealTerms.hpp): atomicity + cession effects.
 *
 * A diplomatic deal used to apply its terms one at a time with no up-front
 * validation: a {CedeCity, GoldLump} deal transferred the city first, then
 * silently skipped the gold payment when the buyer was broke -- handing the
 * city away for free. These cases pin the fix: acceptDeal validates every
 * asset-transferring term first and applies nothing unless the whole deal
 * can execute, so a failed term leaves BOTH portfolios byte-unchanged. They
 * also pin that a ceded city carries its grid footprint (center + 6 ring) to
 * the buyer, without seizing tiles a third party owns.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/core/ErrorCodes.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/diplomacy/DealTerms.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace {

/// Two players, a small grid, and player 0 owning a capital plus a tradeable
/// city. The capital is added first so cities().front() is NOT the sold city:
/// a CedeCity apply that ignored tileCoord and just flipped front() would move
/// the wrong city and be caught.
struct DealWorld {
    aoc::game::GameState gameState;
    aoc::map::HexGrid grid;
    aoc::hex::AxialCoord capitalLoc{6, 6};
    aoc::hex::AxialCoord cityLoc{3, 3};
    aoc::game::City* capital = nullptr;
    aoc::game::City* soldCity = nullptr;

    explicit DealWorld(int32_t playerCount = 2) {
        gameState.initialize(playerCount);
        grid.initialize(12, 12);
        // City objects live on the heap (unique_ptr), so these pointers stay
        // valid across later addCity() vector reallocations.
        capital = &seller().addCity(capitalLoc, "Capital");
        soldCity = &seller().addCity(cityLoc, "Testburg");
    }

    aoc::game::Player& seller() { return *this->gameState.players()[0]; }
    aoc::game::Player& buyer() { return *this->gameState.players()[1]; }

    /// Give the seller a tile bordering a buyer-owned tile and return the
    /// seller tile's coord -- the one a CedeTile term will transfer.
    aoc::hex::AxialCoord setupBorderTile() {
        const aoc::hex::AxialCoord tile{5, 5};
        const int32_t tileIdx = this->grid.toIndex(tile);
        REQUIRE(tileIdx >= 0);
        this->grid.setOwner(tileIdx, this->seller().id());
        for (const aoc::hex::AxialCoord& n : aoc::hex::neighbors(tile)) {
            const int32_t nIdx = this->grid.toIndex(n);
            if (nIdx >= 0) {
                this->grid.setOwner(nIdx, this->buyer().id());
                break;
            }
        }
        return tile;
    }

    /// Build the AI city-sale deal shape: cede the city, then collect gold.
    aoc::sim::DiplomaticDeal citySaleDeal(int32_t price) {
        return twoTermDeal(aoc::sim::DealTermType::CedeCity, this->cityLoc, price);
    }

    /// Build the tile-sale deal shape: cede the tile, then collect gold.
    aoc::sim::DiplomaticDeal tileSaleDeal(aoc::hex::AxialCoord tile, int32_t price) {
        return twoTermDeal(aoc::sim::DealTermType::CedeTile, tile, price);
    }

private:
    /// A cession (city or tile) from seller to buyer, paired with a buyer->
    /// seller gold lump -- the exact ordering the AI builds (asset first).
    aoc::sim::DiplomaticDeal twoTermDeal(aoc::sim::DealTermType cedeType,
                                         aoc::hex::AxialCoord coord, int32_t price) {
        aoc::sim::DiplomaticDeal deal;
        deal.playerA = this->seller().id();
        deal.playerB = this->buyer().id();

        aoc::sim::DealTerm cede;
        cede.type = cedeType;
        cede.fromPlayer = this->seller().id();
        cede.toPlayer = this->buyer().id();
        cede.tileCoord = coord;
        deal.terms.push_back(cede);

        aoc::sim::DealTerm pay;
        pay.type = aoc::sim::DealTermType::GoldLump;
        pay.fromPlayer = this->buyer().id();
        pay.toPlayer = this->seller().id();
        pay.goldLump = price;
        deal.terms.push_back(pay);

        return deal;
    }
};

} // namespace

TEST_CASE("acceptDeal rejects an unpayable deal and leaves both portfolios unchanged") {
    DealWorld w;
    w.seller().setTreasury(500);
    w.buyer().setTreasury(50);  // cannot afford the 300 gold term

    aoc::sim::GlobalDealTracker tracker;
    REQUIRE(aoc::sim::proposeDeal(w.gameState, tracker, w.citySaleDeal(300))
            == aoc::ErrorCode::Ok);

    const aoc::ErrorCode rc = aoc::sim::acceptDeal(w.gameState, w.grid, tracker, 0);

    CHECK(rc == aoc::ErrorCode::InsufficientResources);
    // City not transferred.
    CHECK(w.soldCity->owner() == w.seller().id());
    CHECK(w.capital->owner() == w.seller().id());
    // Treasuries untouched.
    CHECK(w.seller().treasury() == 500);
    CHECK(w.buyer().treasury() == 50);
    // Rejected proposal rolled back out of the tracker.
    CHECK(tracker.activeDeals.empty());
}

TEST_CASE("acceptDeal applies every term when the deal is payable") {
    DealWorld w;
    w.seller().setTreasury(500);
    w.buyer().setTreasury(1000);

    aoc::sim::GlobalDealTracker tracker;
    REQUIRE(aoc::sim::proposeDeal(w.gameState, tracker, w.citySaleDeal(300))
            == aoc::ErrorCode::Ok);

    const aoc::ErrorCode rc = aoc::sim::acceptDeal(w.gameState, w.grid, tracker, 0);

    CHECK(rc == aoc::ErrorCode::Ok);
    // The city named by tileCoord flipped; the capital did NOT.
    CHECK(w.soldCity->owner() == w.buyer().id());
    CHECK(w.capital->owner() == w.seller().id());
    // Gold moved exactly once.
    CHECK(w.seller().treasury() == 800);
    CHECK(w.buyer().treasury() == 700);
    REQUIRE(tracker.activeDeals.size() == 1);
    CHECK(tracker.activeDeals.at(0).isAccepted == true);
}

TEST_CASE("acceptDeal moves a ceded city's footprint but never a third party's land") {
    DealWorld w(3);  // seller=0, buyer=1, third party=2
    w.seller().setTreasury(0);
    w.buyer().setTreasury(1000);
    const aoc::PlayerId thirdId = w.gameState.players()[2]->id();

    // Lay out the sold city's footprint on the grid.
    const int32_t centerIdx = w.grid.toIndex(w.cityLoc);
    REQUIRE(centerIdx >= 0);
    w.grid.setOwner(centerIdx, w.seller().id());

    std::vector<int32_t> ring;
    for (const aoc::hex::AxialCoord& n : aoc::hex::neighbors(w.cityLoc)) {
        const int32_t idx = w.grid.toIndex(n);
        if (idx >= 0) { ring.push_back(idx); }
    }
    REQUIRE(ring.size() >= 3);
    w.grid.setOwner(ring[0], w.seller().id());  // seller-owned -> should flip
    w.grid.setOwner(ring[1], thirdId);          // third party  -> must NOT flip
    // ring[2] left unowned (INVALID_PLAYER)    -> should be claimed
    for (std::size_t i = 3; i < ring.size(); ++i) {
        w.grid.setOwner(ring[i], w.seller().id());
    }

    aoc::sim::GlobalDealTracker tracker;
    REQUIRE(aoc::sim::proposeDeal(w.gameState, tracker, w.citySaleDeal(300))
            == aoc::ErrorCode::Ok);

    const aoc::ErrorCode rc = aoc::sim::acceptDeal(w.gameState, w.grid, tracker, 0);

    CHECK(rc == aoc::ErrorCode::Ok);
    CHECK(w.soldCity->owner() == w.buyer().id());
    CHECK(w.grid.owner(centerIdx) == w.buyer().id());   // center flipped
    CHECK(w.grid.owner(ring[0]) == w.buyer().id());     // seller tile flipped
    CHECK(w.grid.owner(ring[1]) == thirdId);            // third party untouched
    CHECK(w.grid.owner(ring[2]) == w.buyer().id());     // unowned tile claimed
    for (std::size_t i = 3; i < ring.size(); ++i) {
        CHECK(w.grid.owner(ring[i]) == w.buyer().id());
    }
}

TEST_CASE("acceptDeal cedes a map-edge city without touching off-map tiles") {
    DealWorld w;
    w.seller().setTreasury(0);
    w.buyer().setTreasury(1000);

    // A corner city: some hex-ring neighbours are off-map. toIndex() on those
    // would abort (debug assert) or alias a wrapped tile (release), so the
    // footprint reown must isValid-guard first. This case aborts under the
    // debug/ASan preset with an unguarded reown.
    const aoc::hex::AxialCoord edgeLoc{0, 0};
    REQUIRE(w.grid.isValid(edgeLoc));
    aoc::game::City& edgeCity = w.seller().addCity(edgeLoc, "Edgeton");
    const int32_t centerIdx = w.grid.toIndex(edgeLoc);
    w.grid.setOwner(centerIdx, w.seller().id());

    aoc::sim::DiplomaticDeal deal;
    deal.playerA = w.seller().id();
    deal.playerB = w.buyer().id();
    aoc::sim::DealTerm cede;
    cede.type = aoc::sim::DealTermType::CedeCity;
    cede.fromPlayer = w.seller().id();
    cede.toPlayer = w.buyer().id();
    cede.tileCoord = edgeLoc;
    deal.terms.push_back(cede);
    aoc::sim::DealTerm pay;
    pay.type = aoc::sim::DealTermType::GoldLump;
    pay.fromPlayer = w.buyer().id();
    pay.toPlayer = w.seller().id();
    pay.goldLump = 100;
    deal.terms.push_back(pay);

    aoc::sim::GlobalDealTracker tracker;
    REQUIRE(aoc::sim::proposeDeal(w.gameState, tracker, deal) == aoc::ErrorCode::Ok);

    const aoc::ErrorCode rc = aoc::sim::acceptDeal(w.gameState, w.grid, tracker, 0);

    CHECK(rc == aoc::ErrorCode::Ok);
    CHECK(edgeCity.owner() == w.buyer().id());
    CHECK(w.grid.owner(centerIdx) == w.buyer().id());

    // Every buyer-owned tile must lie within the edge city's real, in-bounds
    // footprint -- no off-map neighbour may alias a distant tile to the buyer.
    std::vector<int32_t> footprint{centerIdx};
    for (const aoc::hex::AxialCoord& n : aoc::hex::neighbors(edgeLoc)) {
        if (w.grid.isValid(n)) { footprint.push_back(w.grid.toIndex(n)); }
    }
    for (int32_t i = 0; i < w.grid.tileCount(); ++i) {
        if (w.grid.owner(i) == w.buyer().id()) {
            CHECK(std::find(footprint.begin(), footprint.end(), i) != footprint.end());
        }
    }
}

TEST_CASE("acceptDeal rejects when cumulative lump payments overdraw one payer") {
    DealWorld w;
    w.seller().setTreasury(0);
    w.buyer().setTreasury(400);  // each 300 lump fits alone; 600 together does not

    aoc::sim::DiplomaticDeal deal;
    deal.playerA = w.seller().id();
    deal.playerB = w.buyer().id();
    for (int32_t i = 0; i < 2; ++i) {
        aoc::sim::DealTerm pay;
        pay.type = aoc::sim::DealTermType::GoldLump;
        pay.fromPlayer = w.buyer().id();
        pay.toPlayer = w.seller().id();
        pay.goldLump = 300;
        deal.terms.push_back(pay);
    }

    aoc::sim::GlobalDealTracker tracker;
    REQUIRE(aoc::sim::proposeDeal(w.gameState, tracker, deal) == aoc::ErrorCode::Ok);

    const aoc::ErrorCode rc = aoc::sim::acceptDeal(w.gameState, w.grid, tracker, 0);

    CHECK(rc == aoc::ErrorCode::InsufficientResources);
    CHECK(w.buyer().treasury() == 400);
    CHECK(w.seller().treasury() == 0);
    CHECK(tracker.activeDeals.empty());
}

TEST_CASE("acceptDeal keeps a tile cession atomic when the buyer cannot pay") {
    DealWorld w;
    const aoc::hex::AxialCoord tile = w.setupBorderTile();
    const int32_t tileIdx = w.grid.toIndex(tile);
    w.seller().setTreasury(0);
    w.buyer().setTreasury(50);  // cannot afford the 100 gold term

    aoc::sim::GlobalDealTracker tracker;
    REQUIRE(aoc::sim::proposeDeal(w.gameState, tracker, w.tileSaleDeal(tile, 100))
            == aoc::ErrorCode::Ok);

    const aoc::ErrorCode rc = aoc::sim::acceptDeal(w.gameState, w.grid, tracker, 0);

    CHECK(rc == aoc::ErrorCode::InsufficientResources);
    // Tile not transferred.
    CHECK(w.grid.owner(tileIdx) == w.seller().id());
    CHECK(w.buyer().treasury() == 50);
    CHECK(w.seller().treasury() == 0);
    CHECK(tracker.activeDeals.empty());
}

TEST_CASE("acceptDeal transfers tile and gold when a tile cession is payable") {
    DealWorld w;
    const aoc::hex::AxialCoord tile = w.setupBorderTile();
    const int32_t tileIdx = w.grid.toIndex(tile);
    w.seller().setTreasury(0);
    w.buyer().setTreasury(500);

    aoc::sim::GlobalDealTracker tracker;
    REQUIRE(aoc::sim::proposeDeal(w.gameState, tracker, w.tileSaleDeal(tile, 100))
            == aoc::ErrorCode::Ok);

    const aoc::ErrorCode rc = aoc::sim::acceptDeal(w.gameState, w.grid, tracker, 0);

    CHECK(rc == aoc::ErrorCode::Ok);
    CHECK(w.grid.owner(tileIdx) == w.buyer().id());
    CHECK(w.seller().treasury() == 100);
    CHECK(w.buyer().treasury() == 400);
}

TEST_CASE("acceptDeal rejects a whole deal when a CedeCity term names no city") {
    DealWorld w;
    w.seller().setTreasury(0);
    w.buyer().setTreasury(1000);  // the gold term alone would be payable

    aoc::sim::DiplomaticDeal deal;
    deal.playerA = w.seller().id();
    deal.playerB = w.buyer().id();
    aoc::sim::DealTerm cede;
    cede.type = aoc::sim::DealTermType::CedeCity;
    cede.fromPlayer = w.seller().id();
    cede.toPlayer = w.buyer().id();
    cede.tileCoord = aoc::hex::AxialCoord{9, 9};  // no city of the seller here
    deal.terms.push_back(cede);
    aoc::sim::DealTerm pay;
    pay.type = aoc::sim::DealTermType::GoldLump;
    pay.fromPlayer = w.buyer().id();
    pay.toPlayer = w.seller().id();
    pay.goldLump = 100;
    deal.terms.push_back(pay);

    aoc::sim::GlobalDealTracker tracker;
    REQUIRE(aoc::sim::proposeDeal(w.gameState, tracker, deal) == aoc::ErrorCode::Ok);

    const aoc::ErrorCode rc = aoc::sim::acceptDeal(w.gameState, w.grid, tracker, 0);

    // A bogus asset term rejects the whole deal -- the payable gold term must
    // NOT apply on its own.
    CHECK(rc == aoc::ErrorCode::InvalidState);
    CHECK(w.buyer().treasury() == 1000);
    CHECK(w.seller().treasury() == 0);
    CHECK(w.capital->owner() == w.seller().id());
    CHECK(w.soldCity->owner() == w.seller().id());
    CHECK(tracker.activeDeals.empty());
}

TEST_CASE("acceptDeal still rejects an out-of-range index") {
    DealWorld w;
    aoc::sim::GlobalDealTracker tracker;
    CHECK(aoc::sim::acceptDeal(w.gameState, w.grid, tracker, 0)
          == aoc::ErrorCode::InvalidArgument);
    CHECK(aoc::sim::acceptDeal(w.gameState, w.grid, tracker, -1)
          == aoc::ErrorCode::InvalidArgument);
}

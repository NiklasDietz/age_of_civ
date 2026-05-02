/**
 * @file TradeRouteSystem.cpp
 * @brief Physical trade routes with Trader units carrying real goods.
 */

#include "aoc/simulation/economy/TradeRouteSystem.hpp"

#include "aoc/balance/BalanceParams.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/economy/AdvancedEconomics.hpp"
#include "aoc/simulation/economy/TradeAgreement.hpp"
#include "aoc/simulation/automation/Automation.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Pathfinding.hpp"
#include "aoc/ui/GameNotifications.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace aoc::sim {

namespace {

/// Select goods for trade, prioritizing what the destination needs most.
/// Score: surplus * max(1, destDeficit) * marketPrice.
/// This ensures traders carry high-value goods the destination actually wants.
void selectTradeGoods(const CityStockpileComponent& originStock,
                       const CityStockpileComponent* destStock,
                       const Market& market,
                       std::vector<TradeCargo>& outCargo,
                       int32_t maxGoods) {
    outCargo.clear();

    struct ScoredGood {
        uint16_t goodId;
        int32_t  surplus;
        float    score;
    };
    std::vector<ScoredGood> candidates;
    candidates.reserve(originStock.goods.size() + originStock.exportBuffer.size());

    // WP-O: scan buffer + stockpile combined. Buffer entries are "ready
    // for export" so they get scored first, but the loader pulls from
    // both transparently. Avoid double-counting by walking buffer first
    // and tracking which goodIds have been seen.
    auto scoreCandidate = [&](uint16_t gid, int32_t total) {
        if (total <= 1) { return; }
        int32_t surplus = total - 1;
        int32_t destDeficit = 1;
        if (destStock != nullptr) {
            int32_t destAmount = destStock->getAmount(gid);
            destDeficit = std::max(1, 5 - destAmount);
        }
        int32_t price = market.marketData(gid).currentPrice;
        if (price <= 0) { price = 1; }
        float score = static_cast<float>(surplus)
                    * static_cast<float>(destDeficit)
                    * static_cast<float>(price);
        candidates.push_back({gid, surplus, score});
    };
    std::unordered_map<uint16_t, int32_t> combined;
    for (const auto& entry : originStock.goods) {
        combined[entry.first] += entry.second;
    }
    for (const auto& entry : originStock.exportBuffer) {
        combined[entry.first] += entry.second;
    }
    for (const auto& entry : combined) {
        scoreCandidate(entry.first, entry.second);
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const ScoredGood& a, const ScoredGood& b) { return a.score > b.score; });

    int32_t count = 0;
    for (const ScoredGood& c : candidates) {
        if (count >= maxGoods) { break; }

        // Volume scales with surplus and destination demand: more demand
        // → larger shipment. Higher-tier goods (rank by index) get
        // diminishing volume since they're heavier per unit. Cap 12.
        int32_t transfer = std::max(1, c.surplus / 2);
        if (destStock != nullptr) {
            const int32_t destAmount = destStock->getAmount(c.goodId);
            if (destAmount < 3) {
                transfer = std::min(c.surplus, transfer * 2);  // urgent demand: double load
            } else if (destAmount > 10) {
                transfer = std::max(1, transfer / 2);           // saturated: half load
            }
        }
        // Hard cap: traders can carry 12 units max per cargo entry.
        transfer = std::min(transfer, 12);

        TradeCargo cargo;
        cargo.goodId = c.goodId;
        cargo.amount = transfer;
        outCargo.push_back(cargo);
        ++count;
    }
}

/// WP-R + WP-T: returns the fuel good + per-tile rate for a route. Wagon
/// Land returns goodId 0 (free). Land rail uses COAL (steam). Sea
/// pre-Refining uses COAL, post-Refining FUEL. Air uses FUEL post-Aviation.
/// WP-T: Land rail with Electricity tech + power-pole coverage on path
/// switches to electric mode (free — drains power grid implicitly).
struct FuelSpec {
    uint16_t goodId = 0;
    float    perTile = 0.0f;
};
FuelSpec routeFuelSpec(const TraderComponent& trader,
                        const aoc::map::HexGrid& grid,
                        const aoc::game::Player& owner) {
    FuelSpec spec;
    const bool hasRefining = owner.tech().hasResearched(TechId{12});
    const bool hasElectricity = owner.tech().hasResearched(TechId{14});
    switch (trader.routeType) {
        case TradeRouteType::Air:
            // WP-Q: 2.0 starved Air routes 100% (audit: 0 air after WP-R).
            // 1.0/tile keeps Air rare (still needs Aviation + Airport gates)
            // but viable for late-game premium cargo.
            spec.goodId  = goods::FUEL;
            spec.perTile = 1.0f;
            return spec;
        case TradeRouteType::Sea:
            spec.goodId  = hasRefining ? goods::FUEL : goods::COAL;
            spec.perTile = 0.5f;
            return spec;
        case TradeRouteType::Land:
        default: {
            // Rail-majority path = train tier. Else wagon (free).
            int32_t railCount = 0;
            int32_t poleCount = 0;
            int32_t total = 0;
            for (const aoc::hex::AxialCoord& tile : trader.path) {
                if (!grid.isValid(tile)) { continue; }
                const int32_t tIdx = grid.toIndex(tile);
                const aoc::map::ImprovementType imp = grid.improvement(tIdx);
                if (imp == aoc::map::ImprovementType::Railway
                 || imp == aoc::map::ImprovementType::Highway) {
                    ++railCount;
                }
                if (grid.hasPowerPole(tIdx)) { ++poleCount; }
                ++total;
            }
            if (total > 0
             && static_cast<float>(railCount) / static_cast<float>(total) >= 0.5f) {
                // Train tier. Default coal (steam).
                spec.goodId  = goods::COAL;
                spec.perTile = 1.0f;
                // WP-T: electric trains. Path needs Electricity tech + power-
                // pole coverage on at least 50% of path tiles. Renewables-
                // backed grid is "free" stockpile-wise (real cost = building
                // power plants + maintenance, not consuming coal/oil).
                if (hasElectricity
                 && static_cast<float>(poleCount) / static_cast<float>(total) >= 0.5f) {
                    spec.goodId  = 0;
                    spec.perTile = 0.0f;
                }
            }
            return spec;
        }
    }
}

/// WP-R: drain `requested` fuel of `goodId` from `seller` aggregated stockpiles.
/// Returns units actually drained.
int32_t drainFuel(aoc::game::Player& owner, uint16_t goodId, int32_t requested) {
    if (requested <= 0 || goodId == 0) { return 0; }
    int32_t got = 0;
    for (const std::unique_ptr<aoc::game::City>& city : owner.cities()) {
        if (got >= requested) { break; }
        CityStockpileComponent& sp = city->stockpile();
        const int32_t avail = sp.getAmount(goodId);
        if (avail <= 0) { continue; }
        const int32_t take = std::min(avail, requested - got);
        if (sp.consumeGoods(goodId, take)) {
            got += take;
        }
    }
    return got;
}

/// WP-K3 v2: returns true if the trader's path is at least 50% railway/highway
/// (rail tier) so the trader can haul bulk cargo. Otherwise wagon tier.
bool pathOnRail(const TraderComponent& trader, const aoc::map::HexGrid& grid) {
    if (trader.routeType != TradeRouteType::Land) { return false; }
    if (trader.path.empty()) { return false; }
    int32_t railCount = 0;
    int32_t total = 0;
    for (const aoc::hex::AxialCoord& tile : trader.path) {
        if (!grid.isValid(tile)) { continue; }
        const aoc::map::ImprovementType imp = grid.improvement(grid.toIndex(tile));
        if (imp == aoc::map::ImprovementType::Railway
         || imp == aoc::map::ImprovementType::Highway) {
            ++railCount;
        }
        ++total;
    }
    if (total == 0) { return false; }
    return static_cast<float>(railCount) / static_cast<float>(total) >= 0.5f;
}

/// WP-O: predict and reserve goods at `seller` city for an upcoming pickup.
/// Moves planned amounts from `goods` to `exportBuffer` (frees stockpile during
/// transit). Stores the planned cargo on the trader so death can release it.
void commitPickupReservation(aoc::game::City& seller,
                              const Market& market,
                              TraderComponent& trader,
                              const aoc::map::HexGrid& grid) {
    CityStockpileComponent& sellerStock = seller.stockpile();
    const bool rail = pathOnRail(trader, grid);
    const int32_t slots = trader.maxCargoSlots(rail);

    std::vector<TradeCargo> planned;
    selectTradeGoods(sellerStock, /*destStock*/ nullptr, market, planned, slots);

    trader.pendingPickupCargo.clear();
    trader.pendingPickupCargo.reserve(planned.size());
    for (const TradeCargo& c : planned) {
        if (c.amount <= 0) { continue; }
        sellerStock.commitToExport(c.goodId, c.amount);
        trader.pendingPickupCargo.push_back(c);
    }
    trader.pickupCityLocation = seller.location();
}

/// Find a city by its location across all players.
static aoc::game::City* findCityByLocation(aoc::game::GameState& gameState,
                                            aoc::hex::AxialCoord location) {
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        aoc::game::City* c = p->cityAt(location);
        if (c != nullptr) { return c; }
    }
    return nullptr;
}

/// WP-O: trader died/expired before picking up. Roll back the seller's
/// reservation: pull units from the buffer (capped by what's still there)
/// and restore to stockpile (capped by stockpileSoftCap). Excess is lost
/// (modeled as goods that already aged out of the buffer).
void releasePickupReservation(aoc::game::GameState& gameState,
                                TraderComponent& trader) {
    if (trader.pendingPickupCargo.empty()) { return; }
    aoc::game::City* seller = findCityByLocation(gameState, trader.pickupCityLocation);
    if (seller == nullptr) {
        trader.pendingPickupCargo.clear();
        return;
    }
    CityStockpileComponent& sp = seller->stockpile();
    const int32_t cap = aoc::balance::params().stockpileSoftCap;
    for (const TradeCargo& c : trader.pendingPickupCargo) {
        std::unordered_map<uint16_t, int32_t>::iterator bufIt =
            sp.exportBuffer.find(c.goodId);
        if (bufIt == sp.exportBuffer.end()) { continue; }
        int32_t fromBuf = std::min(c.amount, bufIt->second);
        if (fromBuf <= 0) { continue; }
        bufIt->second -= fromBuf;
        if (bufIt->second <= 0) { sp.exportBuffer.erase(bufIt); }
        const int32_t cur = sp.getAmount(c.goodId);
        const int32_t free = std::max(0, cap - cur);
        const int32_t restore = std::min(fromBuf, free);
        if (restore > 0) { sp.addGoods(c.goodId, restore); }
    }
    trader.pendingPickupCargo.clear();
}

/// Find a Trader unit by EntityId across all players.
/// EntityId.index is the unit's sequence number in the global unit list.
aoc::game::Unit* findTraderByEntityId(aoc::game::GameState& gameState, EntityId id) {
    if (!id.isValid()) { return nullptr; }
    uint32_t remaining = id.index;
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        for (const std::unique_ptr<aoc::game::Unit>& u : p->units()) {
            if (remaining == 0) { return u.get(); }
            --remaining;
        }
    }
    return nullptr;
}

/// Evaluate whether the destination player would accept a trade route from the proposer.
/// AI decision based on: gold need, resource benefit, relations, war/embargo status.
bool evaluateTradeConsent(const aoc::game::GameState& gameState,
                           const Market& market,
                           const DiplomacyManager* diplomacy,
                           PlayerId proposer, PlayerId target) {
    // Block trade during war or embargo
    if (diplomacy != nullptr) {
        if (diplomacy->isAtWar(proposer, target)) {
            return false;
        }
        if (diplomacy->hasEmbargo(proposer, target)) {
            return false;
        }
    }

    // 2026-05-02: Civ6-style consent — peace is sufficient. Earlier scoring
    // formula (baseline 40 + resource match + treasury + relations*0.5)
    // produced 19k rejections per 36-sim audit because post-war negative
    // relations swung the score below zero. Trade is supposed to be the
    // mechanism that REPAIRS relations, not a luxury gated on already-good
    // ones. Suppress the diplomacy 'unused' warning.
    (void)gameState; (void)market; (void)proposer; (void)target;
    return true;
}

/// Compute total market value of cargo currently carried by a trader.
CurrencyAmount computeCargoValue(const std::vector<TradeCargo>& cargo, const Market& market) {
    CurrencyAmount total = 0;
    for (const TradeCargo& c : cargo) {
        int32_t price = market.marketData(c.goodId).currentPrice;
        if (price <= 0) { price = 1; }
        total += static_cast<CurrencyAmount>(c.amount) * static_cast<CurrencyAmount>(price);
    }
    return total;
}

/// WP-K1: civ-wide trade slot pool. Sources:
///   - monetary tier baseline (`monetary.maxTradeRoutes()`)
///   - +1 per Market (BuildingId 6) anywhere in civ
///   - +1 per Bank (20)
///   - +2 per Stock Exchange (21)
///   - +1 per Trading Post improvement on any owned tile
///   - +greatPeople.extraTradeSlots (Merchant GP)
int32_t computeTotalTradeSlots(const aoc::game::Player& player,
                                const aoc::map::HexGrid& grid) {
    int32_t total = player.monetary().maxTradeRoutes()
                  + player.greatPeople().extraTradeSlots;
    for (const std::unique_ptr<aoc::game::City>& cityPtr : player.cities()) {
        if (cityPtr == nullptr) { continue; }
        for (const CityDistrictsComponent::PlacedDistrict& d
                : cityPtr->districts().districts) {
            for (BuildingId bid : d.buildings) {
                if (bid.value == 6)  { total += 1; }   // Market
                else if (bid.value == 20) { total += 1; } // Bank
                else if (bid.value == 21) { total += 2; } // Stock Exchange
            }
        }
    }
    const int32_t tiles = grid.tileCount();
    for (int32_t i = 0; i < tiles; ++i) {
        if (grid.owner(i) != player.id()) { continue; }
        if (grid.improvement(i) == aoc::map::ImprovementType::TradingPost) {
            ++total;
        }
    }
    return total;
}

/// WP-K4: max trade route distance gated by transport tech.
/// Land + Sea progress independently. Air ignores range once Aviation
/// (TechId 26) researched.
int32_t maxTradeRange(const aoc::game::Player& player, TradeRouteType type) {
    auto has = [&](uint16_t techId) {
        return player.tech().hasResearched(TechId{techId});
    };
    switch (type) {
        case TradeRouteType::Air:
            // Air requires Aviation (26) at all; once researched, unlimited.
            return has(26) ? std::numeric_limits<int32_t>::max() : 0;
        case TradeRouteType::Sea: {
            // 2026-05-02: bumped sea baseline 8→16 to match the new land
            // baseline. Sea routes always had it slightly easier than
            // land, keep the gap.
            int32_t r = 16;           // coastal hugger baseline
            if (has(7))  { r = 22; } // Apprenticeship: organized shipping
            if (has(8))  { r = 28; } // Metallurgy: larger sailing fleets
            if (has(11)) { r = 36; } // Industrialization: steam ships
            if (has(12)) { r = 48; } // Refining: oil-burning ships
            if (has(15)) { r = 60; } // Mass Production: diesel cargo
            if (has(26)) { return std::numeric_limits<int32_t>::max(); }
            return r;
        }
        case TradeRouteType::Land:
        default: {
            // 2026-05-02: bumped baseline 4→10 + tiers up. Audit showed
            // ~5800 trade-route rejections with "longest segment > range"
            // — civs at game start could only reach 4-tile-distant cities,
            // and continental neighbours are typically 8-15 hexes apart.
            // Trading Post infrastructure relays the gaps but AI doesn't
            // build them often enough; bumping baseline lets early-game
            // trade actually function.
            int32_t r = 10;           // foot caravan baseline
            if (has(1))  { r = 14; } // Animal Husbandry: pack animals
            if (has(6))  { r = 18; } // Engineering: paved roads
            if (has(7))  { r = 22; } // Apprenticeship: commercial network
            if (has(11)) { r = 30; } // Industrialization: railways
            if (has(12)) { r = 40; } // Refining: trucks
            if (has(15)) { r = 50; } // Mass Production: logistics
            if (has(26)) { return std::numeric_limits<int32_t>::max(); }
            return r;
        }
    }
}

/// WP-K7: walk a path and find longest gap between relay nodes (cities or
/// Trading Posts). Returns the longest segment length so the caller can
/// compare against `maxTradeRange`. Origin / destination tiles are always
/// considered relay points.
int32_t longestRangeGap(const std::vector<aoc::hex::AxialCoord>& path,
                         const aoc::map::HexGrid& grid,
                         const aoc::game::GameState& gameState) {
    if (path.size() < 2) { return 0; }
    int32_t longest = 0;
    int32_t segment = 0;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        ++segment;
        const aoc::hex::AxialCoord next = path[i + 1];
        if (!grid.isValid(next)) { continue; }
        const int32_t nIdx = grid.toIndex(next);
        bool isRelay = false;
        if (grid.improvement(nIdx) == aoc::map::ImprovementType::TradingPost) {
            isRelay = true;
        }
        // Any city on this tile (any owner) acts as a relay.
        if (!isRelay) {
            for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
                if (p == nullptr) { continue; }
                if (p->cityAt(next) != nullptr) { isRelay = true; break; }
            }
        }
        if (isRelay) {
            if (segment > longest) { longest = segment; }
            segment = 0;
        }
    }
    // Final segment to destination.
    if (segment > longest) { longest = segment; }
    return longest;
}

/// Check if two players share a FTZ or Customs Union (0% toll between members).
bool areInFreeTradeAgreement(const aoc::game::Player& territoryOwner, PlayerId trader) {
    const PlayerTradeAgreementsComponent& agreements = territoryOwner.tradeAgreements();
    for (const TradeAgreementDef& agreement : agreements.agreements) {
        if (!agreement.isActive) { continue; }
        // FreeTradeZone, CustomsUnion, and (WP-C3) TransitTreaty all grant
        // zero-toll passage to traders whose owner is a member. Bilateral
        // deals still charge the standard tariff.
        if (agreement.type != TradeAgreementType::FreeTradeZone
            && agreement.type != TradeAgreementType::CustomsUnion
            && agreement.type != TradeAgreementType::TransitTreaty) {
            continue;
        }
        bool traderIsMember = false;
        for (PlayerId member : agreement.members) {
            if (member == trader) { traderIsMember = true; break; }
        }
        if (traderIsMember) { return true; }
    }
    return false;
}

} // anonymous namespace

ErrorCode establishTradeRoute(aoc::game::GameState& gameState,
                               aoc::map::HexGrid& grid,
                               const Market& market,
                               const DiplomacyManager* diplomacy,
                               aoc::game::Unit& traderUnitRef,
                               aoc::game::City& destCityRef) {
    aoc::game::Unit* traderUnit = &traderUnitRef;

    if (traderUnit->typeDef().unitClass != UnitClass::Trader) {
        return ErrorCode::InvalidArgument;
    }

    aoc::game::City* destCity = &destCityRef;

    // 2026-05-02: reject razed / invalid-owner destinations early. Founder
    // lists retain captured-and-razed cities with owner=INVALID; downstream
    // consent and component lookups silently fail on those.
    if (destCity->owner() == INVALID_PLAYER) {
        return ErrorCode::InvalidArgument;
    }

    // Trade consent: foreign trade requires destination player's acceptance.
    // The AI evaluates whether the trade benefits them based on:
    //   - Gold income from the route
    //   - Resources the partner could bring that we need
    //   - Diplomatic relation (friendly players get a bonus)
    //   - War/embargo blocks trade entirely
    if (destCity->owner() != traderUnit->owner()) {
        if (!evaluateTradeConsent(gameState, market, diplomacy,
                                  traderUnit->owner(), destCity->owner())) {
            LOG_INFO("Trade route rejected: player %u -> player %u (no benefit / hostile)",
                     static_cast<unsigned>(traderUnit->owner()),
                     static_cast<unsigned>(destCity->owner()));
            return ErrorCode::InvalidArgument;
        }
    }

    // Find the origin city (closest owned city to the Trader)
    aoc::game::City* originCity = nullptr;
    int32_t bestDist = 9999;

    aoc::game::Player* ownerPlayer = gameState.player(traderUnit->owner());
    if (ownerPlayer == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    for (const std::unique_ptr<aoc::game::City>& c : ownerPlayer->cities()) {
        int32_t dist = grid.distance(traderUnit->position(), c->location());
        if (dist < bestDist) {
            bestDist = dist;
            originCity = c.get();
        }
    }
    if (originCity == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    // WP-K1: civ-wide trade slot pool. Replaces the legacy per-civ cap
    // that was strictly monetary-tier driven. Pool aggregates monetary
    // baseline + Markets/Banks/Stock Exchanges anywhere in the civ +
    // Trading Posts on owned tiles + Merchant GP slots.
    {
        const int32_t cap = computeTotalTradeSlots(*ownerPlayer, grid);
        // 2026-05-02: only count traders that already have an assigned route.
        // Previously every idle Trader unit (default trader.owner=INVALID)
        // was counted, so a civ with 5 idle Caravans waiting for civics
        // would hit the cap on its first route attempt and reject every
        // subsequent establishTradeRoute call. ~6000 traders built across
        // 36 sims, only 50 routes ever opened.
        int32_t activeRoutes = 0;
        for (const std::unique_ptr<aoc::game::Unit>& u : ownerPlayer->units()) {
            if (u == nullptr) { continue; }
            if (u.get() == traderUnit) { continue; }
            if (u->typeDef().unitClass != UnitClass::Trader) { continue; }
            if (u->trader().owner == INVALID_PLAYER) { continue; }
            ++activeRoutes;
        }
        if (activeRoutes >= cap) {
            LOG_INFO("Trade route rejected: player %u at cap %d (active %d)",
                     static_cast<unsigned>(traderUnit->owner()), cap, activeRoutes);
            return ErrorCode::InvalidArgument;
        }
    }
    

    // Create TraderComponent
    TraderComponent& trader = traderUnit->trader();
    trader.owner = traderUnit->owner();
    trader.originCityLocation = originCity->location();
    trader.destCityLocation = destCity->location();
    trader.destOwner = destCity->owner();
    trader.isReturning = false;
    trader.completedTrips = 0;
    trader.turnsActive = 0;
    trader.maxTrips = -1;  // Permanent route

    // Determine route type based on city infrastructure and tech.
    //
    // Selection priority (highest first):
    //   1. Air  -- both cities have Airport (BuildingId{14}) AND owner has Aviation (TechId{26})
    //   2. Sea  -- both cities are coastal (adjacent to water, regardless of Harbor district)
    //   3. Land -- fallback
    //
    // Sea routes previously required both cities to have the Harbor district.
    // That caused 100% Land routes because AI rarely places Harbor districts before
    // needing trade routes. Coastal adjacency is the correct natural precondition.
    const CityDistrictsComponent& originDistricts = originCity->districts();
    const CityDistrictsComponent& destDistricts   = destCity->districts();

    // Airport: BuildingId{14} unlocked by Aviation (TechId{26})
    bool ownerHasAviation = ownerPlayer->tech().hasResearched(TechId{26});
    bool originHasAirport = originDistricts.hasBuilding(BuildingId{14});
    bool destHasAirport   = destDistricts.hasBuilding(BuildingId{14});

    // Coastal check: at least one neighbor tile is water.
    // auto required: lambda type is unnameable.
    auto isCityCoastal = [&grid](const aoc::game::City* city) -> bool {
        std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(city->location());
        for (const aoc::hex::AxialCoord& nbr : nbrs) {
            if (grid.isValid(nbr) && aoc::map::isWater(grid.terrain(grid.toIndex(nbr)))) {
                return true;
            }
        }
        return false;
    };
    bool originIsCoastal = isCityCoastal(originCity);
    bool destIsCoastal   = isCityCoastal(destCity);

    // 2026-05-02: Harbor district no longer required for Sea routes.
    // Caravans/traders can hire boats out of any coastal city even before
    // a civ researches shipbuilding — Harbor + ship-tech still gate
    // proper naval combat units, not commerce. Removing the gate so
    // island civs and pure-coastal empires can trade across water by
    // default. Harbor still gives bonuses (district adjacency, building
    // capacity) but isn't a hard prereq for the route type.
    (void)originDistricts; (void)destDistricts;

    if (ownerHasAviation && originHasAirport && destHasAirport) {
        trader.routeType = TradeRouteType::Air;
    } else if (originIsCoastal && destIsCoastal) {
        trader.routeType = TradeRouteType::Sea;
    } else {
        trader.routeType = TradeRouteType::Land;
    }

    // Compute path based on route type
    aoc::hex::AxialCoord from = originCity->location();
    aoc::hex::AxialCoord to   = destCity->location();

    if (trader.routeType == TradeRouteType::Air) {
        // Air routes: direct line (planes don't need paths through terrain)
        int32_t dist = grid.distance(from, to);
        trader.path.clear();
        for (int32_t step = 0; step <= dist; ++step) {
            float t = (dist > 0) ? static_cast<float>(step) / static_cast<float>(dist) : 0.0f;
            int32_t q = static_cast<int32_t>(std::round(
                static_cast<float>(from.q) * (1.0f - t) + static_cast<float>(to.q) * t));
            int32_t r = static_cast<int32_t>(std::round(
                static_cast<float>(from.r) * (1.0f - t) + static_cast<float>(to.r) * t));
            trader.path.push_back(aoc::hex::AxialCoord{q, r});
        }
    } else if (trader.routeType == TradeRouteType::Sea) {
        // Sea routes: compare canal vs no-canal path for profitability.
        // Canal paths are shorter but charge tolls — only use if time savings
        // outweigh the toll cost.
        std::optional<aoc::map::PathResult> canalPath = aoc::map::findPath(
            grid, from, to, 0, nullptr, INVALID_PLAYER, true, false);
        std::optional<aoc::map::PathResult> noCanalPath = aoc::map::findPath(
            grid, from, to, 0, nullptr, INVALID_PLAYER, true, true);

        bool useCanal = false;
        if (canalPath.has_value() && noCanalPath.has_value()) {
            int32_t canalLen = static_cast<int32_t>(canalPath->path.size());
            int32_t noCanalLen = static_cast<int32_t>(noCanalPath->path.size());
            int32_t savedTiles = noCanalLen - canalLen;

            if (savedTiles > 0) {
                // Estimate canal toll cost for this path
                CurrencyAmount estimatedToll = 0;
                for (const aoc::hex::AxialCoord& tile : canalPath->path) {
                    if (!grid.isValid(tile)) { continue; }
                    int32_t idx = grid.toIndex(tile);
                    if (!grid.hasCanal(idx)) { continue; }
                    PlayerId canalOwner = grid.owner(idx);
                    if (canalOwner == INVALID_PLAYER || canalOwner == trader.owner) { continue; }
                    aoc::game::Player* ownerP = gameState.player(canalOwner);
                    if (ownerP == nullptr) { continue; }
                    // Rough toll estimate: 25% of average cargo value per canal tile
                    float canalRate = ownerP->tariffs().effectiveCanalTollRate(trader.owner);
                    estimatedToll += static_cast<CurrencyAmount>(100.0f * canalRate);
                }

                // Estimate time-savings value: each saved tile = earlier delivery.
                // Sea trader speed ~5 tiles/turn. Saved turns = savedTiles / speed.
                // Value of saved time = goldPerTurn * savedTurns.
                constexpr float SEA_SPEED = 5.0f;
                constexpr float ESTIMATED_CARGO_VALUE = 200.0f;  // Rough average
                float savedTurns = static_cast<float>(savedTiles) / SEA_SPEED;
                float goldPerTurn = ESTIMATED_CARGO_VALUE
                    / (static_cast<float>(canalLen) * 2.0f / SEA_SPEED);
                float timeSavingsValue = goldPerTurn * savedTurns;

                useCanal = timeSavingsValue > static_cast<float>(estimatedToll);
            }
            // If canal path isn't shorter, no reason to use it
        } else if (canalPath.has_value() && !noCanalPath.has_value()) {
            // Canal is the only way through
            useCanal = true;
        }

        if (useCanal && canalPath.has_value()) {
            trader.path = canalPath->path;
            LOG_INFO("Trade route using canal shortcut (saves %d tiles)",
                     noCanalPath.has_value()
                         ? static_cast<int>(noCanalPath->path.size()) - static_cast<int>(canalPath->path.size())
                         : 0);
        } else if (noCanalPath.has_value()) {
            trader.path = noCanalPath->path;
        } else if (canalPath.has_value()) {
            trader.path = canalPath->path;
        } else {
            // Both failed: fall back to straight line
            int32_t dist = grid.distance(from, to);
            trader.path.clear();
            for (int32_t step = 0; step <= dist; ++step) {
                float t = (dist > 0) ? static_cast<float>(step) / static_cast<float>(dist) : 0.0f;
                int32_t q = static_cast<int32_t>(std::round(
                    static_cast<float>(from.q) * (1.0f - t) + static_cast<float>(to.q) * t));
                int32_t r = static_cast<int32_t>(std::round(
                    static_cast<float>(from.r) * (1.0f - t) + static_cast<float>(to.r) * t));
                trader.path.push_back(aoc::hex::AxialCoord{q, r});
            }
        }
    } else {
        // Land routes: use A* pathfinding
        std::optional<aoc::map::PathResult> pathResult = aoc::map::findPath(
            grid, from, to, 0, nullptr, INVALID_PLAYER);
        if (pathResult.has_value()) {
            trader.path = pathResult->path;
        } else {
            // Pathfinding failed: fall back to straight line
            int32_t dist = grid.distance(from, to);
            trader.path.clear();
            for (int32_t step = 0; step <= dist; ++step) {
                float t = (dist > 0) ? static_cast<float>(step) / static_cast<float>(dist) : 0.0f;
                int32_t q = static_cast<int32_t>(std::round(
                    static_cast<float>(from.q) * (1.0f - t) + static_cast<float>(to.q) * t));
                int32_t r = static_cast<int32_t>(std::round(
                    static_cast<float>(from.r) * (1.0f - t) + static_cast<float>(to.r) * t));
                trader.path.push_back(aoc::hex::AxialCoord{q, r});
            }
        }
    }
    trader.pathIndex = 0;

    // WP-K4 + K7: range gate. Compute longest unbroken segment between
    // relay points (cities or Trading Posts) and reject if greater than
    // the player's tech-gated max. Trading Posts on neutral land act as
    // range extenders, mirroring Civ-6 trader chains.
    {
        const int32_t maxRange = maxTradeRange(*ownerPlayer, trader.routeType);
        if (maxRange > 0) {
            const int32_t longestSegment = longestRangeGap(
                trader.path, grid, gameState);
            if (longestSegment > maxRange) {
                LOG_INFO("Trade route rejected: P%u %s longest segment %d > range %d "
                         "(build a Trading Post or extend tech)",
                         static_cast<unsigned>(traderUnit->owner()),
                         (trader.routeType == TradeRouteType::Land ? "land" :
                          trader.routeType == TradeRouteType::Sea  ? "sea"  : "air"),
                         longestSegment, maxRange);
                return ErrorCode::InvalidArgument;
            }
        }
    }

    const char* routeNames[] = {"Land", "Sea", "Air"};
    LOG_INFO("Trade route type: %s (player %u -> player %u)",
             routeNames[static_cast<int>(trader.routeType)],
             static_cast<unsigned>(traderUnit->owner()),
             static_cast<unsigned>(destCity->owner()));

    // Load goods prioritized by what destination needs (demand-driven).
    // Effective cargo = raw - moneyWeight (metal coins hog bay space under
    // CommodityMoney; paper/digital cost nothing).
    CityStockpileComponent& originStock = originCity->stockpile();
    const CityStockpileComponent& destStock = destCity->stockpile();
    aoc::game::Player* ownerPtrForCargo = gameState.player(trader.owner);
    const MonetarySystemType ownerSys = (ownerPtrForCargo != nullptr)
        ? ownerPtrForCargo->monetary().system
        : MonetarySystemType::Barter;
    const bool railOutbound = pathOnRail(trader, grid);
    const int32_t cargoSlots = trader.effectiveCargoSlots(ownerSys, railOutbound);
    selectTradeGoods(originStock, &destStock, market, trader.cargo, cargoSlots);
    for (TradeCargo& c : trader.cargo) {
        // WP-O: pull from exportBuffer first (drains the queue), then
        // stockpile if buffer underflows. May load less than requested
        // if both are empty.
        const int32_t taken = originStock.pullForExport(c.goodId, c.amount);
        c.amount = taken;
        [[maybe_unused]] bool ok = (taken == c.amount);
    }

    // WP-R: pre-fill fuel for the round trip. Drains owner's stockpile.
    // Refuses route if insufficient fuel (clear failure log).
    {
        const FuelSpec fs = routeFuelSpec(trader, grid, *ownerPlayer);
        trader.fuelGoodId = fs.goodId;
        trader.fuelPerTile = fs.perTile;
        if (fs.goodId != 0 && fs.perTile > 0.0f) {
            const float needFloat = static_cast<float>(trader.path.size())
                                   * 2.0f * fs.perTile * 1.1f;
            const int32_t need = static_cast<int32_t>(std::ceil(needFloat));
            const int32_t drained = drainFuel(*ownerPlayer, fs.goodId, need);
            if (drained < need) {
                LOG_INFO("Trade route refused: P%u %s out of fuel (needed %d %s, had %d)",
                         static_cast<unsigned>(traderUnit->owner()),
                         (trader.routeType == TradeRouteType::Land ? "land" :
                          trader.routeType == TradeRouteType::Sea  ? "sea"  : "air"),
                         need,
                         (fs.goodId == goods::FUEL ? "FUEL" : "COAL"),
                         drained);
                // Return drained partial back to nearest origin city stockpile.
                if (drained > 0 && originCity != nullptr) {
                    originCity->stockpile().addGoods(fs.goodId, drained);
                }
                return ErrorCode::InvalidArgument;
            }
            trader.fuelOnBoard = drained;
        }
        trader.idleTurnsNoFuel = 0;
    }

    // WP-O: reserve dest city's pickup goods now. Frees dest stockpile during
    // the outbound transit; trader pulls from buffer on arrival.
    commitPickupReservation(*destCity, market, trader, grid);

    LOG_INFO("Trade route established: player %u, %d goods loaded, %d reserved at dest",
             static_cast<unsigned>(traderUnit->owner()),
             static_cast<int>(trader.cargo.size()),
             static_cast<int>(trader.pendingPickupCargo.size()));

    return ErrorCode::Ok;
}

void processTradeRoutes(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                         const Market& market,
                         DiplomacyManager* diplomacy) {
    // Collect all active trader units across all players
    std::vector<aoc::game::Unit*> traderUnits;
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        for (const std::unique_ptr<aoc::game::Unit>& u : p->units()) {
            if (u->typeDef().unitClass == UnitClass::Trader
                && u->trader().owner != INVALID_PLAYER) {
                traderUnits.push_back(u.get());
            }
        }
    }

    std::vector<aoc::game::Unit*> toRemove;

    for (aoc::game::Unit* unitPtr : traderUnits) {
        TraderComponent& trader = unitPtr->trader();

        ++trader.turnsActive;
        trader.tollPaidThisTurn = 0;

        // Compute cargo value once for toll calculation this turn
        CurrencyAmount cargoValue = computeCargoValue(trader.cargo, market);

        // ----------------------------------------------------------------
        // Toll pre-scan: look ahead at tiles the trader will cross this turn
        // and compute total expected toll per territory owner. AI decides
        // once per turn whether to accept, reroute, or refuse for each owner.
        // ----------------------------------------------------------------

        // Determine movement speed based on terrain under the Trader
        int32_t tileIdx = grid.isValid(unitPtr->position())
                        ? grid.toIndex(unitPtr->position()) : -1;
        bool onRoad = (tileIdx >= 0) && grid.hasRoad(tileIdx);
        bool onRailway = (tileIdx >= 0)
            && (grid.improvement(tileIdx) == aoc::map::ImprovementType::Railway
                || grid.improvement(tileIdx) == aoc::map::ImprovementType::Highway);
        // WP-C3 pipeline speed: pipelines double land-trader throughput.
        bool onPipeline = (tileIdx >= 0) && grid.hasPipeline(tileIdx);
        int32_t speed = trader.movementSpeed(onRoad, onRailway, onPipeline);

        // Scan upcoming tiles to compute toll per foreign owner
        struct TollEntry {
            PlayerId owner;
            int32_t  foreignTiles;  // tiles of this owner on the path
            int32_t  canalTiles;    // canal tiles traversed (charged premium toll)
            float    tollRate;
            float    canalTollRate; // additional per-tile canal transit fee
        };
        std::vector<TollEntry> tollEntries;

        int32_t scanIdx = trader.pathIndex;
        for (int32_t step = 0; step < speed; ++step) {
            if (scanIdx >= static_cast<int32_t>(trader.path.size()) - 1) { break; }
            ++scanIdx;
            aoc::hex::AxialCoord tile = trader.path[static_cast<std::size_t>(scanIdx)];
            if (!grid.isValid(tile)) { continue; }
            int32_t idx = grid.toIndex(tile);
            PlayerId tileOwner = grid.owner(idx);
            if (tileOwner == INVALID_PLAYER || tileOwner == trader.owner) { continue; }
            // City-states and barbarians are not in the diplomacy matrix; skip
            // toll logic (CS passage is governed by envoy/suzerain mechanics).
            if (tileOwner >= aoc::sim::CITY_STATE_PLAYER_BASE) { continue; }
            if (trader.owner >= aoc::sim::CITY_STATE_PLAYER_BASE) { continue; }

            aoc::game::Player* ownerPlayer = gameState.player(tileOwner);
            if (ownerPlayer == nullptr) { continue; }
            if (areInFreeTradeAgreement(*ownerPlayer, trader.owner)) { continue; }
            if (diplomacy != nullptr) {
                const PairwiseRelation& rel = diplomacy->relation(tileOwner, trader.owner);
                if (rel.hasOpenBorders) { continue; }
            }

            float rate = ownerPlayer->tariffs().effectiveTollRate(trader.owner);
            bool isCanal = grid.hasCanal(idx);

            // Canal tiles always charge toll (even if territory rate is 0)
            if (rate <= 0.0f && !isCanal) { continue; }

            // Aggregate by owner
            bool found = false;
            for (TollEntry& te : tollEntries) {
                if (te.owner == tileOwner) {
                    ++te.foreignTiles;
                    if (isCanal) { ++te.canalTiles; }
                    found = true;
                    break;
                }
            }
            if (!found) {
                float canalRate = isCanal ? ownerPlayer->tariffs().effectiveCanalTollRate(trader.owner) : 0.0f;
                tollEntries.push_back({tileOwner, 1, isCanal ? 1 : 0, rate, canalRate});
            }
        }

        // AI toll decision per territory owner.
        // Decision factors:
        //   - tollRate vs cargo profit margin
        //   - diplomatic relation (friendly -> accept, hostile -> refuse)
        //   - reputation consequences (-5 for refusal, +1 for payment)
        //   - military presence (garrison near route increases accept likelihood)
        //
        // Human players always accept for now (UI for decline comes later).
        // AI decision: accept if toll < 30% of cargo value OR relation is friendly.
        //              refuse (pass through anyway) if hostile and toll > 25%.
        //              rerouting is deferred to route establishment (Step 4b).
        for (TollEntry& te : tollEntries) {
            // Territory toll: proportional to path fraction through this owner's land
            float territoryToll = static_cast<float>(cargoValue) * te.tollRate
                * static_cast<float>(te.foreignTiles)
                / static_cast<float>(std::max(static_cast<int32_t>(trader.path.size()), 1));
            // Canal surcharge: flat per-tile premium for canal transit (major infrastructure)
            float canalSurcharge = static_cast<float>(cargoValue) * te.canalTollRate
                * static_cast<float>(te.canalTiles);
            CurrencyAmount totalToll = static_cast<CurrencyAmount>(territoryToll + canalSurcharge);
            if (totalToll <= 0) { totalToll = 1; }

            aoc::game::Player* traderPlayer = gameState.player(trader.owner);
            bool isAI = (traderPlayer != nullptr && !traderPlayer->isHuman());
            bool acceptToll = true;

            if (isAI && diplomacy != nullptr
                && trader.owner < aoc::sim::CITY_STATE_PLAYER_BASE
                && te.owner < aoc::sim::CITY_STATE_PLAYER_BASE) {
                const PairwiseRelation& rel = diplomacy->relation(trader.owner, te.owner);
                int32_t score = rel.totalScore();
                float tollFraction = (cargoValue > 0)
                    ? static_cast<float>(totalToll) / static_cast<float>(cargoValue)
                    : 1.0f;

                // Hostile relations + expensive toll -> refuse and pass through
                if (score < -10 && tollFraction > 0.25f) {
                    acceptToll = false;
                }
                // Very hostile -> refuse even cheap tolls
                if (score < -40 && tollFraction > 0.10f) {
                    acceptToll = false;
                }
            }

            if (acceptToll) {
                // Pay toll: credit territory owner, debit trader
                aoc::game::Player* tollReceiver = gameState.player(te.owner);
                if (tollReceiver != nullptr) { tollReceiver->addGold(totalToll); }
                if (traderPlayer != nullptr) { traderPlayer->addGold(-totalToll); }
                trader.tollPaidThisTurn += totalToll;

                // Reputation: +1 for honoring toll
                if (diplomacy != nullptr) {
                    const_cast<DiplomacyManager*>(diplomacy)->addReputationModifier(
                        trader.owner, te.owner, 1, 20);
                }
            } else {
                // Refuse toll and pass through anyway: -5 reputation
                if (diplomacy != nullptr) {
                    const_cast<DiplomacyManager*>(diplomacy)->addReputationModifier(
                        trader.owner, te.owner, -5, 30);
                }

                // Notify territory owner of refusal
                LOG_INFO("Player %u's trader refused %lld toll from Player %u and passed through",
                         static_cast<unsigned>(trader.owner),
                         static_cast<long long>(totalToll),
                         static_cast<unsigned>(te.owner));

                aoc::ui::pushNotification({
                    aoc::ui::NotificationCategory::Diplomacy,
                    "Toll Refused",
                    "A trade caravan from Player " + std::to_string(trader.owner)
                        + " refused your toll and passed through your territory.",
                    te.owner,
                    3
                });
            }
        }

        // WP-R: fuel-gate movement. If fuel needed and exhausted, stall + try
        // emergency resupply from owner stockpile. After 20 idle turns, give
        // up — caller-side toRemove handles cargo recovery.
        bool stalled = false;
        if (trader.fuelGoodId != 0 && trader.fuelPerTile > 0.0f) {
            aoc::game::Player* tplayer = gameState.player(trader.owner);
            if (tplayer != nullptr && trader.fuelOnBoard <= 0) {
                // Try resupply from any own city stockpile.
                const int32_t need = static_cast<int32_t>(
                    std::ceil(static_cast<float>(speed) * trader.fuelPerTile));
                const int32_t got = drainFuel(*tplayer, trader.fuelGoodId, need);
                if (got > 0) {
                    trader.fuelOnBoard += got;
                    trader.idleTurnsNoFuel = 0;
                    LOG_INFO("Trader P%u resupplied %d fuel from home stockpile",
                             static_cast<unsigned>(trader.owner), got);
                } else {
                    ++trader.idleTurnsNoFuel;
                    stalled = true;
                    if (trader.idleTurnsNoFuel >= 20) {
                        toRemove.push_back(unitPtr);
                        LOG_WARN("Trader P%u abandoned: no fuel for 20 turns",
                                 static_cast<unsigned>(trader.owner));
                        continue;
                    }
                }
            }
        }

        // Move along path (tolls already settled above)
        if (!stalled) {
            for (int32_t step = 0; step < speed; ++step) {
                if (trader.pathIndex >= static_cast<int32_t>(trader.path.size()) - 1) {
                    break;  // Arrived
                }
                ++trader.pathIndex;
                unitPtr->setPosition(trader.path[static_cast<std::size_t>(trader.pathIndex)]);
                // WP-R: per-tile fuel drain.
                if (trader.fuelGoodId != 0 && trader.fuelPerTile > 0.0f) {
                    const int32_t cost = static_cast<int32_t>(
                        std::ceil(trader.fuelPerTile));
                    trader.fuelOnBoard -= cost;
                    if (trader.fuelOnBoard <= 0) {
                        trader.fuelOnBoard = 0;
                        break;  // out of fuel mid-step; rest of speed wasted.
                    }
                }
            }
        }

        // Check if arrived at destination
        bool arrived = (trader.pathIndex >= static_cast<int32_t>(trader.path.size()) - 1);
        if (!arrived) {
            continue;
        }

        // Arrived at either destination or origin
        aoc::hex::AxialCoord targetLoc = trader.isReturning ? trader.originCityLocation : trader.destCityLocation;
        aoc::game::City* targetCity = findCityByLocation(gameState, targetLoc);

        if (targetCity != nullptr) {
            CityStockpileComponent& targetStock = targetCity->stockpile();

            // Check for embargo violations: if trader delivers embargoed goods
            // between different players, apply reputation penalty.
            PlayerId traderOwner = trader.owner;
            PlayerId cityOwner = targetCity->owner();
            if (diplomacy != nullptr && traderOwner != cityOwner
                && traderOwner != INVALID_PLAYER && cityOwner != INVALID_PLAYER
                && traderOwner < aoc::sim::CITY_STATE_PLAYER_BASE
                && cityOwner < aoc::sim::CITY_STATE_PLAYER_BASE) {
                const PairwiseRelation& rel = diplomacy->relation(traderOwner, cityOwner);
                // C29: embargo was toothless — grievance + rep hit but cargo
                // still delivered. Seize embargoed cargo so violation costs
                // the trip's goods, not just reputation. Keeps physical
                // interdiction teeth without needing a separate customs pass.
                bool violated = false;
                for (auto cargoIt = trader.cargo.begin(); cargoIt != trader.cargo.end();) {
                    if (rel.isGoodEmbargoed(cargoIt->goodId)) {
                        diplomacy->addReputationModifier(traderOwner, cityOwner, -5, 30);

                        aoc::game::Player* embargoPartner = gameState.player(cityOwner);
                        if (embargoPartner != nullptr) {
                            embargoPartner->grievances().addGrievance(
                                GrievanceType::ViolatedEmbargo, traderOwner);
                        }

                        LOG_INFO("Embargo violation: Player %u cargo good %u seized "
                                 "entering Player %u city",
                                 static_cast<unsigned>(traderOwner),
                                 static_cast<unsigned>(cargoIt->goodId),
                                 static_cast<unsigned>(cityOwner));
                        violated = true;
                        cargoIt = trader.cargo.erase(cargoIt);
                    } else {
                        ++cargoIt;
                    }
                }
                (void)violated;
            }

            // Unload cargo and earn gold based on market prices
            CurrencyAmount goldEarned = 0;
            int32_t totalUnits = 0;
            for (const TradeCargo& c : trader.cargo) {
                targetStock.addGoods(c.goodId, c.amount);
                totalUnits += c.amount;
                // Gold = 20% of market value per unit traded
                int32_t price = market.marketData(c.goodId).currentPrice;
                if (price <= 0) { price = 1; }
                goldEarned += static_cast<CurrencyAmount>(c.amount)
                            * static_cast<CurrencyAmount>(price) / 5;
            }
            // Distance penalty: long routes lose more cargo in transit.
            // Floor 0.50× at 30+ tile distance. Models physical attrition,
            // doesn't touch underlying money supply.
            {
                const int32_t dist = grid.distance(trader.originCityLocation,
                                                    trader.destCityLocation);
                const float distMult = std::max(0.50f, 1.0f - 0.0167f * static_cast<float>(dist));
                goldEarned = static_cast<CurrencyAmount>(
                    static_cast<float>(goldEarned) * distMult);
            }
            // Diplomatic relation modifier: hostile civs impose tariffs/seizures,
            // friendly civs grant favorable terms. Skip city-states.
            if (diplomacy != nullptr
                && trader.owner != INVALID_PLAYER && cityOwner != INVALID_PLAYER
                && trader.owner != cityOwner
                && trader.owner < aoc::sim::CITY_STATE_PLAYER_BASE
                && cityOwner < aoc::sim::CITY_STATE_PLAYER_BASE) {
                const PairwiseRelation& rel = diplomacy->relation(trader.owner, cityOwner);
                float relMult;
                if (rel.isAtWar) {
                    relMult = 0.20f;  // most cargo seized
                } else {
                    switch (rel.stance()) {
                        case aoc::sim::DiplomaticStance::Hostile:    relMult = 0.50f; break;
                        case aoc::sim::DiplomaticStance::Unfriendly: relMult = 0.75f; break;
                        case aoc::sim::DiplomaticStance::Neutral:    relMult = 1.00f; break;
                        case aoc::sim::DiplomaticStance::Friendly:   relMult = 1.15f; break;
                        case aoc::sim::DiplomaticStance::Allied:     relMult = 1.30f; break;
                        default:                                     relMult = 1.00f; break;
                    }
                    if (rel.hasOpenBorders)     { relMult += 0.10f; }
                    if (rel.hasEconomicAlliance){ relMult += 0.15f; }
                }
                goldEarned = static_cast<CurrencyAmount>(
                    static_cast<float>(goldEarned) * relMult);
            }
            // WP-K3: throughput log per route type for audit ratio analysis.
            const char* routeTag =
                (trader.routeType == TradeRouteType::Land) ? "Land"
              : (trader.routeType == TradeRouteType::Sea)  ? "Sea"  : "Air";
            LOG_INFO("Trade delivery: P%u %s %d units cargo, %lld gold",
                     static_cast<unsigned>(trader.owner),
                     routeTag, totalUnits,
                     static_cast<long long>(goldEarned));

            trader.goldEarnedThisTurn = goldEarned;

            // Settle the sale against the owner's monetary system.
            //   CommodityMoney: coins earned at FOREIGN leg ride home in
            //                    `carriedGold` (at risk from pillage). Arrival
            //                    at HOME flushes carried coins to treasury and
            //                    home-leg revenue credits directly.
            //   GoldStandard / Fiat / Digital: paper or electronic settlement.
            //                    Treasury is credited immediately, nothing carried.
            //   Barter: no money -- only the goods swap counts; no gold credit.
            aoc::game::Player* sellerPlayer = gameState.player(trader.owner);
            if (sellerPlayer != nullptr) {
                MonetaryStateComponent& sellerMon = sellerPlayer->monetary();
                const bool atHome = trader.isReturning;
                if (atHome && trader.carriedGold > 0) {
                    sellerMon.treasury += trader.carriedGold;
                    trader.carriedGold = 0;
                }
                if (goldEarned > 0) {
                    if (traderCarriesGoldOnReturn(sellerMon.system) && !atHome) {
                        trader.carriedGold += goldEarned;
                    } else if (sellerMon.system != MonetarySystemType::Barter) {
                        sellerMon.treasury += goldEarned;
                    }
                }
            }

            // WP-O: load from this city's exportBuffer using the reservation
            // committed when the leg started. Capacity capped by money weight.
            const MonetarySystemType sellerSys = (sellerPlayer != nullptr)
                ? sellerPlayer->monetary().system
                : MonetarySystemType::Barter;
            const bool railReturn = pathOnRail(trader, grid);
            const int32_t returnSlots = trader.effectiveCargoSlots(sellerSys, railReturn);
            trader.cargo.clear();
            int32_t loaded = 0;
            for (const TradeCargo& planned : trader.pendingPickupCargo) {
                if (loaded >= returnSlots) { break; }
                int32_t taken = targetStock.pullForExport(planned.goodId, planned.amount);
                if (taken > 0) {
                    TradeCargo c;
                    c.goodId = planned.goodId;
                    c.amount = taken;
                    trader.cargo.push_back(c);
                    ++loaded;
                }
            }
            trader.pendingPickupCargo.clear();
        }

        // Science/culture spread: trade spreads ideas
        trader.scienceSpread += 0.5f;
        trader.cultureSpread += 0.3f;

        // Tech diffusion: trade spreads ideas between civs.
        // Lag-civ receives a general science boost on every delivery scaled by
        // the origin-target tech gap. Exact-match research gets an extra bonus.
        if (targetCity != nullptr) {
            PlayerId traderOwner = trader.owner;
            PlayerId cityOwner = targetCity->owner();
            if (traderOwner != cityOwner && traderOwner != INVALID_PLAYER
                && cityOwner != INVALID_PLAYER && trader.scienceSpread > 0.0f) {
                aoc::game::Player* originPlayer = gameState.player(traderOwner);
                aoc::game::Player* targetPlayer = gameState.player(cityOwner);
                if (originPlayer != nullptr && targetPlayer != nullptr) {
                    const PlayerTechComponent& originTech = originPlayer->tech();
                    PlayerTechComponent& targetTech = targetPlayer->tech();

                    int32_t originCount = 0;
                    int32_t targetCount = 0;
                    for (bool b : originTech.completedTechs) { originCount += b ? 1 : 0; }
                    for (bool b : targetTech.completedTechs) { targetCount += b ? 1 : 0; }
                    const int32_t techGap = originCount - targetCount;

                    if (techGap > 0 && targetTech.currentResearch.isValid()) {
                        // Base diffusion: scales with tech gap, always applied.
                        float boost = trader.scienceSpread
                                    * (0.1f + 0.02f * static_cast<float>(techGap));
                        // Exact match bonus: origin knows target's current research.
                        if (originTech.hasResearched(targetTech.currentResearch)) {
                            boost *= 2.5f;
                        }
                        LOG_INFO("Tech diffusion: player %u -> player %u, boost=%.2f (gap=%d)",
                                 static_cast<unsigned>(traderOwner),
                                 static_cast<unsigned>(cityOwner),
                                 static_cast<double>(boost), techGap);
                        advanceResearch(targetTech, boost);
                    }
                    trader.scienceSpread = 0.0f;

                    // Culture/civic diffusion: same idea, scaled by civic gap.
                    int32_t originCivics = 0;
                    int32_t targetCivics = 0;
                    const PlayerCivicComponent& originCivic = originPlayer->civics();
                    PlayerCivicComponent& targetCivic = targetPlayer->civics();
                    for (bool b : originCivic.completedCivics) { originCivics += b ? 1 : 0; }
                    for (bool b : targetCivic.completedCivics) { targetCivics += b ? 1 : 0; }
                    const int32_t civicGap = originCivics - targetCivics;
                    if (civicGap > 0 && targetCivic.currentResearch.isValid()
                        && trader.cultureSpread > 0.0f) {
                        float cBoost = trader.cultureSpread
                                    * (0.1f + 0.02f * static_cast<float>(civicGap));
                        if (originCivic.hasCompleted(targetCivic.currentResearch)) {
                            cBoost *= 2.5f;
                        }
                        LOG_INFO("Culture diffusion: player %u -> player %u, boost=%.2f (gap=%d)",
                                 static_cast<unsigned>(traderOwner),
                                 static_cast<unsigned>(cityOwner),
                                 static_cast<double>(cBoost), civicGap);
                        advanceCivicResearch(targetCivic, cBoost, &targetPlayer->government());
                        trader.cultureSpread = 0.0f;
                    }
                }
            }
        }

        if (trader.isReturning) {
            // Completed a full round trip
            ++trader.completedTrips;

            // Auto-build road along the trade route after 2 trips.
            // Previously gated at exactly 3, which never triggered on
            // routes with maxTrips < 3 (short caravan contracts never
            // left roads behind). Two completed round-trips is a strong
            // enough traffic signal and covers shorter contracts.
            if (trader.completedTrips == 2) {
                for (const aoc::hex::AxialCoord& tile : trader.path) {
                    if (grid.isValid(tile)) {
                        int32_t idx = grid.toIndex(tile);
                        if (grid.improvement(idx) == aoc::map::ImprovementType::None
                            && !aoc::map::isWater(grid.terrain(idx))
                            && !aoc::map::isImpassable(grid.terrain(idx))) {
                            grid.setImprovement(idx, aoc::map::ImprovementType::Road);
                        }
                    }
                }
                LOG_INFO("Trade route auto-built road (player %u, trip %d)",
                         static_cast<unsigned>(trader.owner), trader.completedTrips);
            }

            // Check if max trips reached (skip if permanent: maxTrips < 0)
            if (trader.maxTrips > 0 && trader.completedTrips >= trader.maxTrips) {
                LOG_INFO("Trade route expired after %d trips (player %u)",
                         trader.completedTrips, static_cast<unsigned>(trader.owner));
                if (unitPtr->autoRenewRoute) {
                    queueAutoRenewRequest(gameState, trader.owner,
                                          trader.originCityLocation,
                                          trader.destCityLocation,
                                          trader.destOwner,
                                          trader.routeType);
                }
                toRemove.push_back(unitPtr);
                continue;
            }
        }

        // Reverse direction: set up path for the next leg
        trader.isReturning = !trader.isReturning;
        std::vector<aoc::hex::AxialCoord> reversedPath(trader.path.rbegin(), trader.path.rend());
        trader.path = std::move(reversedPath);
        trader.pathIndex = 0;

        // WP-O: reserve next-arrival city's goods now (during this leg's
        // transit). Picks up at next arrival via pendingPickupCargo.
        const aoc::hex::AxialCoord nextPickupLoc = trader.isReturning
            ? trader.originCityLocation : trader.destCityLocation;
        aoc::game::City* nextPickup = findCityByLocation(gameState, nextPickupLoc);
        if (nextPickup != nullptr) {
            commitPickupReservation(*nextPickup, market, trader, grid);
        } else {
            trader.pendingPickupCargo.clear();
        }

        // WP-R: top up fuel for the next leg from owner stockpile.
        // Failure leaves the trader on whatever's already on board; if it
        // depletes mid-route the stall + emergency resupply path handles it.
        if (trader.fuelGoodId != 0 && trader.fuelPerTile > 0.0f) {
            aoc::game::Player* tp = gameState.player(trader.owner);
            if (tp != nullptr) {
                const float legNeedF = static_cast<float>(trader.path.size())
                                     * trader.fuelPerTile * 1.05f;
                const int32_t legNeed = static_cast<int32_t>(std::ceil(legNeedF));
                const int32_t deficit = std::max(0, legNeed - trader.fuelOnBoard);
                if (deficit > 0) {
                    const int32_t got = drainFuel(*tp, trader.fuelGoodId, deficit);
                    trader.fuelOnBoard += got;
                }
            }
        }
    }

    // Remove expired trader units. WP-O: try to return any cargo still
    // aboard to the nearest owned city, capped at the per-good soft cap.
    // Excess is lost (modeled as stale-warehouse pillage). Pillaged
    // traders go through `pillageTrader` instead.
    for (aoc::game::Unit* deadUnit : toRemove) {
        aoc::game::Player* ownerPlayer = gameState.player(deadUnit->owner());
        if (ownerPlayer == nullptr) {
            continue;
        }
        // WP-O: release any outstanding pickup reservation at the seller city.
        releasePickupReservation(gameState, deadUnit->trader());
        const TraderComponent& deadCargo = deadUnit->trader();
        if (!deadCargo.cargo.empty()) {
            const int32_t cap = aoc::balance::params().stockpileSoftCap;
            // Find nearest owned city to trader's last position.
            aoc::game::City* nearest = nullptr;
            int32_t bestDist = std::numeric_limits<int32_t>::max();
            for (const std::unique_ptr<aoc::game::City>& c : ownerPlayer->cities()) {
                if (c == nullptr) { continue; }
                const int32_t d = grid.distance(deadUnit->position(), c->location());
                if (d < bestDist) { bestDist = d; nearest = c.get(); }
            }
            if (nearest != nullptr) {
                CityStockpileComponent& sp = nearest->stockpile();
                int32_t returned = 0;
                int32_t lost = 0;
                for (const TradeCargo& c : deadCargo.cargo) {
                    const int32_t current = sp.getAmount(c.goodId);
                    const int32_t free = std::max(0, cap - current);
                    const int32_t take = std::min(c.amount, free);
                    if (take > 0) {
                        sp.addGoods(c.goodId, take);
                        returned += take;
                    }
                    lost += (c.amount - take);
                }
                if (returned > 0 || lost > 0) {
                    LOG_INFO("Trader expired (P%u): returned %d to %s, lost %d",
                             static_cast<unsigned>(ownerPlayer->id()),
                             returned, nearest->name().c_str(), lost);
                }
            }
        }
        ownerPlayer->removeUnit(deadUnit);
    }
}

CurrencyAmount pillageTrader(aoc::game::GameState& gameState,
                              EntityId traderEntity,
                              PlayerId pillager) {
    aoc::game::Unit* traderUnit = findTraderByEntityId(gameState, traderEntity);
    if (traderUnit == nullptr) {
        return 0;
    }

    // WP-O: pillaged trader can't pick up. Release the seller's reservation.
    releasePickupReservation(gameState, traderUnit->trader());

    const TraderComponent& trader = traderUnit->trader();

    // Calculate cargo value
    CurrencyAmount totalValue = 0;
    for (const TradeCargo& c : trader.cargo) {
        totalValue += static_cast<CurrencyAmount>(c.amount) * 3;  // Loot value
    }

    // Transfer loot to pillager's first city + any metal coin on board to
    // pillager's treasury. Under paper/fiat/digital this is always zero, so
    // pillaging a digital-tier trader yields goods only.
    aoc::game::Player* pillagerPlayer = gameState.player(pillager);
    const CurrencyAmount stolenGold = trader.carriedGold;
    if (pillagerPlayer != nullptr && !pillagerPlayer->cities().empty()) {
        CityStockpileComponent& stock = pillagerPlayer->cities().front()->stockpile();
        for (const TradeCargo& c : trader.cargo) {
            stock.addGoods(c.goodId, c.amount);
        }
        if (stolenGold > 0) {
            pillagerPlayer->monetary().treasury += stolenGold;
        }
    }
    totalValue += stolenGold;

    LOG_INFO("Trader pillaged! Player %u captured %lld gold worth (coins: %lld) from player %u",
             static_cast<unsigned>(pillager),
             static_cast<long long>(totalValue),
             static_cast<long long>(stolenGold),
             static_cast<unsigned>(trader.owner));

    if (traderUnit->autoRenewRoute) {
        queueAutoRenewRequest(gameState, trader.owner,
                              trader.originCityLocation,
                              trader.destCityLocation,
                              trader.destOwner,
                              trader.routeType);
    }

    aoc::game::Player* traderOwner = gameState.player(traderUnit->owner());
    if (traderOwner != nullptr) {
        traderOwner->removeUnit(traderUnit);
    }

    return totalValue;
}

void processLogisticsUnits(aoc::game::GameState& gameState,
                            aoc::map::HexGrid& grid) {
    constexpr int32_t REFILL_THRESHOLD = 50;
    constexpr std::array<uint16_t, 5> FOOD_GOODS = {
        goods::PROCESSED_FOOD, goods::WHEAT, goods::CATTLE,
        goods::FISH, goods::RICE
    };

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        aoc::game::Player& player = *playerPtr;
        for (const std::unique_ptr<aoc::game::Unit>& unitPtr : player.units()) {
            aoc::game::Unit& unit = *unitPtr;
            if (unit.typeDef().unitClass != UnitClass::Logistics) { continue; }
            LogisticsComponent& log = unit.logistics();

            switch (log.state) {
                case LogisticsState::AssigningTask: {
                    // Pick encampment most in need of refill.
                    int32_t bestNeed = -1;
                    int32_t bestIdx = -1;
                    for (const std::pair<const int32_t,
                            aoc::game::GameState::EncampmentBuffer>& kv
                            : gameState.encampments()) {
                        if (kv.second.owner != player.id()) { continue; }
                        const int32_t need = (REFILL_THRESHOLD - kv.second.food)
                                           + (REFILL_THRESHOLD - kv.second.fuel);
                        if (need > bestNeed) {
                            bestNeed = need;
                            bestIdx = kv.first;
                        }
                    }
                    if (bestIdx < 0 || bestNeed <= 0) {
                        // Idle, no work this turn. Keeps unit alive.
                        break;
                    }
                    log.targetDepotLocation = grid.toAxial(bestIdx);

                    // Pick nearest owned city as home.
                    aoc::game::City* nearest = nullptr;
                    int32_t bestDist = std::numeric_limits<int32_t>::max();
                    for (const std::unique_ptr<aoc::game::City>& c : player.cities()) {
                        const int32_t d = grid.distance(unit.position(), c->location());
                        if (d < bestDist) { bestDist = d; nearest = c.get(); }
                    }
                    if (nearest == nullptr) { break; }
                    log.homeCityLocation = nearest->location();
                    log.state = LogisticsState::EnRouteToCity;
                    aoc::sim::orderUnitMove(unit, log.homeCityLocation, grid);
                    aoc::sim::moveUnitAlongPath(gameState, unit, grid);
                    break;
                }
                case LogisticsState::EnRouteToCity: {
                    if (unit.position() == log.homeCityLocation) {
                        log.state = LogisticsState::LoadingAtCity;
                    } else {
                        aoc::sim::orderUnitMove(unit, log.homeCityLocation, grid);
                        aoc::sim::moveUnitAlongPath(gameState, unit, grid);
                        if (unit.position() == log.homeCityLocation) {
                            log.state = LogisticsState::LoadingAtCity;
                        }
                    }
                    break;
                }
                case LogisticsState::LoadingAtCity: {
                    aoc::game::City* home = player.cityAt(log.homeCityLocation);
                    if (home == nullptr) {
                        log.state = LogisticsState::AssigningTask;
                        break;
                    }
                    CityStockpileComponent& sp = home->stockpile();
                    int32_t foodRem = log.foodCapacity - log.food;
                    for (uint16_t gid : FOOD_GOODS) {
                        if (foodRem <= 0) { break; }
                        const int32_t avail = sp.getAmount(gid);
                        if (avail <= 0) { continue; }
                        const int32_t take = std::min(avail, foodRem);
                        if (sp.consumeGoods(gid, take)) {
                            log.food += take;
                            foodRem -= take;
                        }
                    }
                    int32_t fuelRem = log.fuelCapacity - log.fuel;
                    for (uint16_t gid : {goods::FUEL, goods::COAL}) {
                        if (fuelRem <= 0) { break; }
                        const int32_t avail = sp.getAmount(gid);
                        if (avail <= 0) { continue; }
                        const int32_t take = std::min(avail, fuelRem);
                        if (sp.consumeGoods(gid, take)) {
                            log.fuel += take;
                            fuelRem -= take;
                        }
                    }
                    log.state = LogisticsState::EnRouteToDepot;
                    aoc::sim::orderUnitMove(unit, log.targetDepotLocation, grid);
                    aoc::sim::moveUnitAlongPath(gameState, unit, grid);
                    break;
                }
                case LogisticsState::EnRouteToDepot: {
                    if (unit.position() == log.targetDepotLocation) {
                        log.state = LogisticsState::UnloadingAtDepot;
                    } else {
                        aoc::sim::orderUnitMove(unit, log.targetDepotLocation, grid);
                        aoc::sim::moveUnitAlongPath(gameState, unit, grid);
                        if (unit.position() == log.targetDepotLocation) {
                            log.state = LogisticsState::UnloadingAtDepot;
                        }
                    }
                    break;
                }
                case LogisticsState::UnloadingAtDepot: {
                    const int32_t depotIdx = grid.toIndex(log.targetDepotLocation);
                    std::unordered_map<int32_t, aoc::game::GameState::EncampmentBuffer>::iterator
                        it = gameState.encampments().find(depotIdx);
                    if (it != gameState.encampments().end()
                     && it->second.owner == player.id()) {
                        it->second.food += log.food;
                        it->second.fuel += log.fuel;
                        LOG_INFO("Logistics P%u dropped %d food + %d fuel at (%d,%d)",
                                 static_cast<unsigned>(player.id()),
                                 log.food, log.fuel,
                                 log.targetDepotLocation.q,
                                 log.targetDepotLocation.r);
                    }
                    log.food = 0;
                    log.fuel = 0;
                    log.state = LogisticsState::AssigningTask;
                    break;
                }
            }
        }
    }
}

int32_t countActiveTradeRoutes(const aoc::game::GameState& gameState, PlayerId player) {
    const aoc::game::Player* p = gameState.player(player);
    if (p == nullptr) {
        return 0;
    }

    int32_t count = 0;
    for (const std::unique_ptr<aoc::game::Unit>& u : p->units()) {
        if (u->typeDef().unitClass == UnitClass::Trader
            && u->trader().owner != INVALID_PLAYER) {
            ++count;
        }
    }
    return count;
}

TradeRouteEstimate estimateTradeRouteIncome(
    const aoc::game::GameState& gameState,
    const aoc::map::HexGrid& grid,
    const Market& market,
    const aoc::game::Unit& traderUnit,
    const aoc::game::City& destCity) {

    TradeRouteEstimate estimate{};

    // Find origin city (closest owned city to the Trader)
    const aoc::game::Player* ownerPlayer = gameState.player(traderUnit.owner());
    if (ownerPlayer == nullptr || ownerPlayer->cityCount() == 0) {
        return estimate;
    }

    const aoc::game::City* originCity = nullptr;
    int32_t bestDist = 9999;
    for (const std::unique_ptr<aoc::game::City>& c : ownerPlayer->cities()) {
        int32_t dist = grid.distance(traderUnit.position(), c->location());
        if (dist < bestDist) {
            bestDist = dist;
            originCity = c.get();
        }
    }
    if (originCity == nullptr) {
        return estimate;
    }

    // Determine route type (mirrors establishTradeRoute logic)
    bool ownerHasAviation = ownerPlayer->tech().hasResearched(TechId{26});
    bool originHasAirport = originCity->districts().hasBuilding(BuildingId{14});
    bool destHasAirport   = destCity.districts().hasBuilding(BuildingId{14});

    bool originIsCoastal = false;
    bool destIsCoastal   = false;
    {
        std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(originCity->location());
        for (const aoc::hex::AxialCoord& nbr : nbrs) {
            if (grid.isValid(nbr) && aoc::map::isWater(grid.terrain(grid.toIndex(nbr)))) {
                originIsCoastal = true;
                break;
            }
        }
    }
    {
        std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(destCity.location());
        for (const aoc::hex::AxialCoord& nbr : nbrs) {
            if (grid.isValid(nbr) && aoc::map::isWater(grid.terrain(grid.toIndex(nbr)))) {
                destIsCoastal = true;
                break;
            }
        }
    }

    // 2026-05-02: Harbor no longer required (mirrors actual establishTradeRoute).
    if (ownerHasAviation && originHasAirport && destHasAirport) {
        estimate.routeType = TradeRouteType::Air;
    } else if (originIsCoastal && destIsCoastal) {
        estimate.routeType = TradeRouteType::Sea;
    } else {
        estimate.routeType = TradeRouteType::Land;
    }

    // Distance estimate
    int32_t straightDist = grid.distance(originCity->location(), destCity.location());
    // A* paths are typically ~20% longer than straight-line on hex grids
    estimate.distanceTiles = static_cast<int32_t>(static_cast<float>(straightDist) * 1.2f);

    // Speed estimate (assume no roads for conservative estimate)
    TraderComponent tempTrader{};
    tempTrader.routeType = estimate.routeType;
    int32_t speed = tempTrader.movementSpeed(false, false);
    if (speed <= 0) { speed = 2; }
    estimate.roundTripTurns = (estimate.distanceTiles * 2) / speed + 1;

    // Gold estimate: sum market value of top surplus goods that would be traded.
    // Simple heuristic: look at origin surplus goods, price them at market value,
    // then apply route type multiplier.
    const CityStockpileComponent& originStock = originCity->stockpile();
    const CityStockpileComponent& destStock   = destCity.stockpile();

    // Collect origin surplus, scored by demand at destination
    struct ScoredGood {
        uint16_t goodId;
        int32_t  value;
    };
    std::vector<ScoredGood> scoredGoods;

    for (const std::pair<const uint16_t, int32_t>& entry : originStock.goods) {
        if (entry.second <= 0) { continue; }
        int32_t marketPrice = market.price(entry.first);
        if (marketPrice <= 0) { continue; }

        // Demand multiplier: goods the destination lacks are worth more
        int32_t destAmount = 0;
        const std::unordered_map<uint16_t, int32_t>::const_iterator it =
            destStock.goods.find(entry.first);
        if (it != destStock.goods.end()) {
            destAmount = it->second;
        }
        float demandMult = (destAmount == 0) ? 2.0f : 1.0f;

        int32_t tradeAmount = entry.second / 2;  // Ship half surplus
        if (tradeAmount <= 0) { tradeAmount = 1; }
        int32_t value = static_cast<int32_t>(
            static_cast<float>(tradeAmount * marketPrice) * demandMult);
        scoredGoods.push_back({entry.first, value});
    }

    std::sort(scoredGoods.begin(), scoredGoods.end(),
              [](const ScoredGood& a, const ScoredGood& b) {
                  return a.value > b.value;
              });

    const MonetarySystemType estSys = ownerPlayer->monetary().system;
    // Estimate uses optimistic rail-tier capacity for Land routes — actual
    // capacity at load time depends on path coverage, but this matches the
    // potential gain so the AI utility doesn't undervalue future-rail lanes.
    const bool estRail = (estimate.routeType == TradeRouteType::Land)
        && ownerPlayer->tech().hasResearched(TechId{11});
    int32_t maxSlots = tempTrader.effectiveCargoSlots(estSys, estRail);
    int32_t totalValue = 0;
    int32_t slotCount = 0;
    for (const ScoredGood& sg : scoredGoods) {
        if (slotCount >= maxSlots) { break; }
        totalValue += sg.value;
        ++slotCount;
    }

    // C31: AI was booking routes at gross value. Subtract expected tolls so
    // the utility score matches realized profit — keeps AI from signing
    // negative-EV routes when partner raised their rate.
    const CurrencyAmount grossGold = static_cast<CurrencyAmount>(totalValue);
    CurrencyAmount expectedTolls = 0;
    if (destCity.owner() != traderUnit.owner()) {
        const aoc::game::Player* destPlayer = gameState.player(destCity.owner());
        if (destPlayer != nullptr) {
            std::unordered_map<PlayerId, float>::const_iterator tollIt =
                destPlayer->tariffs().perPlayerTollRates.find(traderUnit.owner());
            if (tollIt != destPlayer->tariffs().perPlayerTollRates.end()) {
                expectedTolls = static_cast<CurrencyAmount>(
                    static_cast<float>(grossGold) * tollIt->second);
            }
        }
    }
    estimate.estimatedGoldPerTrip = grossGold - expectedTolls;

    return estimate;
}

} // namespace aoc::sim

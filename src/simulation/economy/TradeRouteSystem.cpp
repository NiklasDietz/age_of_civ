/**
 * @file TradeRouteSystem.cpp
 * @brief Physical trade routes with Trader units carrying real goods.
 */

#include "aoc/simulation/economy/TradeRouteSystem.hpp"

#include "aoc/balance/BalanceParams.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"
#include "aoc/simulation/economy/MonopolyPricing.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/economy/AdvancedEconomics.hpp"
#include "aoc/simulation/economy/TradeAgreement.hpp"
#include "aoc/simulation/automation/Automation.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/tech/CivicEffects.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Pathfinding.hpp"
#include "aoc/simulation/event/GameNotifications.hpp"
#include "aoc/core/Log.hpp"

#include "aoc/simulation/monetary/MoneyFlow.hpp"
#include "aoc/simulation/city/CitySiege.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace aoc::sim {

int32_t cityConsumptionNeed(uint16_t goodId, int32_t population) {
    const int32_t pop = std::max(0, population);
    switch (goodId) {
        case goods::WHEAT:              return pop / 3;
        case goods::CLOTHING:           return pop / 5 + 1;
        case goods::CONSUMER_GOODS:     return pop > 3 ? (pop - 3) / 3 + 1 : 0;
        case goods::PROCESSED_FOOD:     return pop > 8 ? (pop - 8) / 4 + 1 : 0;
        case goods::ADV_CONSUMER_GOODS: return pop > 15 ? (pop - 15) / 5 + 1 : 0;
        default:                        return 0;
    }
}

int32_t localPrice(const Market& market, uint16_t goodId, const aoc::game::City& city) {
    const int32_t base = std::max(1, market.marketData(goodId).currentPrice);
    const float need   = static_cast<float>(cityConsumptionNeed(goodId, city.population()));
    const float have   = static_cast<float>(std::max(0, city.stockpile().getAmount(goodId)));
    const float scale  = std::clamp(std::pow((need + 2.0f) / (have + 2.0f), LOCAL_PRICE_ELASTICITY),
                                    LOCAL_PRICE_MIN, LOCAL_PRICE_MAX);
    return std::max(1, static_cast<int32_t>(static_cast<float>(base) * scale + 0.5f));
}

float destinationSaleMultiplier(const aoc::game::City& city, TradeRouteType routeType) {
    constexpr uint16_t MARKET         = 6u;
    constexpr uint16_t BANK           = 20u;
    constexpr uint16_t STOCK_EXCHANGE = 21u;
    float mult = 1.0f;
    for (const CityDistrictsComponent::PlacedDistrict& d : city.districts().districts) {
        if (d.type == DistrictType::Commercial) {
            mult += 0.10f;
        }
        if (d.type == DistrictType::Harbor && routeType == TradeRouteType::Sea) {
            mult += 0.10f;
        }
        for (BuildingId bid : d.buildings) {
            mult += (bid.value == MARKET) ? 0.05f : 0.0f;
            mult += (bid.value == BANK) ? 0.10f : 0.0f;
            mult += (bid.value == STOCK_EXCHANGE) ? 0.15f : 0.0f;
        }
    }
    return std::min(mult, DESTINATION_SALE_CAP);
}

float routeYieldMultiplier(const aoc::game::GameState& gameState, const DiplomacyManager* diplomacy,
                           PlayerId seller, PlayerId buyer, int32_t distance) {
    // Distance: long routes lose more cargo in transit. Floor 0.50x at 30+
    // tiles. Physical attrition, nothing to do with the money supply.
    float yield = std::max(0.50f, 1.0f - 0.0167f * static_cast<float>(std::max(0, distance)));
    const bool twoMajors = seller != buyer && seller != INVALID_PLAYER && buyer != INVALID_PLAYER &&
                           seller < CITY_STATE_PLAYER_BASE && buyer < CITY_STATE_PLAYER_BASE;
    if (!twoMajors) {
        return yield;
    }
    // Relations: hostile civs impose tariffs and seizures, friendly ones
    // grant favourable terms.
    if (diplomacy != nullptr) {
        const PairwiseRelation& rel = diplomacy->relation(seller, buyer);
        float relMult;
        if (rel.isAtWar) {
            relMult = 0.20f; // most cargo seized
        } else {
            switch (rel.stance()) {
                case DiplomaticStance::Hostile:    relMult = 0.50f; break;
                case DiplomaticStance::Unfriendly: relMult = 0.75f; break;
                case DiplomaticStance::Neutral:    relMult = 1.00f; break;
                case DiplomaticStance::Friendly:   relMult = 1.15f; break;
                case DiplomaticStance::Allied:     relMult = 1.30f; break;
                default:                           relMult = 1.00f; break;
            }
            if (rel.hasOpenBorders)      { relMult += 0.10f; }
            if (rel.hasEconomicAlliance) { relMult += 0.15f; }
        }
        yield *= relMult;
    }
    // The quality of the money the sale settles in: a civ whose currency
    // nobody trusts gets worse terms on the same cargo.
    yield *= bilateralTradeEfficiency(gameState, seller, buyer);
    return yield;
}

int32_t routesBetween(const aoc::game::GameState& gameState, PlayerId a, PlayerId b) {
    if (a == b || a == INVALID_PLAYER || b == INVALID_PLAYER) {
        return 0;
    }
    int32_t routes = 0;
    for (int32_t side = 0; side < 2; ++side) {
        const PlayerId owner = side == 0 ? a : b;
        const PlayerId other = side == 0 ? b : a;
        const aoc::game::Player* p = gameState.player(owner);
        if (p == nullptr) {
            continue;
        }
        for (const std::unique_ptr<aoc::game::Unit>& u : p->units()) {
            if (u != nullptr && u->typeDef().unitClass == UnitClass::Trader &&
                u->trader().owner != INVALID_PLAYER && u->trader().destOwner == other) {
                ++routes;
            }
        }
    }
    return routes;
}

float importTariffRate(const aoc::game::Player& importer, PlayerId seller) {
    const float rate = importer.tariffs().effectiveImportTariff(seller) *
                       importer.tradeAgreements().tariffModifier(seller);
    return std::clamp(rate, 0.0f, MAX_IMPORT_TARIFF);
}

int32_t legCargoSlots(const TraderComponent& trader, MonetarySystemType system, bool onRail,
                      float tradeMult, const aoc::game::City& unloadingAt) {
    const int32_t base = trader.effectiveCargoSlots(system, onRail);
    int32_t slots      = std::max(1, static_cast<int32_t>(static_cast<float>(base) * tradeMult));
    if (trader.routeType == TradeRouteType::Sea && !unloadingAt.districts().hasDistrict(DistrictType::Harbor)) {
        slots = std::min(slots, SEA_SLOTS_WITHOUT_HARBOR);
    }
    return slots;
}

CurrencyAmount saleValueAt(const aoc::game::GameState& gameState, const Market& market,
                           const std::vector<TradeCargo>& cargo, const aoc::game::City& destination,
                           TradeRouteType routeType, float routeYield) {
    const float saleMult = destinationSaleMultiplier(destination, routeType);
    float value          = 0.0f;
    for (const TradeCargo& c : cargo) {
        const float gouge = gameState.monopoly().buyerPriceMultiplier(c.goodId, destination.owner());
        value += static_cast<float>(c.amount) * static_cast<float>(localPrice(market, c.goodId, destination)) *
                 gouge;
    }
    return static_cast<CurrencyAmount>(value * saleMult * routeYield);
}

namespace {

/// Select goods for trade by the spread between the two cities' local
/// prices: what sells dearer there than here, weighted by the surplus. With
/// no destination in view (a pickup reserved for a later leg) the market
/// price stands in, and so it does when nothing has a positive spread.
void selectTradeGoods(const aoc::game::City& origin,
                       const aoc::game::City* dest,
                       const Market& market,
                       std::vector<TradeCargo>& outCargo,
                       int32_t maxGoods) {
    outCargo.clear();
    const CityStockpileComponent& originStock = origin.stockpile();
    const CityStockpileComponent* destStock   = dest != nullptr ? &dest->stockpile() : nullptr;

    struct ScoredGood {
        uint16_t goodId;
        int32_t  surplus;
        float    score;
    };
    std::vector<ScoredGood> candidates;
    std::vector<ScoredGood> fallback;
    candidates.reserve(originStock.goods.size() + originStock.exportBuffer.size());

    // WP-O: scan buffer + stockpile combined. Buffer entries are "ready
    // for export" so they get scored first, but the loader pulls from
    // both transparently. Avoid double-counting by walking buffer first
    // and tracking which goodIds have been seen.
    auto scoreCandidate = [&](uint16_t gid, int32_t total) {
        if (total <= 1) { return; }
        const int32_t surplus = total - 1;
        const int32_t price   = std::max(1, market.marketData(gid).currentPrice);
        fallback.push_back({gid, surplus, static_cast<float>(surplus) * static_cast<float>(price)});
        if (dest == nullptr) { return; }
        const int32_t spread = localPrice(market, gid, *dest) - localPrice(market, gid, origin);
        if (spread > 0) {
            candidates.push_back({gid, surplus, static_cast<float>(surplus) * static_cast<float>(spread)});
        }
    };
    std::unordered_map<uint16_t, int32_t> combined;
    for (const std::pair<const uint16_t, int32_t>& entry : originStock.goods) {
        if (isCoinGood(entry.first)) { continue; } // money, not cargo: the sweep takes it
        combined[entry.first] += entry.second;
    }
    for (const std::pair<const uint16_t, int32_t>& entry : originStock.exportBuffer) {
        if (isCoinGood(entry.first)) { continue; }
        combined[entry.first] += entry.second;
    }
    for (const std::pair<const uint16_t, int32_t>& entry : combined) {
        scoreCandidate(entry.first, entry.second);
    }
    if (candidates.empty()) {
        candidates.swap(fallback);
    }

    // Total order: goodId breaks score ties. `candidates` is built by
    // iterating an unordered_map, so a score-only comparator left equal-score
    // goods in hash-iteration order -- deterministic on one STL build but not
    // portable, which the maxGoods cutoff below then turns into a
    // platform-dependent choice of which goods trade. The tie-break makes the
    // selected set independent of map iteration order.
    std::sort(candidates.begin(), candidates.end(),
        [](const ScoredGood& a, const ScoredGood& b) {
            if (a.score != b.score) { return a.score > b.score; }
            return a.goodId < b.goodId;
        });

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
    selectTradeGoods(seller, /*dest*/ nullptr, market, planned, slots);

    trader.pendingPickupCargo.clear();
    trader.pendingPickupCargo.reserve(planned.size());
    for (const TradeCargo& c : planned) {
        if (c.amount <= 0) { continue; }
        sellerStock.commitToExport(c.goodId, c.amount);
        trader.pendingPickupCargo.push_back(c);
    }
    trader.pickupCityLocation = seller.location();
}

/// Map of city tile -> City* across all players. Built once per turn at
/// the top of processTradeRoutes (or per call to establishTradeRoute) so
/// every downstream lookup is O(1) instead of a fresh O(players × cities)
/// scan. Audit Warning (TradeRouteSystem.cpp:247,263,436,1060,1326):
/// findCityByLocation + longestRangeGap walked all players × cities per
/// tile per trader per turn.
using CityRelayMap = std::unordered_map<aoc::hex::AxialCoord, aoc::game::City*>;

CityRelayMap buildCityRelay(aoc::game::GameState& gameState) {
    CityRelayMap relay;
    std::size_t totalCities = 0;
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        if (p == nullptr) { continue; }
        totalCities += p->cities().size();
    }
    relay.reserve(totalCities * 2);
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        if (p == nullptr) { continue; }
        for (const std::unique_ptr<aoc::game::City>& c : p->cities()) {
            if (c == nullptr) { continue; }
            relay.emplace(c->location(), c.get());
        }
    }
    return relay;
}

/// Find a city by its location across all players (fallback for cold
/// paths). Hot paths under processTradeRoutes / establishTradeRoute go
/// through the pre-built CityRelayMap directly; this helper exists only
/// for the rare trader-death path (releasePickupReservation) where
/// building the map would cost more than the single lookup.
static aoc::game::City* findCityByLocation(aoc::game::GameState& gameState,
                                            aoc::hex::AxialCoord location) {
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        if (p == nullptr) { continue; }
        aoc::game::City* c = p->cityAt(location);
        if (c != nullptr) { return c; }
    }
    return nullptr;
}

/// Same lookup using the precomputed relay map. Returns nullptr if the
/// location holds no city.
static aoc::game::City* lookupCity(const CityRelayMap& relay,
                                    aoc::hex::AxialCoord location) {
    CityRelayMap::const_iterator it = relay.find(location);
    return (it == relay.end()) ? nullptr : it->second;
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
///
/// EntityId is a legacy positional handle, not a stable slot+generation key:
/// id.index is the unit's ordinal position when all players' unit vectors are
/// concatenated in player order (the n-th unit overall maps to EntityId{n}),
/// matching the same convention used by Combat.cpp's and Movement.cpp's
/// findUnitByEntity. Each player's unit count is read individually via
/// units.size() -- this must NOT assume every player holds the same number
/// of units, since per-player unit counts differ and change every turn.
aoc::game::Unit* findTraderByEntityId(aoc::game::GameState& gameState, EntityId id) {
    if (!id.isValid()) { return nullptr; }
    uint32_t remaining = id.index;
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        if (p == nullptr) { continue; }
        const std::vector<std::unique_ptr<aoc::game::Unit>>& units = p->units();
        const uint32_t count = static_cast<uint32_t>(units.size());
        if (remaining < count) {
            return units[static_cast<std::size_t>(remaining)].get();
        }
        remaining -= count;
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
        // A route needs both ends willing, so either side's embargo blocks it.
        if (diplomacy->hasAnyEmbargo(proposer, target)) {
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
namespace {

[[nodiscard]] int32_t unitPrice(const Market& market, uint16_t goodId) {
    return std::max(1, market.marketData(goodId).currentPrice);
}

/// Take goods worth `value` at market prices out of `cargo` (priciest first)
/// into `into` when there is one. Returns the value taken.
CurrencyAmount takeCargoWorth(std::vector<TradeCargo>& cargo, const Market& market,
                              CurrencyAmount value, CityStockpileComponent* into) {
    std::stable_sort(cargo.begin(), cargo.end(), [&market](const TradeCargo& a, const TradeCargo& b) {
        const int32_t pa = unitPrice(market, a.goodId);
        const int32_t pb = unitPrice(market, b.goodId);
        return pa != pb ? pa > pb : a.goodId < b.goodId;
    });
    CurrencyAmount taken = 0;
    for (std::vector<TradeCargo>::iterator it = cargo.begin(); it != cargo.end() && taken < value;) {
        const int32_t price = unitPrice(market, it->goodId);
        const int32_t units = static_cast<int32_t>(
            std::min<CurrencyAmount>(it->amount, (value - taken + price - 1) / price));
        if (into != nullptr && units > 0) {
            into->addGoods(it->goodId, units);
        }
        it->amount -= units;
        taken += static_cast<CurrencyAmount>(units) * price;
        it = it->amount <= 0 ? cargo.erase(it) : it + 1;
    }
    return taken;
}

/// Take goods worth `value` at market prices out of a stockpile (priciest
/// first, never coin) onto a trader, one cargo slot per good, `slots` at
/// most. Returns the value taken.
CurrencyAmount takeGoodsWorth(CityStockpileComponent& from, const Market& market, CurrencyAmount value,
                              std::vector<TradeCargo>& cargo, int32_t slots) {
    std::vector<std::pair<uint16_t, int32_t>> held;
    for (const std::pair<const uint16_t, int32_t>& entry : from.goods) {
        if (entry.second > 0 && !isCoinGood(entry.first)) {
            held.emplace_back(entry.first, entry.second);
        }
    }
    std::sort(held.begin(), held.end(), [&market](const std::pair<uint16_t, int32_t>& a,
                                                  const std::pair<uint16_t, int32_t>& b) {
        const int32_t pa = unitPrice(market, a.first);
        const int32_t pb = unitPrice(market, b.first);
        return pa != pb ? pa > pb : a.first < b.first;
    });
    CurrencyAmount taken = 0;
    for (const std::pair<uint16_t, int32_t>& good : held) {
        if (taken >= value || slots <= 0) {
            break;
        }
        const int32_t price = unitPrice(market, good.first);
        const int32_t units = static_cast<int32_t>(
            std::min<CurrencyAmount>(good.second, (value - taken + price - 1) / price));
        if (units <= 0 || !from.consumeGoods(good.first, units)) {
            continue;
        }
        cargo.push_back({good.first, units});
        --slots;
        taken += static_cast<CurrencyAmount>(units) * price;
    }
    return taken;
}

} // namespace

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
static int32_t computeTotalTradeSlotsImpl(const aoc::game::Player& player,
                                           const aoc::map::HexGrid& grid) {
    // The regime's slots, plus the ones every other system declared and
    // nobody added (plan 3.3): used Great Merchants and civics
    // (extraTradeSlots), the government and its policies, the civ's trait,
    // and standing trade agreements.
    int32_t total = player.monetary().maxTradeRoutes()
                  + player.greatPeople().extraTradeSlots
                  + computeGovernmentModifiers(player.government()).extraTradeRoutes
                  + civDef(player.civId()).modifiers.extraTradeRoutes
                  + player.tradeAgreements().bonusTradeRoutes();
    for (const std::unique_ptr<aoc::game::City>& cityPtr : player.cities()) {
        // A city that revolted or went free stays in this vector with another
        // owner; its market is not ours to route through.
        if (cityPtr == nullptr || cityPtr->owner() != player.id()) { continue; }
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
/// considered relay points. Audit Warning hot-path fix: takes a precomputed
/// CityRelayMap so the inner check is O(1) per tile instead of an O(players
/// × cities) scan.
int32_t longestRangeGap(const std::vector<aoc::hex::AxialCoord>& path,
                         const aoc::map::HexGrid& grid,
                         const CityRelayMap& cityRelay) {
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
        if (!isRelay && cityRelay.find(next) != cityRelay.end()) {
            isRelay = true;
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

int32_t computeTotalTradeSlots(const aoc::game::Player& player,
                                const aoc::map::HexGrid& grid) {
    return computeTotalTradeSlotsImpl(player, grid);
}

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
            return ErrorCode::TradeRouteRefusedConsent;
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
        // A city that went free stays in its old holder's list; a Trader
        // cannot leave from a city its owner no longer holds.
        if (c == nullptr || c->owner() != traderUnit->owner()) { continue; }
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
        const int32_t cap = computeTotalTradeSlotsImpl(*ownerPlayer, grid);
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
            return ErrorCode::TradeRouteCapReached;
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

    bool originIsCoastal = grid.isCoastal(originCity->location());
    bool destIsCoastal   = grid.isCoastal(destCity->location());

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

    // A blockaded port signs no new sea contracts (plan 3.4). The component
    // was already claimed above, so hand the Trader back its idle state:
    // a claimed Trader with no path is invisible to the idle scan, holds a
    // route slot for ever, and is processed every turn as a phantom arrival.
    if (trader.routeType == TradeRouteType::Sea &&
        (originCity->combat().blockadedBy != INVALID_PLAYER ||
         destCity->combat().blockadedBy != INVALID_PLAYER)) {
        LOG_INFO("Trade route rejected: a blockade closes the lane between %s and %s",
                 originCity->name().c_str(), destCity->name().c_str());
        trader       = TraderComponent{};
        trader.owner = INVALID_PLAYER;
        return ErrorCode::TradeRouteBlockaded;
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
                // Guard against a zero-length canal path producing Inf/NaN
                // (savedTiles > 0 can hold while canalLen == 0).
                float goldPerTurn = ESTIMATED_CARGO_VALUE
                    / (static_cast<float>(std::max(1, canalLen)) * 2.0f / SEA_SPEED);
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
            // One-shot CityRelayMap for the range check. Establishing a
            // trade route is rare (vs. processTradeRoutes per-trader-per-turn),
            // so the per-call build is cheaper than threading the map in
            // through the public API.
            const CityRelayMap relay = buildCityRelay(gameState);
            const int32_t longestSegment = longestRangeGap(
                trader.path, grid, relay);
            if (longestSegment > maxRange) {
                LOG_INFO("Trade route rejected: P%u %s longest segment %d > range %d "
                         "(build a Trading Post or extend tech)",
                         static_cast<unsigned>(traderUnit->owner()),
                         (trader.routeType == TradeRouteType::Land ? "land" :
                          trader.routeType == TradeRouteType::Sea  ? "sea"  : "air"),
                         longestSegment, maxRange);
                return ErrorCode::TradeRouteOutOfRange;
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
    aoc::game::Player* ownerPtrForCargo = gameState.player(trader.owner);
    const MonetarySystemType ownerSys = (ownerPtrForCargo != nullptr)
        ? ownerPtrForCargo->monetary().system
        : MonetarySystemType::Barter;
    const bool railOutbound = pathOnRail(trader, grid);
    // REVOLUTION_DEFS names this column tradeCapacityMultiplier: an industrial
    // age widens what a route can haul. cumulativeTradeMultiplier had no caller,
    // so that column of the table was inert while its production and gold
    // siblings both reached the game.
    const float tradeMult = (ownerPtrForCargo != nullptr)
                                ? ownerPtrForCargo->industrial().cumulativeTradeMultiplier()
                                : 1.0f;
    const int32_t cargoSlots = legCargoSlots(trader, ownerSys, railOutbound, tradeMult, *destCity);
    selectTradeGoods(*originCity, destCity, market, trader.cargo, cargoSlots);
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
                return ErrorCode::TradeRouteNoFuel;
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
    // Audit hot-path fix: build the city relay map ONCE per turn and reuse
    // it for every trader-arrival lookup below. The previous code did
    // O(players × cities) per arrival per trader.
    const CityRelayMap cityRelay = buildCityRelay(gameState);

    // Customs are cleared here, at the start of the trade step, not at the top
    // of the turn: the income breakdown runs BEFORE this step, so a counter
    // cleared at the top of the turn would always read zero by the time
    // anything asked for it. It therefore reports the turn just gone.
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        if (p != nullptr) {
            p->setTariffsLastTurn(0);
        }
    }

    // Collect all active trader units across all players
    std::vector<aoc::game::Unit*> traderUnits;
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        for (const std::unique_ptr<aoc::game::Unit>& u : p->units()) {
            if (u->typeDef().unitClass != UnitClass::Trader) {
                continue;
            }
            // Per-turn scratch, cleared for idle Traders too: the income
            // breakdown sums it as this turn's route gold, and a Trader that
            // came home and went idle would otherwise report its last sale forever.
            u->trader().goldEarnedThisTurn = 0;
            u->trader().coinLandedThisTurn = 0;
            if (u->trader().owner != INVALID_PLAYER) {
                traderUnits.push_back(u.get());
            }
        }
    }

    std::vector<aoc::game::Unit*> toRemove;

    for (aoc::game::Unit* unitPtr : traderUnits) {
        TraderComponent& trader = unitPtr->trader();

        ++trader.turnsActive;
        trader.tollPaidThisTurn = 0;

        // A blockade closes the lane: a Sea route waits at anchor, and gives
        // up if the fleet does not leave (plan 3.4). Land and Air routes go
        // round it. Decided before the transit tolls below, because a ship at
        // anchor enters nobody's water and owes nobody passage.
        if (trader.routeType == TradeRouteType::Sea) {
            const aoc::game::City* originCity = lookupCity(cityRelay, trader.originCityLocation);
            const aoc::game::City* destCity   = lookupCity(cityRelay, trader.destCityLocation);
            const int32_t blockedTurns =
                std::max(originCity != nullptr ? originCity->combat().blockadedTurns : 0,
                         destCity != nullptr ? destCity->combat().blockadedTurns : 0);
            if (blockedTurns >= BLOCKADE_ABANDON_TURNS) {
                LOG_WARN("Trader P%u abandoned: the lane has been blockaded %d turns",
                         static_cast<unsigned>(trader.owner), blockedTurns);
                toRemove.push_back(unitPtr);
                continue;
            }
            if (blockedTurns > 0) {
                continue; // at anchor: no passage, no movement, no arrival
            }
        }

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
                // Paid on the road: out of the purse first, then out of the
                // cargo at market prices into the toll-keeper's first city.
                aoc::game::Player* tollReceiver = gameState.player(te.owner);
                if (tollReceiver != nullptr) {
                    const CurrencyAmount fromPurse = std::min(totalToll, trader.carriedGold);
                    trader.carriedGold -= fromPurse;
                    tollReceiver->addGold(fromPurse, aoc::sim::MoneyFlow::transfer(trader.owner));
                    CityStockpileComponent* tollCity =
                        tollReceiver->cities().empty() ? nullptr : &tollReceiver->cities().front()->stockpile();
                    totalToll = fromPurse + takeCargoWorth(trader.cargo, market, totalToll - fromPurse, tollCity);
                }
                trader.tollPaidThisTurn += totalToll;

                // Reputation: +1 for honoring toll. `diplomacy` is already
                // a non-const pointer at the function signature; the prior
                // const_cast was a stale leftover.
                if (diplomacy != nullptr) {
                    diplomacy->addReputationModifier(
                        trader.owner, te.owner, 1, 20);
                }
            } else {
                // Refuse toll and pass through anyway: -5 reputation
                if (diplomacy != nullptr) {
                    diplomacy->addReputationModifier(
                        trader.owner, te.owner, -5, 30);
                }

                // Notify territory owner of refusal
                LOG_INFO("Player %u's trader refused %lld toll from Player %u and passed through",
                         static_cast<unsigned>(trader.owner),
                         static_cast<long long>(totalToll),
                         static_cast<unsigned>(te.owner));

                // Pre-format the notification body with snprintf so we don't
                // pay for std::to_string + 3 std::string concatenations on
                // every refused toll on the per-trader-per-turn hot path.
                char notifBody[160];
                std::snprintf(notifBody, sizeof(notifBody),
                              "A trade caravan from Player %u refused your toll "
                              "and passed through your territory.",
                              static_cast<unsigned>(trader.owner));
                aoc::sim::event::pushNotification({
                    aoc::sim::event::NotificationCategory::Diplomacy,
                    "Toll Refused",
                    notifBody,
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
        aoc::game::City* targetCity = lookupCity(cityRelay, targetLoc);

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

            // The sale (plan 3.2): priced at the destination's local prices
            // before the goods land, with the monopolist's markup, the
            // destination's commerce and the route's yield, the same
            // function the preview uses.
            const int32_t routeDist = grid.distance(trader.originCityLocation, trader.destCityLocation);
            const float routeYield =
                routeYieldMultiplier(gameState, diplomacy, trader.owner, cityOwner, routeDist);
            CurrencyAmount goldEarned =
                saleValueAt(gameState, market, trader.cargo, *targetCity, trader.routeType, routeYield);
            int32_t totalUnits = 0;
            for (const TradeCargo& c : trader.cargo) {
                targetStock.addGoods(c.goodId, c.amount);
                totalUnits += c.amount;
                // Squeezing is not free. The buyer resents the civ that cornered
                // the good, which is what makes the markup a decision rather
                // than free gold -- see aiChooseMonopolyPrices. Charged on the
                // delivery so it follows real transactions, and addGrievance
                // dedups by (type, against), so a standing markup refreshes one
                // grievance instead of stacking a new one every shipment.
                const float gouge = gameState.monopoly().buyerPriceMultiplier(c.goodId, cityOwner);
                if (gouge > 1.0f && cityOwner != INVALID_PLAYER
                    && cityOwner < aoc::sim::CITY_STATE_PLAYER_BASE) {
                    const PlayerId squeezer = gameState.monopoly().monopolistOf(c.goodId);
                    aoc::game::Player* buyerPlayer = gameState.player(cityOwner);
                    if (buyerPlayer != nullptr && squeezer != INVALID_PLAYER
                        && squeezer != cityOwner
                        && squeezer < aoc::sim::CITY_STATE_PLAYER_BASE) {
                        buyerPlayer->grievances().addGrievance(
                            GrievanceType::PriceGouged, squeezer);
                    }
                }
            }

            // WP-K3: throughput log per route type for audit ratio analysis.
            const char* routeTag =
                (trader.routeType == TradeRouteType::Land) ? "Land"
              : (trader.routeType == TradeRouteType::Sea)  ? "Sea"  : "Air";
            LOG_INFO("Trade delivery: P%u %s %d units cargo, %lld gold",
                     static_cast<unsigned>(trader.owner),
                     routeTag, totalUnits,
                     static_cast<long long>(goldEarned));

            // Settlement (plan 2.4): the buyer's people pay the price in specie
            // into the trader's purse; what they cannot pay they owe in goods
            // (fetched below, once the return cargo is loaded). A city-state
            // buys with money from beyond the ledger. The purse lands when the
            // coin is home: the customs share to the treasury, the rest to the
            // merchants, or to bullion for a civ still in Barter.
            aoc::game::Player* sellerPlayer = gameState.player(trader.owner);
            CurrencyAmount owedInGoods      = 0;
            // What the purse already held when it reached this city is what
            // came from abroad; anything this leg adds is our own people
            // paying us (M3 counts the former).
            const CurrencyAmount purseFromAbroad = trader.carriedGold;
            if (sellerPlayer != nullptr && goldEarned > 0) {
                CurrencyAmount paid = 0;
                if (cityOwner >= aoc::sim::CITY_STATE_PLAYER_BASE) {
                    paid = goldEarned;
                    if (aoc::sim::MoneyLedger* book = sellerPlayer->moneyLedger(); book != nullptr) {
                        book->record(trader.owner, aoc::sim::MoneyFlow::external(), paid);
                    }
                } else if (aoc::game::Player* buyerPlayer = gameState.player(cityOwner); buyerPlayer != nullptr) {
                    // Two rungs and a fallback (plan B2): trusted paper between
                    // paper regimes, at the capped exchange rate; else coin; and
                    // goods for what neither covers. A purse holds one medium.
                    const bool notes = trader.carriedGold > 0
                                           ? trader.carriedMedium == 1
                                           : settlesInNotes(*sellerPlayer, *buyerPlayer);
                    if (notes) {
                        const CurrencyAmount inNotes = static_cast<CurrencyAmount>(
                            static_cast<float>(goldEarned) * settlementRate(*sellerPlayer, *buyerPlayer));
                        paid = payInNotes(*buyerPlayer, inNotes);
                        // Owed in goods is measured in price, not notes.
                        owedInGoods = goldEarned - static_cast<CurrencyAmount>(
                                                       static_cast<float>(paid) /
                                                       settlementRate(*sellerPlayer, *buyerPlayer));
                        trader.carriedMedium = 1;
                    } else {
                        paid                 = payInSpecie(*buyerPlayer, goldEarned);
                        owedInGoods          = goldEarned - paid;
                        trader.carriedMedium = 0;
                    }
                    if (trader.owner >= aoc::sim::CITY_STATE_PLAYER_BASE) {
                        bookExternal(*buyerPlayer, -paid); // a city-state's trader carries it out of the world
                    }
                    trader.carriedGold += paid;
                    // Customs (plan 3.3): the importer's tariff comes out of the
                    // purse into its treasury, a transfer, raised by its
                    // tariffEfficiency (Mercantilism). Between major civs only.
                    if (trader.owner != cityOwner && trader.owner < aoc::sim::CITY_STATE_PLAYER_BASE) {
                        // The premium multiplies the rate, but the ceiling is
                        // on what customs may actually take.
                        const float efficiency =
                            1.0f + computeGovernmentModifiers(buyerPlayer->government()).tariffEfficiency;
                        const float rate = std::clamp(
                            importTariffRate(*buyerPlayer, trader.owner) * efficiency, 0.0f, MAX_IMPORT_TARIFF);
                        const CurrencyAmount tariff = std::min(
                            trader.carriedGold,
                            static_cast<CurrencyAmount>(static_cast<float>(paid) * rate));
                        if (tariff > 0) {
                            trader.carriedGold -= tariff;
                            buyerPlayer->addGold(tariff, aoc::sim::MoneyFlow::transfer(trader.owner));
                            buyerPlayer->setTariffsLastTurn(buyerPlayer->tariffsLastTurn() + tariff);
                        }
                    }
                    paid = -1; // handled
                }
                if (paid >= 0) {
                    trader.carriedGold += paid;
                    owedInGoods = goldEarned - paid;
                }
            }
            if (sellerPlayer != nullptr && (trader.isReturning || cityOwner == trader.owner)) {
                // M3 counts coin brought home from abroad; a sale to our own
                // people is a tax on them, not trade income.
                trader.coinLandedThisTurn = purseFromAbroad;
                trader.goldEarnedThisTurn =
                    receiveTradeCoin(*sellerPlayer, trader.carriedGold, trader.carriedMedium == 1);
                trader.carriedGold        = 0;
                trader.carriedMedium      = 0;
                owedInGoods               = 0; // our own people, delivering to themselves
            }

            // Record the sale as an export for the seller and an import for the
            // buyer, which is what ForexMarket means by `tradeBalance`: a civ
            // selling more abroad than it buys sees its currency firm.
            //
            // The only other writer of tradeBalance was the legacy
            // settleTradeInCoins, deleted 2026-09-11: it settled a route list
            // only the human's trade screen filled, so over 500 turns of seed
            // 42 it settled exactly zero payments. Traders are how everyone
            // trades, so the channel has to be fed from here to exist at all.
            if (goldEarned > 0
                && trader.owner != cityOwner
                && trader.owner != INVALID_PLAYER && cityOwner != INVALID_PLAYER
                && trader.owner < aoc::sim::CITY_STATE_PLAYER_BASE
                && cityOwner < aoc::sim::CITY_STATE_PLAYER_BASE) {
                aoc::game::Player* buyerPlayer = gameState.player(cityOwner);
                if (sellerPlayer != nullptr && buyerPlayer != nullptr) {
                    sellerPlayer->currencyExchange().tradeBalance += goldEarned;
                    buyerPlayer->currencyExchange().tradeBalance  -= goldEarned;
                }

                // And the two courts notice (plan 4.1): a standing trade tie is
                // worth a standing goodwill, refreshed by each delivery and
                // decayed by tickModifiers once the deliveries stop.
                if (diplomacy != nullptr) {
                    const int32_t worth = std::min(
                        TRADE_PARTNER_MAX,
                        static_cast<int32_t>(goldEarned / TRADE_PARTNER_VALUE_DIVISOR) +
                            TRADE_PARTNER_PER_ROUTE * routesBetween(gameState, trader.owner, cityOwner));
                    diplomacy->refreshModifier(trader.owner, cityOwner, TRADE_PARTNER_REASON, worth,
                                               TRADE_PARTNER_TURNS);
                }
            }

            // WP-O: load from this city's exportBuffer using the reservation
            // committed when the leg started. Capacity capped by money weight.
            const MonetarySystemType sellerSys = (sellerPlayer != nullptr)
                ? sellerPlayer->monetary().system
                : MonetarySystemType::Barter;
            const bool railReturn = pathOnRail(trader, grid);
            const aoc::hex::AxialCoord nextUnloadLoc =
                trader.isReturning ? trader.destCityLocation : trader.originCityLocation;
            const aoc::game::City* nextUnload = lookupCity(cityRelay, nextUnloadLoc);
            const float sellerTradeMult =
                sellerPlayer != nullptr ? sellerPlayer->industrial().cumulativeTradeMultiplier() : 1.0f;
            const int32_t returnSlots =
                nextUnload != nullptr
                    ? legCargoSlots(trader, sellerSys, railReturn, sellerTradeMult, *nextUnload)
                    : trader.effectiveCargoSlots(sellerSys, railReturn);
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

            // Goods for goods: what the buyer could not pay in coin it pays in
            // kind, as far as the return cargo has room.
            if (owedInGoods > 0) {
                takeGoodsWorth(targetStock, market, owedInGoods, trader.cargo, returnSlots - loaded);
            }
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
                        // Capture the in-progress civic before advancing: once
                        // completeResearch() runs inside advanceCivicResearch,
                        // currentResearch no longer names the civic that just
                        // finished, so the id must be read beforehand.
                        const CivicId civicBeforeAdvance = targetCivic.currentResearch;
                        const bool civicCompleted = advanceCivicResearch(
                            targetCivic, cBoost, &targetPlayer->government());
                        if (civicCompleted && civicBeforeAdvance.isValid()) {
                            applyCivicEffect(gameState, cityOwner,
                                              static_cast<uint8_t>(civicBeforeAdvance.value));
                        }
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
        std::reverse(trader.path.begin(), trader.path.end());
        trader.pathIndex = 0;

        // WP-O: reserve next-arrival city's goods now (during this leg's
        // transit). Picks up at next arrival via pendingPickupCargo.
        const aoc::hex::AxialCoord nextPickupLoc = trader.isReturning
            ? trader.originCityLocation : trader.destCityLocation;
        aoc::game::City* nextPickup = lookupCity(cityRelay, nextPickupLoc);
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

CurrencyAmount lootTraderCargo(aoc::game::GameState& gameState,
                               aoc::game::Unit& traderUnitRef,
                               PlayerId pillager) {
    aoc::game::Unit* traderUnit = &traderUnitRef;

    // WP-O: pillaged trader can't pick up. Release the seller's reservation.
    releasePickupReservation(gameState, traderUnit->trader());

    TraderComponent& trader = traderUnit->trader();

    // Calculate cargo value
    CurrencyAmount totalValue = 0;
    for (const TradeCargo& c : trader.cargo) {
        totalValue += static_cast<CurrencyAmount>(c.amount) * 3;  // Loot value
    }

    // The crates go to the pillager's first city; the purse goes to the
    // soldiers who took it (the pillager's people). A barbarian purse leaves
    // the world: the owner's loss.
    aoc::game::Player* pillagerPlayer =
        pillager == BARBARIAN_PLAYER ? nullptr : gameState.player(pillager);
    const CurrencyAmount stolenGold = trader.carriedGold;
    if (pillagerPlayer != nullptr && !pillagerPlayer->cities().empty()) {
        CityStockpileComponent& stock = pillagerPlayer->cities().front()->stockpile();
        for (const TradeCargo& c : trader.cargo) {
            stock.addGoods(c.goodId, c.amount);
        }
    }
    aoc::game::Player* victim = gameState.player(trader.owner);
    if (pillagerPlayer != nullptr && pillager < aoc::sim::CITY_STATE_PLAYER_BASE) {
        giveToPrivate(*pillagerPlayer, stolenGold, trader.carriedMedium == 1);
    } else if (pillagerPlayer != nullptr && victim != nullptr) {
        bookExternal(*victim, -stolenGold); // a city-state's soldiers: out of the world
    } else if (victim != nullptr) {
        loseCoin(*victim, stolenGold);
    }
    trader.carriedGold = 0;
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

    return totalValue;
}

int32_t cancelRoutesToCity(aoc::game::GameState& gameState, aoc::hex::AxialCoord at,
                           PlayerId newOwner) {
    int32_t cancelled = 0;
    // Collect first, act after: removeUnit frees the unique_ptr storage and
    // would invalidate the iteration.
    struct Doomed {
        aoc::game::Player* owner;
        aoc::game::Unit* unit;
    };
    std::vector<Doomed> toRemove;

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) {
            continue;
        }
        for (const std::unique_ptr<aoc::game::Unit>& unitPtr : playerPtr->units()) {
            if (unitPtr == nullptr || unitPtr->typeDef().unitClass != UnitClass::Trader) {
                continue;
            }
            TraderComponent& trader = unitPtr->trader();
            const bool destTaken    = (trader.destCityLocation == at);
            const bool originTaken  = (trader.originCityLocation == at);
            if (!destTaken && !originTaken) {
                continue;
            }
            // The new owner's own traders now run an internal route. Leave them.
            if (trader.owner == newOwner) {
                continue;
            }
            // Already ended by an earlier change of hands: heading home with no
            // renewal left to stop. Skipping keeps the count honest when a city
            // is taken twice.
            if (destTaken && trader.isReturning && !unitPtr->autoRenewRoute) {
                continue;
            }

            releasePickupReservation(gameState, trader);
            // A route to a city someone else now holds must not renew itself
            // against the old partner.
            unitPtr->autoRenewRoute = false;
            trader.destOwner        = newOwner;
            ++cancelled;

            if (originTaken) {
                // Its home is gone: there is nowhere to carry the cargo back to.
                toRemove.push_back({playerPtr.get(), unitPtr.get()});
                continue;
            }
            // Outbound to a city that just fell: turn around and carry the cargo
            // home rather than handing it to the conqueror.
            if (!trader.isReturning) {
                trader.isReturning = true;
                std::reverse(trader.path.begin(), trader.path.end());
                trader.pathIndex = 0;
            }
        }
    }

    for (const Doomed& d : toRemove) {
        d.owner->removeUnit(d.unit);
    }
    if (cancelled > 0) {
        LOG_INFO("City (%d,%d) changed hands: %d trade route(s) ended", at.q, at.r, cancelled);
    }
    return cancelled;
}

CurrencyAmount pillageTrader(aoc::game::GameState& gameState, EntityId traderEntity,
                             PlayerId pillager) {
    aoc::game::Unit* traderUnit = findTraderByEntityId(gameState, traderEntity);
    if (traderUnit == nullptr) {
        return 0;
    }
    const CurrencyAmount looted = lootTraderCargo(gameState, *traderUnit, pillager);
    aoc::game::Player* traderOwner = gameState.player(traderUnit->owner());
    if (traderOwner != nullptr) {
        traderOwner->removeUnit(traderUnit);
    }
    return looted;
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
                        // encampments() is an unordered_map, so break need ties
                        // by lowest tile index for an order-independent depot
                        // pick. bestIdx starts at -1 (below every real index),
                        // so the tie branch can only fire once a real winner is
                        // set -- matching the old strict-'>' idle behaviour.
                        if (need > bestNeed
                            || (need == bestNeed && kv.first < bestIdx)) {
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
    const aoc::game::City& destCity,
    const DiplomacyManager* diplomacy) {

    TradeRouteEstimate estimate{};

    // Find origin city (closest owned city to the Trader)
    const aoc::game::Player* ownerPlayer = gameState.player(traderUnit.owner());
    if (ownerPlayer == nullptr || ownerPlayer->ownedCityCount() == 0) {
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

    bool originIsCoastal = grid.isCoastal(originCity->location());
    bool destIsCoastal   = grid.isCoastal(destCity.location());

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

    // The preview is the sale: the cargo establishTradeRoute would choose,
    // sold by the function the delivery uses (plan 3.2), so what the screen
    // shows is what the purse brings home when nothing changes in between.
    // Rail is not assumed: the path is not known here.
    const MonetarySystemType estSys = ownerPlayer->monetary().system;
    const int32_t maxSlots =
        legCargoSlots(tempTrader, estSys, false, ownerPlayer->industrial().cumulativeTradeMultiplier(), destCity);
    std::vector<TradeCargo> cargo;
    selectTradeGoods(*originCity, &destCity, market, cargo, maxSlots);
    const float routeYield =
        routeYieldMultiplier(gameState, diplomacy, traderUnit.owner(), destCity.owner(), straightDist);
    const CurrencyAmount grossGold =
        saleValueAt(gameState, market, cargo, destCity, estimate.routeType, routeYield);

    // The medium is part of the price: a sale settled in trusted paper hands
    // over notes at the exchange rate, and both the purse and the customs are
    // in that money (plan 2.6). A coin sale leaves this at 1.
    CurrencyAmount purse = grossGold;
    if (destCity.owner() != traderUnit.owner() && destCity.owner() < CITY_STATE_PLAYER_BASE &&
        ownerPlayer != nullptr) {
        if (const aoc::game::Player* buyer = gameState.player(destCity.owner());
            buyer != nullptr && settlesInNotes(*ownerPlayer, *buyer)) {
            purse = static_cast<CurrencyAmount>(static_cast<float>(grossGold) *
                                                settlementRate(*ownerPlayer, *buyer));
        }
    }

    // The importer's customs come out of the purse at delivery (plan 3.3).
    CurrencyAmount expectedTariff = 0;
    if (destCity.owner() != traderUnit.owner() && destCity.owner() < CITY_STATE_PLAYER_BASE &&
        traderUnit.owner() < CITY_STATE_PLAYER_BASE) {
        if (const aoc::game::Player* importer = gameState.player(destCity.owner()); importer != nullptr) {
            const float efficiency = 1.0f + computeGovernmentModifiers(importer->government()).tariffEfficiency;
            const float rate = std::clamp(
                importTariffRate(*importer, traderUnit.owner()) * efficiency, 0.0f, MAX_IMPORT_TARIFF);
            expectedTariff = static_cast<CurrencyAmount>(static_cast<float>(purse) * rate);
        }
    }

    // C31: AI was booking routes at gross value. Subtract expected tolls so
    // the utility score matches realized profit — keeps AI from signing
    // negative-EV routes when partner raised their rate.
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
    estimate.estimatedGoldPerTrip = purse - expectedTolls - expectedTariff;

    return estimate;
}

} // namespace aoc::sim

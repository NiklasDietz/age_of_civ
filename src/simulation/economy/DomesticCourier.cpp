/**
 * @file DomesticCourier.cpp
 * @brief Implementation of player-dispatched domestic goods couriers.
 */

#include "aoc/simulation/economy/DomesticCourier.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Pathfinding.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/core/Types.hpp"

#include <algorithm>
#include <limits>

namespace aoc::sim {

namespace {

constexpr uint16_t COURIER_TYPE_ID = 32;

bool isCourierUnit(const aoc::game::Unit& unit) {
    return unit.typeId().value == COURIER_TYPE_ID;
}

} // namespace

int32_t courierSlots(const aoc::game::City& city) {
    switch (city.stage()) {
        case aoc::game::CitySize::Hamlet:  return 0;
        case aoc::game::CitySize::Village: return 1;
        case aoc::game::CitySize::Town:    return 2;
        case aoc::game::CitySize::City:    return 3;
    }
    return 0;
}

int32_t stockpileCap(const aoc::game::City& city) {
    switch (city.stage()) {
        case aoc::game::CitySize::Hamlet:  return 50;
        case aoc::game::CitySize::Village: return 200;
        case aoc::game::CitySize::Town:    return 500;
        case aoc::game::CitySize::City:    return 1500;
    }
    return 50;
}

int32_t countActiveCouriersFrom(const aoc::game::GameState& gameState,
                                 PlayerId owner,
                                 aoc::hex::AxialCoord originLoc) {
    const aoc::game::Player* player = gameState.player(owner);
    if (player == nullptr) {
        return 0;
    }
    int32_t count = 0;
    for (const std::unique_ptr<aoc::game::Unit>& unitPtr : player->units()) {
        if (unitPtr == nullptr) { continue; }
        if (!isCourierUnit(*unitPtr)) { continue; }
        const DomesticCourierComponent& c = unitPtr->courier();
        if (c.delivered) { continue; }
        if (c.originCityLocation == originLoc) {
            ++count;
        }
    }
    return count;
}

bool dispatchCourier(aoc::game::GameState& gameState,
                      const aoc::map::HexGrid& grid,
                      PlayerId owner,
                      aoc::hex::AxialCoord sourceCityLoc,
                      aoc::hex::AxialCoord destCityLoc,
                      uint16_t goodId,
                      int32_t quantity) {
    if (quantity <= 0) { return false; }
    if (sourceCityLoc == destCityLoc) { return false; }

    aoc::game::Player* player = gameState.player(owner);
    if (player == nullptr) { return false; }

    aoc::game::City* src = player->cityAt(sourceCityLoc);
    aoc::game::City* dst = player->cityAt(destCityLoc);
    if (src == nullptr || dst == nullptr) { return false; }

    // Slot budget
    const int32_t slots = courierSlots(*src);
    const int32_t active = countActiveCouriersFrom(gameState, owner, sourceCityLoc);
    if (active >= slots) {
        LOG_INFO("Courier dispatch refused: %s has no free slots (%d/%d)",
                 src->name().c_str(), active, slots);
        return false;
    }

    // Goods available
    CityStockpileComponent& srcStock = src->stockpile();
    const int32_t have = srcStock.getAmount(goodId);
    if (have < quantity) {
        LOG_INFO("Courier dispatch refused: %s has %d of good %u (need %d)",
                 src->name().c_str(), have, static_cast<unsigned>(goodId), quantity);
        return false;
    }

    // Path
    std::optional<aoc::map::PathResult> pathResult = aoc::map::findPath(
        grid, sourceCityLoc, destCityLoc, 0, &gameState, owner);
    if (!pathResult.has_value() || pathResult->path.empty()) {
        LOG_INFO("Courier dispatch refused: no path %s -> %s",
                 src->name().c_str(), dst->name().c_str());
        return false;
    }

    // Commit: remove goods from source stockpile, spawn unit, init component
    if (!srcStock.consumeGoods(goodId, quantity)) {
        return false;
    }

    aoc::game::Unit& unit = player->addUnit(UnitTypeId{COURIER_TYPE_ID}, sourceCityLoc);

    DomesticCourierComponent& cc = unit.courier();
    cc.owner = owner;
    cc.originCityLocation = sourceCityLoc;
    cc.destCityLocation = destCityLoc;
    cc.goodId = goodId;
    cc.quantity = quantity;
    cc.path = std::move(pathResult->path);
    cc.pathIndex = 0;
    cc.delivered = false;

    LOG_INFO("Courier dispatched: %s -> %s, good %u x%d (%zu hops)",
             src->name().c_str(), dst->name().c_str(),
             static_cast<unsigned>(goodId), quantity, cc.path.size());
    return true;
}

void processDomesticCouriers(aoc::game::GameState& gameState,
                              aoc::map::HexGrid& grid) {
    // Advance every courier by its movement allowance this turn, mark delivered
    // when it reaches destination, and deposit cargo.
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        // Hamlet pooling: each Hamlet has 0 courier slots so cannot dispatch.
        // Instead, any stockpile surplus above a small reserve flows to the
        // nearest Town/City owned by the same player.  Represents local peasant
        // caravans the player doesn't micromanage.  Two gates prevent the pool
        // from acting as a free teleporter:
        //   1. Distance cap (kHamletPoolMaxDist hexes) -- hamlets cut off from
        //      any town are stranded, not magically linked to the capital.
        //   2. Per-hex spoilage/tariff -- a fraction of the shipment is lost
        //      per hex travelled, so long hauls pay a cost.
        constexpr int32_t kHamletReserve        = 10;
        constexpr int32_t kHamletPoolMaxDist    = 8;
        constexpr float   kHamletPoolCostPerHex = 0.05f;
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }
            if (cityPtr->stage() != aoc::game::CitySize::Hamlet) { continue; }

            // Find nearest Town or City same owner.
            aoc::game::City* pool = nullptr;
            int32_t bestDist = std::numeric_limits<int32_t>::max();
            for (const std::unique_ptr<aoc::game::City>& other : playerPtr->cities()) {
                if (other == nullptr || other.get() == cityPtr.get()) { continue; }
                if (other->stage() == aoc::game::CitySize::Hamlet
                    || other->stage() == aoc::game::CitySize::Village) {
                    continue;
                }
                const int32_t d = grid.distance(cityPtr->location(), other->location());
                if (d < bestDist) { bestDist = d; pool = other.get(); }
            }
            if (pool == nullptr) { continue; }
            if (bestDist > kHamletPoolMaxDist) { continue; }

            const float keepFraction = std::max(
                0.0f, 1.0f - (kHamletPoolCostPerHex * static_cast<float>(bestDist)));

            CityStockpileComponent& src = cityPtr->stockpile();
            CityStockpileComponent& dst = pool->stockpile();
            for (auto& kv : src.goods) {
                const int32_t excess = kv.second - kHamletReserve;
                if (excess <= 0) { continue; }
                kv.second = kHamletReserve;
                const int32_t delivered = static_cast<int32_t>(
                    static_cast<float>(excess) * keepFraction);
                if (delivered > 0) {
                    dst.addGoods(kv.first, delivered);
                }
                // Remainder (excess - delivered) is spoilage en route, lost.
            }
        }

        // Zeroth pass: standing orders. For each city, attempt to re-dispatch
        // any persistent rules whose goods and slot budget are satisfied. Silent
        // failures are expected (slot busy, not enough stockpile yet).
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }
            const aoc::hex::AxialCoord srcLoc = cityPtr->location();
            for (const StandingOrder& order : cityPtr->standingOrders()) {
                if (order.batchSize <= 0) { continue; }
                if (order.destCityLocation == srcLoc) { continue; }
                dispatchCourier(gameState, grid, playerPtr->id(), srcLoc,
                                order.destCityLocation, order.goodId, order.batchSize);
            }
        }

        // First pass: advance + deliver.
        for (const std::unique_ptr<aoc::game::Unit>& unitPtr : playerPtr->units()) {
            if (unitPtr == nullptr) { continue; }
            if (!isCourierUnit(*unitPtr)) { continue; }

            DomesticCourierComponent& cc = unitPtr->courier();
            if (cc.delivered) { continue; }
            if (cc.path.empty()) {
                cc.delivered = true;  // Malformed, drop it.
                continue;
            }

            // War disruption / dest loss: abandon courier (cargo lost) if
            //   (a) the destination city is no longer owned by the courier's player, or
            //   (b) the tile the courier currently stands on is owned by another
            //       non-barbarian player (territory turned hostile mid-flight).
            if (playerPtr->cityAt(cc.destCityLocation) == nullptr) {
                LOG_INFO("Courier abandoned: destination lost");
                cc.delivered = true;
                continue;
            }
            const aoc::hex::AxialCoord curAx = cc.path[static_cast<std::size_t>(cc.pathIndex)];
            if (grid.isValid(curAx)) {
                const PlayerId tileOwner = grid.owner(grid.toIndex(curAx));
                if (tileOwner != INVALID_PLAYER
                    && tileOwner != BARBARIAN_PLAYER
                    && tileOwner != cc.owner) {
                    LOG_INFO("Courier abandoned in foreign territory (owner %u)",
                             static_cast<unsigned>(tileOwner));
                    cc.delivered = true;
                    continue;
                }
            }

            const int32_t speed = unitPtr->typeDef().movementPoints;
            int32_t budget = speed;
            while (cc.pathIndex + 1 < static_cast<int32_t>(cc.path.size())) {
                const aoc::hex::AxialCoord fromAx = cc.path[static_cast<std::size_t>(cc.pathIndex)];
                const aoc::hex::AxialCoord toAx   = cc.path[static_cast<std::size_t>(cc.pathIndex + 1)];
                if (!grid.isValid(fromAx) || !grid.isValid(toAx)) { break; }
                const int32_t fromIdx = grid.toIndex(fromAx);
                const int32_t toIdx   = grid.toIndex(toAx);
                int32_t cost = grid.movementCost(fromIdx, toIdx);
                if (cost <= 0) { cost = 1; }
                if (cost > budget) { break; }
                budget -= cost;
                ++cc.pathIndex;
            }
            unitPtr->setPosition(cc.path[static_cast<std::size_t>(cc.pathIndex)]);

            if (cc.pathIndex + 1 >= static_cast<int32_t>(cc.path.size())) {
                // Arrived at destination
                aoc::game::City* dst = playerPtr->cityAt(cc.destCityLocation);
                if (dst != nullptr) {
                    dst->stockpile().addGoods(cc.goodId, cc.quantity);
                    LOG_INFO("Courier delivered: %s received good %u x%d",
                             dst->name().c_str(),
                             static_cast<unsigned>(cc.goodId), cc.quantity);
                }
                cc.delivered = true;
            }
        }

        // Second pass: remove delivered courier units.
        std::vector<aoc::game::Unit*> toRemove;
        for (const std::unique_ptr<aoc::game::Unit>& unitPtr : playerPtr->units()) {
            if (unitPtr == nullptr) { continue; }
            if (!isCourierUnit(*unitPtr)) { continue; }
            if (unitPtr->courier().delivered) {
                toRemove.push_back(unitPtr.get());
            }
        }
        for (aoc::game::Unit* u : toRemove) {
            playerPtr->removeUnit(u);
        }

        // Third pass: clamp stockpiles to per-city caps (overflow discarded).
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }
            const int32_t cap = stockpileCap(*cityPtr);
            CityStockpileComponent& stock = cityPtr->stockpile();
            for (auto& kv : stock.goods) {
                if (kv.second > cap) {
                    kv.second = cap;
                }
            }
        }
    }
}

} // namespace aoc::sim

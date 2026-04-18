/**
 * @file Automation.cpp
 * @brief Player automation: research queue, auto-explore, alert, auto-improve.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/automation/Automation.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/economy/AdvancedEconomics.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace aoc::sim {

void processResearchQueue(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return; }

    PlayerTechComponent& tech = gsPlayer->tech();
    PlayerResearchQueueComponent& researchQueue = gsPlayer->researchQueue();

    // If no current research and queue has entries, start next
    if (!tech.currentResearch.isValid() && !researchQueue.researchQueue.empty()) {
        TechId nextTech = researchQueue.popNext();
        // Skip already-researched techs
        while (nextTech.isValid() && tech.hasResearched(nextTech)) {
            nextTech = researchQueue.popNext();
        }
        if (nextTech.isValid()) {
            tech.currentResearch = nextTech;
            tech.researchProgress = 0.0f;
            LOG_INFO("Research queue: player %u auto-started %.*s",
                     static_cast<unsigned>(player),
                     static_cast<int>(techDef(nextTech).name.size()),
                     techDef(nextTech).name.data());
        }
    }
}

void processAutoExplore(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, PlayerId player) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return; }

    for (const std::unique_ptr<aoc::game::Unit>& unit : gsPlayer->units()) {
        if (!unit->autoExplore) { continue; }

        const UnitTypeDef& def = unitTypeDef(unit->typeId());
        if (def.unitClass != UnitClass::Scout) { continue; }

        // Find nearest unexplored tile (simple: check ring-1 through ring-5)
        aoc::hex::AxialCoord bestTarget = unit->position();
        int32_t bestDist = std::numeric_limits<int32_t>::max();

        for (int32_t ring = 1; ring <= 5; ++ring) {
            std::vector<aoc::hex::AxialCoord> tiles;
            tiles.reserve(static_cast<std::size_t>(ring) * 6);
            aoc::hex::ring(unit->position(), ring, std::back_inserter(tiles));

            for (const aoc::hex::AxialCoord& tile : tiles) {
                if (!grid.isValid(tile)) { continue; }
                const int32_t idx = grid.toIndex(tile);
                // Check if tile is unowned (proxy for unexplored)
                if (grid.owner(idx) != INVALID_PLAYER) { continue; }
                if (grid.movementCost(idx) <= 0) { continue; }

                const int32_t dist = grid.distance(unit->position(), tile);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestTarget = tile;
                }
            }
            if (bestDist < std::numeric_limits<int32_t>::max()) { break; }
        }

        if (bestTarget != unit->position()) {
            orderUnitMove(*unit, bestTarget, grid);
            moveUnitAlongPath(gameState, *unit, grid);
        }
    }
}

void processAlertStance(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid, PlayerId player) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return; }

    for (const std::unique_ptr<aoc::game::Unit>& unit : gsPlayer->units()) {
        if (!unit->alertStance) { continue; }

        // Only wake sleeping/fortified units
        if (unit->state() != UnitState::Sleeping && unit->state() != UnitState::Fortified) {
            continue;
        }

        // Scan for enemy units within alert radius
        bool enemyNearby = false;
        for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
            if (other->id() == player) { continue; }
            for (const std::unique_ptr<aoc::game::Unit>& enemyUnit : other->units()) {
                const int32_t dist = grid.distance(unit->position(), enemyUnit->position());
                if (dist <= unit->alertRadius) {
                    enemyNearby = true;
                    break;
                }
            }
            if (enemyNearby) { break; }
        }

        if (enemyNearby) {
            unit->setState(UnitState::Idle);
            LOG_INFO("Alert: player %u unit at (%d,%d) woke up - enemy nearby!",
                     static_cast<unsigned>(player),
                     unit->position().q, unit->position().r);
        }
    }
}

void queueAutoRenewRequest(aoc::game::GameState& gameState,
                           PlayerId owner,
                           aoc::hex::AxialCoord origin,
                           aoc::hex::AxialCoord destination,
                           PlayerId destOwner,
                           TradeRouteType routeType) {
    aoc::game::Player* ownerPlayer = gameState.player(owner);
    if (ownerPlayer == nullptr) { return; }

    PendingTradeRoute req;
    req.origin       = origin;
    req.destination  = destination;
    req.destOwner    = destOwner;
    req.routeType    = routeType;
    req.turnsWaiting = 0;
    ownerPlayer->tradeAutoRenew().pending.push_back(req);
}

void processAutoRenewTradeRoutes(aoc::game::GameState& gameState,
                                 aoc::map::HexGrid& grid,
                                 const Market& market,
                                 DiplomacyManager* diplomacy,
                                 PlayerId player) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return; }

    PlayerTradeAutoRenewComponent& queue = gsPlayer->tradeAutoRenew();
    if (queue.pending.empty()) { return; }

    constexpr int32_t kMaxWait = 5;
    std::vector<PendingTradeRoute> keep;
    keep.reserve(queue.pending.size());

    for (PendingTradeRoute& req : queue.pending) {
        aoc::game::City* originCity = gsPlayer->cityAt(req.origin);
        if (originCity == nullptr) {
            LOG_INFO("Auto-renew drop: player %u lost origin city",
                     static_cast<unsigned>(player));
            continue;
        }

        aoc::game::Player* destPlayer = gameState.player(req.destOwner);
        aoc::game::City* destCity = (destPlayer != nullptr)
            ? destPlayer->cityAt(req.destination) : nullptr;
        if (destCity == nullptr) {
            LOG_INFO("Auto-renew drop: player %u destination vanished",
                     static_cast<unsigned>(player));
            continue;
        }

        aoc::game::Unit& traderUnit =
            gsPlayer->addUnit(UnitTypeId{30}, originCity->location());
        traderUnit.autoRenewRoute = true;

        const ErrorCode ec = establishTradeRoute(gameState, grid, market,
                                                  diplomacy, traderUnit, *destCity);
        if (ec != ErrorCode::Ok) {
            gsPlayer->removeUnit(&traderUnit);
            ++req.turnsWaiting;
            if (req.turnsWaiting < kMaxWait) {
                keep.push_back(req);
            } else {
                LOG_INFO("Auto-renew drop: player %u could not re-establish after %d turns",
                         static_cast<unsigned>(player), kMaxWait);
            }
            continue;
        }

        LOG_INFO("Auto-renew: player %u re-established route to (%d,%d)",
                 static_cast<unsigned>(player),
                 req.destination.q, req.destination.r);
    }

    queue.pending = std::move(keep);
}

void processAutoSpreadReligion(aoc::game::GameState& gameState,
                               aoc::map::HexGrid& grid,
                               const DiplomacyManager* diplomacy,
                               PlayerId player) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return; }

    std::vector<aoc::game::Unit*> toRemove;

    for (const std::unique_ptr<aoc::game::Unit>& unitPtr : gsPlayer->units()) {
        aoc::game::Unit* unit = unitPtr.get();
        if (unit == nullptr)               { continue; }
        if (!unit->autoSpreadReligion)     { continue; }
        if (unit->spreadCharges <= 0)      { continue; }
        if (unit->spreadingReligion >= MAX_RELIGIONS) { continue; }

        const UnitTypeDef& udef = unit->typeDef();
        if (udef.unitClass != UnitClass::Religious) { continue; }

        const uint16_t tid = unit->typeId().value;
        const bool isInquisitor = (tid == 21);
        const bool isMissionary = (tid == 19);
        const bool isApostle    = (tid == 20);
        if (!isInquisitor && !isMissionary && !isApostle) { continue; }

        aoc::game::City* target = nullptr;
        int32_t bestDist = std::numeric_limits<int32_t>::max();

        if (isInquisitor) {
            for (const std::unique_ptr<aoc::game::City>& cityPtr : gsPlayer->cities()) {
                aoc::game::City* city = cityPtr.get();
                if (city == nullptr) { continue; }
                const CityReligionComponent& cr = city->religion();
                bool hasForeign = false;
                for (uint8_t ri = 0; ri < MAX_RELIGIONS; ++ri) {
                    if (ri != unit->spreadingReligion && cr.pressure[ri] > 0.0f) {
                        hasForeign = true;
                        break;
                    }
                }
                if (!hasForeign) { continue; }
                const int32_t dist = grid.distance(unit->position(), city->location());
                if (dist < bestDist) { bestDist = dist; target = city; }
            }
        } else {
            for (const std::unique_ptr<aoc::game::Player>& otherPtr : gameState.players()) {
                aoc::game::Player* other = otherPtr.get();
                if (other == nullptr) { continue; }
                if (other != gsPlayer && diplomacy != nullptr
                    && diplomacy->isAtWar(player, other->id())) { continue; }
                for (const std::unique_ptr<aoc::game::City>& cityPtr : other->cities()) {
                    aoc::game::City* city = cityPtr.get();
                    if (city == nullptr) { continue; }
                    const CityReligionComponent& cr = city->religion();
                    if (cr.dominantReligion() == unit->spreadingReligion) { continue; }
                    const int32_t dist = grid.distance(unit->position(), city->location());
                    if (dist < bestDist) { bestDist = dist; target = city; }
                }
            }
        }

        if (target == nullptr) { continue; }

        if (unit->position() == target->location()) {
            const float pressure = isInquisitor ? 0.0f
                                  : (isMissionary ? 100.0f : 150.0f);
            if (isInquisitor) {
                CityReligionComponent& cityRel = target->religion();
                for (uint8_t ri = 0; ri < MAX_RELIGIONS; ++ri) {
                    if (ri != unit->spreadingReligion) {
                        cityRel.pressure[ri] = 0.0f;
                    }
                }
            } else {
                target->religion().addPressure(unit->spreadingReligion, pressure);
            }
            --unit->spreadCharges;
            LOG_INFO("Auto-spread: player %u %.*s acted on %s (charges left %d)",
                     static_cast<unsigned>(player),
                     static_cast<int>(udef.name.size()), udef.name.data(),
                     target->name().c_str(),
                     static_cast<int>(unit->spreadCharges));
            if (unit->spreadCharges <= 0) { toRemove.push_back(unit); }
        } else {
            orderUnitMove(*unit, target->location(), grid);
            moveUnitAlongPath(gameState, *unit, grid);
        }
    }

    for (aoc::game::Unit* dead : toRemove) {
        gsPlayer->removeUnit(dead);
    }
}

void processAutoTariffs(aoc::game::GameState& gameState,
                        const DiplomacyManager* diplomacy,
                        PlayerId player) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return; }

    PlayerTariffComponent& tariffs = gsPlayer->tariffs();
    if (!tariffs.autoTariffs) { return; }

    const CurrencyAmount treasury = gsPlayer->treasury();
    float baseImport;
    if      (treasury < 500)   { baseImport = 0.30f; }
    else if (treasury < 2000)  { baseImport = 0.20f; }
    else if (treasury < 5000)  { baseImport = 0.15f; }
    else                       { baseImport = 0.10f; }
    tariffs.importTariffRate = baseImport;
    tariffs.exportTariffRate = 0.0f;

    float baseToll;
    if      (treasury < 500)   { baseToll = 0.25f; }
    else if (treasury < 5000)  { baseToll = 0.15f; }
    else                       { baseToll = 0.10f; }
    tariffs.defaultTollRate = baseToll;

    tariffs.perPlayerTariffs.clear();
    tariffs.perPlayerTollRates.clear();

    if (diplomacy == nullptr) { return; }

    for (const std::unique_ptr<aoc::game::Player>& otherPtr : gameState.players()) {
        if (otherPtr == nullptr) { continue; }
        const PlayerId other = otherPtr->id();
        if (other == player) { continue; }
        if (!diplomacy->haveMet(player, other)) { continue; }

        const PairwiseRelation& rel = diplomacy->relation(player, other);
        const int32_t score = rel.totalScore();

        float tariffOverride = baseImport;
        float tollOverride   = baseToll;

        if (rel.isAtWar) {
            tariffOverride = 0.50f;
            tollOverride   = 0.50f;
        } else if (rel.hasEmbargo) {
            tariffOverride = 0.50f;
            tollOverride   = 0.40f;
        } else if (rel.hasEconomicAlliance) {
            tariffOverride = 0.00f;
            tollOverride   = 0.00f;
        } else if (rel.hasMilitaryAlliance || rel.hasDefensiveAlliance) {
            tariffOverride = std::min(baseImport, 0.05f);
            tollOverride   = std::min(baseToll,   0.05f);
        } else if (rel.hasOpenBorders && score > 30) {
            tariffOverride = std::min(baseImport, 0.08f);
            tollOverride   = std::min(baseToll,   0.08f);
        } else if (score < -30) {
            tariffOverride = std::min(baseImport + 0.15f, 0.50f);
            tollOverride   = std::min(baseToll   + 0.15f, 0.50f);
        } else if (score < 0) {
            tariffOverride = std::min(baseImport + 0.05f, 0.50f);
            tollOverride   = std::min(baseToll   + 0.05f, 0.50f);
        }

        if (std::fabs(tariffOverride - baseImport) > 1e-4f) {
            tariffs.perPlayerTariffs[other] = tariffOverride;
        }
        if (std::fabs(tollOverride - baseToll) > 1e-4f) {
            tariffs.perPlayerTollRates[other] = tollOverride;
        }
    }
}

void processAutoPolicies(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return; }

    PlayerGovernmentComponent& gov = gsPlayer->government();
    if (!gov.autoPolicies) { return; }

    equipBestPolicies(gov);
}

void processAutomation(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, PlayerId player) {
    processResearchQueue(gameState, player);
    processAutoExplore(gameState, grid, player);
    processAlertStance(gameState, grid, player);
    processAutoPolicies(gameState, player);
}

} // namespace aoc::sim

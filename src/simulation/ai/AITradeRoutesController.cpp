/**
 * @file AITradeRoutesController.cpp
 * @brief AI trade-route management turn-step. Extracted from
 *        AIController.cpp on 2026-05-04.
 */

#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/ai/AIController.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/tech/TechGating.hpp"
#include "aoc/simulation/turn/TurnEventLog.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>

namespace aoc::sim::ai {

namespace {

/// The rejection tally the headless tool prints per run. Which rule keeps
/// trade partners at one is the question the money programme's Phase 1 asks
/// before it touches geography.
void noteRouteRejection(const DiplomacyManager& diplomacy, PlayerId player, PlayerId destOwner,
                        ErrorCode why) {
    TurnEventLog* log = diplomacy.eventLog();
    if (log != nullptr) {
        log->record(TurnEventType::TradeRouteRejected, player, destOwner,
                    static_cast<int32_t>(why), 0, std::string(describeError(why)));
    }
}

} // namespace

void AIController::manageTradeRoutes(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                                      const Market& market, const DiplomacyManager& diplomacy) {
    aoc::game::Player* gsPlayer = gameState.player(this->m_player);
    if (gsPlayer == nullptr) { return; }

    // Collect idle Trader units: those whose TraderComponent has no route assigned yet
    std::vector<aoc::game::Unit*> idleTraders;
    for (const std::unique_ptr<aoc::game::Unit>& u : gsPlayer->units()) {
        if (unitTypeDef(u->typeId()).unitClass != UnitClass::Trader) { continue; }
        if (u->trader().owner == INVALID_PLAYER) {
            idleTraders.push_back(u.get());
        }
    }

    if (idleTraders.empty()) { return; }

    // 2026-05-03: skip the whole scoring loop when civ already saturated
    // its trade-route cap. Audit showed 82k "at cap" rejection logs per
    // 72-sim run because every idle trader probed every turn. Cheap up-front
    // check avoids the expensive city-scan + establishTradeRoute call.
    {
        const int32_t cap = computeTotalTradeSlots(*gsPlayer, grid);
        int32_t activeRoutes = 0;
        for (const std::unique_ptr<aoc::game::Unit>& u : gsPlayer->units()) {
            if (u == nullptr) { continue; }
            if (u->typeDef().unitClass != UnitClass::Trader) { continue; }
            if (u->trader().owner == INVALID_PLAYER) { continue; }
            ++activeRoutes;
        }
        if (activeRoutes >= cap) {
            noteRouteRejection(diplomacy, this->m_player, INVALID_PLAYER,
                               ErrorCode::TradeRouteCapReached);
            return;
        }
    }

    // Local prices per (city, good), memoised for this call: the scorer
    // asks for the same city's price for every trader and every partner.
    std::unordered_map<const aoc::game::City*, std::unordered_map<uint16_t, int32_t>> priceMemo;
    const auto priceAt = [&](const aoc::game::City& city, uint16_t good) {
        std::unordered_map<uint16_t, int32_t>& row = priceMemo[&city];
        const std::unordered_map<uint16_t, int32_t>::iterator it = row.find(good);
        if (it != row.end()) { return it->second; }
        const int32_t price = localPrice(market, good, city);
        row.emplace(good, price);
        return price;
    };
    // The spread a leg earns: what `from` holds beyond one unit, sold at
    // `to` (with its sale multiplier) against what it fetches at `from`.
    const auto legValue = [&](const aoc::game::City& from, const aoc::game::City& to, float saleMult) {
        float value = 0.0f;
        for (const std::pair<const uint16_t, int32_t>& entry : from.stockpile().goods) {
            if (entry.second <= 1 || isCoinGood(entry.first)) { continue; }
            const float sells = static_cast<float>(priceAt(to, entry.first)) * saleMult;
            const float spread = sells - static_cast<float>(priceAt(from, entry.first));
            if (spread > 0.0f) {
                value += spread * static_cast<float>(std::min(12, entry.second - 1));
            }
        }
        return value;
    };

    for (aoc::game::Unit* traderUnit : idleTraders) {
        // The route starts at the nearest own city, where the cargo is.
        aoc::game::City* origin = nullptr;
        int32_t originDist      = std::numeric_limits<int32_t>::max();
        for (const std::unique_ptr<aoc::game::City>& c : gsPlayer->cities()) {
            if (c == nullptr || c->owner() != this->m_player) { continue; }
            const int32_t d = grid.distance(traderUnit->position(), c->location());
            if (d < originDist) {
                originDist = d;
                origin     = c.get();
            }
        }
        if (origin == nullptr) { continue; }
        const bool originCoastal = grid.isCoastal(origin->location());

        // Score each city as a destination by the spread on both legs.
        aoc::game::City* bestCity = nullptr;
        float bestScore = -1.0f;

        for (const std::unique_ptr<aoc::game::Player>& pPtr : gameState.players()) {
            for (const std::unique_ptr<aoc::game::City>& cityPtr : pPtr->cities()) {
                if (cityPtr.get() == origin) { continue; }
                // 2026-05-02: skip razed / invalid-owner cities. Founder list
                // retains captured-then-razed cities with owner == INVALID;
                // every trade-route attempt against those rejected as
                // "no benefit / hostile" because gameState.player(255)==null.
                if (cityPtr->owner() == aoc::INVALID_PLAYER) { continue; }
                // Skip cities owned by civs we're at war with or embargoing.
                if (cityPtr->owner() != this->m_player
                    && (diplomacy.isAtWar(this->m_player, cityPtr->owner())
                        || diplomacy.hasAnyEmbargo(this->m_player, cityPtr->owner()))) {
                    continue;
                }

                const int32_t dist = grid.distance(origin->location(), cityPtr->location());
                const float distPenalty = 1.0f / static_cast<float>(std::max(1, dist));
                const TradeRouteType routeType =
                    originCoastal && grid.isCoastal(cityPtr->location())
                        ? TradeRouteType::Sea
                        : TradeRouteType::Land;
                float score = legValue(*origin, *cityPtr, destinationSaleMultiplier(*cityPtr, routeType)) +
                              legValue(*cityPtr, *origin, destinationSaleMultiplier(*origin, routeType));
                if (cityPtr->owner() != this->m_player) {
                    score += 10.0f; // foreign trade: the customs come home
                }
                score *= distPenalty;
                if (score > bestScore) {
                    bestScore = score;
                    bestCity  = cityPtr.get();
                }
            }
        }

        if (bestCity != nullptr) {
            const ErrorCode result = establishTradeRoute(
                gameState, grid, market, &diplomacy, *traderUnit, *bestCity);
            if (result == ErrorCode::Ok) {
                // AI always auto-renews trade routes so tech diffusion and
                // economy stay active late-game (route lifetime is a few trips).
                traderUnit->autoRenewRoute = true;
                LOG_INFO("AI %u established trade route to %s (player %u, score %.0f)",
                         static_cast<unsigned>(this->m_player),
                         bestCity->name().c_str(),
                         static_cast<unsigned>(bestCity->owner()),
                         static_cast<double>(bestScore));
            } else {
                noteRouteRejection(diplomacy, this->m_player, bestCity->owner(), result);
            }
        } else {
            noteRouteRejection(diplomacy, this->m_player, INVALID_PLAYER,
                               ErrorCode::TradeRouteNoDestination);
        }
    }
}

// ============================================================================
// Monetary system management
// ============================================================================

} // namespace aoc::sim::ai

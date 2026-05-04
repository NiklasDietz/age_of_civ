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
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/tech/TechGating.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <unordered_map>

namespace aoc::sim::ai {

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
        if (activeRoutes >= cap) { return; }
    }

    const aoc::sim::PlayerEconomyComponent& myEcon = gsPlayer->economy();

    for (aoc::game::Unit* traderUnit : idleTraders) {
        // Score each city as a trade destination based on complementary resources
        aoc::game::City* bestCity = nullptr;
        float bestScore = -1.0f;

        for (const std::unique_ptr<aoc::game::Player>& pPtr : gameState.players()) {
            for (const std::unique_ptr<aoc::game::City>& cityPtr : pPtr->cities()) {
                if (cityPtr->location() == traderUnit->position()) { continue; }
                // 2026-05-02: skip razed / invalid-owner cities. Founder list
                // retains captured-then-razed cities with owner == INVALID;
                // every trade-route attempt against those rejected as
                // "no benefit / hostile" because gameState.player(255)==null.
                if (cityPtr->owner() == aoc::INVALID_PLAYER) { continue; }
                // 2026-05-02: skip cities owned by civs we're at war with or
                // embargoing. Audit: 14k Trade-route-rejected logs were all
                // from this collision — AI proposed routes to enemy capitals
                // because score formula ignored war state. Pre-filter here
                // so traders pick a peaceful partner immediately.
                if (cityPtr->owner() != this->m_player
                    && (diplomacy.isAtWar(this->m_player, cityPtr->owner())
                        || diplomacy.hasEmbargo(this->m_player, cityPtr->owner()))) {
                    continue;
                }

                float score = 0.0f;
                const int32_t dist =
                    grid.distance(traderUnit->position(), cityPtr->location());
                const float distPenalty =
                    1.0f / static_cast<float>(std::max(1, dist));

                if (cityPtr->owner() != this->m_player) {
                    const aoc::game::Player* destPlayerObj = gameState.player(cityPtr->owner());
                    if (destPlayerObj != nullptr) {
                        for (const std::pair<const uint16_t, int32_t>& need : myEcon.totalNeeds) {
                            const int32_t destHas =
                                cityPtr->stockpile().getAmount(need.first);
                            if (destHas > 1) {
                                score +=
                                    static_cast<float>(std::min(destHas, need.second))
                                    * static_cast<float>(
                                          market.marketData(need.first).currentPrice);
                            }
                        }
                        const aoc::sim::PlayerEconomyComponent& destEcon = destPlayerObj->economy();
                        for (const std::pair<const uint16_t, int32_t>& theirNeed :
                                 destEcon.totalNeeds) {
                            int32_t weHave = 0;
                            std::unordered_map<uint16_t, int32_t>::const_iterator supIt =
                                myEcon.lastTurnProduction.find(theirNeed.first);
                            if (supIt != myEcon.lastTurnProduction.end()) { weHave = supIt->second; }
                            if (weHave > 1) {
                                score +=
                                    static_cast<float>(std::min(weHave, theirNeed.second))
                                    * static_cast<float>(
                                          market.marketData(theirNeed.first).currentPrice)
                                    * 0.5f;
                            }
                        }
                    }
                    score += 50.0f;  // Foreign trade base bonus
                } else {
                    score += 10.0f;  // Internal trade: lower priority
                }

                score *= distPenalty;
                if (score > bestScore) {
                    bestScore = score;
                    bestCity = cityPtr.get();
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
            }
        }
    }
}

// ============================================================================
// Monetary system management
// ============================================================================

} // namespace aoc::sim::ai

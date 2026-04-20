/**
 * @file Secession.cpp
 * @brief City secession — flip a disloyal city to a neighbor or Free City.
 *
 * Extracted from CityLoyalty.cpp. The trigger uses the same loyalty pressure
 * radius as the loyalty computation (cities within 9 hexes exert pressure).
 */

#include "aoc/simulation/city/Secession.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/balance/BalanceParams.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/core/Log.hpp"

#include <array>
#include <memory>
#include <unordered_map>
#include <utility>

namespace aoc::sim {

// Loyalty pressure radius, distant-city threshold, and sustained-unrest turn
// count all live in BalanceParams so the balance GA can sweep them.

bool checkAndPerformSecession(aoc::game::GameState& gameState,
                              aoc::map::HexGrid& grid,
                              aoc::game::City& city,
                              CityLoyaltyComponent& loyalty,
                              PlayerId player,
                              bool secededThisTurn) {
    if (secededThisTurn) {
        return false;
    }

    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) {
        return false;
    }

    const aoc::balance::BalanceParams& bal = aoc::balance::params();

    // Primary trigger: loyalty floor hit zero.
    bool trigger = (loyalty.loyalty <= 0.0f);

    // Secondary trigger: sustained unrest in a distant periphery city.
    if (!trigger && loyalty.unrestTurns >= bal.sustainedUnrestTurns) {
        int32_t distFromCapital = 0;
        for (const std::unique_ptr<aoc::game::City>& other : gsPlayer->cities()) {
            if (other->isOriginalCapital()) {
                distFromCapital = grid.distance(city.location(), other->location());
                break;
            }
        }
        if (distFromCapital >= bal.distantCityThreshold) {
            trigger = true;
            LOG_INFO("SECESSION: %s (player %u) unrest %d turns, %d from capital",
                     city.name().c_str(),
                     static_cast<unsigned>(player),
                     loyalty.unrestTurns, distFromCapital);
        }
    }

    if (!trigger) {
        return false;
    }

    // Find the dominant foreign neighbor by summed pressure.
    std::unordered_map<PlayerId, float> neighborPressure;
    for (const std::unique_ptr<aoc::game::Player>& otherPlayer : gameState.players()) {
        if (otherPlayer->id() == player) { continue; }
        for (const std::unique_ptr<aoc::game::City>& nearCity : otherPlayer->cities()) {
            const int32_t dist = grid.distance(city.location(), nearCity->location());
            if (dist > bal.loyaltyPressureRadius || dist <= 0) { continue; }
            const float pressure = static_cast<float>(nearCity->population()) * 0.5f
                                 / static_cast<float>(dist);
            neighborPressure[otherPlayer->id()] += pressure;
        }
    }

    PlayerId bestNeighbor = INVALID_PLAYER;
    float    bestPressure = 0.0f;
    for (const std::pair<const PlayerId, float>& entry : neighborPressure) {
        if (entry.second > bestPressure) {
            bestPressure = entry.second;
            bestNeighbor = entry.first;
        }
    }

    // Grievance on the former owner against the gainer (or an anchor if none).
    const PlayerId gainer = (bestNeighbor != INVALID_PLAYER) ? bestNeighbor
                                                             : INVALID_PLAYER;
    gsPlayer->grievances().addGrievance(
        GrievanceType::LostCityToSecession, gainer);

    loyalty.unrestTurns = 0;

    if (bestNeighbor != INVALID_PLAYER) {
        LOG_INFO("REVOLT: %s (player %u) loyalty 0 -- flips to player %u!",
                 city.name().c_str(),
                 static_cast<unsigned>(player),
                 static_cast<unsigned>(bestNeighbor));
        city.setOwner(bestNeighbor);
        loyalty.loyalty = 50.0f;

        if (grid.isValid(city.location())) {
            grid.setOwner(grid.toIndex(city.location()), bestNeighbor);
        }
        const std::array<aoc::hex::AxialCoord, 6> nbrs =
            aoc::hex::neighbors(city.location());
        for (const aoc::hex::AxialCoord& n : nbrs) {
            if (grid.isValid(n)) {
                grid.setOwner(grid.toIndex(n), bestNeighbor);
            }
        }
    } else {
        LOG_INFO("REVOLT: %s (player %u) loyalty 0 -- becomes Free City!",
                 city.name().c_str(),
                 static_cast<unsigned>(player));
        city.setOwner(INVALID_PLAYER);
        loyalty.loyalty = 50.0f;
    }

    return true;
}

} // namespace aoc::sim

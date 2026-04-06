/**
 * @file CityLoyalty.cpp
 * @brief City loyalty computation and city-flipping logic.
 */

#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/tech/EraScore.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <unordered_map>

namespace aoc::sim {

void computeCityLoyalty(aoc::ecs::World& world, aoc::map::HexGrid& grid,
                        PlayerId player) {
    aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    // Look up the player's age type (Golden/Dark/Normal)
    AgeType playerAge = AgeType::Normal;
    const aoc::ecs::ComponentPool<PlayerEraScoreComponent>* eraPool =
        world.getPool<PlayerEraScoreComponent>();
    if (eraPool != nullptr) {
        for (uint32_t i = 0; i < eraPool->size(); ++i) {
            if (eraPool->data()[i].owner == player) {
                playerAge = eraPool->data()[i].currentAgeType;
                break;
            }
        }
    }

    // Iterate all cities owned by this player
    for (uint32_t ci = 0; ci < cityPool->size(); ++ci) {
        CityComponent& city = cityPool->data()[ci];
        if (city.owner != player) {
            continue;
        }

        EntityId cityEntity = cityPool->entities()[ci];

        // Ensure loyalty component exists
        if (!world.hasComponent<CityLoyaltyComponent>(cityEntity)) {
            world.addComponent<CityLoyaltyComponent>(
                cityEntity, CityLoyaltyComponent{});
        }
        CityLoyaltyComponent& loyalty =
            world.getComponent<CityLoyaltyComponent>(cityEntity);

        // ----- Compute loyalty change this turn -----
        float change = 0.0f;

        // +8 base loyalty for being your city
        constexpr float BASE_LOYALTY = 8.0f;
        change += BASE_LOYALTY;

        // Golden/Dark age effects
        if (playerAge == AgeType::Golden) {
            change += 5.0f;
        } else if (playerAge == AgeType::Dark) {
            change -= 5.0f;
        }

        // Unhappiness penalty
        const CityHappinessComponent* happiness =
            world.tryGetComponent<CityHappinessComponent>(cityEntity);
        if (happiness != nullptr && happiness->happiness < 0.0f) {
            change += happiness->happiness * 2.0f;  // -2 per point of unhappiness
        }

        // Recently captured penalty
        if (city.originalOwner != INVALID_PLAYER &&
            city.originalOwner != city.owner) {
            change -= 3.0f;
        }

        // Nearby foreign cities with higher population
        for (uint32_t fi = 0; fi < cityPool->size(); ++fi) {
            const CityComponent& foreignCity = cityPool->data()[fi];
            if (foreignCity.owner == player || foreignCity.owner == INVALID_PLAYER) {
                continue;
            }
            const int32_t dist = hex::distance(city.location, foreignCity.location);
            if (dist <= 5 && foreignCity.population > city.population) {
                change -= 3.0f;
            }
        }

        loyalty.loyaltyPerTurn = change;
        loyalty.loyalty += change;
        loyalty.loyalty = std::clamp(loyalty.loyalty, 0.0f, 100.0f);

        // ----- Check for city flip -----
        if (loyalty.loyalty <= 0.0f) {
            // Find the neighbor player with the most cities within 5 hexes
            std::unordered_map<PlayerId, int32_t> nearbyCounts;
            for (uint32_t ni = 0; ni < cityPool->size(); ++ni) {
                const CityComponent& nearCity = cityPool->data()[ni];
                if (nearCity.owner == player || nearCity.owner == INVALID_PLAYER) {
                    continue;
                }
                const int32_t dist = hex::distance(city.location, nearCity.location);
                if (dist <= 5) {
                    nearbyCounts[nearCity.owner] += 1;
                }
            }

            PlayerId bestNeighbor = INVALID_PLAYER;
            int32_t bestCount = 0;
            for (const std::pair<const PlayerId, int32_t>& entry : nearbyCounts) {
                if (entry.second > bestCount) {
                    bestCount = entry.second;
                    bestNeighbor = entry.first;
                }
            }

            if (bestNeighbor != INVALID_PLAYER) {
                LOG_INFO("City %s (player %u) loyalty 0 -- flipping to player %u",
                         city.name.c_str(),
                         static_cast<unsigned>(player),
                         static_cast<unsigned>(bestNeighbor));
                city.owner = bestNeighbor;
                loyalty.loyalty = 50.0f;  // Reset to half loyalty under new owner

                // Update tile ownership around the city
                const int32_t centerIdx = grid.toIndex(city.location);
                if (grid.isValid(city.location)) {
                    grid.setOwner(centerIdx, bestNeighbor);
                }
            } else {
                // No nearby neighbor -- become free city
                LOG_INFO("City %s (player %u) loyalty 0 -- becomes free city",
                         city.name.c_str(),
                         static_cast<unsigned>(player));
                city.owner = INVALID_PLAYER;
                loyalty.loyalty = 50.0f;
            }
        }
    }
}

} // namespace aoc::sim

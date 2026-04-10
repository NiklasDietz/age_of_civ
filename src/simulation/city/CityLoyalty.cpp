/**
 * @file CityLoyalty.cpp
 * @brief City loyalty computation with Civ 6-style pressure mechanics.
 */

#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/Governor.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/tech/EraScore.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <unordered_map>

namespace aoc::sim {

/// Loyalty pressure radius (how far cities exert influence).
constexpr int32_t LOYALTY_PRESSURE_RADIUS = 9;

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
            world.addComponent<CityLoyaltyComponent>(cityEntity, CityLoyaltyComponent{});
        }
        CityLoyaltyComponent& loyalty =
            world.getComponent<CityLoyaltyComponent>(cityEntity);

        // ================================================================
        // Reset breakdown for this turn
        // ================================================================
        loyalty.baseLoyalty = 8.0f;
        loyalty.ownCityPressure = 0.0f;
        loyalty.foreignCityPressure = 0.0f;
        loyalty.governorBonus = 0.0f;
        loyalty.garrisonBonus = 0.0f;
        loyalty.monumentBonus = 0.0f;
        loyalty.happinessEffect = 0.0f;
        loyalty.ageEffect = 0.0f;
        loyalty.capturedPenalty = 0.0f;

        // ================================================================
        // City pressure from ALL nearby cities (own and foreign)
        // Pressure = population * 0.5 / distance for each nearby city.
        // Own cities contribute positive, foreign contribute negative.
        // ================================================================
        for (uint32_t ni = 0; ni < cityPool->size(); ++ni) {
            if (ni == ci) { continue; }  // Skip self
            const CityComponent& nearCity = cityPool->data()[ni];
            if (nearCity.owner == INVALID_PLAYER) { continue; }

            int32_t dist = aoc::hex::distance(city.location, nearCity.location);
            if (dist > LOYALTY_PRESSURE_RADIUS || dist <= 0) { continue; }

            float pressure = static_cast<float>(nearCity.population) * 0.5f
                           / static_cast<float>(dist);

            if (nearCity.owner == player) {
                loyalty.ownCityPressure += pressure;
            } else {
                loyalty.foreignCityPressure -= pressure;
            }
        }

        // ================================================================
        // Governor bonus (+4 loyalty if governor is active)
        // ================================================================
        const CityGovernorComponent* governor =
            world.tryGetComponent<CityGovernorComponent>(cityEntity);
        if (governor != nullptr && governor->isActive) {
            loyalty.governorBonus = 4.0f;
        }

        // ================================================================
        // Garrison bonus (+3 per military unit on the city tile)
        // ================================================================
        const aoc::ecs::ComponentPool<UnitComponent>* unitPool =
            world.getPool<UnitComponent>();
        if (unitPool != nullptr) {
            for (uint32_t ui = 0; ui < unitPool->size(); ++ui) {
                const UnitComponent& unit = unitPool->data()[ui];
                if (unit.owner == player && unit.position == city.location) {
                    if (isMilitary(unitTypeDef(unit.typeId).unitClass)) {
                        loyalty.garrisonBonus += 3.0f;
                    }
                }
            }
        }
        loyalty.garrisonBonus = std::min(loyalty.garrisonBonus, 9.0f);  // Cap at 3 units

        // ================================================================
        // Monument bonus (+2 per Monument building in city)
        // ================================================================
        const CityDistrictsComponent* districts =
            world.tryGetComponent<CityDistrictsComponent>(cityEntity);
        if (districts != nullptr) {
            for (const CityDistrictsComponent::PlacedDistrict& d : districts->districts) {
                for (BuildingId bid : d.buildings) {
                    if (bid.value == 16) {  // Monument
                        loyalty.monumentBonus += 2.0f;
                    }
                }
            }
        }

        // ================================================================
        // Golden/Dark age effects
        // ================================================================
        if (playerAge == AgeType::Golden) {
            loyalty.ageEffect = 5.0f;
        } else if (playerAge == AgeType::Dark) {
            loyalty.ageEffect = -5.0f;
        }

        // ================================================================
        // Unhappiness penalty (-2 per point below 0)
        // ================================================================
        const CityHappinessComponent* happiness =
            world.tryGetComponent<CityHappinessComponent>(cityEntity);
        if (happiness != nullptr && happiness->happiness < 0.0f) {
            loyalty.happinessEffect = happiness->happiness * 2.0f;
        }

        // ================================================================
        // Recently captured penalty
        // ================================================================
        if (city.originalOwner != INVALID_PLAYER && city.originalOwner != city.owner) {
            loyalty.capturedPenalty = -3.0f;
        }

        // ================================================================
        // Sum it all up
        // ================================================================
        float change = loyalty.baseLoyalty
                     + loyalty.ownCityPressure
                     + loyalty.foreignCityPressure
                     + loyalty.governorBonus
                     + loyalty.garrisonBonus
                     + loyalty.monumentBonus
                     + loyalty.ageEffect
                     + loyalty.happinessEffect
                     + loyalty.capturedPenalty;

        loyalty.loyaltyPerTurn = change;
        loyalty.loyalty += change;
        loyalty.loyalty = std::clamp(loyalty.loyalty, 0.0f, 100.0f);

        // ================================================================
        // Check for city flip at 0 loyalty
        // ================================================================
        if (loyalty.loyalty <= 0.0f) {
            // Find the dominant neighbor (most pressure from nearby foreign cities)
            std::unordered_map<PlayerId, float> neighborPressure;
            for (uint32_t ni = 0; ni < cityPool->size(); ++ni) {
                const CityComponent& nearCity = cityPool->data()[ni];
                if (nearCity.owner == player || nearCity.owner == INVALID_PLAYER) {
                    continue;
                }
                int32_t dist = aoc::hex::distance(city.location, nearCity.location);
                if (dist > LOYALTY_PRESSURE_RADIUS || dist <= 0) { continue; }
                float pressure = static_cast<float>(nearCity.population) * 0.5f
                               / static_cast<float>(dist);
                neighborPressure[nearCity.owner] += pressure;
            }

            PlayerId bestNeighbor = INVALID_PLAYER;
            float bestPressure = 0.0f;
            for (const std::pair<const PlayerId, float>& entry : neighborPressure) {
                if (entry.second > bestPressure) {
                    bestPressure = entry.second;
                    bestNeighbor = entry.first;
                }
            }

            if (bestNeighbor != INVALID_PLAYER) {
                LOG_INFO("REVOLT: %s (player %u) loyalty 0 -- flips to player %u!",
                         city.name.c_str(),
                         static_cast<unsigned>(player),
                         static_cast<unsigned>(bestNeighbor));
                city.owner = bestNeighbor;
                loyalty.loyalty = 50.0f;

                // Update tile ownership
                int32_t centerIdx = grid.toIndex(city.location);
                if (grid.isValid(city.location)) {
                    grid.setOwner(centerIdx, bestNeighbor);
                }
                std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(city.location);
                for (const aoc::hex::AxialCoord& n : nbrs) {
                    if (grid.isValid(n)) {
                        grid.setOwner(grid.toIndex(n), bestNeighbor);
                    }
                }
            } else {
                // No dominant neighbor: become Free City
                LOG_INFO("REVOLT: %s (player %u) loyalty 0 -- becomes Free City!",
                         city.name.c_str(),
                         static_cast<unsigned>(player));
                city.owner = INVALID_PLAYER;
                loyalty.loyalty = 50.0f;
            }
        }
    }
}

} // namespace aoc::sim

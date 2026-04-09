/**
 * @file CommunicationSpeed.cpp
 * @brief Communication speed computation and empire cohesion penalties.
 */

#include "aoc/simulation/empire/CommunicationSpeed.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

// ============================================================================
// Communication tier determination
// ============================================================================

CommTier determineCommTier(const aoc::ecs::World& world, PlayerId player) {
    const aoc::ecs::ComponentPool<PlayerTechComponent>* techPool =
        world.getPool<PlayerTechComponent>();
    if (techPool == nullptr) {
        return CommTier::FootMessenger;
    }

    const PlayerTechComponent* playerTech = nullptr;
    for (uint32_t i = 0; i < techPool->size(); ++i) {
        if (techPool->data()[i].owner == player) {
            playerTech = &techPool->data()[i];
            break;
        }
    }
    if (playerTech == nullptr) {
        return CommTier::FootMessenger;
    }

    // Check from highest tier downward, return first that's researched
    for (int32_t t = static_cast<int32_t>(CommTier::Internet); t >= 0; --t) {
        CommTier tier = static_cast<CommTier>(t);
        TechId required = commTierRequiredTech(tier);
        if (!required.isValid() || playerTech->hasResearched(required)) {
            return tier;
        }
    }

    return CommTier::FootMessenger;
}

// ============================================================================
// Communication distance computation
// ============================================================================

void updateCommunicationDistances(aoc::ecs::World& world,
                                   const aoc::map::HexGrid& grid,
                                   PlayerId player) {
    // Find or create communication component
    aoc::ecs::ComponentPool<PlayerCommunicationComponent>* commPool =
        world.getPool<PlayerCommunicationComponent>();
    if (commPool == nullptr) {
        return;
    }

    PlayerCommunicationComponent* comm = nullptr;
    for (uint32_t i = 0; i < commPool->size(); ++i) {
        if (commPool->data()[i].owner == player) {
            comm = &commPool->data()[i];
            break;
        }
    }
    if (comm == nullptr) {
        return;
    }

    // Determine comm tier
    comm->currentTier = determineCommTier(world, player);
    int32_t speed = commSpeedTilesPerTurn(comm->currentTier);

    // Find the player's capital
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    hex::AxialCoord capitalPos{0, 0};
    bool hasCapital = false;
    for (uint32_t c = 0; c < cityPool->size(); ++c) {
        if (cityPool->data()[c].owner == player && cityPool->data()[c].isOriginalCapital) {
            capitalPos = cityPool->data()[c].location;
            hasCapital = true;
            break;
        }
    }
    if (!hasCapital) {
        // Use first city as de-facto capital
        for (uint32_t c = 0; c < cityPool->size(); ++c) {
            if (cityPool->data()[c].owner == player) {
                capitalPos = cityPool->data()[c].location;
                hasCapital = true;
                break;
            }
        }
    }
    if (!hasCapital) {
        comm->cityCount = 0;
        return;
    }

    // Identify regional capitals (cities with Governor's Palace = BuildingId 22?)
    // For now: any city with a "Hospital" or high-tier building acts as regional hub.
    // We'll use a dedicated check for the Governor concept.
    struct RegionalCapital {
        hex::AxialCoord location;
    };
    std::vector<RegionalCapital> regionalCapitals;

    for (uint32_t c = 0; c < cityPool->size(); ++c) {
        if (cityPool->data()[c].owner != player) {
            continue;
        }
        // A city with a Commercial Hub district + Bank acts as regional capital
        const CityDistrictsComponent* districts =
            world.tryGetComponent<CityDistrictsComponent>(cityPool->entities()[c]);
        if (districts != nullptr && districts->hasDistrict(DistrictType::Commercial)
            && districts->hasBuilding(BuildingId{20})) { // Bank = regional capital
            if (cityPool->data()[c].location != capitalPos) {
                regionalCapitals.push_back({cityPool->data()[c].location});
            }
        }
    }

    // Compute communication distance for each city
    comm->cityCount = 0;
    for (uint32_t c = 0; c < cityPool->size(); ++c) {
        if (cityPool->data()[c].owner != player) {
            continue;
        }
        if (comm->cityCount >= PlayerCommunicationComponent::MAX_TRACKED_CITIES) {
            break;
        }

        hex::AxialCoord cityPos = cityPool->data()[c].location;
        EntityId cityEntity = cityPool->entities()[c];

        // Hex distance from capital
        int32_t distFromCapital = hex::distance(cityPos, capitalPos);

        // Check if closer to a regional capital
        int32_t effectiveDist = distFromCapital;
        for (const RegionalCapital& rc : regionalCapitals) {
            int32_t distFromRC = hex::distance(cityPos, rc.location);
            if (distFromRC <= REGIONAL_CAPITAL_RADIUS) {
                // Use distance to regional capital + distance of RC to capital
                int32_t rcToCapital = hex::distance(rc.location, capitalPos);
                int32_t viaDist = distFromRC + rcToCapital / 2;  // RC halves remaining distance
                effectiveDist = std::min(effectiveDist, viaDist);
            }
        }

        // Infrastructure bonus: if road/railway connects, effective speed is higher
        // (already captured by comm tier for railway/telegraph/radio)

        // Communication distance = hex distance / comm speed
        float commDist = static_cast<float>(effectiveDist) / static_cast<float>(speed);

        // Check for garrison
        bool hasGarrison = false;
        const aoc::ecs::ComponentPool<UnitComponent>* unitPool =
            world.getPool<UnitComponent>();
        if (unitPool != nullptr) {
            for (uint32_t u = 0; u < unitPool->size(); ++u) {
                const UnitComponent& unit = unitPool->data()[u];
                if (unit.owner == player && unit.position == cityPos
                    && isMilitary(unitTypeDef(unit.typeId).unitClass)) {
                    hasGarrison = true;
                    break;
                }
            }
        }

        int32_t idx = comm->cityCount;
        comm->cities[idx].cityEntity = cityEntity;
        comm->cities[idx].hexDistance = distFromCapital;
        comm->cities[idx].commDistance = commDist;
        comm->cities[idx].isRegionalCapital = false;
        comm->cities[idx].hasGarrison = hasGarrison;

        // Check if this city IS a regional capital
        for (const RegionalCapital& rc : regionalCapitals) {
            if (cityPos == rc.location) {
                comm->cities[idx].isRegionalCapital = true;
                break;
            }
        }

        ++comm->cityCount;
    }
}

// ============================================================================
// City communication modifiers
// ============================================================================

CityCommModifiers computeCityCommModifiers(
    const PlayerCommunicationComponent::CityCommData& commData) {
    CityCommModifiers mods;

    float dist = commData.commDistance;
    if (dist <= 1.0f) {
        return mods;  // Capital or adjacent city -- no penalty
    }

    // Penalties scale with communication distance beyond 1.0
    float excessDist = dist - 1.0f;

    mods.loyaltyPenalty = excessDist * COMM_LOYALTY_PENALTY_PER_TURN;
    mods.corruptionAdd = excessDist * COMM_CORRUPTION_PER_TURN;

    float prodPenalty = excessDist * COMM_PRODUCTION_PENALTY_PER_TURN;
    mods.productionMultiplier = std::max(0.50f, 1.0f - prodPenalty);

    float sciPenalty = excessDist * COMM_SCIENCE_PENALTY_PER_TURN;
    mods.scienceMultiplier = std::max(0.50f, 1.0f - sciPenalty);

    // Garrison halves loyalty penalty
    if (commData.hasGarrison) {
        mods.loyaltyPenalty *= (1.0f - GARRISON_LOYALTY_REDUCTION);
    }

    // Regional capitals have reduced penalties (they ARE the local authority)
    if (commData.isRegionalCapital) {
        mods.loyaltyPenalty *= 0.5f;
        mods.corruptionAdd *= 0.5f;
    }

    return mods;
}

// ============================================================================
// Per-turn processing
// ============================================================================

void processCommunication(aoc::ecs::World& world, const aoc::map::HexGrid& grid) {
    aoc::ecs::ComponentPool<PlayerCommunicationComponent>* commPool =
        world.getPool<PlayerCommunicationComponent>();
    if (commPool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < commPool->size(); ++i) {
        PlayerCommunicationComponent& comm = commPool->data()[i];
        updateCommunicationDistances(world, grid, comm.owner);

        // Apply loyalty penalties from communication distance
        for (int32_t c = 0; c < comm.cityCount; ++c) {
            const PlayerCommunicationComponent::CityCommData& cityComm = comm.cities[c];
            CityCommModifiers mods = computeCityCommModifiers(cityComm);

            if (mods.loyaltyPenalty > 0.01f) {
                CityLoyaltyComponent* loyalty =
                    world.tryGetComponent<CityLoyaltyComponent>(cityComm.cityEntity);
                if (loyalty != nullptr) {
                    loyalty->loyalty -= mods.loyaltyPenalty;
                    loyalty->loyalty = std::max(0.0f, loyalty->loyalty);
                }
            }
        }
    }
}

} // namespace aoc::sim

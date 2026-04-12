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
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"

#include <algorithm>

namespace aoc::sim {

// ============================================================================
// Communication tier determination
// ============================================================================

CommTier determineCommTier(const aoc::game::GameState& gameState, PlayerId player) {
    const aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return CommTier::FootMessenger;
    }

    const PlayerTechComponent& playerTech = playerObj->tech();

    // Check from highest tier downward, return first that's researched
    for (int32_t t = static_cast<int32_t>(CommTier::Internet); t >= 0; --t) {
        CommTier tier    = static_cast<CommTier>(t);
        TechId required  = commTierRequiredTech(tier);
        if (!required.isValid() || playerTech.hasResearched(required)) {
            return tier;
        }
    }

    return CommTier::FootMessenger;
}

// ============================================================================
// Communication distance computation
// ============================================================================

void updateCommunicationDistances(aoc::game::GameState& gameState,
                                   const aoc::map::HexGrid& grid,
                                   PlayerId player) {
    (void)grid;  // Grid is reserved for future road/terrain-weighted distance lookups

    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return;
    }

    PlayerCommunicationComponent& comm = playerObj->communication();

    // Determine comm tier
    comm.currentTier = determineCommTier(gameState, player);
    int32_t speed    = commSpeedTilesPerTurn(comm.currentTier);

    // Find the player's capital
    hex::AxialCoord capitalPos{0, 0};
    bool hasCapital = false;
    for (const std::unique_ptr<aoc::game::City>& cityPtr : playerObj->cities()) {
        if (cityPtr == nullptr) {
            continue;
        }
        if (cityPtr->isOriginalCapital()) {
            capitalPos = cityPtr->location();
            hasCapital = true;
            break;
        }
    }
    if (!hasCapital) {
        // Use first city as de-facto capital
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerObj->cities()) {
            if (cityPtr != nullptr) {
                capitalPos = cityPtr->location();
                hasCapital = true;
                break;
            }
        }
    }
    if (!hasCapital) {
        comm.cityCount = 0;
        return;
    }

    // Identify regional capitals: any city with a Commercial district + Bank
    struct RegionalCapital {
        hex::AxialCoord location;
    };
    std::vector<RegionalCapital> regionalCapitals;

    for (const std::unique_ptr<aoc::game::City>& cityPtr : playerObj->cities()) {
        if (cityPtr == nullptr) {
            continue;
        }
        // A city with a Commercial Hub district + Bank (BuildingId 20) acts as regional capital
        if (cityPtr->districts().hasDistrict(DistrictType::Commercial)
            && cityPtr->districts().hasBuilding(BuildingId{20})
            && cityPtr->location() != capitalPos) {
            regionalCapitals.push_back({cityPtr->location()});
        }
    }

    // Compute communication distance for each city
    comm.cityCount = 0;
    for (const std::unique_ptr<aoc::game::City>& cityPtr : playerObj->cities()) {
        if (cityPtr == nullptr) {
            continue;
        }
        if (comm.cityCount >= PlayerCommunicationComponent::MAX_TRACKED_CITIES) {
            break;
        }

        const hex::AxialCoord cityPos = cityPtr->location();

        // Hex distance from capital
        int32_t distFromCapital = hex::distance(cityPos, capitalPos);

        // Check if closer via a regional capital
        int32_t effectiveDist = distFromCapital;
        for (const RegionalCapital& rc : regionalCapitals) {
            int32_t distFromRC = hex::distance(cityPos, rc.location);
            if (distFromRC <= REGIONAL_CAPITAL_RADIUS) {
                // RC halves the remaining distance to the main capital
                int32_t rcToCapital = hex::distance(rc.location, capitalPos);
                int32_t viaDist     = distFromRC + rcToCapital / 2;
                effectiveDist = std::min(effectiveDist, viaDist);
            }
        }

        // Communication distance = hex distance / comm speed
        float commDist = static_cast<float>(effectiveDist) / static_cast<float>(speed);

        // Check for garrison: any military unit belonging to this player at this tile
        bool hasGarrison = false;
        for (const std::unique_ptr<aoc::game::Unit>& unitPtr : playerObj->units()) {
            if (unitPtr == nullptr) {
                continue;
            }
            if (unitPtr->position() == cityPos && unitPtr->isMilitary()) {
                hasGarrison = true;
                break;
            }
        }

        int32_t idx                        = comm.cityCount;
        comm.cities[idx].cityEntity        = NULL_ENTITY;  // EntityId not used in object model
        comm.cities[idx].hexDistance       = distFromCapital;
        comm.cities[idx].commDistance      = commDist;
        comm.cities[idx].isRegionalCapital = false;
        comm.cities[idx].hasGarrison       = hasGarrison;

        // Check if this city IS a regional capital
        for (const RegionalCapital& rc : regionalCapitals) {
            if (cityPos == rc.location) {
                comm.cities[idx].isRegionalCapital = true;
                break;
            }
        }

        ++comm.cityCount;
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
        return mods;  // Capital or adjacent city — no penalty
    }

    // Penalties scale with communication distance beyond 1.0
    float excessDist = dist - 1.0f;

    mods.loyaltyPenalty    = excessDist * COMM_LOYALTY_PENALTY_PER_TURN;
    mods.corruptionAdd     = excessDist * COMM_CORRUPTION_PER_TURN;

    float prodPenalty      = excessDist * COMM_PRODUCTION_PENALTY_PER_TURN;
    mods.productionMultiplier = std::max(0.50f, 1.0f - prodPenalty);

    float sciPenalty       = excessDist * COMM_SCIENCE_PENALTY_PER_TURN;
    mods.scienceMultiplier = std::max(0.50f, 1.0f - sciPenalty);

    // Garrison halves loyalty penalty
    if (commData.hasGarrison) {
        mods.loyaltyPenalty *= (1.0f - GARRISON_LOYALTY_REDUCTION);
    }

    // Regional capitals have reduced penalties (they ARE the local authority)
    if (commData.isRegionalCapital) {
        mods.loyaltyPenalty *= 0.5f;
        mods.corruptionAdd  *= 0.5f;
    }

    return mods;
}

// ============================================================================
// Per-turn processing
// ============================================================================

void processCommunication(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) {
            continue;
        }

        updateCommunicationDistances(gameState, grid, playerPtr->id());

        const PlayerCommunicationComponent& comm = playerPtr->communication();

        // Apply loyalty penalties derived from communication distance
        int32_t cityIdx = 0;
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr || cityIdx >= comm.cityCount) {
                break;
            }

            const PlayerCommunicationComponent::CityCommData& cityComm = comm.cities[cityIdx];
            CityCommModifiers mods = computeCityCommModifiers(cityComm);

            if (mods.loyaltyPenalty > 0.01f) {
                CityLoyaltyComponent& loyalty = cityPtr->loyalty();
                loyalty.loyalty -= mods.loyaltyPenalty;
                loyalty.loyalty  = std::max(0.0f, loyalty.loyalty);
            }

            ++cityIdx;
        }
    }
}

} // namespace aoc::sim

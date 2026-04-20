/**
 * @file CityLoyalty.cpp
 * @brief City loyalty computation with Civ 6-style pressure mechanics.
 *
 * Migrated from ECS to GameState object model.
 */

#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/Governor.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/tech/EraScore.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <unordered_map>

namespace aoc::sim {

/// Loyalty pressure radius (how far cities exert influence).
constexpr int32_t LOYALTY_PRESSURE_RADIUS = 9;

void computeCityLoyalty(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                        PlayerId player) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) {
        return;
    }

    // Look up the player's age type (Golden/Dark/Normal)
    AgeType playerAge = gsPlayer->eraScore().currentAgeType;

    // Religion provides a loyalty floor in the early eras (Ancient, Classical,
    // Medieval) and nothing afterwards.  Computed once per player because the
    // era coefficient is empire-wide.
    const float devotionLoyaltyCoef =
        religionLoyaltyCoefficient(effectiveEraFromTech(*gsPlayer));

    // Cap secessions at one per civ per turn. Prevents cascade where a low-loyalty
    // empire loses half its periphery in a single turn -- stress tests showed
    // same-turn multi-city loss was possible and bricks the affected civ.
    bool secededThisTurn = false;

    // Iterate all cities owned by this player. Cities captured/seceded away
    // remain in the old owner's vector (capture mechanic never rewires lists),
    // so filter by current owner to avoid processing stale entries twice.
    for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
        if (city->owner() != player) { continue; }
        CityLoyaltyComponent& loyalty = city->loyalty();

        // Reset breakdown for this turn
        loyalty.baseLoyalty = 8.0f;
        loyalty.ownCityPressure = 0.0f;
        loyalty.foreignCityPressure = 0.0f;
        loyalty.governorBonus = 0.0f;
        loyalty.garrisonBonus = 0.0f;
        loyalty.monumentBonus = 0.0f;
        loyalty.happinessEffect = 0.0f;
        loyalty.ageEffect = 0.0f;
        loyalty.capturedPenalty = 0.0f;
        loyalty.devotionBonus = 0.0f;

        // City pressure from ALL nearby cities (own and foreign).
        // Iterates all players' cities to compute cross-player pressure.
        for (const std::unique_ptr<aoc::game::Player>& otherPlayer : gameState.players()) {
            for (const std::unique_ptr<aoc::game::City>& nearCity : otherPlayer->cities()) {
                if (nearCity->location() == city->location()) { continue; }  // Skip self

                int32_t dist = grid.distance(city->location(), nearCity->location());
                if (dist > LOYALTY_PRESSURE_RADIUS || dist <= 0) { continue; }

                float pressure = static_cast<float>(nearCity->population()) * 0.5f
                               / static_cast<float>(dist);

                if (otherPlayer->id() == player) {
                    loyalty.ownCityPressure += pressure;
                } else {
                    loyalty.foreignCityPressure -= pressure;
                }
            }
        }

        // Governor bonus (+4 loyalty if governor is active)
        if (city->governor().isActive) {
            loyalty.governorBonus = 4.0f;
        }

        // Garrison bonus (+3 per military unit on the city tile, max 9)
        for (const std::unique_ptr<aoc::game::Unit>& unit : gsPlayer->units()) {
            if (unit->position() == city->location() && unit->isMilitary()) {
                loyalty.garrisonBonus += 3.0f;
            }
        }
        loyalty.garrisonBonus = std::min(loyalty.garrisonBonus, 9.0f);

        // Monument bonus (+2 per Monument building)
        const CityDistrictsComponent& districts = city->districts();
        for (const CityDistrictsComponent::PlacedDistrict& d : districts.districts) {
            for (BuildingId bid : d.buildings) {
                if (bid.value == 16) {  // Monument
                    loyalty.monumentBonus += 2.0f;
                }
            }
        }

        // Golden/Dark age effects
        if (playerAge == AgeType::Golden) {
            loyalty.ageEffect = 5.0f;
        } else if (playerAge == AgeType::Dark) {
            loyalty.ageEffect = -5.0f;
        }

        // Unhappiness penalty (-2 per point below 0)
        if (city->happiness().happiness < 0.0f) {
            loyalty.happinessEffect = city->happiness().happiness * 2.0f;
        }

        // Recently captured penalty. Bumped from -3 to -8 so captured
        // cities drop into Unrest and trigger the secession mechanic.
        // Stronger distance scaling broke capture dynamics entirely.
        if (city->originalOwner() != INVALID_PLAYER && city->originalOwner() != city->owner()) {
            loyalty.capturedPenalty = -8.0f;
        }

        // Devotion loyalty bonus: religion stabilises large empires in the
        // early eras.  Coefficient becomes zero at Renaissance+, at which
        // point the state -- not the church -- has to do the stabilising.
        if (devotionLoyaltyCoef > 0.0f) {
            const float netDevotion = computeCityNetDevotion(*city);
            loyalty.devotionBonus = netDevotion * devotionLoyaltyCoef;
        }

        // Sum it all up
        float change = loyalty.baseLoyalty
                     + loyalty.ownCityPressure
                     + loyalty.foreignCityPressure
                     + loyalty.governorBonus
                     + loyalty.garrisonBonus
                     + loyalty.monumentBonus
                     + loyalty.ageEffect
                     + loyalty.happinessEffect
                     + loyalty.capturedPenalty
                     + loyalty.devotionBonus;

        loyalty.loyaltyPerTurn = change;
        loyalty.loyalty += change;
        loyalty.loyalty = std::clamp(loyalty.loyalty, 0.0f, 100.0f);

        // Track consecutive turns below the Unrest threshold.
        if (loyalty.loyalty < 40.0f) {
            ++loyalty.unrestTurns;
        } else {
            loyalty.unrestTurns = 0;
        }

        // Secession path: sustained unrest in a distant city flips even above 0.
        // Captures "periphery secession" missed by the loyalty <= 0 gate when
        // pressure keeps the city hovering in Unrest without hitting bottom.
        bool triggerSecession = (loyalty.loyalty <= 0.0f);
        if (secededThisTurn) { triggerSecession = false; }
        if (!triggerSecession && loyalty.unrestTurns >= 3 && !secededThisTurn) {
            int32_t distFromCapital = 0;
            for (const std::unique_ptr<aoc::game::City>& other : gsPlayer->cities()) {
                if (other->isOriginalCapital()) {
                    distFromCapital = grid.distance(city->location(), other->location());
                    break;
                }
            }
            if (distFromCapital >= 5) {
                triggerSecession = true;
                LOG_INFO("SECESSION: %s (player %u) unrest %d turns, %d from capital",
                         city->name().c_str(),
                         static_cast<unsigned>(player),
                         loyalty.unrestTurns, distFromCapital);
            }
        }

        if (triggerSecession) {
            secededThisTurn = true;
            // Find the dominant neighbor (most pressure from nearby foreign cities)
            std::unordered_map<PlayerId, float> neighborPressure;
            for (const std::unique_ptr<aoc::game::Player>& otherPlayer : gameState.players()) {
                if (otherPlayer->id() == player) { continue; }
                for (const std::unique_ptr<aoc::game::City>& nearCity : otherPlayer->cities()) {
                    int32_t dist = grid.distance(city->location(), nearCity->location());
                    if (dist > LOYALTY_PRESSURE_RADIUS || dist <= 0) { continue; }
                    float pressure = static_cast<float>(nearCity->population()) * 0.5f
                                   / static_cast<float>(dist);
                    neighborPressure[otherPlayer->id()] += pressure;
                }
            }

            PlayerId bestNeighbor = INVALID_PLAYER;
            float bestPressure = 0.0f;
            for (const std::pair<const PlayerId, float>& entry : neighborPressure) {
                if (entry.second > bestPressure) {
                    bestPressure = entry.second;
                    bestNeighbor = entry.first;
                }
            }

            // Former owner remembers the loss: permanent grievance against
            // whoever gained the city (or a generic INVALID_PLAYER anchor for
            // Free Cities so the severity still counts toward world-stance).
            aoc::game::Player* formerOwner = gameState.player(player);
            const PlayerId gainer = (bestNeighbor != INVALID_PLAYER) ? bestNeighbor : INVALID_PLAYER;
            if (formerOwner != nullptr) {
                formerOwner->grievances().addGrievance(
                    GrievanceType::LostCityToSecession, gainer);
            }

            loyalty.unrestTurns = 0;

            if (bestNeighbor != INVALID_PLAYER) {
                LOG_INFO("REVOLT: %s (player %u) loyalty 0 -- flips to player %u!",
                         city->name().c_str(),
                         static_cast<unsigned>(player),
                         static_cast<unsigned>(bestNeighbor));
                city->setOwner(bestNeighbor);
                loyalty.loyalty = 50.0f;

                // Update tile ownership
                if (grid.isValid(city->location())) {
                    grid.setOwner(grid.toIndex(city->location()), bestNeighbor);
                }
                std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(city->location());
                for (const aoc::hex::AxialCoord& n : nbrs) {
                    if (grid.isValid(n)) {
                        grid.setOwner(grid.toIndex(n), bestNeighbor);
                    }
                }
            } else {
                LOG_INFO("REVOLT: %s (player %u) loyalty 0 -- becomes Free City!",
                         city->name().c_str(),
                         static_cast<unsigned>(player));
                city->setOwner(INVALID_PLAYER);
                loyalty.loyalty = 50.0f;
            }
        }
    }
}

} // namespace aoc::sim

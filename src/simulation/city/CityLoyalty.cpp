/**
 * @file CityLoyalty.cpp
 * @brief City loyalty computation with Civ 6-style pressure mechanics.
 *
 * Migrated from ECS to GameState object model.
 */

#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/balance/BalanceParams.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/Governor.hpp"
#include "aoc/simulation/city/Secession.hpp"
#include "aoc/simulation/tech/EraScore.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/simulation/diplomacy/WarWeariness.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"

#include <algorithm>

namespace aoc::sim {

// Loyalty pressure radius now lives in BalanceParams::loyaltyPressureRadius so
// the balance GA can retune it without a recompile.

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

    const aoc::balance::BalanceParams& bal = aoc::balance::params();

    // WP-A5 combined-stress revolt: cities currently in temporary Free-City
    // status still live in the original player's cities() vector. Tick down
    // before the normal loyalty pass so a city whose timer hit 0 resumes
    // normal owner processing the same turn.
    for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
        CityLoyaltyComponent& loyalty = city->loyalty();
        if (loyalty.revoltFreeCityTurns > 0) {
            --loyalty.revoltFreeCityTurns;
            if (loyalty.revoltFreeCityTurns == 0
                && loyalty.revoltOriginalOwner != INVALID_PLAYER) {
                city->setOwner(loyalty.revoltOriginalOwner);
                if (grid.isValid(city->location())) {
                    grid.setOwner(grid.toIndex(city->location()),
                                  loyalty.revoltOriginalOwner);
                }
                LOG_INFO("REVOLT-END: %s returns to player %u",
                         city->name().c_str(),
                         static_cast<unsigned>(loyalty.revoltOriginalOwner));
                loyalty.revoltOriginalOwner = INVALID_PLAYER;
                loyalty.loyalty = 50.0f;
                loyalty.unrestTurns = 0;
            }
        }
    }

    // WP-A5 combined-stress trigger eligibility: war weariness, grievance
    // count, and city-level unhappiness all above their thresholds simulcast
    // a soft 10-turn Free-City flip.
    const float weariness = gsPlayer->warWeariness().weariness;
    const bool  civStressed = (weariness > 40.0f)
                           && (gsPlayer->grievances().grievances.size() >= 4);

    // Iterate all cities owned by this player. Cities captured/seceded away
    // remain in the old owner's vector (capture mechanic never rewires lists),
    // so filter by current owner to avoid processing stale entries twice.
    for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
        if (city->owner() != player) { continue; }
        CityLoyaltyComponent& loyalty = city->loyalty();

        // Reset breakdown for this turn. baseLoyalty is balance-tunable --
        // at 8.0 the floor dominates and no city ever hit Unrest; default is
        // now 4.0 but the balance GA can sweep.
        loyalty.baseLoyalty = bal.baseLoyalty;
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
                if (dist > bal.loyaltyPressureRadius || dist <= 0) { continue; }

                float pressure = static_cast<float>(nearCity->population()) * 0.5f
                               / static_cast<float>(dist);

                if (otherPlayer->id() == player) {
                    loyalty.ownCityPressure += pressure;
                } else {
                    loyalty.foreignCityPressure -= pressure;
                }
            }
        }

        // H4.3: cap own and foreign pressure independently so an overwhelming
        // city count on one side can't crowd out the other. Without caps the
        // civ with more cities in the region always wins loyalty, making
        // asymmetric borders unrecoverable regardless of governance.
        loyalty.ownCityPressure = std::clamp(loyalty.ownCityPressure, 0.0f, 50.0f);
        loyalty.foreignCityPressure = std::clamp(loyalty.foreignCityPressure, -50.0f, 0.0f);

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

        // Era decay on foreign pressure + communication-building floor.
        // Idea: as a civ enters Electric / Digital / Information Age, the
        // physical-proximity pressure from rival cities matters less.  Mass
        // communication, travel, financial integration all dampen local
        // separatist momentum.  Cities with Telecom Hub or Research Lab
        // also get a flat loyalty floor (the infrastructure keeps citizens
        // plugged in even far from the capital).
        {
            // WP-C1: loyalty decay table sourced from BalanceParams so the
            // GA tuner can sweep it instead of hitting a compile wall.
            const uint8_t rev = static_cast<uint8_t>(gsPlayer->industrial().currentRevolution);
            const float mult = bal.loyaltyEraDecay[std::min<uint8_t>(rev, 5u)];
            loyalty.foreignCityPressure *= mult;
            // Symmetric: own-city pressure also decays slightly so big
            // empires aren't over-glued together.
            loyalty.ownCityPressure *= (0.80f + 0.20f * mult);
        }
        if (city->hasBuilding(BuildingId{13}) || city->hasBuilding(BuildingId{12})) {
            // Telecom Hub or Research Lab — +3 communication floor.
            loyalty.monumentBonus += 3.0f;
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

        // WP-A5 combined-stress revolt: if civ-level war-weariness and
        // grievance count are both high, AND the city itself is actively
        // unhappy (happiness < -1), flip to Free-City for 10 turns. Softer
        // than true secession — city reverts automatically.
        if (civStressed && loyalty.revoltFreeCityTurns == 0
            && city->happiness().happiness < -1.0f) {
            loyalty.revoltFreeCityTurns = 10;
            loyalty.revoltOriginalOwner = player;
            city->setOwner(INVALID_PLAYER);
            if (grid.isValid(city->location())) {
                grid.setOwner(grid.toIndex(city->location()), INVALID_PLAYER);
            }
            LOG_INFO("COMBINED REVOLT: %s (P%u) → Free City for 10 turns "
                     "(weariness=%.1f, grievances=%zu, happiness=%.1f)",
                     city->name().c_str(), static_cast<unsigned>(player),
                     static_cast<double>(weariness),
                     gsPlayer->grievances().grievances.size(),
                     static_cast<double>(city->happiness().happiness));
            continue;
        }

        // Secession path: sustained unrest in a distant city flips even above 0.
        // Captures "periphery secession" missed by the loyalty <= 0 gate when
        // pressure keeps the city hovering in Unrest without hitting bottom.
        if (checkAndPerformSecession(gameState, grid, *city, loyalty, player,
                                     secededThisTurn)) {
            secededThisTurn = true;
        }
    }
}

} // namespace aoc::sim

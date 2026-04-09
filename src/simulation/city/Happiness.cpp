/**
 * @file Happiness.cpp
 * @brief City happiness calculation.
 */

#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/Inflation.hpp"
#include "aoc/simulation/monetary/FiscalPolicy.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/diplomacy/WarWeariness.hpp"
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/production/Waste.hpp"
#include "aoc/ecs/World.hpp"

#include <cmath>

namespace aoc::sim {

void computeCityHappiness(aoc::ecs::World& world, PlayerId player) {
    aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    // Find player's war weariness penalty
    float warWearinessPenalty = 0.0f;
    const aoc::ecs::ComponentPool<PlayerWarWearinessComponent>* wwPool =
        world.getPool<PlayerWarWearinessComponent>();
    if (wwPool != nullptr) {
        for (uint32_t i = 0; i < wwPool->size(); ++i) {
            if (wwPool->data()[i].owner == player) {
                warWearinessPenalty = warWearinessHappinessPenalty(
                    wwPool->data()[i].weariness);
                break;
            }
        }
    }

    // Find player's monetary state for inflation/tax penalties
    float inflationPenalty = 0.0f;
    float taxPenalty = 0.0f;
    const aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool != nullptr) {
        for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
            const MonetaryStateComponent& ms = monetaryPool->data()[i];
            if (ms.owner == player) {
                inflationPenalty = inflationHappinessPenalty(ms.inflationRate);
                taxPenalty = -taxHappinessModifier(ms.taxRate);
                break;
            }
        }
    }

    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        CityComponent& city = cityPool->data()[i];
        if (city.owner != player) {
            continue;
        }

        EntityId cityEntity = cityPool->entities()[i];

        if (!world.hasComponent<CityHappinessComponent>(cityEntity)) {
            world.addComponent<CityHappinessComponent>(cityEntity, CityHappinessComponent{});
        }
        CityHappinessComponent& happiness = world.getComponent<CityHappinessComponent>(cityEntity);

        // Base amenities: 1 from palace/capital
        happiness.amenities = 1.0f;

        // Amenity bonus from luxury resources and processed goods
        const CityStockpileComponent* stockpile =
            world.tryGetComponent<CityStockpileComponent>(cityEntity);
        if (stockpile != nullptr) {
            // Each unique luxury resource provides +1 amenity (like Civ 6)
            if (stockpile->getAmount(goods::WINE) > 0)    { happiness.amenities += 1.0f; }
            if (stockpile->getAmount(goods::SPICES) > 0)  { happiness.amenities += 1.0f; }
            if (stockpile->getAmount(goods::SILK) > 0)    { happiness.amenities += 1.0f; }
            if (stockpile->getAmount(goods::IVORY) > 0)   { happiness.amenities += 1.0f; }
            if (stockpile->getAmount(goods::GEMS) > 0)    { happiness.amenities += 1.0f; }
            if (stockpile->getAmount(goods::DYES) > 0)    { happiness.amenities += 1.0f; }
            if (stockpile->getAmount(goods::FURS) > 0)    { happiness.amenities += 1.0f; }
            if (stockpile->getAmount(goods::INCENSE) > 0) { happiness.amenities += 1.0f; }
            if (stockpile->getAmount(goods::SUGAR) > 0)   { happiness.amenities += 0.5f; }
            // Processed luxury goods
            if (stockpile->getAmount(goods::CLOTHING) > 0)          { happiness.amenities += 1.5f; }
            if (stockpile->getAmount(goods::ADV_CONSUMER_GOODS) > 0){ happiness.amenities += 2.0f; }
            if (stockpile->getAmount(goods::CONSUMER_GOODS) > 0)    { happiness.amenities += 1.0f; }
        }

        // Amenity bonus from buildings (districts and their buildings provide comfort)
        const CityDistrictsComponent* districts =
            world.tryGetComponent<CityDistrictsComponent>(cityEntity);
        if (districts != nullptr) {
            // Each district beyond CityCenter adds +0.5 amenity (urban services)
            for (const CityDistrictsComponent::PlacedDistrict& d : districts->districts) {
                if (d.type != DistrictType::CityCenter) {
                    happiness.amenities += 0.5f;
                }
                // Specific buildings that provide amenities
                for (BuildingId bid : d.buildings) {
                    // Granary: +0.5 (food security)
                    if (bid.value == 15) { happiness.amenities += 0.5f; }
                    // Hospital: +1.0 (healthcare)
                    if (bid.value == 22) { happiness.amenities += 1.0f; }
                    // Market: +0.5 (commerce)
                    if (bid.value == 6) { happiness.amenities += 0.5f; }
                    // Monument: +0.5 (culture)
                    if (bid.value == 16) { happiness.amenities += 0.5f; }
                }
            }
        }

        // Amenity bonus from wonders in this city
        const CityWondersComponent* cityWonders =
            world.tryGetComponent<CityWondersComponent>(cityEntity);
        if (cityWonders != nullptr) {
            for (const WonderId wid : cityWonders->wonders) {
                const WonderDef& wdef = wonderDef(wid);
                happiness.amenities += wdef.effect.amenityBonus;
            }
        }

        // Religion follower belief amenity bonus
        const CityReligionComponent* cityReligion =
            world.tryGetComponent<CityReligionComponent>(cityEntity);
        if (cityReligion != nullptr) {
            ReligionId dominant = cityReligion->dominantReligion();
            if (dominant != NO_RELIGION) {
                const aoc::ecs::ComponentPool<GlobalReligionTracker>* trackerPool =
                    world.getPool<GlobalReligionTracker>();
                if (trackerPool != nullptr && trackerPool->size() > 0) {
                    const GlobalReligionTracker& tracker = trackerPool->data()[0];
                    if (dominant < tracker.religionsFoundedCount) {
                        const ReligionDef& religion = tracker.religions[dominant];
                        if (religion.followerBelief < BELIEF_COUNT) {
                            const BeliefDef& belief = allBeliefs()[religion.followerBelief];
                            happiness.amenities += belief.amenityBonus;
                        }
                    }
                }
            }
        }

        // Demand: scales sub-linearly with population (sqrt-based)
        // Small cities (pop 3): demand 1.4. Pop 10: demand 2.5. Pop 20: demand 3.6.
        // This is much gentler than the old 0.5 per citizen which made large cities always unhappy.
        happiness.demand = std::sqrt(static_cast<float>(city.population)) * 0.8f;

        // Modifiers from economy and war weariness
        happiness.modifiers = -inflationPenalty - taxPenalty + warWearinessPenalty;

        // Currency crisis amenity penalty
        const aoc::ecs::ComponentPool<CurrencyCrisisComponent>* crisisPool =
            world.getPool<CurrencyCrisisComponent>();
        if (crisisPool != nullptr) {
            for (uint32_t ci = 0; ci < crisisPool->size(); ++ci) {
                if (crisisPool->data()[ci].owner == player) {
                    happiness.amenities -= static_cast<float>(crisisPool->data()[ci].amenityPenalty());
                    break;
                }
            }
        }

        // Pollution amenity penalty
        const CityPollutionComponent* pollution =
            world.tryGetComponent<CityPollutionComponent>(cityEntity);
        if (pollution != nullptr) {
            happiness.amenities -= static_cast<float>(pollution->amenityPenalty());
        }

        // Net happiness
        happiness.happiness = happiness.amenities - happiness.demand + happiness.modifiers;
    }
}

} // namespace aoc::sim

/**
 * @file Happiness.cpp
 * @brief City happiness calculation.
 */

#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/Inflation.hpp"
#include "aoc/simulation/monetary/FiscalPolicy.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/ecs/World.hpp"

namespace aoc::sim {

void computeCityHappiness(aoc::ecs::World& world, PlayerId player) {
    aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
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

        // Amenity bonus from luxury processed goods in city stockpile
        const CityStockpileComponent* stockpile =
            world.tryGetComponent<CityStockpileComponent>(cityEntity);
        if (stockpile != nullptr) {
            // Clothing: +1 amenity if city has any
            if (stockpile->getAmount(goods::CLOTHING) > 0) {
                happiness.amenities += 1.0f;
            }
            // Advanced Consumer Goods: +2 amenities
            if (stockpile->getAmount(goods::ADV_CONSUMER_GOODS) > 0) {
                happiness.amenities += 2.0f;
            }
            // Consumer Goods: +0.5 amenity
            if (stockpile->getAmount(goods::CONSUMER_GOODS) > 0) {
                happiness.amenities += 0.5f;
            }
            // Wine: +1 amenity (luxury)
            if (stockpile->getAmount(goods::WINE) > 0) {
                happiness.amenities += 1.0f;
            }
            // Spices: +0.5 amenity (luxury)
            if (stockpile->getAmount(goods::SPICES) > 0) {
                happiness.amenities += 0.5f;
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

        // Demand: 1 per 2 citizens
        happiness.demand = static_cast<float>(city.population) * 0.5f;

        // Modifiers from economy
        happiness.modifiers = -inflationPenalty - taxPenalty;

        // Net happiness
        happiness.happiness = happiness.amenities - happiness.demand + happiness.modifiers;
    }
}

} // namespace aoc::sim

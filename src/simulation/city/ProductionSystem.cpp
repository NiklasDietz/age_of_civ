/**
 * @file ProductionSystem.cpp
 * @brief City production queue processing implementation.
 */

#include "aoc/simulation/city/ProductionSystem.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/city/DistrictAdjacency.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/diplomacy/WarWeariness.hpp"
#include "aoc/simulation/tech/EraScore.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/economy/EnvironmentModifier.hpp"
#include "aoc/simulation/monetary/Inflation.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <vector>

namespace aoc::sim {

int32_t computeMaxProductionSlots(bool hasIndustrialDistrict,
                                   bool hasFactory,
                                   bool hasIndustrialRevolution,
                                   bool hasFiatMoney,
                                   int32_t population) {
    // Population provides base labor slots: 1 per 5 population
    int32_t laborSlots = std::max(1, population / 5);

    // Buildings/tech provide infrastructure cap (organization, tools, capital)
    int32_t infraCap = 1;  // Every city has 1 base slot
    if (hasIndustrialDistrict)    { ++infraCap; }
    if (hasFactory)               { ++infraCap; }
    if (hasIndustrialRevolution)  { ++infraCap; }
    if (hasFiatMoney)             { ++infraCap; }

    // Actual slots = min(labor available, infrastructure capacity), max 5
    return std::min(std::min(laborSlots, infraCap), 5);
}

float computeCityProduction(const aoc::ecs::World& world,
                             const aoc::map::HexGrid& grid,
                             EntityId cityEntity) {
    const CityComponent& city = world.getComponent<CityComponent>(cityEntity);

    // Sum production from worked tiles (with environment modifiers on improvements)
    float totalProduction = 0.0f;
    for (const hex::AxialCoord& tile : city.workedTiles) {
        if (grid.isValid(tile)) {
            const int32_t index = grid.toIndex(tile);
            const aoc::map::TileYield yield = grid.tileYield(index);
            float tileProduction = static_cast<float>(yield.production);

            // Apply environment modifier to the improvement portion of the yield
            const aoc::map::ImprovementType imp = grid.improvement(index);
            if (imp != aoc::map::ImprovementType::None) {
                const aoc::map::TileYield impYield = aoc::map::improvementYieldBonus(imp);
                const float envMod = computeImprovementEnvironmentModifier(grid, index, imp);
                // Adjust: subtract base improvement production, add modified version
                const float impProduction = static_cast<float>(impYield.production);
                tileProduction += impProduction * (envMod - 1.0f);
            }

            totalProduction += tileProduction;
        }
    }

    // Add building production bonuses + district adjacency bonuses
    const CityDistrictsComponent* districts =
        world.tryGetComponent<CityDistrictsComponent>(cityEntity);
    if (districts != nullptr) {
        for (const CityDistrictsComponent::PlacedDistrict& district : districts->districts) {
            for (BuildingId bid : district.buildings) {
                const BuildingDef& bdef = buildingDef(bid);
                totalProduction += static_cast<float>(bdef.productionBonus);
            }
            // District adjacency production bonus
            if (grid.isValid(district.location)) {
                AdjacencyBonus adj = computeAdjacencyBonus(
                    grid, world, district.type, grid.toIndex(district.location));
                totalProduction += adj.production;
            }
        }
    }

    // Apply happiness production multiplier
    const CityHappinessComponent* happiness =
        world.tryGetComponent<CityHappinessComponent>(cityEntity);
    if (happiness != nullptr) {
        totalProduction *= happiness->productionMultiplier();
    }

    // Apply loyalty yield penalty (Disloyal: -25%, Unrest: -50%)
    const CityLoyaltyComponent* loyaltyComp =
        world.tryGetComponent<CityLoyaltyComponent>(cityEntity);
    if (loyaltyComp != nullptr) {
        totalProduction *= loyaltyComp->yieldMultiplier();
    }

    // Apply government production multiplier
    GovernmentModifiers govMods = computeGovernmentModifiers(world, city.owner);
    totalProduction *= govMods.productionMultiplier;

    // Apply civilization production multiplier
    const aoc::ecs::ComponentPool<PlayerCivilizationComponent>* civPool =
        world.getPool<PlayerCivilizationComponent>();
    if (civPool != nullptr) {
        for (uint32_t ci = 0; ci < civPool->size(); ++ci) {
            const PlayerCivilizationComponent& civ = civPool->data()[ci];
            if (civ.owner == city.owner) {
                totalProduction *= civDef(civ.civId).modifiers.productionMultiplier;
                break;
            }
        }
    }

    // Apply war weariness production modifier
    const aoc::ecs::ComponentPool<PlayerWarWearinessComponent>* wwPool =
        world.getPool<PlayerWarWearinessComponent>();
    if (wwPool != nullptr) {
        for (uint32_t wi = 0; wi < wwPool->size(); ++wi) {
            if (wwPool->data()[wi].owner == city.owner) {
                totalProduction *= warWearinessProductionModifier(
                    wwPool->data()[wi].weariness);
                break;
            }
        }
    }

    // Apply golden/dark age yield modifier
    const aoc::ecs::ComponentPool<PlayerEraScoreComponent>* eraScorePool =
        world.getPool<PlayerEraScoreComponent>();
    if (eraScorePool != nullptr) {
        for (uint32_t ei = 0; ei < eraScorePool->size(); ++ei) {
            if (eraScorePool->data()[ei].owner == city.owner) {
                const AgeType age = eraScorePool->data()[ei].currentAgeType;
                if (age == AgeType::Golden) {
                    totalProduction *= 1.1f;   // +10% all yields
                } else if (age == AgeType::Dark) {
                    totalProduction *= 0.85f;  // -15% all yields
                }
                break;
            }
        }
    }

    // Apply corruption loss (scales with empire size and government type)
    {
        GovernmentType govType = GovernmentType::Chiefdom;
        const aoc::ecs::ComponentPool<PlayerGovernmentComponent>* govPool =
            world.getPool<PlayerGovernmentComponent>();
        if (govPool != nullptr) {
            for (uint32_t gi = 0; gi < govPool->size(); ++gi) {
                if (govPool->data()[gi].owner == city.owner) {
                    govType = govPool->data()[gi].government;
                    break;
                }
            }
        }
        int32_t playerCityCount = 0;
        const aoc::ecs::ComponentPool<CityComponent>* allCities = world.getPool<CityComponent>();
        if (allCities != nullptr) {
            for (uint32_t ci2 = 0; ci2 < allCities->size(); ++ci2) {
                if (allCities->data()[ci2].owner == city.owner) { ++playerCityCount; }
            }
        }
        float corruption = computeCorruption(govType, playerCityCount,
                                              govMods.corruptionReduction);
        totalProduction *= (1.0f - corruption);
    }

    // Inflation reduces effective production (supply chain disruption, wage demands).
    // Mild inflation (1-3%) slightly helps. High inflation hurts significantly.
    const aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool2 =
        world.getPool<MonetaryStateComponent>();
    if (monetaryPool2 != nullptr) {
        for (uint32_t mi = 0; mi < monetaryPool2->size(); ++mi) {
            if (monetaryPool2->data()[mi].owner == city.owner) {
                float inflationMod = inflationProductionModifier(
                    monetaryPool2->data()[mi].inflationRate);
                // inflationMod > 1.0 means costs are higher, so production buys less
                totalProduction /= inflationMod;
                break;
            }
        }
    }

    // Minimum 1 production per turn so cities always make progress
    if (totalProduction < 1.0f) {
        totalProduction = 1.0f;
    }

    return totalProduction;
}

void processProductionQueues(aoc::ecs::World& world,
                              const aoc::map::HexGrid& grid,
                              PlayerId player) {
    aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool == nullptr) {
        return;
    }

    // Collect city entities first to avoid iterator invalidation
    std::vector<EntityId> cityEntities;
    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        if (cityPool->data()[i].owner == player) {
            cityEntities.push_back(cityPool->entities()[i]);
        }
    }

    for (EntityId cityEntity : cityEntities) {
        if (!world.isAlive(cityEntity)) {
            continue;
        }

        ProductionQueueComponent* queue =
            world.tryGetComponent<ProductionQueueComponent>(cityEntity);
        if (queue == nullptr || queue->isEmpty()) {
            continue;
        }

        float production = computeCityProduction(world, grid, cityEntity);
        bool completed = queue->addProgress(production);

        if (completed) {
            const ProductionQueueItem& item = queue->queue.front();
            const CityComponent& city =
                world.getComponent<CityComponent>(cityEntity);

            switch (item.type) {
                case ProductionItemType::Unit: {
                    UnitTypeId unitTypeId{item.itemId};
                    EntityId unitEntity = world.createEntity();
                    world.addComponent<UnitComponent>(
                        unitEntity,
                        UnitComponent::create(player, unitTypeId, city.location));
                    LOG_INFO("Produced %.*s in %s",
                             static_cast<int>(item.name.size()),
                             item.name.c_str(),
                             city.name.c_str());
                    break;
                }
                case ProductionItemType::Building: {
                    BuildingId buildingId{item.itemId};
                    CityDistrictsComponent* districts =
                        world.tryGetComponent<CityDistrictsComponent>(cityEntity);
                    if (districts == nullptr) {
                        // Create districts component if it doesn't exist
                        world.addComponent<CityDistrictsComponent>(cityEntity, CityDistrictsComponent{});
                        districts = &world.getComponent<CityDistrictsComponent>(cityEntity);
                    }
                    {
                        // Find or create the required district
                        const BuildingDef& bdef = buildingDef(buildingId);
                        bool placed = false;
                        for (CityDistrictsComponent::PlacedDistrict& district :
                             districts->districts) {
                            if (district.type == bdef.requiredDistrict) {
                                district.buildings.push_back(buildingId);
                                placed = true;
                                break;
                            }
                        }
                        if (!placed) {
                            // Create new district of the required type at city location
                            CityDistrictsComponent::PlacedDistrict newDistrict;
                            newDistrict.type = bdef.requiredDistrict;
                            newDistrict.location = city.location;
                            newDistrict.buildings.push_back(buildingId);
                            districts->districts.push_back(std::move(newDistrict));
                        }
                    }
                    LOG_INFO("Built %.*s in %s",
                             static_cast<int>(item.name.size()),
                             item.name.c_str(),
                             city.name.c_str());
                    break;
                }
                case ProductionItemType::District: {
                    CityDistrictsComponent* districts =
                        world.tryGetComponent<CityDistrictsComponent>(cityEntity);
                    if (districts != nullptr) {
                        CityDistrictsComponent::PlacedDistrict newDistrict;
                        newDistrict.type = static_cast<DistrictType>(item.itemId);
                        newDistrict.location = city.location;
                        districts->districts.push_back(std::move(newDistrict));
                    }
                    LOG_INFO("Completed district %.*s in %s",
                             static_cast<int>(item.name.size()),
                             item.name.c_str(),
                             city.name.c_str());
                    break;
                }
                case ProductionItemType::Wonder: {
                    const WonderId wonderId = static_cast<WonderId>(item.itemId);

                    // Mark built in global tracker
                    aoc::ecs::ComponentPool<GlobalWonderTracker>* trackerPool =
                        world.getPool<GlobalWonderTracker>();
                    if (trackerPool != nullptr && trackerPool->size() > 0) {
                        GlobalWonderTracker& tracker = trackerPool->data()[0];
                        tracker.markBuilt(wonderId, player);
                    }

                    // Add to city's wonder list
                    CityWondersComponent* cityWonders =
                        world.tryGetComponent<CityWondersComponent>(cityEntity);
                    if (cityWonders == nullptr) {
                        world.addComponent<CityWondersComponent>(
                            cityEntity, CityWondersComponent{});
                        cityWonders = world.tryGetComponent<CityWondersComponent>(cityEntity);
                    }
                    if (cityWonders != nullptr) {
                        cityWonders->wonders.push_back(wonderId);
                    }

                    LOG_INFO("Completed wonder %.*s in %s",
                             static_cast<int>(item.name.size()),
                             item.name.c_str(),
                             city.name.c_str());
                    break;
                }
            }

            queue->popCompleted();
        }
    }
}

} // namespace aoc::sim

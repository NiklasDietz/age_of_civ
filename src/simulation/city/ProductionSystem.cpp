/**
 * @file ProductionSystem.cpp
 * @brief City production queue processing implementation.
 *
 * Migrated from ECS to GameState object model.
 */

#include "aoc/simulation/city/ProductionSystem.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/city/DistrictAdjacency.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/event/VisibilityEvents.hpp"
#include "aoc/simulation/diplomacy/WarWeariness.hpp"
#include "aoc/simulation/tech/EraScore.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/economy/EnvironmentModifier.hpp"
#include "aoc/simulation/monetary/Inflation.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

#include <vector>

namespace aoc::sim {

static float computeCityProductionGS(const aoc::game::Player& player,
                                      const aoc::game::City& city,
                                      const aoc::map::HexGrid& grid,
                                      const aoc::game::GameState& gameState) {
    // Sum production from worked tiles
    float totalProduction = 0.0f;
    for (const aoc::hex::AxialCoord& tile : city.workedTiles()) {
        if (grid.isValid(tile)) {
            int32_t index = grid.toIndex(tile);
            aoc::map::TileYield yield = grid.tileYield(index);
            totalProduction += static_cast<float>(yield.production);
        }
    }

    // Building production bonuses + district adjacency
    const CityDistrictsComponent& districts = city.districts();
    for (const CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
        for (BuildingId bid : district.buildings) {
            totalProduction += static_cast<float>(buildingDef(bid).productionBonus);
        }
        // Adjacency bonus (still uses legacy world for cross-city district lookup)
        if (grid.isValid(district.location)) {
            AdjacencyBonus adj = computeAdjacencyBonus(
                grid, gameState, district.type, grid.toIndex(district.location));
            totalProduction += adj.production;
        }
    }

    // Happiness production multiplier
    totalProduction *= city.happiness().productionMultiplier();

    // Loyalty yield penalty
    totalProduction *= city.loyalty().yieldMultiplier();

    // Government production multiplier
    GovernmentModifiers govMods = computeGovernmentModifiers(player.government());
    totalProduction *= govMods.productionMultiplier;

    // Civilization production multiplier
    totalProduction *= civDef(player.civId()).modifiers.productionMultiplier;

    // Wonder productionMultiplier (H4.9): Pyramids, Ruhr Valley. Applied before
    // stability modifiers so corruption/inflation still erode a fortified pipe.
    for (const WonderId wid : city.wonders().wonders) {
        const float mult = wonderDef(wid).effect.productionMultiplier;
        if (mult > 0.0f) {
            totalProduction *= mult;
        }
    }

    // War weariness production modifier
    totalProduction *= warWearinessProductionModifier(player.warWeariness().weariness);

    // Golden/Dark age modifier
    if (player.eraScore().currentAgeType == AgeType::Golden) {
        totalProduction *= 1.1f;
    } else if (player.eraScore().currentAgeType == AgeType::Dark) {
        totalProduction *= 0.85f;
    }

    // Corruption
    float corruption = computeCorruption(player.government().government,
                                          player.cityCount(), govMods.corruptionReduction);
    totalProduction *= (1.0f - corruption);

    // Inflation modifier
    float inflationMod = inflationProductionModifier(player.monetary().inflationRate);
    totalProduction /= inflationMod;

    // Minimum 1 per turn
    if (totalProduction < 1.0f) {
        totalProduction = 1.0f;
    }

    return totalProduction;
}

void processProductionQueues(aoc::game::GameState& gameState,
                              const aoc::map::HexGrid& grid,
                              PlayerId player) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return; }

    for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
        ProductionQueueComponent& queue = city->production();
        if (queue.isEmpty()) { continue; }

        float production = computeCityProductionGS(*gsPlayer, *city, grid, gameState);
        bool completed = queue.addProgress(production);

        if (completed) {
            const ProductionQueueItem& item = queue.queue.front();

            // Check resource requirements
            CityStockpileComponent& stockpile = city->stockpile();
            bool resourcesAvailable = true;

            if (item.type == ProductionItemType::Unit) {
                const UnitTypeDef& udef = unitTypeDef(UnitTypeId{item.itemId});
                for (const UnitResourceReq& req : udef.resourceReqs) {
                    if (req.isValid() && stockpile.getAmount(req.goodId) < req.amount) {
                        resourcesAvailable = false;
                        break;
                    }
                }
            } else if (item.type == ProductionItemType::Building) {
                const BuildingDef& bdef = buildingDef(BuildingId{item.itemId});
                for (const BuildingResourceCost& cost : bdef.resourceCosts) {
                    if (cost.isValid() && stockpile.getAmount(cost.goodId) < cost.amount) {
                        resourcesAvailable = false;
                        break;
                    }
                }
            }

            if (!resourcesAvailable) {
                queue.queue.front().progress = queue.queue.front().totalCost;
                continue;
            }

            // Consume resources
            if (item.type == ProductionItemType::Unit) {
                const UnitTypeDef& udef = unitTypeDef(UnitTypeId{item.itemId});
                for (const UnitResourceReq& req : udef.resourceReqs) {
                    if (req.isValid()) {
                        [[maybe_unused]] bool ok = stockpile.consumeGoods(req.goodId, req.amount);
                    }
                }
            } else if (item.type == ProductionItemType::Building) {
                const BuildingDef& bdef = buildingDef(BuildingId{item.itemId});
                for (const BuildingResourceCost& cost : bdef.resourceCosts) {
                    if (cost.isValid()) {
                        [[maybe_unused]] bool ok = stockpile.consumeGoods(cost.goodId, cost.amount);
                    }
                }
            }

            // Complete the item
            switch (item.type) {
                case ProductionItemType::Unit: {
                    UnitTypeId unitTypeId{item.itemId};
                    gsPlayer->addUnit(unitTypeId, city->location());
                    LOG_INFO("Produced %.*s in %s",
                             static_cast<int>(item.name.size()),
                             item.name.c_str(),
                             city->name().c_str());
                    break;
                }
                case ProductionItemType::Building: {
                    BuildingId buildingId{item.itemId};
                    CityDistrictsComponent& districts = city->districts();
                    const BuildingDef& bdef = buildingDef(buildingId);
                    bool placed = false;
                    for (CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
                        if (district.type == bdef.requiredDistrict) {
                            district.buildings.push_back(buildingId);
                            placed = true;
                            break;
                        }
                    }
                    if (!placed) {
                        CityDistrictsComponent::PlacedDistrict newDistrict;
                        newDistrict.type = bdef.requiredDistrict;
                        newDistrict.location = city->location();
                        newDistrict.buildings.push_back(buildingId);
                        districts.districts.push_back(std::move(newDistrict));
                    }
                    LOG_INFO("Built %.*s in %s",
                             static_cast<int>(item.name.size()),
                             item.name.c_str(),
                             city->name().c_str());
                    break;
                }
                case ProductionItemType::District: {
                    CityDistrictsComponent& districts = city->districts();
                    CityDistrictsComponent::PlacedDistrict newDistrict;
                    newDistrict.type = static_cast<DistrictType>(item.itemId);
                    newDistrict.location = city->location();
                    districts.districts.push_back(std::move(newDistrict));
                    LOG_INFO("Completed district %.*s in %s",
                             static_cast<int>(item.name.size()),
                             item.name.c_str(),
                             city->name().c_str());
                    break;
                }
                case ProductionItemType::Wonder: {
                    WonderId wonderId = static_cast<WonderId>(item.itemId);
                    city->wonders().wonders.push_back(wonderId);
                    gameState.wonderTracker().markBuilt(wonderId, city->owner());
                    LOG_INFO("Completed wonder %.*s in %s (player %u)",
                             static_cast<int>(item.name.size()),
                             item.name.c_str(),
                             city->name().c_str(),
                             static_cast<unsigned>(city->owner()));
                    {
                        VisibilityEvent ev{};
                        ev.type = VisibilityEventType::WonderCompleted;
                        ev.location = city->location();
                        ev.actor = city->owner();
                        ev.payload = static_cast<int32_t>(wonderId);
                        gameState.visibilityBus().emit(ev);
                    }
                    // H4.8: refund 50% of invested production (as gold, 1:1) to
                    // every other civ that had the same wonder queued, and drop
                    // the queue entry so they don't waste another turn on it.
                    for (const std::unique_ptr<aoc::game::Player>& otherPtr : gameState.players()) {
                        if (otherPtr == nullptr) { continue; }
                        if (otherPtr->id() == city->owner()) { continue; }
                        for (const std::unique_ptr<aoc::game::City>& otherCity : otherPtr->cities()) {
                            if (otherCity == nullptr) { continue; }
                            ProductionQueueComponent& otherQueue = otherCity->production();
                            for (auto qit = otherQueue.queue.begin(); qit != otherQueue.queue.end(); ) {
                                if (qit->type == ProductionItemType::Wonder
                                    && static_cast<WonderId>(qit->itemId) == wonderId) {
                                    const int32_t refund = static_cast<int32_t>(qit->progress * 0.5f);
                                    if (refund > 0) {
                                        otherPtr->setTreasury(otherPtr->treasury()
                                            + static_cast<CurrencyAmount>(refund));
                                    }
                                    LOG_INFO("Wonder race loss: Player %u refunded %d gold from %.*s in %s",
                                             static_cast<unsigned>(otherPtr->id()),
                                             refund,
                                             static_cast<int>(qit->name.size()),
                                             qit->name.c_str(),
                                             otherCity->name().c_str());
                                    qit = otherQueue.queue.erase(qit);
                                } else {
                                    ++qit;
                                }
                            }
                        }
                    }
                    break;
                }
            }

            queue.popCompleted();
        }
    }
}

ErrorCode purchaseInCity(aoc::game::GameState& /*gameState*/,
                         aoc::game::Player& player,
                         aoc::game::City& city,
                         ProductionItemType type,
                         uint16_t itemId) {
    float baseCost = 0.0f;

    if (type == ProductionItemType::Unit) {
        const UnitTypeDef& udef = unitTypeDef(UnitTypeId{itemId});
        baseCost = static_cast<float>(udef.productionCost);
    } else if (type == ProductionItemType::Building) {
        const BuildingDef& bdef = buildingDef(BuildingId{itemId});
        baseCost = static_cast<float>(bdef.productionCost);
    } else {
        return ErrorCode::InvalidArgument;
    }

    const int32_t goldCost = purchaseCost(baseCost);
    if (goldCost <= 0) {
        return ErrorCode::InvalidArgument;
    }

    if (player.treasury() < static_cast<CurrencyAmount>(goldCost)) {
        return ErrorCode::InsufficientResources;
    }

    // Deduct gold.
    player.setTreasury(player.treasury() - static_cast<CurrencyAmount>(goldCost));

    // Create the item immediately.
    if (type == ProductionItemType::Unit) {
        player.addUnit(UnitTypeId{itemId}, city.location());
        LOG_INFO("Purchased %.*s in %s for %d gold (player %u)",
                 static_cast<int>(unitTypeDef(UnitTypeId{itemId}).name.size()),
                 unitTypeDef(UnitTypeId{itemId}).name.data(),
                 city.name().c_str(),
                 goldCost,
                 static_cast<unsigned>(player.id()));
    } else if (type == ProductionItemType::Building) {
        const BuildingDef& bdef = buildingDef(BuildingId{itemId});
        CityDistrictsComponent& districts = city.districts();
        bool placed = false;
        for (CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
            if (district.type == bdef.requiredDistrict) {
                district.buildings.push_back(BuildingId{itemId});
                placed = true;
                break;
            }
        }
        if (!placed) {
            CityDistrictsComponent::PlacedDistrict newDistrict;
            newDistrict.type = bdef.requiredDistrict;
            newDistrict.location = city.location();
            newDistrict.buildings.push_back(BuildingId{itemId});
            districts.districts.push_back(std::move(newDistrict));
        }
        LOG_INFO("Purchased %.*s in %s for %d gold (player %u)",
                 static_cast<int>(bdef.name.size()),
                 bdef.name.data(),
                 city.name().c_str(),
                 goldCost,
                 static_cast<unsigned>(player.id()));
    }

    return ErrorCode::Ok;
}

} // namespace aoc::sim

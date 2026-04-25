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
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/economy/EnvironmentModifier.hpp"
#include "aoc/simulation/monetary/Inflation.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/simulation/map/Improvement.hpp"

#include <algorithm>
#include <array>
#include <unordered_set>
#include <vector>
#include "aoc/core/Log.hpp"

#include <vector>

namespace aoc::sim {

// WP-C3: city is fully powered if a plant-owning city can reach it through
// a chain of same-owner PowerPole tiles (BFS). City that contains a plant
// is trivially powered.
static bool cityHasPowerPlant(const aoc::game::City& c) {
    for (const CityDistrictsComponent::PlacedDistrict& d : c.districts().districts) {
        for (BuildingId bid : d.buildings) {
            if (bid.value >= 26 && bid.value <= 35) {
                return true;
            }
        }
    }
    return false;
}

static bool cityIsPowered(const aoc::game::Player& player,
                          const aoc::game::City& city,
                          const aoc::map::HexGrid& grid) {
    if (cityHasPowerPlant(city)) {
        return true;
    }
    if (!grid.isValid(city.location())) {
        return false;
    }

    // Collect same-player plant-city center tile indices for fast check.
    std::unordered_set<int32_t> plantTileIndices;
    for (const std::unique_ptr<aoc::game::City>& other : player.cities()) {
        if (other.get() == &city) { continue; }
        if (!grid.isValid(other->location())) { continue; }
        if (cityHasPowerPlant(*other)) {
            plantTileIndices.insert(grid.toIndex(other->location()));
        }
    }
    if (plantTileIndices.empty()) {
        return false;
    }

    // BFS: start at city center, expand across owned PowerPole tiles, plus
    // allow traversal through *any* own-city center (acts as a hub so plant
    // and destination don't each need an adjacent pole on their own tile).
    std::unordered_set<int32_t> ownedCityCenters;
    for (const std::unique_ptr<aoc::game::City>& c : player.cities()) {
        if (grid.isValid(c->location())) {
            ownedCityCenters.insert(grid.toIndex(c->location()));
        }
    }
    const int32_t startIdx = grid.toIndex(city.location());
    std::vector<int32_t> frontier{startIdx};
    std::unordered_set<int32_t> visited{startIdx};
    constexpr std::size_t MAX_BFS_NODES = 256;

    while (!frontier.empty() && visited.size() < MAX_BFS_NODES) {
        const int32_t idx = frontier.back();
        frontier.pop_back();
        if (plantTileIndices.count(idx) != 0) {
            return true;
        }
        const aoc::hex::AxialCoord pos = grid.toAxial(idx);
        const std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(pos);
        for (const aoc::hex::AxialCoord& n : nbrs) {
            if (!grid.isValid(n)) { continue; }
            const int32_t nIdx = grid.toIndex(n);
            if (visited.count(nIdx) != 0) { continue; }
            if (grid.owner(nIdx) != player.id()) { continue; }
            const bool passable = grid.hasPowerPole(nIdx)
                               || ownedCityCenters.count(nIdx) != 0;
            if (!passable) { continue; }
            visited.insert(nIdx);
            frontier.push_back(nIdx);
        }
    }
    return false;
}

static float computeCityProductionGS(const aoc::game::Player& player,
                                      const aoc::game::City& city,
                                      const aoc::map::HexGrid& grid,
                                      const aoc::game::GameState& gameState) {
    // Sum production from worked tiles
    float totalProduction = 0.0f;
    for (const aoc::hex::AxialCoord& tile : city.workedTiles()) {
        if (grid.isValid(tile)) {
            int32_t index = grid.toIndex(tile);
            aoc::map::TileYield yield = effectiveTileYield(grid, index);
            totalProduction += static_cast<float>(yield.production);
        }
    }

    // Building production bonuses + district adjacency
    const CivilizationDef& civSpec = civDef(player.civId());
    const CityDistrictsComponent& districts = city.districts();
    for (const CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
        for (BuildingId bid : district.buildings) {
            totalProduction += static_cast<float>(buildingDef(bid).productionBonus);
            // Civ-6 style unique building bonus: if civ has uniqueBuilding
            // with this base, add productionBonus.
            if (civSpec.uniqueBuilding.baseBuilding == bid) {
                totalProduction += static_cast<float>(civSpec.uniqueBuilding.productionBonus);
            }
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

    // WP-C3 power-grid bonus: BFS from this city's center along same-owner
    // PowerPole tiles (own-city centers act as hubs). If it reaches a
    // plant-hosting city, full +25% bonus. Else fall back to a small
    // adjacency readiness bonus (+5% per adjacent pole, cap +15%) so
    // civs that wire poles pre-plant still see a modest return.
    if (grid.isValid(city.location())) {
        if (cityIsPowered(player, city, grid)) {
            totalProduction *= 1.25f;
        } else {
            const int32_t cityIdx = grid.toIndex(city.location());
            int32_t poleCount = 0;
            if (grid.hasPowerPole(cityIdx) && grid.owner(cityIdx) == player.id()) {
                ++poleCount;
            }
            const std::array<aoc::hex::AxialCoord, 6> nbrs =
                aoc::hex::neighbors(city.location());
            for (const aoc::hex::AxialCoord& n : nbrs) {
                if (!grid.isValid(n)) { continue; }
                const int32_t nIdx = grid.toIndex(n);
                if (grid.hasPowerPole(nIdx) && grid.owner(nIdx) == player.id()) {
                    ++poleCount;
                }
            }
            const float gridBonus = std::min(0.15f, 0.05f * static_cast<float>(poleCount));
            totalProduction *= (1.0f + gridBonus);
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

        // A7 unit-class wonder boosts:
        //   - Statue of Liberty (20): +50% Settler production, empire-wide.
        //   - Venetian Arsenal (17): doubles Naval unit production (+100%),
        //     empire-wide. Bonus applied only in cities whose production
        //     queue front is a unit of the matching class.
        if (!queue.isEmpty()
            && queue.queue.front().type == ProductionItemType::Unit) {
            const UnitTypeDef& udef = unitTypeDef(UnitTypeId{queue.queue.front().itemId});
            bool hasStatue = false;
            bool hasArsenal = false;
            for (const std::unique_ptr<aoc::game::City>& c : gsPlayer->cities()) {
                const CityWondersComponent& cw = c->wonders();
                if (!hasStatue  && cw.hasWonder(static_cast<aoc::sim::WonderId>(20))) {
                    hasStatue = true;
                }
                if (!hasArsenal && cw.hasWonder(static_cast<aoc::sim::WonderId>(17))) {
                    hasArsenal = true;
                }
                if (hasStatue && hasArsenal) { break; }
            }
            if (hasStatue && udef.unitClass == UnitClass::Settler) {
                production *= 1.50f;
            }
            if (hasArsenal && udef.unitClass == UnitClass::Naval) {
                production *= 2.00f;
            }
        }

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
                    aoc::game::Unit& newUnit =
                        gsPlayer->addUnit(unitTypeId, city->location());
                    // A7 Pyramids (0) unique effect: +1 builder charge on
                    // newly produced Civilian-class (Builder) units.
                    if (newUnit.typeDef().unitClass == UnitClass::Civilian) {
                        bool hasPyramids = false;
                        for (const std::unique_ptr<aoc::game::City>& c
                                : gsPlayer->cities()) {
                            if (c->wonders().hasWonder(
                                    static_cast<aoc::sim::WonderId>(0))) {
                                hasPyramids = true;
                                break;
                            }
                        }
                        if (hasPyramids && newUnit.chargesRemaining() > 0) {
                            newUnit.setChargesRemaining(
                                newUnit.chargesRemaining() + 1);
                        }
                    }
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

                    // A7 Oracle (WonderId 13): grant the owner one free
                    // researchable civic — the first eligible civic whose
                    // prereqs are met and which isn't already completed.
                    if (wonderId == 13) {
                        PlayerCivicComponent& civics = gsPlayer->civics();
                        const uint16_t total = aoc::sim::civicCount();
                        for (uint16_t cid = 0; cid < total; ++cid) {
                            const CivicId c{cid};
                            if (!civics.hasCompleted(c) && civics.canResearch(c)) {
                                if (c.value < civics.completedCivics.size()) {
                                    civics.completedCivics[c.value] = true;
                                }
                                LOG_INFO("Oracle: player %u granted free civic %u",
                                         static_cast<unsigned>(gsPlayer->id()),
                                         static_cast<unsigned>(cid));
                                break;
                            }
                        }
                    }
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

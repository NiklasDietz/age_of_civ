/**
 * @file EconomySimulation.cpp
 * @brief Per-turn economic simulation implementation.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/simulation/economy/ResourceCurse.hpp"
#include "aoc/simulation/economy/InternalTrade.hpp"
#include "aoc/simulation/economy/EnvironmentModifier.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/Inflation.hpp"
#include "aoc/simulation/monetary/FiscalPolicy.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/economy/AdvancedEconomics.hpp"
#include "aoc/simulation/monetary/CentralBank.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/CurrencyWar.hpp"
#include "aoc/simulation/monetary/ForexMarket.hpp"
#include "aoc/simulation/monetary/Bonds.hpp"
#include "aoc/simulation/economy/Sanctions.hpp"
#include "aoc/simulation/economy/Speculation.hpp"
#include "aoc/simulation/economy/ColonialEconomics.hpp"
#include "aoc/simulation/economy/EconomicDepth.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"
#include "aoc/simulation/production/ProductionEfficiency.hpp"
#include "aoc/simulation/production/BuildingCapacity.hpp"
#include "aoc/simulation/production/QualityTier.hpp"
#include "aoc/simulation/production/Waste.hpp"
#include "aoc/simulation/production/PowerGrid.hpp"
#include "aoc/simulation/production/Automation.hpp"
#include "aoc/map/HexGrid.hpp"

#include <algorithm>
#include <unordered_set>

namespace aoc::sim {

EconomySimulation::EconomySimulation() = default;

void EconomySimulation::initialize() {
    this->m_productionChain.build();
    this->m_market.initialize();

    LOG_INFO("Initialized: %zu recipes in production chain, %u goods on market",
             this->m_productionChain.executionOrder().size(),
             static_cast<unsigned>(goodCount()));
}

void EconomySimulation::executeTurn(aoc::game::GameState& gameState, aoc::map::HexGrid& grid) {
    this->harvestResources(gameState, grid);
    this->applyResourceDepletion(gameState, grid);
    this->processInternalTradeForAllPlayers(gameState, grid);
    this->consumeBuildingFuel(gameState, grid);
    this->executeProduction(gameState, grid);
    this->reportToMarket(gameState);
    this->computePlayerNeeds(gameState);
    this->m_market.updatePrices();
    this->executeTradeRoutes(gameState);
    this->settleTradeInCoins(gameState);
    this->updateCoinReservesFromStockpiles(gameState);
    this->tickMonetaryMechanics(gameState);
    this->processCrisisAndBonds(gameState);
    this->processEconomicZonesAndSpeculation(gameState, grid);
    this->executeMonetaryPolicy(gameState);
}

// ============================================================================
// Step 1: Harvest raw resources from worked tiles into city stockpiles
// ============================================================================

void EconomySimulation::harvestResources(aoc::game::GameState& gameState,
                                          aoc::map::HexGrid& grid) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }

            CityStockpileComponent& stockpile = cityPtr->stockpile();

            for (const aoc::hex::AxialCoord& tileCoord : cityPtr->workedTiles()) {
                if (!grid.isValid(tileCoord)) {
                    continue;
                }
                int32_t tileIndex = grid.toIndex(tileCoord);

                aoc::map::ImprovementType imp = grid.improvement(tileIndex);
                uint16_t cultivatedGood = 0;
                bool isCultivated = false;
                switch (imp) {
                    case aoc::map::ImprovementType::Vineyard:    cultivatedGood = goods::WINE;   isCultivated = true; break;
                    case aoc::map::ImprovementType::SilkFarm:    cultivatedGood = goods::SILK;   isCultivated = true; break;
                    case aoc::map::ImprovementType::SpiceFarm:   cultivatedGood = goods::SPICES; isCultivated = true; break;
                    case aoc::map::ImprovementType::DyeWorks:    cultivatedGood = goods::DYES;   isCultivated = true; break;
                    case aoc::map::ImprovementType::CottonField: cultivatedGood = goods::COTTON; isCultivated = true; break;
                    default: break;
                }
                if (isCultivated) {
                    stockpile.addGoods(cultivatedGood, 2);
                }

                ResourceId resId = grid.resource(tileIndex);
                if (!resId.isValid()) {
                    continue;
                }

                uint16_t goodId = resId.value;
                if (goodId >= goodCount()) {
                    continue;
                }

                TechId revealTech = resourceRevealTech(goodId);
                if (revealTech.isValid()) {
                    if (!playerPtr->tech().hasResearched(revealTech)) {
                        continue;
                    }
                }

                int32_t yield = 1;
                if (imp == aoc::map::ImprovementType::Mine) {
                    yield = 2;
                } else if (imp == aoc::map::ImprovementType::Plantation
                           || imp == aoc::map::ImprovementType::Camp
                           || imp == aoc::map::ImprovementType::Pasture) {
                    yield = 2;
                }

                int16_t currentReserves = grid.reserves(tileIndex);
                if (currentReserves >= 0) {
                    int32_t actualYield = std::min(yield, static_cast<int32_t>(currentReserves));
                    if (actualYield <= 0) {
                        continue;
                    }
                    grid.setReserves(tileIndex, static_cast<int16_t>(currentReserves - actualYield));
                    if (grid.reserves(tileIndex) <= 0) {
                        grid.setResource(tileIndex, ResourceId{});
                        grid.setReserves(tileIndex, 0);
                        LOG_INFO("Resource exhausted at tile (%d,%d): %.*s depleted after extraction",
                                 tileCoord.q, tileCoord.r,
                                 static_cast<int>(goodDef(goodId).name.size()),
                                 goodDef(goodId).name.data());
                    }
                    stockpile.addGoods(goodId, actualYield);
                } else {
                    stockpile.addGoods(goodId, yield);
                }
            }
        }
    }
}

// ============================================================================
// Step 1b: Consume ongoing fuel for buildings
// ============================================================================

void EconomySimulation::consumeBuildingFuel(aoc::game::GameState& gameState,
                                             const aoc::map::HexGrid& grid) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }

            const CityDistrictsComponent& districts = cityPtr->districts();
            CityStockpileComponent&       stockpile  = cityPtr->stockpile();

            // Fusion Reactor deuterium self-supply for coastal cities
            if (districts.hasBuilding(BuildingId{35})) {
                bool isCoastal = false;
                std::array<aoc::hex::AxialCoord, 6> neighbors = aoc::hex::neighbors(cityPtr->location());
                for (const aoc::hex::AxialCoord& nbr : neighbors) {
                    if (grid.isValid(nbr)) {
                        int32_t nbrIdx = grid.toIndex(nbr);
                        if (aoc::map::isWater(grid.terrain(nbrIdx))) {
                            isCoastal = true;
                            break;
                        }
                    }
                }
                if (isCoastal) {
                    stockpile.addGoods(goods::DEUTERIUM, 1);
                }
            }

            for (const CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
                for (BuildingId bid : district.buildings) {
                    const BuildingDef& bdef = buildingDef(bid);
                    if (!bdef.needsFuel()) { continue; }
                    int32_t available = stockpile.getAmount(bdef.ongoingFuelGoodId);
                    if (available >= bdef.ongoingFuelPerTurn) {
                        [[maybe_unused]] bool ok =
                            stockpile.consumeGoods(bdef.ongoingFuelGoodId, bdef.ongoingFuelPerTurn);
                    }
                }
            }
        }
    }
}

// ============================================================================
// Step 1c: Compute per-player resource needs
// ============================================================================

void EconomySimulation::computePlayerNeeds(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        PlayerEconomyComponent& econ = playerPtr->economy();
        econ.totalNeeds.clear();

        // Aggregate stockpile across all cities
        std::unordered_map<uint16_t, int32_t> totalStock;
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }
            for (const std::pair<const uint16_t, int32_t>& entry : cityPtr->stockpile().goods) {
                totalStock[entry.first] += entry.second;
            }
        }

        // Recipe input needs
        for (const ProductionRecipe& recipe : allRecipes()) {
            bool hasBuildingSomewhere = false;
            for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
                if (cityPtr == nullptr) { continue; }
                if (cityPtr->districts().hasBuilding(recipe.requiredBuilding)) {
                    hasBuildingSomewhere = true;
                    break;
                }
            }
            if (!hasBuildingSomewhere) { continue; }

            for (const RecipeInput& input : recipe.inputs) {
                int32_t have = 0;
                std::unordered_map<uint16_t, int32_t>::iterator it = totalStock.find(input.goodId);
                if (it != totalStock.end()) { have = it->second; }
                int32_t deficit = input.amount - have;
                if (deficit > 0) {
                    econ.totalNeeds[input.goodId] += deficit;
                }
            }
        }

        // Building fuel needs
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }
            for (const CityDistrictsComponent::PlacedDistrict& d : cityPtr->districts().districts) {
                for (BuildingId bid : d.buildings) {
                    const BuildingDef& bdef = buildingDef(bid);
                    if (bdef.needsFuel()) {
                        int32_t have = 0;
                        std::unordered_map<uint16_t, int32_t>::iterator it =
                            totalStock.find(bdef.ongoingFuelGoodId);
                        if (it != totalStock.end()) { have = it->second; }
                        if (have < bdef.ongoingFuelPerTurn) {
                            econ.totalNeeds[bdef.ongoingFuelGoodId] +=
                                (bdef.ongoingFuelPerTurn - have);
                        }
                    }
                }
            }
        }

        // Population-based consumption: citizens consume goods each turn.
        // This is the core demand driver that makes production meaningful.
        // Without it, goods pile up in stockpiles with no purpose.
        {
            const int32_t totalPop = playerPtr->totalPopulation();

            // Food: 1 Wheat per 3 citizens (supplementing tile food yields)
            econ.totalNeeds[goods::WHEAT] += totalPop / 3;

            // Consumer Goods: modern citizens expect manufactured products
            if (totalPop > 3) {
                econ.totalNeeds[goods::CONSUMER_GOODS] += (totalPop - 3) / 3 + 1;
            }

            // Processed Food: larger cities need processed food, not just raw wheat
            if (totalPop > 8) {
                econ.totalNeeds[goods::PROCESSED_FOOD] += (totalPop - 8) / 4 + 1;
            }

            // Clothing: all citizens need clothing
            econ.totalNeeds[goods::CLOTHING] += totalPop / 5 + 1;

            // Advanced Consumer Goods: wealthy large populations
            if (totalPop > 15) {
                econ.totalNeeds[goods::ADV_CONSUMER_GOODS] += (totalPop - 15) / 5 + 1;
            }

            // Actually consume these goods from stockpiles each turn
            // (not just register as demand — actually deplete them)
            for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
                if (cityPtr == nullptr) { continue; }
                CityStockpileComponent& stock = cityPtr->stockpile();
                const int32_t cityPop = cityPtr->population();
                [[maybe_unused]] bool c1 = stock.consumeGoods(goods::WHEAT, cityPop / 3);
                if (cityPop > 3) {
                    [[maybe_unused]] bool c2 = stock.consumeGoods(goods::CONSUMER_GOODS, (cityPop - 3) / 4);
                }
                if (cityPop > 5) {
                    [[maybe_unused]] bool c3 = stock.consumeGoods(goods::PROCESSED_FOOD, (cityPop - 5) / 5);
                }
                [[maybe_unused]] bool c4 = stock.consumeGoods(goods::CLOTHING, cityPop / 6);
            }
        }

        // Count unique luxuries
        constexpr uint16_t RAW_LUXURY_IDS[] = {
            goods::WINE, goods::SPICES, goods::SILK, goods::IVORY, goods::GEMS,
            goods::DYES, goods::FURS, goods::INCENSE, goods::SUGAR,
            goods::PEARLS, goods::TEA, goods::COFFEE, goods::TOBACCO
        };
        int32_t uniqueCount = 0;
        for (uint16_t luxId : RAW_LUXURY_IDS) {
            std::unordered_map<uint16_t, int32_t>::iterator it = totalStock.find(luxId);
            if (it != totalStock.end() && it->second > 0) {
                ++uniqueCount;
            } else {
                econ.totalNeeds[luxId] += 1;
            }
        }
        econ.uniqueLuxuryCount = uniqueCount;
    }
}

// ============================================================================
// Step 2: Execute production recipes
// ============================================================================

void EconomySimulation::executeProduction(aoc::game::GameState& gameState,
                                          aoc::map::HexGrid& grid) {
    // City is the authority for all subsystems: automation, quality, experience,
    // building levels, power, and strike are all owned by City objects.

    // --- Pass 1: per-city power + automation setup ---
    // Use a pointer-keyed map so we can correlate cities across both passes.
    std::unordered_map<aoc::game::City*, float> cityPowerEfficiency;

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }

            CityPowerComponent power = computeCityPower(gameState, grid, *cityPtr);
            power.energyDemand += cityPtr->automation().robotEnergyDemand();
            cityPowerEfficiency[cityPtr.get()] = power.powerEfficiency();

            if (power.hasNuclear) {
                // Use player id + city pointer address as a stable turn hash seed.
                uint32_t turnHash = this->m_depletionTurnCounter * 7919u
                    + static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cityPtr.get()));
                checkNuclearMeltdown(gameState, grid, *cityPtr, turnHash);
            }

            // Update robot automation directly on the city's owned components.
            CityStockpileComponent& stockpile    = cityPtr->stockpile();
            CityAutomationComponent& automation  = cityPtr->automation();
            int32_t robotsAvailable = stockpile.getAmount(ROBOT_WORKERS_GOOD);
            if (robotsAvailable <= 0) {
                automation.robotWorkers = 0;
            } else {
                automation.robotWorkers = robotsAvailable;
                ++automation.turnsSinceLastMaintenance;
                if (automation.turnsSinceLastMaintenance >= ROBOT_MAINTENANCE_INTERVAL) {
                    [[maybe_unused]] bool consumed = stockpile.consumeGoods(ROBOT_WORKERS_GOOD, 1);
                    automation.turnsSinceLastMaintenance = 0;
                    --automation.robotWorkers;
                }
            }
        }
    }

    // --- Pass 2: recipe execution ---
    struct CityProductionState {
        int32_t totalRecipesExecuted = 0;
        std::unordered_map<uint16_t, int32_t> buildingBatchesUsed;
    };
    std::unordered_map<aoc::game::City*, CityProductionState> cityState;

    for (const ProductionRecipe* recipe : this->m_productionChain.executionOrder()) {
        for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
            if (playerPtr == nullptr) { continue; }

            for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
                if (cityPtr == nullptr) { continue; }

                aoc::game::City* city = cityPtr.get();
                CityProductionState& state = cityState[city];

                if (city->strike().isOnStrike) {
                    continue;
                }

                int32_t robotSlots = city->automation().bonusRecipeSlots();
                const int32_t maxSlots = totalWorkerCapacity(city->population(), robotSlots);
                // Each recipe consumes workerSlots (1 for basic, 2-3 for advanced).
                // A city can only run recipes whose total slots fit within capacity.
                if (state.totalRecipesExecuted + recipe->workerSlots > maxSlots) {
                    continue;
                }

                // Tech gate: skip recipe if the player hasn't researched the required tech
                if (recipe->requiredTech.isValid()
                    && !playerPtr->tech().hasResearched(recipe->requiredTech)) {
                    continue;
                }

                const CityDistrictsComponent& districts = city->districts();
                if (!districts.hasBuilding(recipe->requiredBuilding)) {
                    continue;
                }

                const CityBuildingLevelsComponent& levels = city->buildingLevels();
                int32_t buildingCap   = levels.capacity(recipe->requiredBuilding);
                int32_t buildingLevel = levels.getLevel(recipe->requiredBuilding);
                int32_t buildingUsed  = state.buildingBatchesUsed[recipe->requiredBuilding.value];
                if (buildingUsed >= buildingCap) {
                    continue;
                }

                CityStockpileComponent& stockpile = city->stockpile();

                bool hasAllInputs = true;
                for (const RecipeInput& input : recipe->inputs) {
                    if (stockpile.getAmount(input.goodId) < input.amount) {
                        hasAllInputs = false;
                        break;
                    }
                }
                if (!hasAllInputs) {
                    continue;
                }

                float inputQualitySum = 0.0f;
                int32_t inputCount    = 0;
                CityQualityComponent& quality = city->quality();

                for (const RecipeInput& input : recipe->inputs) {
                    if (input.consumed) {
                        [[maybe_unused]] bool ok = stockpile.consumeGoods(input.goodId, input.amount);
                        float q = quality.consumeGoods(input.goodId, input.amount);
                        inputQualitySum += q;
                        ++inputCount;
                    }
                }

                float avgInputQuality = (inputCount > 0)
                    ? inputQualitySum / static_cast<float>(inputCount)
                    : 0.0f;

                // Compute infrastructure bonus inline (mirrors computeInfrastructureBonus).
                constexpr float BONUS_PER_INFRA = 0.05f;
                constexpr float MAX_INFRA_BONUS  = 1.5f;
                float infraBonus = 1.0f;
                for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
                    if (grid.isValid(tile) && grid.hasRoad(grid.toIndex(tile))) {
                        infraBonus += BONUS_PER_INFRA;
                    }
                }
                if (districts.hasDistrict(DistrictType::Harbor))  { infraBonus += BONUS_PER_INFRA; }
                if (districts.hasBuilding(BuildingId{23}))         { infraBonus += BONUS_PER_INFRA; }
                if (districts.hasBuilding(BuildingId{6}))          { infraBonus += BONUS_PER_INFRA; }
                if (districts.hasBuilding(BuildingId{20}))         { infraBonus += BONUS_PER_INFRA; }
                infraBonus = std::min(infraBonus, MAX_INFRA_BONUS);

                const float envModifier = computeEnvironmentModifier(
                    grid, city->location(), recipe->requiredBuilding);

                float powerEff = 1.0f;
                std::unordered_map<aoc::game::City*, float>::iterator powerIt =
                    cityPowerEfficiency.find(city);
                if (powerIt != cityPowerEfficiency.end()) {
                    powerEff = powerIt->second;
                }

                CityProductionExperienceComponent& experience = city->productionExperience();
                float expMultiplier = experience.efficiencyMultiplier(recipe->recipeId);
                experience.addExperience(recipe->recipeId);

                const float revMultiplier = playerPtr->industrial().cumulativeProductionMultiplier();

                // Tool efficiency: industrial buildings need Tools (good 63) to
                // operate at full capacity. Without tools, output is reduced to 60%.
                // This creates demand for the tools supply chain and makes the
                // Forge→Tools production path economically important.
                float toolEff = 1.0f;
                if (recipe->requiredBuilding.value <= 14  // Industrial buildings (0-14)
                    && recipe->requiredBuilding.value != 6  // Not Market
                    && recipe->requiredBuilding.value != 7) { // Not Library
                    constexpr uint16_t TOOLS_GOOD_ID = 63;
                    if (stockpile.getAmount(TOOLS_GOOD_ID) > 0) {
                        // Consume 1 tool per 3 recipe batches (tools wear out)
                        if (state.totalRecipesExecuted % 3 == 0) {
                            [[maybe_unused]] bool toolConsumed =
                                stockpile.consumeGoods(TOOLS_GOOD_ID, 1);
                        }
                    } else {
                        toolEff = 0.60f;  // No tools = 60% efficiency
                    }
                }

                const int32_t boostedOutput = std::max(1, static_cast<int32_t>(
                    static_cast<float>(recipe->outputAmount)
                    * infraBonus * envModifier * powerEff * expMultiplier
                    * revMultiplier * toolEff));
                stockpile.addGoods(recipe->outputGoodId, boostedOutput);

                bool hasPrecisionInstr = stockpile.getAmount(goods::PRECISION_INSTRUMENTS) > 0;
                uint32_t qualityHash   = this->m_depletionTurnCounter * 2654435761u
                    + static_cast<uint32_t>(reinterpret_cast<uintptr_t>(city)) * 2246822519u
                    + recipe->recipeId * 104729u;
                QualityTier outputQuality = determineOutputQuality(
                    buildingLevel,
                    experience.getExperience(recipe->recipeId),
                    hasPrecisionInstr,
                    avgInputQuality,
                    qualityHash);
                quality.addGoods(recipe->outputGoodId, boostedOutput, outputQuality);

                if (recipe->outputGoodId == goods::COPPER_COINS
                    || recipe->outputGoodId == goods::SILVER_COINS
                    || recipe->outputGoodId == goods::GOLD_BARS) {
                    int32_t existing = stockpile.getAmount(recipe->outputGoodId);
                    if (existing <= boostedOutput) {
                        LOG_INFO("First coins minted: %d x good %u in '%s' (player %u)",
                                 boostedOutput,
                                 static_cast<unsigned>(recipe->outputGoodId),
                                 city->name().c_str(),
                                 static_cast<unsigned>(city->owner()));
                    }
                }

                // Accumulate waste inline (mirrors accumulateWaste).
                WasteOutput waste = buildingWasteOutput(recipe->requiredBuilding);
                if (waste.amount > 0
                    && waste.type != static_cast<WasteType>(
                            static_cast<uint8_t>(WasteType::Count))) {
                    CityPollutionComponent& pollution = city->pollution();
                    pollution.wasteAccumulated += waste.amount;
                    if (waste.type == WasteType::Emissions) {
                        pollution.co2ContributionPerTurn += waste.amount;
                    }
                }

                state.totalRecipesExecuted += recipe->workerSlots;
                ++state.buildingBatchesUsed[recipe->requiredBuilding.value];
            }
        }
    }

    // --- Pass 3: waste treatment ---
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }
            CityPollutionComponent& pollution = cityPtr->pollution();
            if (pollution.wasteAccumulated <= 0) { continue; }
            if (!cityPtr->districts().hasBuilding(WASTE_TREATMENT_PLANT)) { continue; }
            constexpr int32_t TREATMENT_RATE = 5;
            int32_t treated = std::min(pollution.wasteAccumulated, TREATMENT_RATE);
            pollution.wasteAccumulated -= treated;
            if (treated > 0) {
                cityPtr->stockpile().addGoods(goods::CONSTRUCTION_MAT, treated / 2);
            }
        }
    }
}

// ============================================================================
// Internal trade: redistribute surplus goods between a player's own cities
// ============================================================================

void EconomySimulation::processInternalTradeForAllPlayers(aoc::game::GameState& gameState,
                                                          const aoc::map::HexGrid& grid) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        if (playerPtr->cities().empty()) { continue; }
        processInternalTrade(gameState, grid, playerPtr->id());
    }
}

// ============================================================================
// Resource depletion / renewable regeneration (counter tick only)
// ============================================================================

void EconomySimulation::applyResourceDepletion(aoc::game::GameState& /*gameState*/,
                                                aoc::map::HexGrid& /*grid*/) {
    ++this->m_depletionTurnCounter;
    // Depletion is handled via reserve consumption in harvestResources().
    // Future: add renewable regeneration for unworked tiles here.
}

// ============================================================================
// Step 3: Report supply/demand to the market
// ============================================================================

void EconomySimulation::reportToMarket(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }

            const CityStockpileComponent& stockpile = cityPtr->stockpile();
            for (const std::pair<const uint16_t, int32_t>& entry : stockpile.goods) {
                if (entry.second > 0) {
                    this->m_market.reportSupply(entry.first, entry.second);
                } else if (entry.second < 0) {
                    this->m_market.reportDemand(entry.first, -entry.second);
                }
            }

            // Food and consumer goods demand based on population
            this->m_market.reportDemand(goods::WHEAT, cityPtr->population());
            this->m_market.reportDemand(goods::CONSUMER_GOODS, cityPtr->population() / 3 + 1);
            if (cityPtr->population() > 5) {
                this->m_market.reportDemand(goods::PROCESSED_FOOD,
                    (cityPtr->population() - 5) / 2 + 1);
            }
            if (cityPtr->population() > 10) {
                this->m_market.reportDemand(goods::ADV_CONSUMER_GOODS,
                    (cityPtr->population() - 10) / 3 + 1);
            }
        }
    }
}

// ============================================================================
// Step 4: Execute active trade routes
// ============================================================================

void EconomySimulation::executeTradeRoutes(aoc::game::GameState& gameState) {
    for (TradeRouteComponent& route : gameState.tradeRoutes()) {
        if (route.turnsRemaining > 0) {
            --route.turnsRemaining;
            continue;
        }

        // Resolve destination stockpile via destPlayer + first city.
        // TradeRouteComponent carries destPlayer for exactly this purpose.
        aoc::game::Player* destPlayer = gameState.player(route.destPlayer);
        if (destPlayer == nullptr || destPlayer->cities().empty()) {
            continue;
        }
        // Deliver to the first (capital) city of the destination player.
        aoc::game::City* destCity = destPlayer->cities().front().get();
        if (destCity == nullptr) {
            continue;
        }
        CityStockpileComponent& destStockpile = destCity->stockpile();

        // Export price multiplier from source player's currency devaluation.
        float exportMult = 1.0f;
        aoc::game::Player* srcPlayer = gameState.player(route.sourcePlayer);
        if (srcPlayer != nullptr) {
            exportMult = srcPlayer->currencyDevaluation().exportPriceMultiplier();
        }

        for (const TradeOffer& offer : route.cargo) {
            int32_t adjusted = static_cast<int32_t>(
                static_cast<float>(offer.amountPerTurn) / std::max(0.5f, exportMult));
            destStockpile.addGoods(offer.goodId, adjusted);
        }

        int32_t baseTurns = static_cast<int32_t>(route.path.size()) / 5 + 1;

        // Tech check: Computers (TechId 16) reduces travel time by 1 turn.
        if (srcPlayer != nullptr && srcPlayer->hasResearched(TechId{16})) {
            baseTurns = std::max(1, baseTurns - 1);
        }

        route.turnsRemaining = baseTurns;
    }
}

// ============================================================================
// Step 5: Monetary policy
// ============================================================================

void EconomySimulation::executeMonetaryPolicy(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        MonetaryStateComponent& state = playerPtr->monetary();

        CurrencyAmount currentGDP = 0;
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }
            for (const std::pair<const uint16_t, int32_t>& entry : cityPtr->stockpile().goods) {
                if (entry.second > 0) {
                    currentGDP += static_cast<CurrencyAmount>(entry.second)
                                * static_cast<CurrencyAmount>(this->m_market.price(entry.first));
                }
            }
        }

        float bankingMult = bankingGDPMultiplier(state);
        currentGDP = static_cast<CurrencyAmount>(
            static_cast<float>(currentGDP) * bankingMult);

        CurrencyAmount prevGDP   = this->m_previousGDP[playerPtr->id()];
        CurrencyAmount prevMoney = this->m_previousMoneySupply[playerPtr->id()];

        executeFiscalPolicy(state, currentGDP);
        computeInflation(state, prevGDP, currentGDP, prevMoney);
        applyInflationEffects(state);

        this->m_previousGDP[playerPtr->id()]         = currentGDP;
        this->m_previousMoneySupply[playerPtr->id()] = state.moneySupply;
    }

    // Seigniorage: reserve currency holders earn income from foreign GDP
    CurrencyAmount totalGDP = 0;
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr != nullptr) {
            totalGDP += playerPtr->monetary().gdp;
        }
    }

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        const CurrencyTrustComponent& trust = playerPtr->currencyTrust();
        if (!trust.isReserveCurrency) { continue; }

        MonetaryStateComponent& reserveState = playerPtr->monetary();
        CurrencyAmount foreignGDP   = totalGDP - reserveState.gdp;
        CurrencyAmount seigniorage  = computeSeigniorage(reserveState, true, foreignGDP);
        if (seigniorage > 0) {
            reserveState.treasury += seigniorage;
        }
    }
}

// ============================================================================
// Step 4b: Settle trade route imbalances in coins
// ============================================================================

void EconomySimulation::settleTradeInCoins(aoc::game::GameState& gameState) {
    const std::vector<TradeRouteComponent>& tradeRoutes = gameState.tradeRoutes();

    struct PlayerPairHash {
        std::size_t operator()(const std::pair<PlayerId, PlayerId>& p) const {
            return std::hash<uint32_t>()(
                (static_cast<uint32_t>(p.first) << 16) | static_cast<uint32_t>(p.second));
        }
    };
    std::unordered_map<std::pair<PlayerId, PlayerId>, int32_t, PlayerPairHash> tradeFlows;

    for (const TradeRouteComponent& route : tradeRoutes) {
        if (route.turnsRemaining > 0) { continue; }

        int32_t cargoValue = 0;
        for (const TradeOffer& offer : route.cargo) {
            cargoValue += offer.amountPerTurn * this->m_market.price(offer.goodId);
        }

        if (cargoValue > 0) {
            tradeFlows[std::make_pair(route.sourcePlayer, route.destPlayer)] += cargoValue;
        }
    }

    std::unordered_set<uint64_t> processed;
    for (const std::pair<const std::pair<PlayerId, PlayerId>, int32_t>& entry : tradeFlows) {
        PlayerId pA = entry.first.first;
        PlayerId pB = entry.first.second;

        uint64_t pairKey = (static_cast<uint64_t>(std::min(pA, pB)) << 32)
                         | static_cast<uint64_t>(std::max(pA, pB));
        if (processed.count(pairKey) > 0) { continue; }
        processed.insert(pairKey);

        int32_t flowAtoB = entry.second;
        std::pair<PlayerId, PlayerId> reverseKey = std::make_pair(pB, pA);
        std::unordered_map<std::pair<PlayerId, PlayerId>, int32_t, PlayerPairHash>::iterator reverseIt =
            tradeFlows.find(reverseKey);
        int32_t flowBtoA   = (reverseIt != tradeFlows.end()) ? reverseIt->second : 0;
        int32_t netBalance = flowAtoB - flowBtoA;

        if (netBalance == 0) { continue; }

        PlayerId payer    = (netBalance > 0) ? pB : pA;
        PlayerId receiver = (netBalance > 0) ? pA : pB;
        int32_t paymentValue = std::abs(netBalance);

        aoc::game::Player* payerPlayer    = gameState.player(payer);
        aoc::game::Player* receiverPlayer = gameState.player(receiver);
        if (payerPlayer == nullptr || receiverPlayer == nullptr) { continue; }

        MonetaryStateComponent& payerState = payerPlayer->monetary();
        MonetaryStateComponent& recvState  = receiverPlayer->monetary();

        float efficiency = bilateralTradeEfficiency(gameState, payer, receiver);
        int32_t effectivePayment = static_cast<int32_t>(
            static_cast<float>(paymentValue) * efficiency * 0.05f);
        effectivePayment = std::max(1, effectivePayment);

        int32_t remaining = effectivePayment;

        if (remaining > 0 && payerState.goldBarReserves > 0) {
            int32_t goldToTransfer = std::min(payerState.goldBarReserves,
                (remaining + GOLD_BAR_VALUE - 1) / GOLD_BAR_VALUE);
            payerState.goldBarReserves -= goldToTransfer;
            recvState.goldBarReserves  += goldToTransfer;
            remaining -= goldToTransfer * GOLD_BAR_VALUE;
        }
        if (remaining > 0 && payerState.silverCoinReserves > 0) {
            int32_t silverToTransfer = std::min(payerState.silverCoinReserves,
                (remaining + SILVER_COIN_VALUE - 1) / SILVER_COIN_VALUE);
            payerState.silverCoinReserves -= silverToTransfer;
            recvState.silverCoinReserves  += silverToTransfer;
            remaining -= silverToTransfer * SILVER_COIN_VALUE;
        }
        if (remaining > 0 && payerState.copperCoinReserves > 0) {
            int32_t copperToTransfer = std::min(payerState.copperCoinReserves, remaining);
            payerState.copperCoinReserves -= copperToTransfer;
            recvState.copperCoinReserves  += copperToTransfer;
        }

        payerState.updateCoinTier();
        recvState.updateCoinTier();
    }
}

// ============================================================================
// Sync coin reserves from city stockpiles into the monetary state
// ============================================================================

void EconomySimulation::updateCoinReservesFromStockpiles(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        MonetaryStateComponent& state = playerPtr->monetary();
        state.copperCoinReserves = 0;
        state.silverCoinReserves = 0;
        state.goldBarReserves   = 0;

        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }
            const CityStockpileComponent& stockpile = cityPtr->stockpile();
            state.copperCoinReserves += stockpile.getAmount(goods::COPPER_COINS);
            state.silverCoinReserves += stockpile.getAmount(goods::SILVER_COINS);
            state.goldBarReserves   += stockpile.getAmount(goods::GOLD_BARS);
        }

        CoinTier previousTier = state.effectiveCoinTier;
        state.updateCoinTier();

        if (state.system == MonetarySystemType::Barter
            && previousTier == CoinTier::None
            && state.effectiveCoinTier != CoinTier::None) {
            LOG_INFO("Player %u reached coin tier %.*s: Cu=%d Ag=%d Au=%d",
                     static_cast<unsigned>(playerPtr->id()),
                     static_cast<int>(coinTierName(state.effectiveCoinTier).size()),
                     coinTierName(state.effectiveCoinTier).data(),
                     state.copperCoinReserves, state.silverCoinReserves, state.goldBarReserves);
        }

        if (state.effectiveCoinTier != previousTier) {
            LOG_INFO("Player %u coin tier changed: %.*s -> %.*s",
                     static_cast<unsigned>(playerPtr->id()),
                     static_cast<int>(coinTierName(previousTier).size()),
                     coinTierName(previousTier).data(),
                     static_cast<int>(coinTierName(state.effectiveCoinTier).size()),
                     coinTierName(state.effectiveCoinTier).data());
        }

        if (state.system == MonetarySystemType::CommodityMoney) {
            state.moneySupply = static_cast<CurrencyAmount>(state.totalCoinValue());
        }

        // === MONEY SUPPLY UPDATE ===
        // The coin stockpile determines the money supply, which governs:
        //   - Trade efficiency
        //   - Tax revenue base (taxation of circulating coins)
        //   - Inflation (more coins vs more goods)
        //
        // Treasury is NOT directly set to coinValue here. Treasury is the
        // government's spending account: it accumulates from income (taxation of
        // the money supply) and is drained by expenses (unit/building maintenance).
        // Starting at 0 with no money, it grows as coins are minted and taxed.
        //
        // This fixes the critical bug where treasury was overwritten each turn,
        // undoing all income and expense calculations from the previous turn.
        //
        // In BARTER mode (no coins): treasury is forced to 0 (no spending power).
        // In COMMODITY/GOLD/FIAT: moneySupply tracks coin pool, treasury accumulates.
        if (state.system == MonetarySystemType::Barter) {
            if (state.totalCoinCount() == 0) {
                playerPtr->setTreasury(0);
                state.treasury = 0;
            }
            // Once coins exist (transition just happened), let treasury accumulate.
        } else {
            // Update money supply for display, trade efficiency, and inflation.
            // CommodityMoney: moneySupply = physical coins
            // GoldStandard: moneySupply = coins + paper notes
            // Fiat: moneySupply also includes printed notes (managed separately)
            if (state.system == MonetarySystemType::CommodityMoney) {
                state.moneySupply = static_cast<CurrencyAmount>(state.totalCoinValue());
            } else if (state.system == MonetarySystemType::GoldStandard) {
                const int32_t coinWealth = state.totalCoinValue();
                state.moneySupply = static_cast<CurrencyAmount>(
                    static_cast<float>(coinWealth) * (1.0f + state.goldBackingRatio));
            }
            // Fiat moneySupply is managed by printMoney() and tracked separately.
        }
    }
}

// ============================================================================
// Per-turn monetary mechanic ticks
// ============================================================================

void EconomySimulation::tickMonetaryMechanics(aoc::game::GameState& gameState) {
    int32_t playerCount = static_cast<int32_t>(gameState.players().size());

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        MonetaryStateComponent& state = playerPtr->monetary();
        ++state.turnsInCurrentSystem;

        if (state.system == MonetarySystemType::CommodityMoney) {
            if (tickDebasementDiscovery(state)) {
                LOG_INFO("Player %u: coin debasement discovered by trade partners!",
                         static_cast<unsigned>(playerPtr->id()));
            }
        }

        if (state.system == MonetarySystemType::FiatMoney) {
            CurrencyTrustComponent& trust = playerPtr->currencyTrust();
            if (trust.trustScore == 0.0f) {
                // First fiat turn: initialise trust
                trust.owner      = playerPtr->id();
                trust.trustScore = 0.30f;
            }
            computeCurrencyTrust(gameState, state, trust, playerCount);
        }
    }

    updateReserveCurrencyStatus(gameState);
    updateExchangeRates(gameState);
}

// ============================================================================
// Currency crises, bond payments, and currency war processing
// ============================================================================

void EconomySimulation::processCrisisAndBonds(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }

        MonetaryStateComponent& state = playerPtr->monetary();
        CurrencyCrisisComponent& crisis = playerPtr->currencyCrisis();
        if (crisis.owner == INVALID_PLAYER) {
            crisis.owner = playerPtr->id();
        }
        processCurrencyCrisis(gameState, state, crisis);
    }

    processBondPayments(gameState);
    processIOUPayments(gameState);
    processCurrencyWar(gameState, this->m_currencyWarState);
}

// ============================================================================
// Economic zones, speculation, and sanctions
// ============================================================================

void EconomySimulation::processEconomicZonesAndSpeculation(aoc::game::GameState& gameState,
                                                            aoc::map::HexGrid& grid) {
    processEconomicZones(gameState, grid, this->m_market, this->m_economicZones);
    processSanctions(gameState, this->m_sanctions);
    processSpeculation(gameState, this->m_market);

    // Check industrial revolution progress for all players
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        checkIndustrialRevolution(gameState, playerPtr->id(),
                                  static_cast<TurnNumber>(this->m_depletionTurnCounter));
    }
}

} // namespace aoc::sim

/**
 * @file TurnProcessor.cpp
 * @brief Unified turn processing: the single source of truth for game logic.
 *
 * All ECS bridge synchronisation has been removed. Game state is accessed
 * exclusively through the GameState / Player / City / Unit object model.
 * been fully migrated: GlobalClimateComponent, GlobalOilReserves,
 * PlayerBubbleComponent, PlayerEnergyComponent, and processFlooding().
 */

#include "aoc/simulation/turn/TurnProcessor.hpp"
#include "aoc/simulation/turn/GameLength.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/RiverGameplay.hpp"

// City systems
#include "aoc/simulation/city/CityGrowth.hpp"
#include "aoc/simulation/city/CityScience.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/city/ProductionSystem.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/BorderExpansion.hpp"
#include "aoc/simulation/city/CityBombardment.hpp"
#include "aoc/simulation/city/CityConnection.hpp"

// Economy
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/economy/Maintenance.hpp"
#include "aoc/simulation/economy/AdvancedEconomics.hpp"
#include "aoc/simulation/economy/EconomicDepth.hpp"
#include "aoc/simulation/economy/Market.hpp"

// Tech
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/tech/EraScore.hpp"
#include "aoc/simulation/tech/EurekaBoost.hpp"

// Military
#include "aoc/simulation/unit/Movement.hpp"

// Diplomacy
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/WarWeariness.hpp"

// Government
#include "aoc/simulation/government/GovernmentComponent.hpp"

// Religion
#include "aoc/simulation/religion/Religion.hpp"

// Victory
#include "aoc/simulation/victory/VictoryCondition.hpp"

// Barbarians
#include "aoc/simulation/barbarian/BarbarianController.hpp"

// Great People
#include "aoc/simulation/greatpeople/GreatPeople.hpp"

// City-States
#include "aoc/simulation/citystate/CityState.hpp"

// Climate & Disasters
#include "aoc/simulation/climate/Climate.hpp"
#include "aoc/simulation/climate/NaturalDisasters.hpp"

// Empire
#include "aoc/simulation/empire/CommunicationSpeed.hpp"

// Events
#include "aoc/simulation/event/WorldEvents.hpp"

// Production
#include "aoc/simulation/production/Waste.hpp"

// Automation
#include "aoc/simulation/city/Governor.hpp"
#include "aoc/simulation/automation/Automation.hpp"

// AI
#include "aoc/simulation/ai/AIController.hpp"

// Monetary
#include "aoc/simulation/monetary/MonetarySystem.hpp"

// Object model
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"

// Civilization definitions (for city name lists)
#include "aoc/simulation/civilization/Civilization.hpp"

// Energy
#include "aoc/simulation/economy/EnergyDependency.hpp"

// Advanced economics
#include "aoc/simulation/economy/StockMarket.hpp"
#include "aoc/simulation/economy/TradeAgreement.hpp"
#include "aoc/simulation/economy/SupplyChain.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/economy/SpeculationBubble.hpp"
#include "aoc/simulation/economy/MonopolyPricing.hpp"
#include "aoc/simulation/economy/TechUnemployment.hpp"
#include "aoc/simulation/economy/BlackMarket.hpp"
#include "aoc/simulation/economy/HumanCapital.hpp"

// Logging
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <string>

namespace aoc::sim {

// ============================================================================
// City founding -- single source of truth
// ============================================================================

/**
 * @brief Found a new city for the given player at the specified location.
 *
 * Creates the City in the GameState object model, assigns worked tiles based
 * on yield scoring, claims the surrounding tiles on the hex grid, and logs
 * the event. Returns a reference to the newly created City.
 */
aoc::game::City& foundCity(aoc::game::GameState& gameState,
                            aoc::map::HexGrid& grid,
                            PlayerId owner,
                            aoc::hex::AxialCoord location,
                            const std::string& name,
                            bool isOriginalCapital,
                            int32_t startingPop) {
    aoc::game::Player* gsPlayer = gameState.player(owner);
    assert(gsPlayer != nullptr && "foundCity: player not found in GameState");

    aoc::game::City& city = gsPlayer->addCity(location, name);
    city.setOriginalCapital(isOriginalCapital);
    city.setOriginalOwner(owner);
    city.setPopulation(startingPop);

    // Center tile is always worked (free slot -- does not consume a citizen)
    city.workedTiles().push_back(location);

    // Auto-assign nearby tiles: prefer tiles with resources, then high yield
    std::array<aoc::hex::AxialCoord, 6> neighbors = aoc::hex::neighbors(location);

    struct TileScore {
        int32_t index;
        float score;
    };
    std::vector<TileScore> tileScores;
    tileScores.reserve(6);
    for (int32_t n = 0; n < 6; ++n) {
        if (!grid.isValid(neighbors[static_cast<std::size_t>(n)])) { continue; }
        int32_t idx = grid.toIndex(neighbors[static_cast<std::size_t>(n)]);
        if (aoc::map::isWater(grid.terrain(idx)) || aoc::map::isImpassable(grid.terrain(idx))) {
            continue;
        }
        float score = 0.0f;
        aoc::map::TileYield yield = grid.tileYield(idx);
        score += static_cast<float>(yield.food) * 2.0f;
        score += static_cast<float>(yield.production) * 1.5f;
        score += static_cast<float>(yield.gold) * 1.0f;
        if (grid.resource(idx).isValid()) {
            score += 5.0f;  // Strong preference for resource tiles
        }
        tileScores.push_back({n, score});
    }
    std::sort(tileScores.begin(), tileScores.end(),
        [](const TileScore& a, const TileScore& b) { return a.score > b.score; });

    // Assign workers up to population count (center tile is free)
    const int32_t maxWorkers = startingPop;
    int32_t assigned = 0;
    for (const TileScore& ts : tileScores) {
        if (assigned >= maxWorkers) { break; }
        city.workedTiles().push_back(neighbors[static_cast<std::size_t>(ts.index)]);
        ++assigned;
    }

    // Districts: CityCenter is always present at founding
    aoc::sim::CityDistrictsComponent::PlacedDistrict centerDistrict{};
    centerDistrict.type = DistrictType::CityCenter;
    centerDistrict.location = location;
    city.districts().districts.push_back(std::move(centerDistrict));

    // Loyalty starts at full
    city.loyalty().loyalty = 100.0f;

    // Claim the city tile and immediately adjacent tiles for this player
    const int32_t centerIdx = grid.toIndex(location);
    grid.setOwner(centerIdx, owner);
    for (const aoc::hex::AxialCoord& nbr : neighbors) {
        if (grid.isValid(nbr)) {
            const int32_t nbrIdx = grid.toIndex(nbr);
            if (grid.owner(nbrIdx) == INVALID_PLAYER) {
                grid.setOwner(nbrIdx, owner);
            }
        }
    }

    LOG_INFO("City founded: %s by player %u at (%d,%d)",
             name.c_str(), static_cast<unsigned>(owner),
             location.q, location.r);

    return city;
}

std::string getNextCityName(const aoc::game::GameState& gameState, PlayerId player) {
    const aoc::game::Player* gsPlayer = gameState.player(player);

    // Determine how many cities this player already owns to pick the next name.
    int32_t cityCount = (gsPlayer != nullptr) ? gsPlayer->cityCount() : 0;

    // Find the player's civilization to get its city name list.
    CivId civId{0};
    if (gsPlayer != nullptr) {
        civId = gsPlayer->civId();
    }

    const CivilizationDef& civ = civDef(civId);
    if (cityCount < static_cast<int32_t>(MAX_CIV_CITY_NAMES)) {
        return std::string(civ.cityNames[static_cast<std::size_t>(cityCount)]);
    }

    return std::string(civ.name) + " City " + std::to_string(cityCount + 1);
}

// ============================================================================
// Per-player turn processing
// ============================================================================

void processPlayerTurn(TurnContext& ctx, PlayerId player) {
    assert(ctx.gameState != nullptr && "GameState is required for turn processing");

    aoc::map::HexGrid& grid = *ctx.grid;
    aoc::game::Player* gsPlayer = ctx.gameState->player(player);
    assert(gsPlayer != nullptr && "processPlayerTurn: invalid player id");

    // Civilization meeting detection: check if any of our units/cities are
    // within 4 hexes of another player's entity. Triggers MeetCivilization eureka.
    {
        bool metAnotherCiv = false;

        // Check our units against all foreign cities
        for (const std::unique_ptr<aoc::game::Unit>& ownUnit : gsPlayer->units()) {
            if (metAnotherCiv) { break; }
            for (const std::unique_ptr<aoc::game::Player>& other : ctx.gameState->players()) {
                if (other->id() == player) { continue; }
                for (const std::unique_ptr<aoc::game::City>& foreignCity : other->cities()) {
                    if (aoc::hex::distance(ownUnit->position(), foreignCity->location()) <= 4) {
                        metAnotherCiv = true;
                        break;
                    }
                }
                if (metAnotherCiv) { break; }
            }
        }

        // Check our cities against all foreign units
        if (!metAnotherCiv) {
            for (const std::unique_ptr<aoc::game::City>& ownCity : gsPlayer->cities()) {
                if (metAnotherCiv) { break; }
                for (const std::unique_ptr<aoc::game::Player>& other : ctx.gameState->players()) {
                    if (other->id() == player) { continue; }
                    for (const std::unique_ptr<aoc::game::Unit>& foreignUnit : other->units()) {
                        if (aoc::hex::distance(foreignUnit->position(), ownCity->location()) <= 4) {
                            metAnotherCiv = true;
                            break;
                        }
                    }
                    if (metAnotherCiv) { break; }
                }
            }
        }

        if (metAnotherCiv) {
            checkEurekaConditions(*gsPlayer, EurekaCondition::MeetCivilization);
        }
    }

    // Trigger FoundCity eureka once the player has 2 or more cities
    if (gsPlayer->cityCount() >= 2) {
        checkEurekaConditions(*gsPlayer, EurekaCondition::FoundCity);
    }

    // Gold income: reads from Player/City objects, writes to Player::treasury()
    processGoldIncome(*gsPlayer, grid);

    // Maintenance: per-unit era-scaled, per-building, per-district, per-city
    processUnitMaintenance(*gsPlayer);
    processBuildingMaintenance(*gsPlayer);

    // City connections: uses GameState directly
    processCityConnections(*gsPlayer, grid);

    // Advanced economics (tariffs, banking, debt)
    processAdvancedEconomics(*ctx.gameState, grid, player, ctx.economy->market());

    // War weariness
    processWarWeariness(*gsPlayer, *ctx.diplomacy);

    // Golden/Dark age effects
    processAgeEffects(*gsPlayer);

    // City growth
    processCityGrowth(*gsPlayer, grid);

    // City happiness
    computeCityHappiness(*gsPlayer);

    // City loyalty
    computeCityLoyalty(*ctx.gameState, grid, player);

    // Government processing
    processGovernment(*gsPlayer);

    // Religion
    accumulateFaith(*gsPlayer, grid);
    applyReligionBonuses(*gsPlayer);

    // Science and tech research
    {
        const float science = computePlayerScience(*gsPlayer, grid);
        const float culture = computePlayerCulture(*gsPlayer, grid);

        advanceResearch(gsPlayer->tech(), science);
        advanceCivicResearch(gsPlayer->civics(), culture, &gsPlayer->government());
    }

    // Production queues
    processProductionQueues(*ctx.gameState, grid, player);

    // City bombardment
    processCityBombardment(*ctx.gameState, grid, player, *ctx.rng);

    // Border expansion
    processBorderExpansion(*gsPlayer, grid);

    // Great people
    accumulateGreatPeoplePoints(*ctx.gameState, player);
    checkGreatPeopleRecruitment(*ctx.gameState, player);

    // City-state bonuses
    processCityStateBonuses(*ctx.gameState, player);

    // Governor: auto-queue production for cities with governors
    processGovernors(*ctx.gameState, grid, player);

    // Automation: research queue, auto-explore, military alert
    processAutomation(*ctx.gameState, grid, player);
}

// ============================================================================
// Global systems (not per-player)
// ============================================================================

void processGlobalSystems(TurnContext& ctx) {
    aoc::game::GameState& gameState = *ctx.gameState;
    aoc::map::HexGrid& grid = *ctx.grid;

    // Religious spread (global, affects all cities)
    processReligiousSpread(gameState, grid);

    // Barbarians
    if (ctx.barbarians != nullptr) {
        ctx.barbarians->executeTurn(gameState, grid, *ctx.rng);
    }

    // Communication speed (affects all players)
    processCommunication(gameState, grid);

    // Tick prospect cooldowns (tiles that were surveyed become available again)
    grid.tickProspectCooldowns();

    // Tick nuclear fallout decay
    grid.tickFallout();

    // Labor strikes
    checkLaborStrikes(gameState);
    processStrikes(gameState);

    // Migration between civilizations
    processMigration(gameState, grid);

    // Insurance premium payments
    processInsurancePremiums(gameState);

    // Futures contract settlement
    settleFutures(gameState, ctx.economy->market());

    // River flooding (seasonal) -- TODO: migrate processFlooding to GameState
    processFlooding(gameState, grid, static_cast<int32_t>(ctx.currentTurn));

    // Natural disasters and climate
    {
        const float globalTemp = gameState.climate().globalTemperature;
        processNaturalDisasters(gameState, grid, static_cast<int32_t>(ctx.currentTurn), globalTemp);

        GlobalClimateComponent& climate = gameState.climate();

        // Accumulate CO2 from population across all cities of all players
        for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
            for (const std::unique_ptr<aoc::game::City>& city : p->cities()) {
                const float co2PerCity = static_cast<float>(city->population()) * 0.1f;
                climate.addCO2(co2PerCity);
            }
        }

        // Industrial pollution CO2
        climate.addCO2(static_cast<float>(totalIndustrialCO2(gameState)));
        climate.processTurn(grid, *ctx.rng);
    }

    // Energy dependency and peak oil tracking
    {
        updateGlobalOilReserves(grid, gameState.oilReserves());

        for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
            PlayerEnergyComponent& energy = playerPtr->energy();

            // Sum oil consumed from city stockpiles for this player
            int32_t oilConsumed = 0;
            for (const std::unique_ptr<aoc::game::City>& city : playerPtr->cities()) {
                oilConsumed += city->stockpile().getAmount(goods::OIL);
            }

            const int32_t renewables = countRenewableBuildings(gameState, playerPtr->id());
            updateEnergyDependency(energy, oilConsumed, renewables);
            processOilShock(energy);
        }
    }

    // Physical trade routes: move Traders, exchange goods
    processTradeRoutes(gameState, grid, ctx.economy->market());

    // Stock market: dividends, value updates
    processStockMarket(gameState);

    // Trade agreements: tick durations
    processTradeAgreements(gameState);

    // Supply chain health: check import dependencies
    processSupplyChains(gameState);

    // Monopoly detection and pricing
    detectMonopolies(gameState, grid);
    applyMonopolyIncome(gameState);

    // Black market smuggling (for embargoed players)
    processBlackMarketTrade(gameState);

    // Speculation bubbles (per player)
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        PlayerBubbleComponent& bubble = playerPtr->bubble();
        const aoc::sim::MonetaryStateComponent& mon = playerPtr->monetary();
        // Shock triggers: inflation spike or negative treasury
        const bool hasShock = (mon.inflationRate > 0.15f || mon.treasury < 0);
        processSpeculationBubble(bubble, mon.gdp, mon.interestRate, hasShock);
    }

    // Per-player: unemployment and education
    for (PlayerId player : ctx.allPlayers) {
        processUnemployment(gameState, player);
        updateHumanCapital(gameState, player);
    }

    // World events (per player)
    for (PlayerId player : ctx.allPlayers) {
        checkWorldEvents(gameState, player, static_cast<int32_t>(ctx.currentTurn));
    }
    tickWorldEvents(gameState);

    // Victory tracking
    updateVictoryTrackers(gameState, grid, *ctx.economy, ctx.currentTurn);
}

// ============================================================================
// Main turn entry point
// ============================================================================

void processTurn(TurnContext& ctx) {
    // 1. AI decisions
    for (ai::AIController* ai : ctx.aiControllers) {
        if (ai != nullptr) {
            ai->executeTurn(*ctx.gameState, *ctx.grid, *ctx.diplomacy,
                           ctx.economy->market(), *ctx.rng);
        }
    }

    // 2. Economy simulation (harvest, produce, trade, market prices)
    ctx.economy->executeTurn(*ctx.gameState, *ctx.grid);

    // 3. Per-player processing
    for (PlayerId player : ctx.allPlayers) {
        processPlayerTurn(ctx, player);
    }

    // 4. Global systems
    processGlobalSystems(ctx);

    ++ctx.currentTurn;
}

} // namespace aoc::sim

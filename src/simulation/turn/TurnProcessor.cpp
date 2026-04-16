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
#include "aoc/simulation/turn/TurnEventLog.hpp"
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

// Promotions
#include "aoc/simulation/unit/Promotion.hpp"

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
/// Minimum hex distance between any two cities (Civ 6 rule: 3 tiles apart).
static constexpr int32_t MIN_CITY_DISTANCE = 3;

aoc::game::City& foundCity(aoc::game::GameState& gameState,
                            aoc::map::HexGrid& grid,
                            PlayerId owner,
                            aoc::hex::AxialCoord location,
                            const std::string& name,
                            bool isOriginalCapital,
                            int32_t startingPop) {
    aoc::game::Player* gsPlayer = gameState.player(owner);
    assert(gsPlayer != nullptr && "foundCity: player not found in GameState");

    // Enforce minimum distance from the SAME player's existing cities.
    // In Civ 6, the 3-tile rule applies to your own cities only.
    for (const std::unique_ptr<aoc::game::City>& existingCity : gsPlayer->cities()) {
        const int32_t dist = aoc::hex::distance(location, existingCity->location());
        if (dist < MIN_CITY_DISTANCE) {
            // Too close to own city - find the nearest valid tile by spiraling outward.
            std::vector<aoc::hex::AxialCoord> candidates;
            candidates.reserve(50);
            aoc::hex::spiral(location, MIN_CITY_DISTANCE + 2, std::back_inserter(candidates));

            bool relocated = false;
            for (const aoc::hex::AxialCoord& alt : candidates) {
                if (!grid.isValid(alt)) { continue; }
                const int32_t altIdx = grid.toIndex(alt);
                if (aoc::map::isWater(grid.terrain(altIdx))
                    || aoc::map::isImpassable(grid.terrain(altIdx))) {
                    continue;
                }
                bool tooCloseToOwn = false;
                for (const std::unique_ptr<aoc::game::City>& ownCity : gsPlayer->cities()) {
                    if (aoc::hex::distance(alt, ownCity->location()) < MIN_CITY_DISTANCE) {
                        tooCloseToOwn = true;
                        break;
                    }
                }
                if (!tooCloseToOwn) {
                    location = alt;
                    relocated = true;
                    LOG_INFO("foundCity: relocated from too-close position to (%d,%d)",
                             location.q, location.r);
                    break;
                }
            }
            if (!relocated) {
                LOG_WARN("foundCity: could not find valid location %d+ tiles from own cities",
                         MIN_CITY_DISTANCE);
            }
            break;
        }
    }

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
            // Minting ores get an extra bonus so the city immediately starts
            // producing coins once the Mint is built.  Without this, high-food
            // tiles (cattle) always win and copper ore is never worked until pop≥4.
            const uint16_t resId = grid.resource(idx).value;
            if (resId == aoc::sim::goods::COPPER_ORE
                || resId == aoc::sim::goods::SILVER_ORE) {
                score += 8.0f;
            }
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

    // Sync monetary().treasury from the actual spending account so AI decisions
    // and display both see the real value.
    gsPlayer->monetary().treasury = gsPlayer->treasury();

    // --- Per-turn unit healing ---
    // Units heal each turn based on territory:
    //   Friendly territory: 20 HP (near city within 3 tiles)
    //   Own territory:      10 HP (in own borders)
    //   Neutral territory:   5 HP
    //   Fortified:          +5 HP bonus
    //   Embarked/Zero Move:  0 HP (no healing)
    for (const std::unique_ptr<aoc::game::Unit>& unitPtr : gsPlayer->units()) {
        if (unitPtr->hitPoints() >= unitPtr->typeDef().maxHitPoints) { continue; }
        if (unitPtr->state() == UnitState::Embarked) { continue; }
        if (unitPtr->movementRemaining() <= 0
            && unitPtr->state() != UnitState::Fortified) { continue; }

        int32_t healAmount = 5;  // Neutral territory base

        // Check if near own city (friendly territory)
        bool nearOwnCity = false;
        for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
            if (aoc::hex::distance(unitPtr->position(), city->location()) <= 3) {
                nearOwnCity = true;
                break;
            }
        }
        if (nearOwnCity) {
            healAmount = 20;
        } else {
            // Check if in own territory (could expand with border check)
            healAmount = 10;
        }

        // Fortification bonus
        if (unitPtr->state() == UnitState::Fortified) {
            healAmount += 5;
        }

        const int32_t newHP = std::min(
            unitPtr->hitPoints() + healAmount,
            unitPtr->typeDef().maxHitPoints);
        unitPtr->setHitPoints(newHP);
    }

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
    // Science costs gold: each point of science generated costs 0.2 gold (research
    // funding). This means a player generating 100 science/turn pays 20 gold/turn
    // for research — making science an investment with ROI from better tech.
    // If the player can't afford it, science is reduced proportionally (unfunded
    // research operates at minimum 50% efficiency).
    {
        float science = computePlayerScience(*gsPlayer, grid);
        const float culture = computePlayerCulture(*gsPlayer, grid);

        // Science funding cost: 0.2 gold per science point
        constexpr float SCIENCE_FUNDING_COST = 0.2f;
        const CurrencyAmount fundingCost = static_cast<CurrencyAmount>(science * SCIENCE_FUNDING_COST);
        if (fundingCost > 0) {
            if (gsPlayer->treasury() >= fundingCost) {
                gsPlayer->addGold(-fundingCost);
            } else {
                // Can't fully fund: research at reduced efficiency (min 50%)
                const float affordableFraction = (gsPlayer->treasury() > 0)
                    ? static_cast<float>(gsPlayer->treasury()) / static_cast<float>(fundingCost)
                    : 0.0f;
                const float efficiency = 0.5f + affordableFraction * 0.5f;
                science *= efficiency;
                if (gsPlayer->treasury() > 0) {
                    gsPlayer->addGold(-gsPlayer->treasury());  // Spend what we can
                }
            }
        }

        // Goods-based science boost: having certain goods in any city stockpile
        // accelerates research. Computers (+15%), Glass (+5%), Paper/Books (+5%).
        // This incentivizes producing these goods and creates the loop:
        //   Research Computers tech → produce Computer goods → research faster.
        {
            bool hasComputers = false;
            bool hasGlass = false;
            for (const std::unique_ptr<aoc::game::City>& cityPtr : gsPlayer->cities()) {
                if (cityPtr->stockpile().getAmount(77) > 0) { hasComputers = true; } // Computers
                if (cityPtr->stockpile().getAmount(76) > 0) { hasGlass = true; }     // Glass
            }
            if (hasComputers) { science *= 1.15f; }
            if (hasGlass) { science *= 1.05f; }
        }

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

    // Unit promotions: AI auto-selects, human gets UI prompt
    {
        const bool isHuman = (player == ctx.humanPlayer);
        processUnitPromotions(*gsPlayer, isHuman);
    }

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

    // AI religion founding: auto-found pantheons and religions for non-human players
    // once they accumulate sufficient faith. Human players use the UI screen.
    processAIReligionFounding(gameState);

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

    // River flooding (seasonal)
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
    TurnEventLog* eventLog = ctx.eventLog;

    // Snapshot pre-turn state for event detection
    struct PlayerPre {
        int32_t techs = 0;
        int32_t cities = 0;
        int32_t units = 0;
        int32_t military = 0;
        bool atWar[MAX_PLAYERS] = {};
    };
    std::vector<PlayerPre> preState;
    preState.resize(ctx.allPlayers.size());
    for (std::size_t i = 0; i < ctx.allPlayers.size(); ++i) {
        const aoc::game::Player* p = ctx.gameState->player(ctx.allPlayers[i]);
        if (p == nullptr) { continue; }
        preState[i].techs = static_cast<int32_t>(std::count(p->tech().completedTechs.begin(), p->tech().completedTechs.end(), true));
        preState[i].cities = p->cityCount();
        preState[i].units = static_cast<int32_t>(p->units().size());
        preState[i].military = p->militaryUnitCount();
        if (ctx.diplomacy != nullptr) {
            for (std::size_t j = 0; j < ctx.allPlayers.size(); ++j) {
                if (i != j) {
                    preState[i].atWar[j] = ctx.diplomacy->relation(
                        ctx.allPlayers[i], ctx.allPlayers[j]).isAtWar;
                }
            }
        }
    }

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

    // Detect and record events by diffing post-turn state
    if (eventLog != nullptr) {
        for (std::size_t i = 0; i < ctx.allPlayers.size(); ++i) {
            const PlayerId pid = ctx.allPlayers[i];
            const aoc::game::Player* p = ctx.gameState->player(pid);
            if (p == nullptr) { continue; }

            const int32_t newTechs = static_cast<int32_t>(std::count(p->tech().completedTechs.begin(), p->tech().completedTechs.end(), true));
            if (newTechs > preState[i].techs) {
                eventLog->record(TurnEventType::TechResearched, pid,
                                 INVALID_PLAYER, newTechs, 0, "Tech completed");
            }

            const int32_t newCities = p->cityCount();
            if (newCities > preState[i].cities) {
                eventLog->record(TurnEventType::CityFounded, pid,
                                 INVALID_PLAYER, newCities, 0, "New city");
            }

            const int32_t newUnits = static_cast<int32_t>(p->units().size());
            if (newUnits > preState[i].units) {
                eventLog->record(TurnEventType::UnitProduced, pid,
                                 INVALID_PLAYER, newUnits - preState[i].units, 0,
                                 "Unit produced");
            }

            const int32_t newMil = p->militaryUnitCount();
            if (newMil < preState[i].military) {
                eventLog->record(TurnEventType::UnitKilled, pid,
                                 INVALID_PLAYER, preState[i].military - newMil, 0,
                                 "Unit lost");
            }

            // War state changes
            if (ctx.diplomacy != nullptr) {
                for (std::size_t j = 0; j < ctx.allPlayers.size(); ++j) {
                    if (i == j) { continue; }
                    const bool nowAtWar = ctx.diplomacy->relation(pid, ctx.allPlayers[j]).isAtWar;
                    if (nowAtWar && !preState[i].atWar[j]) {
                        eventLog->record(TurnEventType::WarDeclared, pid,
                                         ctx.allPlayers[j], 0, 0, "War declared");
                    } else if (!nowAtWar && preState[i].atWar[j]) {
                        eventLog->record(TurnEventType::PeaceMade, pid,
                                         ctx.allPlayers[j], 0, 0, "Peace made");
                    }
                }
            }
        }
    }

    ++ctx.currentTurn;
}

} // namespace aoc::sim

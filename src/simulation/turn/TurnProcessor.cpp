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
#include "aoc/simulation/resource/ResourceTypes.hpp"
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
#include "aoc/simulation/diplomacy/BorderViolation.hpp"
#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/simulation/diplomacy/NavalPassage.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/diplomacy/AllianceObligations.hpp"
#include "aoc/simulation/diplomacy/WarWeariness.hpp"

// Government
#include "aoc/simulation/government/GovernmentComponent.hpp"

// Religion
#include "aoc/simulation/religion/Religion.hpp"

// Victory
#include "aoc/simulation/victory/VictoryCondition.hpp"
#include "aoc/simulation/victory/Prestige.hpp"
#include "aoc/simulation/victory/SpaceRace.hpp"
#include "aoc/simulation/culture/Tourism.hpp"

// Promotions
#include "aoc/simulation/unit/Promotion.hpp"

// Barbarians
#include "aoc/simulation/barbarian/BarbarianController.hpp"

// Great People
#include "aoc/simulation/greatpeople/GreatPeople.hpp"

// City-States
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/diplomacy/EspionageSystem.hpp"
#include "aoc/simulation/diplomacy/WorldCongress.hpp"

// Climate & Disasters
#include "aoc/simulation/climate/Climate.hpp"
#include "aoc/simulation/climate/NaturalDisasters.hpp"

// Empire
#include "aoc/simulation/empire/CommunicationSpeed.hpp"

// Events
#include "aoc/simulation/event/WorldEvents.hpp"
#include "aoc/simulation/ai/AIEventChoice.hpp"
#include "aoc/simulation/ai/AIInvestmentController.hpp"

// Production
#include "aoc/simulation/production/Waste.hpp"

// Unit supply
#include "aoc/simulation/unit/SupplyLines.hpp"

// Automation
#include "aoc/simulation/city/Governor.hpp"
#include "aoc/simulation/automation/Automation.hpp"
#include "aoc/simulation/event/VisibilityEvents.hpp"

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
#include "aoc/simulation/economy/DomesticCourier.hpp"
#include "aoc/simulation/economy/SpeculationBubble.hpp"
#include "aoc/simulation/economy/MonopolyPricing.hpp"
#include "aoc/simulation/economy/TechUnemployment.hpp"
#include "aoc/simulation/economy/BlackMarket.hpp"
#include "aoc/simulation/economy/HumanCapital.hpp"

// Logging
#include "aoc/core/Log.hpp"
#include "aoc/core/DecisionLog.hpp"

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
        const int32_t dist = grid.distance(location, existingCity->location());
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
                    if (grid.distance(alt, ownCity->location()) < MIN_CITY_DISTANCE) {
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
                // Mark the player's AI as expansion-exhausted so the settler
                // production/purchase paths stop wasting cycles until the
                // map state changes (war outcome, new tech, etc.).
                aoc::sim::ai::AIBlackboard& aiBb = gsPlayer->blackboard();
                aiBb.expansionExhausted = true;
                aiBb.expansionExhaustedTurn = gameState.currentTurn();
                aiBb.expansionOpportunity = 0.0f;
            }
            break;
        }
    }

    aoc::game::City& city = gsPlayer->addCity(location, name);
    city.setOriginalCapital(isOriginalCapital);
    city.setOriginalOwner(owner);
    city.setPopulation(startingPop);

    // Settlement stage: every founding starts as a Hamlet. The original capital
    // at game start begins further along the ladder so the opening turns still
    // feel like a proper city game rather than a frontier-building sim.
    if (isOriginalCapital) {
        city.setStage(aoc::game::CitySize::Town);
    } else {
        city.setStage(aoc::game::CitySize::Hamlet);
    }

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
        aoc::map::TerrainType nTerrain = grid.terrain(idx);
        if (aoc::map::isWater(nTerrain)) {
            continue;
        }
        // Mountain tiles are impassable, but if they host a mountain-mineable
        // metal we allow citizens to work them (resource extraction only; terrain
        // yields remain zero). Everything else impassable is still skipped.
        if (aoc::map::isImpassable(nTerrain)) {
            const ResourceId mRes = grid.resource(idx);
            const bool workableMountain = (nTerrain == aoc::map::TerrainType::Mountain
                                           && mRes.isValid()
                                           && aoc::sim::isMountainMetal(mRes.value));
            if (!workableMountain) {
                continue;
            }
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
            // Mountain metal tiles have zero terrain yield; give them a boost
            // so the city actually puts a worker there.
            if (grid.terrain(idx) == aoc::map::TerrainType::Mountain
                && aoc::sim::isMountainMetal(resId)) {
                score += 6.0f;
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

    {
        VisibilityEvent ev{};
        ev.type = VisibilityEventType::CityFounded;
        ev.location = location;
        ev.actor = owner;
        gameState.visibilityBus().emit(ev);
    }

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

void processPlayerTurn(TurnContext& turnContext, PlayerId player) {
    assert(turnContext.gameState != nullptr && "GameState is required for turn processing");

    aoc::map::HexGrid& grid = *turnContext.grid;
    aoc::game::Player* gsPlayer = turnContext.gameState->player(player);
    assert(gsPlayer != nullptr && "processPlayerTurn: invalid player id");

    // Civilization meeting detection: check if any of our units/cities are
    // within 4 hexes of another player's entity. Triggers MeetCivilization eureka.
    {
        bool metAnotherCiv = false;

        // Check our units against all foreign cities
        for (const std::unique_ptr<aoc::game::Unit>& ownUnit : gsPlayer->units()) {
            if (metAnotherCiv) { break; }
            for (const std::unique_ptr<aoc::game::Player>& other : turnContext.gameState->players()) {
                if (other->id() == player) { continue; }
                for (const std::unique_ptr<aoc::game::City>& foreignCity : other->cities()) {
                    if (grid.distance(ownUnit->position(), foreignCity->location()) <= 4) {
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
                for (const std::unique_ptr<aoc::game::Player>& other : turnContext.gameState->players()) {
                    if (other->id() == player) { continue; }
                    for (const std::unique_ptr<aoc::game::Unit>& foreignUnit : other->units()) {
                        if (grid.distance(foreignUnit->position(), ownCity->location()) <= 4) {
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

    // Supply lines: compute per-unit supply status, then attrition.
    // Attrition only kicks in for unsupplied military units (far from cities/forts).
    computeSupplyLines(*turnContext.gameState, grid, player);
    applySupplyAttrition(*turnContext.gameState, player);

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
            if (grid.distance(unitPtr->position(), city->location()) <= 3) {
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
    processAdvancedEconomics(*turnContext.gameState, grid, player, turnContext.economy->market());

    // War weariness
    processWarWeariness(*gsPlayer, *turnContext.diplomacy);

    // Golden/Dark age effects
    processAgeEffects(*gsPlayer);

    // City growth
    processCityGrowth(*gsPlayer, grid);

    // City happiness
    computeCityHappiness(*gsPlayer);

    // City loyalty
    computeCityLoyalty(*turnContext.gameState, grid, player);

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

        // Catch-up bonus: players well behind the tech leader get a science
        // multiplier proportional to the gap. Keeps laggards in contention
        // and prevents runaway science snowball. Max +60% when 4+ techs behind.
        {
            int32_t myTechs = 0;
            int32_t maxTechs = 0;
            for (uint16_t ti = 0; ti < techCount(); ++ti) {
                if (gsPlayer->tech().hasResearched(TechId{ti})) { ++myTechs; }
            }
            for (const std::unique_ptr<aoc::game::Player>& otherPtr : turnContext.gameState->players()) {
                if (otherPtr == nullptr) { continue; }
                int32_t ot = 0;
                for (uint16_t ti = 0; ti < techCount(); ++ti) {
                    if (otherPtr->tech().hasResearched(TechId{ti})) { ++ot; }
                }
                if (ot > maxTechs) { maxTechs = ot; }
            }
            const int32_t gap = maxTechs - myTechs;
            if (gap >= 2) {
                const float bonus = std::min(0.60f, 0.15f * static_cast<float>(gap));
                science *= (1.0f + bonus);
            }
        }

        advanceResearch(gsPlayer->tech(), science);
        advanceCivicResearch(gsPlayer->civics(), culture, &gsPlayer->government());
    }

    // Production queues
    processProductionQueues(*turnContext.gameState, grid, player);

    // City bombardment
    processCityBombardment(*turnContext.gameState, grid, player, *turnContext.rng);

    // Border expansion
    processBorderExpansion(*gsPlayer, grid);

    // Great people
    accumulateGreatPeoplePoints(*turnContext.gameState, player);
    checkGreatPeopleRecruitment(*turnContext.gameState, player);

    // City-state bonuses
    processCityStateBonuses(*turnContext.gameState, player);

    // Unit promotions: AI auto-selects, human gets UI prompt
    {
        const bool isHuman = (player == turnContext.humanPlayer);
        processUnitPromotions(*gsPlayer, isHuman);
    }

    // Governor: auto-queue production for cities with governors
    processGovernors(*turnContext.gameState, grid, player);

    // Automation: research queue, auto-explore, military alert
    processAutomation(*turnContext.gameState, grid, player);
    processAutoTariffs(*turnContext.gameState, turnContext.diplomacy, player);
    processAutoSpreadReligion(*turnContext.gameState, grid, turnContext.diplomacy, player);
}

// ============================================================================
// Global systems (not per-player)
// ============================================================================

void processGlobalSystems(TurnContext& turnContext) {
    aoc::game::GameState& gameState = *turnContext.gameState;
    aoc::map::HexGrid& grid = *turnContext.grid;

    // Religious spread (global, affects all cities)
    processReligiousSpread(gameState, grid);

    // AI religion founding: auto-found pantheons and religions for non-human players
    // once they accumulate sufficient faith. Human players use the UI screen.
    processAIReligionFounding(gameState);

    // Space Race progress toward Science Victory. Gates on Campus + required tech.
    processSpaceRace(gameState, grid);

    // Barbarians
    if (turnContext.barbarians != nullptr) {
        turnContext.barbarians->executeTurn(gameState, grid, *turnContext.rng);
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
    settleFutures(gameState, turnContext.economy->market());

    // River flooding (seasonal)
    processFlooding(gameState, grid, static_cast<int32_t>(turnContext.currentTurn));

    // Natural disasters and climate
    {
        const float globalTemp = gameState.climate().globalTemperature;
        processNaturalDisasters(gameState, grid, static_cast<int32_t>(turnContext.currentTurn), globalTemp);

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
        climate.processTurn(grid, *turnContext.rng);

        // Climate thresholds push narrative events into the per-player queue.
        // The per-event cooldown (WORLD_EVENT_COOLDOWN_TURNS) prevents spam.
        const bool floodThreshold = climate.seaLevelRise >= 5;
        const bool droughtThreshold = climate.globalTemperature >= 2.0f;
        if (floodThreshold || droughtThreshold) {
            const int32_t currentTurn = gameState.currentTurn();
            for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
                PlayerEventComponent& events = p->events();
                if (events.pendingEvent != static_cast<WorldEventId>(255)) { continue; }
                if (floodThreshold
                    && currentTurn - events.lastFiredTurn[static_cast<uint8_t>(WorldEventId::MigrantWave)]
                        >= WORLD_EVENT_COOLDOWN_TURNS) {
                    events.pendingEvent = WorldEventId::MigrantWave;
                    events.pendingChoice = -1;
                    continue;
                }
                if (droughtThreshold
                    && currentTurn - events.lastFiredTurn[static_cast<uint8_t>(WorldEventId::FamineWarning)]
                        >= WORLD_EVENT_COOLDOWN_TURNS) {
                    events.pendingEvent = WorldEventId::FamineWarning;
                    events.pendingChoice = -1;
                }
            }
        }
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
    processTradeRoutes(gameState, grid, turnContext.economy->market(), turnContext.diplomacy);

    // Domestic couriers: player-dispatched goods transport between own cities.
    // Advances each active courier one turn, delivers on arrival, clamps
    // stockpiles to per-city caps.
    processDomesticCouriers(gameState, grid);

    // Auto-renew queued trade routes (per-player). Runs after processTradeRoutes
    // so pending requests enqueued during expiration this turn are handled next
    // turn, giving UI a tick to show the expiration notification first.
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        processAutoRenewTradeRoutes(gameState, grid, turnContext.economy->market(),
                                    turnContext.diplomacy, playerPtr->id());
    }

    // Soft border violation detection: scan military units in foreign territory
    if (turnContext.diplomacy != nullptr) {
        updateBorderViolations(gameState, grid, *turnContext.diplomacy);
        updateNavalPassageViolations(gameState, grid, *turnContext.diplomacy);
    }

    // Stock market: dividends, value updates
    processStockMarket(gameState);

    // AI investment decisions (gene-driven: speculationAppetite + riskTolerance)
    ai::runAIInvestmentDecisions(gameState);

    // Trade agreements: tick durations
    processTradeAgreements(gameState);

    // Diplomatic deals: enforce terms (reparations, DMZ, arms limits, non-aggression)
    if (turnContext.dealTracker != nullptr && turnContext.diplomacy != nullptr) {
        processDeals(gameState, *turnContext.dealTracker, *turnContext.diplomacy, grid);
    }

    // Ideological friction: different post-industrial governments accrue
    // grievance every turn until they converge or hit the per-pair cap.
    accrueIdeologicalGrievances(gameState);

    // Grievances -> relation score. Refresh a single "Grievances" modifier
    // per ordered pair each turn using the accumulated grievance total.
    // Without this step grievances are recorded but never influence
    // DiplomacyState.totalScore, so AIs never develop hostility and wars
    // rarely fire. Capped at -40 to leave room for other modifiers.
    if (turnContext.diplomacy != nullptr) {
        for (const std::unique_ptr<aoc::game::Player>& a : gameState.players()) {
            for (const std::unique_ptr<aoc::game::Player>& b : gameState.players()) {
                if (a->id() == b->id()) { continue; }
                aoc::sim::PairwiseRelation& rel =
                    turnContext.diplomacy->relation(a->id(), b->id());
                // Remove existing Grievances modifier (single slot).
                for (auto it = rel.modifiers.begin(); it != rel.modifiers.end(); ) {
                    if (it->reason == "Grievances") { it = rel.modifiers.erase(it); }
                    else { ++it; }
                }
                const int32_t gTotal =
                    a->grievances().totalGrievanceAgainst(b->id());
                if (gTotal > 0) {
                    const int32_t penalty = std::min(gTotal / 2, 40);
                    if (penalty > 0) {
                        rel.modifiers.push_back({"Grievances", -penalty, 0});
                    }
                }
            }
        }
    }

    // Alliance obligations: tick countdowns, check fulfillment, apply penalties
    if (turnContext.allianceTracker != nullptr && turnContext.diplomacy != nullptr) {
        turnContext.allianceTracker->tickObligations(*turnContext.diplomacy, gameState);
    }

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
    for (PlayerId player : turnContext.allPlayers) {
        processUnemployment(gameState, player);
        updateHumanCapital(gameState, player);
    }

    // World events (per player)
    for (PlayerId player : turnContext.allPlayers) {
        checkWorldEvents(gameState, player, static_cast<int32_t>(turnContext.currentTurn));
    }
    // AI players pick a choice utility-style; humans keep their pending events
    // so the UI can present the dilemma.
    ai::resolvePendingAIEvents(gameState);
    tickWorldEvents(gameState);

    // Victory tracking
    updateVictoryTrackers(gameState, grid, *turnContext.economy, turnContext.currentTurn);

    // Prestige accrual (participation-based endgame tally).
    processPrestige(gameState, grid, turnContext.diplomacy);
}

// ============================================================================
// Main turn entry point
// ============================================================================

void processTurn(TurnContext& turnContext) {
    // Install decision logger for this thread so AI call sites can reach it
    // via currentDecisionLog() without threading a pointer through every API.
    aoc::core::ScopedDecisionLog scopedLog(turnContext.decisionLog);

    // Keep GameState's turn counter aligned with the processor's — several
    // systems (world events, AI blackboards) stamp this value and rely on
    // monotonic progression for cooldowns.
    turnContext.gameState->setCurrentTurn(static_cast<int32_t>(turnContext.currentTurn));

    TurnEventLog* eventLog = turnContext.eventLog;

    // Snapshot pre-turn state for event detection
    struct PlayerPre {
        int32_t techs = 0;
        int32_t cities = 0;
        int32_t units = 0;
        int32_t military = 0;
        bool atWar[MAX_PLAYERS] = {};
    };
    std::vector<PlayerPre> preState;
    preState.resize(turnContext.allPlayers.size());
    for (std::size_t i = 0; i < turnContext.allPlayers.size(); ++i) {
        const aoc::game::Player* p = turnContext.gameState->player(turnContext.allPlayers[i]);
        if (p == nullptr) { continue; }
        preState[i].techs = static_cast<int32_t>(std::count(p->tech().completedTechs.begin(), p->tech().completedTechs.end(), true));
        preState[i].cities = p->cityCount();
        preState[i].units = static_cast<int32_t>(p->units().size());
        preState[i].military = p->militaryUnitCount();
        if (turnContext.diplomacy != nullptr) {
            for (std::size_t j = 0; j < turnContext.allPlayers.size(); ++j) {
                if (i != j) {
                    preState[i].atWar[j] = turnContext.diplomacy->relation(
                        turnContext.allPlayers[i], turnContext.allPlayers[j]).isAtWar;
                }
            }
        }
    }

    // 1. AI decisions
    for (ai::AIController* ai : turnContext.aiControllers) {
        if (ai != nullptr) {
            ai->executeTurn(*turnContext.gameState, *turnContext.grid, turnContext.fogOfWar,
                           *turnContext.diplomacy, turnContext.economy->market(), *turnContext.rng);
        }
    }

    // 2. Economy simulation (harvest, produce, trade, market prices)
    turnContext.economy->executeTurn(*turnContext.gameState, *turnContext.grid);

    // 3. Per-player processing
    for (PlayerId player : turnContext.allPlayers) {
        processPlayerTurn(turnContext, player);
    }

    // 4. Global systems
    processGlobalSystems(turnContext);

    // 5. Visibility-filtered event dispatch (fog is up to date from step 1/3)
    if (turnContext.fogOfWar != nullptr) {
        processVisibilityEvents(*turnContext.gameState, *turnContext.grid, *turnContext.fogOfWar,
                                turnContext.gameState->visibilityBus());
    } else {
        turnContext.gameState->visibilityBus().clear();
    }

    // Detect and record events by diffing post-turn state
    if (eventLog != nullptr) {
        for (std::size_t i = 0; i < turnContext.allPlayers.size(); ++i) {
            const PlayerId pid = turnContext.allPlayers[i];
            const aoc::game::Player* p = turnContext.gameState->player(pid);
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
            if (turnContext.diplomacy != nullptr) {
                for (std::size_t j = 0; j < turnContext.allPlayers.size(); ++j) {
                    if (i == j) { continue; }
                    const bool nowAtWar = turnContext.diplomacy->relation(pid, turnContext.allPlayers[j]).isAtWar;
                    if (nowAtWar && !preState[i].atWar[j]) {
                        eventLog->record(TurnEventType::WarDeclared, pid,
                                         turnContext.allPlayers[j], 0, 0, "War declared");
                    } else if (!nowAtWar && preState[i].atWar[j]) {
                        eventLog->record(TurnEventType::PeaceMade, pid,
                                         turnContext.allPlayers[j], 0, 0, "Peace made");
                    }
                }
            }
        }
    }

    // --- Late-turn global systems previously wired only in Application.cpp ---
    // Running here so headless simulations (and the GA) exercise them too.

    // Decay per-turn diplomacy relation modifiers.
    if (turnContext.diplomacy != nullptr) {
        turnContext.diplomacy->tickModifiers();
    }

    // Espionage: resolve spy mission outcomes for all players.
    if (turnContext.rng != nullptr && turnContext.grid != nullptr) {
        processSpyMissions(*turnContext.gameState, *turnContext.grid,
                           *turnContext.rng, turnContext.diplomacy);
    }

    // Grievance decay per player. Moved here so grievances accumulated above
    // shrink uniformly whether running via Application.cpp or headless.
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : turnContext.gameState->players()) {
        playerPtr->grievances().tickGrievances();
    }

    // World Congress: propose / vote / resolve resolutions.
    if (turnContext.rng != nullptr) {
        processWorldCongress(*turnContext.gameState,
                              static_cast<TurnNumber>(turnContext.currentTurn),
                              *turnContext.rng,
                              turnContext.diplomacy);
    }

    // City-state diplomacy: meet-check, passive envoy accrual, suzerain
    // recompute, levy expiry, bully cooldown. Server-authoritative.
    if (turnContext.grid != nullptr) {
        processCityStateDiplomacy(*turnContext.gameState,
                                   *turnContext.grid,
                                   static_cast<int32_t>(turnContext.currentTurn));
    }

    // City-state AI: defend-only production (no settlers/wonders/districts).
    if (turnContext.grid != nullptr) {
        processCityStateAI(*turnContext.gameState, *turnContext.grid);
    }

    // City-state quests: issue/resolve quests and award rewards.
    checkCityStateQuests(*turnContext.gameState);

    // Tourism per-player + cultural victory probe.
    if (turnContext.grid != nullptr) {
        for (const std::unique_ptr<aoc::game::Player>& playerPtr :
                 turnContext.gameState->players()) {
            if (playerPtr == nullptr) { continue; }
            computeTourism(*turnContext.gameState, playerPtr->id(), *turnContext.grid);
        }
        const PlayerId culturalWinner = checkCulturalVictory(*turnContext.gameState);
        if (culturalWinner != INVALID_PLAYER) {
            aoc::game::Player* win = turnContext.gameState->player(culturalWinner);
            if (win != nullptr) {
                win->victoryTracker().eraVictoryPoints += 5;
            }
        }
    }

    // Per-turn per-player aggregate snapshot for the decision log. Emitted
    // once at end-of-turn so readers can join production/research decisions
    // to the state they produced.
    if (turnContext.decisionLog != nullptr && turnContext.decisionLog->active()) {
        for (std::size_t i = 0; i < turnContext.allPlayers.size(); ++i) {
            const PlayerId pid = turnContext.allPlayers[i];
            const aoc::game::Player* p = turnContext.gameState->player(pid);
            if (p == nullptr) { continue; }

            aoc::core::TurnSummary s{};
            s.era = static_cast<uint8_t>(effectiveEraFromTech(*p).value);
            s.cityCount = static_cast<uint16_t>(p->cityCount());
            s.unitCount = static_cast<uint16_t>(p->units().size());
            s.treasury = static_cast<int64_t>(p->treasury());
            s.science = computePlayerScience(*p, *turnContext.grid);
            s.culture = computePlayerCulture(*p, *turnContext.grid);
            s.faith = p->faith().faith;
            s.techsResearched = static_cast<uint16_t>(std::count(
                p->tech().completedTechs.begin(), p->tech().completedTechs.end(), true));
            s.grievanceCount = static_cast<uint16_t>(p->grievances().grievances.size());

            uint8_t wars = 0;
            if (turnContext.diplomacy != nullptr) {
                for (std::size_t j = 0; j < turnContext.allPlayers.size(); ++j) {
                    if (i == j) { continue; }
                    if (turnContext.diplomacy->relation(pid, turnContext.allPlayers[j]).isAtWar) {
                        ++wars;
                    }
                }
            }
            s.warCount = wars;
            s.victoryTypeLead = 0; // Reserved: front-running victory type id.

            turnContext.decisionLog->logTurnSummary(
                static_cast<uint16_t>(turnContext.currentTurn),
                static_cast<uint8_t>(pid),
                s);
        }
    }

    ++turnContext.currentTurn;
}

} // namespace aoc::sim

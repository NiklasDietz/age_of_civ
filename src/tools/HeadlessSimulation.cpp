/**
 * @file HeadlessSimulation.cpp
 * @brief Headless AI-vs-AI simulation runner with comprehensive logging.
 *
 * Runs a full game with N AI players for M turns without any rendering.
 * Logs game state each turn to a CSV file for analysis.
 *
 * Usage: aoc_simulate [config.yaml]
 *        aoc_simulate [turns] [players] [output_file]
 *
 * Output CSV columns:
 *   Turn, Player, GDP, Treasury, CoinTier, MonetarySystem, Inflation,
 *   Population, Cities, Military, TechsResearched, CultureTotal,
 *   TradePartners, TradeVolume, CompositeCSI, EraVP, Happiness,
 *   Corruption, CrisisType, IndustrialRevolution, GovernmentType
 */

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/MapGenerator.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/core/SimpleYaml.hpp"
#include "aoc/simulation/turn/GameLength.hpp"
#include "aoc/simulation/turn/TurnProcessor.hpp"
#include "aoc/simulation/turn/TurnEventLog.hpp"
#include "aoc/simulation/economy/Maintenance.hpp"
#include "aoc/simulation/map/GoodyHuts.hpp"

// Simulation systems
#include "aoc/simulation/turn/TurnManager.hpp"
#include "aoc/simulation/ai/AIController.hpp"
#include "aoc/simulation/barbarian/BarbarianController.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/city/CityGrowth.hpp"
#include "aoc/simulation/city/CityScience.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/city/ProductionSystem.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/CityConnection.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/tech/EraScore.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/economy/AdvancedEconomics.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/WarWeariness.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/victory/VictoryCondition.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/tech/EurekaBoost.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"

#include <random>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

// All types use full namespace paths per coding standards.

// ============================================================================
// Progress bar
// ============================================================================

static void printProgressBar(int32_t current, int32_t total, int32_t barWidth = 50) {
    float progress = static_cast<float>(current) / static_cast<float>(total);
    int32_t filled = static_cast<int32_t>(progress * static_cast<float>(barWidth));

    std::fprintf(stderr, "\r  [");
    for (int32_t i = 0; i < barWidth; ++i) {
        if (i < filled) { std::fputc('=', stderr); }
        else if (i == filled) { std::fputc('>', stderr); }
        else { std::fputc(' ', stderr); }
    }
    std::fprintf(stderr, "] %3d%% (%d/%d turns)",
                 static_cast<int>(progress * 100.0f), current, total);
    std::fflush(stderr);
}

namespace {

struct PlayerSnapshot {
    aoc::PlayerId player;
    aoc::CurrencyAmount gdp = 0;
    aoc::CurrencyAmount treasury = 0;
    uint8_t coinTier = 0;
    uint8_t monetarySystem = 0;
    float inflationRate = 0.0f;
    int32_t population = 0;
    int32_t cities = 0;
    int32_t militaryUnits = 0;
    int32_t techsResearched = 0;
    float cultureTotal = 0.0f;
    int32_t tradePartners = 0;
    float compositeCSI = 0.0f;
    int32_t eraVP = 0;
    float avgHappiness = 0.0f;
    float corruption = 0.0f;
    uint8_t crisisType = 0;
    uint8_t industrialRev = 0;
    uint8_t governmentType = 0;
};

/**
 * @brief Build a turn snapshot for one player from the GameState object model.
 *
 * All data is read from Player/City/Unit objects rather than ECS component pools.
 */
PlayerSnapshot snapshotPlayer(const aoc::game::GameState& gameState,
                               aoc::PlayerId playerId) {
    PlayerSnapshot snap{};
    snap.player = playerId;

    const aoc::game::Player* player = gameState.player(playerId);
    if (player == nullptr) {
        return snap;
    }

    // Monetary state
    const aoc::sim::MonetaryStateComponent& ms = player->monetary();
    snap.gdp = ms.gdp;
    snap.treasury = ms.treasury;
    snap.coinTier = static_cast<uint8_t>(ms.effectiveCoinTier);
    snap.monetarySystem = static_cast<uint8_t>(ms.system);
    snap.inflationRate = ms.inflationRate;

    // Cities and population
    snap.cities = player->cityCount();
    snap.population = player->totalPopulation();

    // Happiness: average across all cities with a happiness component
    {
        float totalHappy = 0.0f;
        int32_t happyCities = 0;
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            totalHappy += city->happiness().amenities - city->happiness().demand;
            ++happyCities;
        }
        if (happyCities > 0) {
            snap.avgHappiness = totalHappy / static_cast<float>(happyCities);
        }
    }

    // Military units
    snap.militaryUnits = player->militaryUnitCount();

    // Techs researched: count set bits in completedTechs bitset
    {
        const aoc::sim::PlayerTechComponent& tech = player->tech();
        for (std::size_t b = 0; b < tech.completedTechs.size(); ++b) {
            if (tech.completedTechs[b]) { ++snap.techsResearched; }
        }
    }

    // Victory tracker: CSI, era VP, culture
    {
        const aoc::sim::VictoryTrackerComponent& vt = player->victoryTracker();
        snap.compositeCSI = vt.compositeCSI;
        snap.eraVP = vt.eraVictoryPoints;
        snap.cultureTotal = vt.totalCultureAccumulated;
    }

    // Currency crisis
    snap.crisisType = static_cast<uint8_t>(player->currencyCrisis().activeCrisis);

    // Industrial revolution
    snap.industrialRev = static_cast<uint8_t>(player->industrial().currentRevolution);

    // Government and corruption
    {
        const aoc::sim::PlayerGovernmentComponent& gov = player->government();
        snap.governmentType = static_cast<uint8_t>(gov.government);
        snap.corruption = aoc::sim::computeCorruption(gov.government, snap.cities, 0.0f);
    }

    // Trade partners: count unique partner players from the GameState trade route list
    {
        std::unordered_set<aoc::PlayerId> partners;
        for (const aoc::sim::TradeRouteComponent& route : gameState.tradeRoutes()) {
            if (route.sourcePlayer == playerId && route.destPlayer != playerId) {
                partners.insert(route.destPlayer);
            }
            if (route.destPlayer == playerId && route.sourcePlayer != playerId) {
                partners.insert(route.sourcePlayer);
            }
        }
        // Also count active trader units this player owns
        for (const std::unique_ptr<aoc::game::Unit>& unit : player->units()) {
            const aoc::sim::TraderComponent& trader = unit->trader();
            if (trader.destOwner != playerId && trader.destOwner != aoc::INVALID_PLAYER) {
                partners.insert(trader.destOwner);
            }
        }
        snap.tradePartners = static_cast<int32_t>(partners.size());
    }

    return snap;
}

} // anonymous namespace

int runHeadlessSimulation(int32_t maxTurns, int32_t playerCount,
                          const std::string& outputPath) {
    LOG_INFO("=== HEADLESS SIMULATION: %d turns, %d AI players ===", maxTurns, playerCount);

    // Open output CSV
    std::ofstream csv(outputPath);
    if (!csv.is_open()) {
        LOG_ERROR("Failed to open output file: %s", outputPath.c_str());
        return 1;
    }

    // CSV header — includes game-level context and training signals.
    // IsLastPlayer: 1 if this player is the last to finish their turn (temporal
    // pressure signal — external state is frozen, must commit all actions now).
    csv << "Turn,Player,PlayerCount,MapWidth,MapHeight,CivId,MetPlayersMask,IsLastPlayer,"
        << "GDP,Treasury,CoinTier,MonetarySystem,Inflation,"
        << "Population,Cities,Military,TechsResearched,CultureTotal,"
        << "TradePartners,CompositeCSI,EraVP,AvgHappiness,"
        << "Corruption,CrisisType,IndustrialRev,GovernmentType,"
        << "IncomeTax,IncomeCommercial,IncomeIndustrial,IncomeTileGold,"
        << "IncomeGoodsEcon,TotalIncome,EffectiveIncome,"
        << "ExpenseUnits,ExpenseBuildings,TotalExpense,NetFlow,GoodsStockpiled\n";

    aoc::map::HexGrid grid;
    std::random_device rd;
    aoc::Random rng(rd());

    // Generate map
    aoc::map::MapGenerator::Config mapConfig{};
    mapConfig.width = 80;
    mapConfig.height = 52;
    mapConfig.seed = rng.next();
    mapConfig.mapType = aoc::map::MapType::Realistic;
    aoc::map::MapGenerator generator;
    generator.generate(mapConfig, grid);

    LOG_INFO("Map generated: %dx%d", mapConfig.width, mapConfig.height);

    // Initialize simulation subsystems
    aoc::sim::DiplomacyManager diplomacy;
    diplomacy.initialize(static_cast<uint8_t>(playerCount));

    aoc::sim::EconomySimulation economy;
    economy.initialize();

    aoc::sim::TurnManager turnManager;
    turnManager.setPlayerCount(0, static_cast<uint8_t>(playerCount));

    aoc::sim::BarbarianController barbarians;

    std::vector<aoc::sim::ai::AIController> aiControllers;
    aiControllers.reserve(static_cast<std::size_t>(playerCount));

    // Track placed starting positions for minimum distance enforcement
    std::vector<aoc::hex::AxialCoord> startPositions;
    constexpr int32_t MIN_START_DISTANCE = 8;

    // Initialize GameState before the spawning loop so foundCity can populate it.
    aoc::game::GameState gameState;
    gameState.initialize(playerCount);

    // Spawn each AI player with a starting city and scout
    for (int32_t p = 0; p < playerCount; ++p) {
        aoc::PlayerId player = static_cast<aoc::PlayerId>(p);

        // Score candidate tiles by food potential, rejecting bad starts
        aoc::hex::AxialCoord startPos{0, 0};
        float bestScore = -1.0f;
        for (int32_t attempts = 0; attempts < 2000; ++attempts) {
            int32_t rx = rng.nextInt(5, mapConfig.width - 5);
            int32_t ry = rng.nextInt(5, mapConfig.height - 5);
            int32_t idx = ry * mapConfig.width + rx;
            if (aoc::map::isWater(grid.terrain(idx))
                || aoc::map::isImpassable(grid.terrain(idx))) {
                continue;
            }
            aoc::hex::AxialCoord candidate = aoc::hex::offsetToAxial({rx, ry});

            bool tooClose = false;
            for (const aoc::hex::AxialCoord& existing : startPositions) {
                if (aoc::hex::distance(candidate, existing) < MIN_START_DISTANCE) {
                    tooClose = true;
                    break;
                }
            }
            if (tooClose) { continue; }

            float score = 0.0f;
            aoc::map::TileYield centerYield = grid.tileYield(idx);
            score += static_cast<float>(std::max(centerYield.food, static_cast<int8_t>(2))) * 2.0f;
            score += static_cast<float>(centerYield.production);

            std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(candidate);
            int32_t landNeighbors = 0;
            for (const aoc::hex::AxialCoord& nbr : nbrs) {
                if (!grid.isValid(nbr)) { continue; }
                int32_t nbrIdx = grid.toIndex(nbr);
                if (aoc::map::isWater(grid.terrain(nbrIdx))
                    || aoc::map::isImpassable(grid.terrain(nbrIdx))) {
                    continue;
                }
                ++landNeighbors;
                aoc::map::TileYield nbrYield = grid.tileYield(nbrIdx);
                score += static_cast<float>(nbrYield.food) * 1.5f;
                score += static_cast<float>(nbrYield.production);
                if (grid.resource(nbrIdx).isValid()) {
                    score += 3.0f;
                }
            }

            if (landNeighbors < 3) { continue; }

            if (score > bestScore) {
                bestScore = score;
                startPos = candidate;
            }
        }

        startPositions.push_back(startPos);

        // Found starting city (creates City in player's city list via GameState)
        std::string cityName = std::string(
            aoc::sim::civDef(static_cast<aoc::sim::CivId>(p % aoc::sim::CIV_COUNT)).cityNames[0]);
        aoc::sim::foundCity(gameState, grid, player, startPos, cityName, true, 1);

        // Guarantee minimum resources near starting position
        std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(startPos);
        int32_t centerIdx = grid.toIndex(startPos);
        if (!grid.resource(centerIdx).isValid()) {
            grid.setResource(centerIdx, aoc::ResourceId{aoc::sim::goods::WHEAT});
            grid.setReserves(centerIdx, aoc::sim::defaultReserves(aoc::sim::goods::WHEAT));
        }
        int32_t resourcesPlaced = 0;
        const uint16_t STARTER_RESOURCES[] = {
            aoc::sim::goods::IRON_ORE, aoc::sim::goods::COPPER_ORE,
            aoc::sim::goods::WOOD, aoc::sim::goods::STONE,
            aoc::sim::goods::COAL, aoc::sim::goods::CATTLE
        };
        for (const aoc::hex::AxialCoord& nbr2 : nbrs) {
            if (!grid.isValid(nbr2)) { continue; }
            int32_t nbrIdx = grid.toIndex(nbr2);
            if (!grid.resource(nbrIdx).isValid()
                && !aoc::map::isWater(grid.terrain(nbrIdx))
                && !aoc::map::isImpassable(grid.terrain(nbrIdx))
                && resourcesPlaced < 6) {
                grid.setResource(nbrIdx, aoc::ResourceId{STARTER_RESOURCES[resourcesPlaced]});
                grid.setReserves(nbrIdx, aoc::sim::defaultReserves(STARTER_RESOURCES[resourcesPlaced]));
                ++resourcesPlaced;
            }
        }

        // Place 2 unique luxury resources near each player's capital
        {
            constexpr uint16_t LUXURY_POOL[] = {
                aoc::sim::goods::WINE, aoc::sim::goods::SPICES, aoc::sim::goods::SILK,
                aoc::sim::goods::FURS, aoc::sim::goods::GEMS, aoc::sim::goods::DYES,
                aoc::sim::goods::TEA, aoc::sim::goods::COFFEE, aoc::sim::goods::TOBACCO,
                aoc::sim::goods::PEARLS, aoc::sim::goods::INCENSE, aoc::sim::goods::IVORY
            };
            constexpr int32_t LUXURY_POOL_SIZE = 12;

            int32_t luxPlaced = 0;
            std::vector<aoc::hex::AxialCoord> ring2;
            ring2.reserve(12);
            aoc::hex::ring(startPos, 2, std::back_inserter(ring2));

            for (const aoc::hex::AxialCoord& luxTile : ring2) {
                if (luxPlaced >= 2) { break; }
                if (!grid.isValid(luxTile)) { continue; }
                int32_t luxIdx = grid.toIndex(luxTile);
                if (grid.resource(luxIdx).isValid()) { continue; }
                if (aoc::map::isWater(grid.terrain(luxIdx))
                    || aoc::map::isImpassable(grid.terrain(luxIdx))) { continue; }

                uint16_t luxId = LUXURY_POOL[(p * 2 + luxPlaced) % LUXURY_POOL_SIZE];
                grid.setResource(luxIdx, aoc::ResourceId{luxId});
                grid.setReserves(luxIdx, aoc::sim::defaultReserves(luxId));
                ++luxPlaced;
            }
        }

        // Configure player state via the GameState Player object (no ECS entity creation)
        aoc::game::Player* gsPlayer = gameState.player(player);
        if (gsPlayer != nullptr) {
            gsPlayer->setCivId(static_cast<aoc::sim::CivId>(p % aoc::sim::CIV_COUNT));
            gsPlayer->setHuman(false);
            gsPlayer->setTreasury(100);

            // Initialize monetary state
            gsPlayer->monetary().owner = player;
            gsPlayer->monetary().system = aoc::sim::MonetarySystemType::Barter;
            gsPlayer->monetary().treasury = 100;

            // Initialize economy component
            gsPlayer->economy().owner = player;
            gsPlayer->economy().treasury = 100;

            // Initialize tech
            gsPlayer->tech().owner = player;
            gsPlayer->tech().initialize();
            gsPlayer->tech().completedTechs[0] = true;
            gsPlayer->tech().currentResearch = aoc::TechId{1};

            // Initialize civics
            gsPlayer->civics().owner = player;
            gsPlayer->civics().initialize();
            gsPlayer->civics().currentResearch = aoc::CivicId{0};

            // Initialize government
            gsPlayer->government().owner = player;

            // Initialize victory tracker
            gsPlayer->victoryTracker().owner = player;

            // Add scout unit to the player's unit list
            gsPlayer->addUnit(aoc::UnitTypeId{2}, startPos);
        }

        // Create AI controller
        aiControllers.emplace_back(player);

        LOG_INFO("Player %d (%.*s) placed at (%d,%d)",
                 p,
                 static_cast<int>(aoc::sim::civDef(static_cast<aoc::sim::CivId>(p % aoc::sim::CIV_COUNT)).name.size()),
                 aoc::sim::civDef(static_cast<aoc::sim::CivId>(p % aoc::sim::CIV_COUNT)).name.data(),
                 startPos.q, startPos.r);
    }

    LOG_INFO("GameState initialized for %d players", playerCount);

    // Place goody huts (ancient ruins) on the map
    aoc::sim::GoodyHutState goodyHuts;
    aoc::sim::placeGoodyHuts(goodyHuts, grid, startPositions, rng);

    // Build TurnContext
    aoc::sim::TurnContext turnCtx{};
    turnCtx.grid = &grid;
    turnCtx.economy = &economy;
    turnCtx.diplomacy = &diplomacy;
    turnCtx.barbarians = &barbarians;
    turnCtx.rng = &rng;
    turnCtx.gameState = &gameState;
    for (aoc::sim::ai::AIController& ai : aiControllers) {
        turnCtx.aiControllers.push_back(&ai);
        turnCtx.allPlayers.push_back(ai.player());
    }
    turnCtx.humanPlayer = aoc::INVALID_PLAYER;
    turnCtx.currentTurn = 0;

    // Mid-turn event log for ML training data
    aoc::sim::TurnEventLog eventLog;
    turnCtx.eventLog = &eventLog;

    // Event CSV: separate file with sub-turn granularity
    std::string eventPath = outputPath;
    {
        std::size_t dotPos = eventPath.rfind('.');
        if (dotPos != std::string::npos) {
            eventPath.insert(dotPos, "_events");
        } else {
            eventPath += "_events";
        }
    }
    std::ofstream eventCsv(eventPath);
    if (eventCsv.is_open()) {
        eventCsv << "Turn,SubStep,EventType,Player,OtherPlayer,Value1,Value2,Detail\n";
    }

    // === Main simulation loop ===
    for (int32_t turn = 1; turn <= maxTurns; ++turn) {
        turnCtx.currentTurn = static_cast<aoc::TurnNumber>(turn);
        eventLog.clear();

        aoc::sim::processTurn(turnCtx);

        // --- Goody hut exploration ---
        // Check all units against hut locations after movement.
        if (!goodyHuts.hutLocations.empty()) {
            for (int32_t p = 0; p < playerCount; ++p) {
                aoc::game::Player* gsp = gameState.player(static_cast<aoc::PlayerId>(p));
                if (gsp == nullptr) { continue; }
                for (const std::unique_ptr<aoc::game::Unit>& unitPtr : gsp->units()) {
                    aoc::sim::GoodyHutReward reward = aoc::sim::checkAndClaimGoodyHut(
                        goodyHuts, gameState, *gsp, unitPtr->position(), rng);
                    if (reward != aoc::sim::GoodyHutReward::Count) {
                        eventLog.record(aoc::sim::TurnEventType::CityFounded,
                                        static_cast<aoc::PlayerId>(p),
                                        aoc::INVALID_PLAYER, static_cast<int32_t>(reward), 0,
                                        "Goody hut claimed");
                    }
                }
            }
        }

        // --- Player meeting detection ---
        // Two players meet when any unit/city of one is within sight range (3 tiles)
        // of any unit/city of the other. Checked once per turn.
        constexpr int32_t MEETING_SIGHT_RANGE = 3;
        for (int32_t pa = 0; pa < playerCount; ++pa) {
            for (int32_t pb = pa + 1; pb < playerCount; ++pb) {
                const aoc::PlayerId pidA = static_cast<aoc::PlayerId>(pa);
                const aoc::PlayerId pidB = static_cast<aoc::PlayerId>(pb);
                if (diplomacy.haveMet(pidA, pidB)) { continue; }

                const aoc::game::Player* playerA = gameState.player(pidA);
                const aoc::game::Player* playerB = gameState.player(pidB);
                if (playerA == nullptr || playerB == nullptr) { continue; }

                // Collect all positions of player A (units + cities)
                bool met = false;
                for (const std::unique_ptr<aoc::game::Unit>& uA : playerA->units()) {
                    if (met) { break; }
                    for (const std::unique_ptr<aoc::game::Unit>& uB : playerB->units()) {
                        if (aoc::hex::distance(uA->position(), uB->position()) <= MEETING_SIGHT_RANGE) {
                            met = true; break;
                        }
                    }
                    if (!met) {
                        for (const std::unique_ptr<aoc::game::City>& cB : playerB->cities()) {
                            if (aoc::hex::distance(uA->position(), cB->location()) <= MEETING_SIGHT_RANGE) {
                                met = true; break;
                            }
                        }
                    }
                }
                if (!met) {
                    for (const std::unique_ptr<aoc::game::City>& cA : playerA->cities()) {
                        if (met) { break; }
                        for (const std::unique_ptr<aoc::game::Unit>& uB : playerB->units()) {
                            if (aoc::hex::distance(cA->location(), uB->position()) <= MEETING_SIGHT_RANGE) {
                                met = true; break;
                            }
                        }
                    }
                }
                if (met) {
                    diplomacy.meetPlayers(pidA, pidB, turn);
                    eventLog.record(aoc::sim::TurnEventType::PlayersMet,
                                    pidA, pidB, 0, 0, "First contact");
                }
            }
        }

        // Flush mid-turn events to event CSV
        if (eventCsv.is_open()) {
            for (const aoc::sim::TurnEvent& evt : eventLog.events()) {
                eventCsv << turn << ","
                         << evt.subStep << ","
                         << aoc::sim::TurnEventLog::eventTypeName(evt.type) << ","
                         << static_cast<int>(evt.player) << ","
                         << static_cast<int>(evt.otherPlayer) << ","
                         << evt.value1 << ","
                         << evt.value2 << ","
                         << evt.detail << "\n";
            }
        }

        // Detailed economy log every 25 turns
        if (turn % 25 == 0 || turn == 1) {
            LOG_INFO("=== TURN %d ECONOMY SUMMARY ===", turn);
            for (int32_t p = 0; p < playerCount; ++p) {
                const aoc::game::Player* gsp = gameState.player(static_cast<aoc::PlayerId>(p));
                if (gsp == nullptr) { continue; }

                float science = aoc::sim::computePlayerScience(*gsp, grid);
                float culture = aoc::sim::computePlayerCulture(*gsp, grid);

                // Count active trade routes (trader units owned by this player)
                int32_t activeRoutes = 0;
                for (const std::unique_ptr<aoc::game::Unit>& unit : gsp->units()) {
                    if (unit->trader().destOwner != aoc::INVALID_PLAYER) {
                        ++activeRoutes;
                    }
                }

                const char* techName = gsp->tech().currentResearch.isValid()
                    ? aoc::sim::techDef(gsp->tech().currentResearch).name.data() : "none";
                // Show the last completed civic (not current research, which resets
                // between completions and appears as "none" at snapshot time).
                const char* civicName = "none";
                {
                    const aoc::sim::PlayerCivicComponent& civics = gsp->civics();
                    // Find the highest-index completed civic
                    for (int32_t ci = static_cast<int32_t>(aoc::sim::civicCount()) - 1; ci >= 0; --ci) {
                        if (civics.hasCompleted(aoc::CivicId{static_cast<uint16_t>(ci)})) {
                            civicName = aoc::sim::civicDef(aoc::CivicId{static_cast<uint16_t>(ci)}).name.data();
                            break;
                        }
                    }
                }

                LOG_INFO("  P%d: Pop=%d Cities=%d Treasury=%lld Science=%.1f Culture=%.1f "
                         "Tech=%s Civic=%s TradeRoutes=%d MonSys=%d",
                         p, gsp->totalPopulation(), gsp->cityCount(),
                         static_cast<long long>(gsp->treasury()),
                         static_cast<double>(science), static_cast<double>(culture),
                         techName, civicName, activeRoutes,
                         static_cast<int>(gsp->monetary().system));

                // Resource stockpile summary: aggregate across all cities
                int32_t ironOre = 0, copperOre = 0, coal = 0, wood = 0, stone = 0;
                int32_t ironIngots = 0, tools = 0, steel = 0, lumber = 0;
                int32_t machinery = 0, electronics = 0, consGoods = 0;
                for (const std::unique_ptr<aoc::game::City>& city : gsp->cities()) {
                    const aoc::sim::CityStockpileComponent& st = city->stockpile();
                    ironOre    += st.getAmount(aoc::sim::goods::IRON_ORE);
                    copperOre  += st.getAmount(aoc::sim::goods::COPPER_ORE);
                    coal       += st.getAmount(aoc::sim::goods::COAL);
                    wood       += st.getAmount(aoc::sim::goods::WOOD);
                    stone      += st.getAmount(aoc::sim::goods::STONE);
                    ironIngots += st.getAmount(aoc::sim::goods::IRON_INGOTS);
                    tools      += st.getAmount(aoc::sim::goods::TOOLS);
                    steel      += st.getAmount(aoc::sim::goods::STEEL);
                    lumber     += st.getAmount(aoc::sim::goods::LUMBER);
                    machinery  += st.getAmount(aoc::sim::goods::MACHINERY);
                    electronics += st.getAmount(aoc::sim::goods::ELECTRONICS);
                    consGoods  += st.getAmount(aoc::sim::goods::CONSUMER_GOODS);
                }
                LOG_INFO("    Resources: Iron=%d Cu=%d Coal=%d Wood=%d Stone=%d | "
                         "Ingots=%d Tools=%d Steel=%d Lumber=%d | "
                         "Machinery=%d Electronics=%d ConsGoods=%d",
                         ironOre, copperOre, coal, wood, stone,
                         ironIngots, tools, steel, lumber,
                         machinery, electronics, consGoods);

                // Trade cargo details for each active trader unit
                for (const std::unique_ptr<aoc::game::Unit>& unit : gsp->units()) {
                    const aoc::sim::TraderComponent& trader = unit->trader();
                    if (trader.destOwner == aoc::INVALID_PLAYER) { continue; }
                    std::string cargoStr;
                    for (const aoc::sim::TradeCargo& c : trader.cargo) {
                        if (!cargoStr.empty()) { cargoStr += ", "; }
                        cargoStr += "g" + std::to_string(c.goodId);
                        cargoStr += "x" + std::to_string(c.amount);
                    }
                    const char* routeTypeNames[] = {"Land", "Sea", "Air"};
                    LOG_INFO("    Trade[%s]: -> P%u trips=%d gold=%lld cargo=[%s]",
                             routeTypeNames[static_cast<int>(trader.routeType)],
                             static_cast<unsigned>(trader.destOwner),
                             trader.completedTrips,
                             static_cast<long long>(trader.goldEarnedThisTurn),
                             cargoStr.c_str());
                }
            }
        }

        // Extended economy detail every 50 turns
        if (turn % 50 == 0) {
            LOG_INFO("=== TURN %d DETAILED ECONOMY ===", turn);
            for (int32_t p = 0; p < playerCount; ++p) {
                const aoc::game::Player* dp = gameState.player(static_cast<aoc::PlayerId>(p));
                if (dp == nullptr) { continue; }

                // Happiness: average across all cities
                float avgHappiness = 0.0f;
                int32_t happyCities = 0;
                for (const std::unique_ptr<aoc::game::City>& city : dp->cities()) {
                    avgHappiness += city->happiness().happiness;
                    ++happyCities;
                }
                if (happyCities > 0) {
                    avgHappiness /= static_cast<float>(happyCities);
                }

                // Needs summary from player economy component
                int32_t needsCount = static_cast<int32_t>(dp->economy().totalNeeds.size());
                int32_t uniqueLux = dp->economy().uniqueLuxuryCount;

                // Count trade routes by type from trader units
                int32_t landRoutes = 0, seaRoutes = 0, airRoutes = 0;
                for (const std::unique_ptr<aoc::game::Unit>& unit : dp->units()) {
                    const aoc::sim::TraderComponent& tr = unit->trader();
                    if (tr.destOwner == aoc::INVALID_PLAYER) { continue; }
                    switch (tr.routeType) {
                        case aoc::sim::TradeRouteType::Land: ++landRoutes; break;
                        case aoc::sim::TradeRouteType::Sea:  ++seaRoutes; break;
                        case aoc::sim::TradeRouteType::Air:  ++airRoutes; break;
                    }
                }

                LOG_INFO("  P%d: Happiness=%.2f UniqueLux=%d Needs=%d Routes(L/S/A)=%d/%d/%d",
                         p, static_cast<double>(avgHappiness), uniqueLux, needsCount,
                         landRoutes, seaRoutes, airRoutes);
            }
        }

        // Write snapshot row for each player
        for (int32_t p = 0; p < playerCount; ++p) {
            PlayerSnapshot snap = snapshotPlayer(gameState, static_cast<aoc::PlayerId>(p));
            // Game-level context columns
            const aoc::game::Player* snapPlayer = gameState.player(static_cast<aoc::PlayerId>(p));
            const uint8_t civId = (snapPlayer != nullptr)
                ? static_cast<uint8_t>(snapPlayer->civId()) : 0u;
            // MetPlayersMask: bit i is set if player p has met player i.
            // The ML pipeline uses this to mask out unmet players' data.
            uint16_t metMask = 0;
            for (int32_t other = 0; other < playerCount; ++other) {
                if (other == p) { metMask |= (1u << other); continue; }
                if (diplomacy.haveMet(static_cast<aoc::PlayerId>(p),
                                       static_cast<aoc::PlayerId>(other))) {
                    metMask |= (1u << other);
                }
            }
            // In headless mode, AI players process sequentially so the last
            // player (highest index) is the "last player" each turn.
            const int32_t isLastPlayer = (p == playerCount - 1) ? 1 : 0;

            csv << turn << ","
                << static_cast<int>(snap.player) << ","
                << playerCount << ","
                << mapConfig.width << ","
                << mapConfig.height << ","
                << static_cast<int>(civId) << ","
                << metMask << ","
                << isLastPlayer << ","
                << snap.gdp << ","
                << snap.treasury << ","
                << static_cast<int>(snap.coinTier) << ","
                << static_cast<int>(snap.monetarySystem) << ","
                << snap.inflationRate << ","
                << snap.population << ","
                << snap.cities << ","
                << snap.militaryUnits << ","
                << snap.techsResearched << ","
                << snap.cultureTotal << ","
                << snap.tradePartners << ","
                << snap.compositeCSI << ","
                << snap.eraVP << ","
                << snap.avgHappiness << ","
                << snap.corruption << ","
                << static_cast<int>(snap.crisisType) << ","
                << static_cast<int>(snap.industrialRev) << ","
                << static_cast<int>(snap.governmentType) << ",";
            // Economic breakdown columns
            if (snapPlayer != nullptr) {
                aoc::sim::EconomicBreakdown bd =
                    aoc::sim::computeEconomicBreakdown(*snapPlayer, grid);
                csv << bd.incomeTax << "," << bd.incomeCommercial << ","
                    << bd.incomeIndustrial << "," << bd.incomeTileGold << ","
                    << bd.incomeGoodsEcon << "," << bd.totalIncome << ","
                    << bd.effectiveIncome << ","
                    << bd.expenseUnits << "," << bd.expenseBuildings << ","
                    << bd.totalExpense << "," << bd.netFlow << ","
                    << bd.goodsStockpiled;
            } else {
                csv << "0,0,0,0,0,0,0,0,0,0,0,0";
            }
            csv << "\n";
        }

        // Check victory
        aoc::sim::VictoryResult vr = aoc::sim::checkVictoryConditions(
            gameState, static_cast<aoc::TurnNumber>(turn), static_cast<aoc::TurnNumber>(maxTurns));
        if (vr.type != aoc::sim::VictoryType::None) {
            printProgressBar(turn, maxTurns);
            std::fprintf(stderr, "\n\n  GAME OVER on turn %d: Player %u wins (type %d)\n",
                         turn, static_cast<unsigned>(vr.winner),
                         static_cast<int>(vr.type));
            break;
        }

        printProgressBar(turn, maxTurns);

        if (turn % 25 == 0) {
            PlayerSnapshot s0 = snapshotPlayer(gameState, 0);
            std::fprintf(stderr, "\n  Turn %d: P0 pop=%d cities=%d techs=%d GDP=%lld\n",
                         turn, s0.population, s0.cities, s0.techsResearched,
                         static_cast<long long>(s0.gdp));
        }

        turnManager.beginNewTurn();
    }

    csv.close();
    LOG_INFO("Simulation complete. Data written to: %s", outputPath.c_str());
    return 0;
}

#ifdef AOC_HEADLESS_MAIN
int main(int argc, char* argv[]) {
    int32_t turns = 200;
    int32_t players = 4;
    std::string outputPath = "simulation_log.csv";

    bool loadedConfig = false;
    if (argc >= 2) {
        std::string arg1(argv[1]);
        if (arg1.size() > 4 && (arg1.substr(arg1.size() - 5) == ".yaml"
                                 || arg1.substr(arg1.size() - 4) == ".yml")) {
            aoc::SimpleYaml config;
            if (config.loadFromFile(arg1)) {
                turns      = config.getInt("max_turns", 200);
                players    = config.getInt("player_count", 4);
                outputPath = config.getString("output_file", "simulation_log.csv");

                std::string gameLengthStr = config.getString("game_length", "");
                if (!gameLengthStr.empty()) {
                    aoc::sim::GameLength gl = aoc::sim::parseGameLength(gameLengthStr);
                    const aoc::sim::GameLengthDef& glDef = aoc::sim::gameLengthDef(gl);
                    turns = glDef.maxTurns;
                    aoc::sim::GamePace::instance().setFromLength(gl);
                }

                std::fprintf(stderr, "\n  === Age of Civilization: Headless Simulation ===\n\n");
                std::fprintf(stderr, "  Config:  %s\n", arg1.c_str());
                std::fprintf(stderr, "  Length:  %s\n", gameLengthStr.empty() ? "Custom" : gameLengthStr.c_str());
                std::fprintf(stderr, "  Turns:   %d\n", turns);
                std::fprintf(stderr, "  Players: %d\n", players);
                std::fprintf(stderr, "  Map:     %s (%dx%d)\n",
                             config.getString("map_type", "Continents").c_str(),
                             config.getInt("map_width", 60),
                             config.getInt("map_height", 40));
                std::fprintf(stderr, "  Seed:    %d\n", config.getInt("seed", 42));
                std::fprintf(stderr, "  Output:  %s\n\n", outputPath.c_str());

                loadedConfig = true;
            } else {
                std::fprintf(stderr, "  Error: could not open config file: %s\n", arg1.c_str());
                return 1;
            }
        }
    }

    if (!loadedConfig) {
        int32_t mapWidth = 60;
        int32_t mapHeight = 40;
        for (int i = 1; i < argc; ++i) {
            std::string arg(argv[i]);
            if (arg == "--turns" && i + 1 < argc) {
                turns = std::atoi(argv[++i]);
            } else if (arg == "--players" && i + 1 < argc) {
                players = std::atoi(argv[++i]);
            } else if (arg == "--output" && i + 1 < argc) {
                outputPath = argv[++i];
            } else if (arg == "--map-size" && i + 1 < argc) {
                std::string sizeStr(argv[++i]);
                std::size_t xPos = sizeStr.find('x');
                if (xPos != std::string::npos) {
                    mapWidth = std::atoi(sizeStr.substr(0, xPos).c_str());
                    mapHeight = std::atoi(sizeStr.substr(xPos + 1).c_str());
                }
            } else if (arg == "--log-level" && i + 1 < argc) {
                ++i;
            } else if (arg == "--seed" && i + 1 < argc) {
                ++i;
            } else {
                int val = std::atoi(arg.c_str());
                if (val > 0 && turns == 200) { turns = val; }
                else if (val > 0 && players == 4) { players = val; }
            }
        }
        (void)mapWidth;
        (void)mapHeight;

        std::fprintf(stderr, "\n  === Age of Civilization: Headless Simulation ===\n\n");
        std::fprintf(stderr, "  Turns:   %d\n", turns);
        std::fprintf(stderr, "  Players: %d\n", players);
        std::fprintf(stderr, "  Output:  %s\n\n", outputPath.c_str());
    }

    if (turns <= 0) { turns = 200; }
    if (players < 2) { players = 2; }
    if (players > 12) { players = 12; }

    int result = runHeadlessSimulation(turns, players, outputPath);

    std::fprintf(stderr, "\n\n");
    return result;
}
#endif

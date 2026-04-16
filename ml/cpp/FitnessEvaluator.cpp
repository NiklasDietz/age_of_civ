/**
 * @file FitnessEvaluator.cpp
 * @brief Embedded simulation runner for GA fitness evaluation.
 *
 * Each simulation creates its own GameState, grid, RNG, and AI controllers.
 * No shared mutable state — fully thread-safe.
 */

#include "FitnessEvaluator.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/MapGenerator.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/turn/TurnProcessor.hpp"
#include "aoc/simulation/turn/TurnEventLog.hpp"
#include "aoc/simulation/turn/TurnManager.hpp"
#include "aoc/simulation/turn/GameLength.hpp"
#include "aoc/simulation/ai/AIController.hpp"
#include "aoc/simulation/barbarian/BarbarianController.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/economy/Maintenance.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/economy/AdvancedEconomics.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/victory/VictoryCondition.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/EurekaBoost.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/map/GoodyHuts.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <semaphore>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

namespace aoc::ga {

SimulationResult runSimulation(int32_t turns, int32_t playerCount, uint64_t seed) {
    SimulationResult result{};
    result.eraVP.resize(static_cast<std::size_t>(playerCount), 0);
    result.compositeCSI.resize(static_cast<std::size_t>(playerCount), 0.0f);

    aoc::map::HexGrid grid;
    aoc::Random rng(seed);

    // Generate map
    aoc::map::MapGenerator::Config mapConfig{};
    mapConfig.width = 80;
    mapConfig.height = 52;
    mapConfig.seed = rng.next();
    mapConfig.mapType = aoc::map::MapType::Realistic;
    aoc::map::MapGenerator generator;
    generator.generate(mapConfig, grid);

    // Initialize subsystems
    aoc::sim::DiplomacyManager diplomacy;
    diplomacy.initialize(static_cast<uint8_t>(playerCount));

    aoc::sim::EconomySimulation economy;
    economy.initialize();

    aoc::sim::TurnManager turnManager;
    turnManager.setPlayerCount(0, static_cast<uint8_t>(playerCount));

    aoc::sim::BarbarianController barbarians;

    std::vector<aoc::sim::ai::AIController> aiControllers;
    aiControllers.reserve(static_cast<std::size_t>(playerCount));

    std::vector<aoc::hex::AxialCoord> startPositions;
    constexpr int32_t MIN_START_DISTANCE = 8;

    aoc::game::GameState gameState;
    gameState.initialize(playerCount);

    // Spawn players
    for (int32_t p = 0; p < playerCount; ++p) {
        aoc::PlayerId player = static_cast<aoc::PlayerId>(p);

        // Find starting position
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
                if (grid.distance(candidate, existing) < MIN_START_DISTANCE) {
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

        // Found starting city
        std::string cityName = std::string(
            aoc::sim::civDef(static_cast<aoc::sim::CivId>(p % aoc::sim::CIV_COUNT)).cityNames[0]);
        aoc::sim::foundCity(gameState, grid, player, startPos, cityName, true, 1);

        // Starter resources
        std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(startPos);
        int32_t centerIdx = grid.toIndex(startPos);
        if (!grid.resource(centerIdx).isValid()) {
            grid.setResource(centerIdx, aoc::ResourceId{aoc::sim::goods::WHEAT});
            grid.setReserves(centerIdx, aoc::sim::defaultReserves(aoc::sim::goods::WHEAT));
        }

        const uint16_t STARTER_RESOURCES[] = {
            aoc::sim::goods::COPPER_ORE, aoc::sim::goods::SILVER_ORE,
            aoc::sim::goods::IRON_ORE,   aoc::sim::goods::WOOD,
            aoc::sim::goods::STONE,      aoc::sim::goods::CATTLE
        };
        int32_t resourcesPlaced = 0;
        for (const aoc::hex::AxialCoord& nbr : nbrs) {
            if (!grid.isValid(nbr)) { continue; }
            int32_t nbrIdx = grid.toIndex(nbr);
            if (!grid.resource(nbrIdx).isValid()
                && !aoc::map::isWater(grid.terrain(nbrIdx))
                && !aoc::map::isImpassable(grid.terrain(nbrIdx))
                && resourcesPlaced < 6) {
                grid.setResource(nbrIdx, aoc::ResourceId{STARTER_RESOURCES[resourcesPlaced]});
                grid.setReserves(nbrIdx, aoc::sim::defaultReserves(STARTER_RESOURCES[resourcesPlaced]));
                ++resourcesPlaced;
            }
        }

        // Ring-2 fallback for minting ores
        if (resourcesPlaced < 2) {
            std::vector<aoc::hex::AxialCoord> ring2;
            ring2.reserve(12);
            aoc::hex::ring(startPos, 2, std::back_inserter(ring2));
            for (const aoc::hex::AxialCoord& tile : ring2) {
                if (resourcesPlaced >= 2) { break; }
                if (!grid.isValid(tile)) { continue; }
                int32_t tileIdx = grid.toIndex(tile);
                if (!grid.resource(tileIdx).isValid()
                    && !aoc::map::isWater(grid.terrain(tileIdx))
                    && !aoc::map::isImpassable(grid.terrain(tileIdx))) {
                    grid.setResource(tileIdx, aoc::ResourceId{STARTER_RESOURCES[resourcesPlaced]});
                    grid.setReserves(tileIdx, aoc::sim::defaultReserves(STARTER_RESOURCES[resourcesPlaced]));
                    ++resourcesPlaced;
                }
            }
        }

        // Luxury resources
        {
            constexpr uint16_t LUXURY_POOL[] = {
                aoc::sim::goods::WINE, aoc::sim::goods::SPICES, aoc::sim::goods::SILK,
                aoc::sim::goods::FURS, aoc::sim::goods::GEMS, aoc::sim::goods::DYES,
                aoc::sim::goods::TEA, aoc::sim::goods::COFFEE, aoc::sim::goods::TOBACCO,
                aoc::sim::goods::PEARLS, aoc::sim::goods::INCENSE, aoc::sim::goods::IVORY
            };
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
                uint16_t luxId = LUXURY_POOL[(p * 2 + luxPlaced) % 12];
                grid.setResource(luxIdx, aoc::ResourceId{luxId});
                grid.setReserves(luxIdx, aoc::sim::defaultReserves(luxId));
                ++luxPlaced;
            }
        }

        // Configure player
        aoc::game::Player* gsPlayer = gameState.player(player);
        if (gsPlayer != nullptr) {
            gsPlayer->setCivId(static_cast<aoc::sim::CivId>(p % aoc::sim::CIV_COUNT));
            gsPlayer->setHuman(false);
            gsPlayer->setTreasury(0);

            gsPlayer->monetary().owner = player;
            gsPlayer->monetary().system = aoc::sim::MonetarySystemType::Barter;
            gsPlayer->monetary().treasury = 0;

            gsPlayer->economy().owner = player;
            gsPlayer->economy().treasury = 0;

            gsPlayer->tech().owner = player;
            gsPlayer->tech().initialize();
            gsPlayer->tech().completedTechs[0] = true;
            gsPlayer->tech().currentResearch = aoc::TechId{1};

            gsPlayer->civics().owner = player;
            gsPlayer->civics().initialize();
            gsPlayer->civics().currentResearch = aoc::CivicId{0};

            gsPlayer->government().owner = player;
            gsPlayer->victoryTracker().owner = player;

            gsPlayer->addUnit(aoc::UnitTypeId{2}, startPos);
        }

        aiControllers.emplace_back(player);
    }

    // Goody huts
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

    aoc::sim::TurnEventLog eventLog;
    turnCtx.eventLog = &eventLog;

    // Main simulation loop (no CSV, no progress bar)
    for (int32_t turn = 1; turn <= turns; ++turn) {
        turnCtx.currentTurn = static_cast<aoc::TurnNumber>(turn);
        eventLog.clear();

        aoc::sim::processTurn(turnCtx);

        // Goody hut exploration.
        // Snapshot unit positions first because claiming a hut can add a free
        // unit to the player, which reallocates the units vector.
        if (!goodyHuts.hutLocations.empty()) {
            for (int32_t p = 0; p < playerCount; ++p) {
                aoc::game::Player* gsp = gameState.player(static_cast<aoc::PlayerId>(p));
                if (gsp == nullptr) { continue; }
                std::vector<aoc::hex::AxialCoord> positions;
                positions.reserve(gsp->units().size());
                for (const std::unique_ptr<aoc::game::Unit>& unitPtr : gsp->units()) {
                    positions.push_back(unitPtr->position());
                }
                for (const aoc::hex::AxialCoord& pos : positions) {
                    (void)aoc::sim::checkAndClaimGoodyHut(
                        goodyHuts, gameState, *gsp, pos, rng);
                }
            }
        }

        // Player meeting detection
        constexpr int32_t MEETING_SIGHT_RANGE = 3;
        for (int32_t pa = 0; pa < playerCount; ++pa) {
            for (int32_t pb = pa + 1; pb < playerCount; ++pb) {
                const aoc::PlayerId pidA = static_cast<aoc::PlayerId>(pa);
                const aoc::PlayerId pidB = static_cast<aoc::PlayerId>(pb);
                if (diplomacy.haveMet(pidA, pidB)) { continue; }

                const aoc::game::Player* playerA = gameState.player(pidA);
                const aoc::game::Player* playerB = gameState.player(pidB);
                if (playerA == nullptr || playerB == nullptr) { continue; }

                bool met = false;
                for (const std::unique_ptr<aoc::game::Unit>& uA : playerA->units()) {
                    if (met) { break; }
                    for (const std::unique_ptr<aoc::game::Unit>& uB : playerB->units()) {
                        if (grid.distance(uA->position(), uB->position()) <= MEETING_SIGHT_RANGE) {
                            met = true; break;
                        }
                    }
                    if (!met) {
                        for (const std::unique_ptr<aoc::game::City>& cB : playerB->cities()) {
                            if (grid.distance(uA->position(), cB->location()) <= MEETING_SIGHT_RANGE) {
                                met = true; break;
                            }
                        }
                    }
                }
                if (!met) {
                    for (const std::unique_ptr<aoc::game::City>& cA : playerA->cities()) {
                        if (met) { break; }
                        for (const std::unique_ptr<aoc::game::Unit>& uB : playerB->units()) {
                            if (grid.distance(cA->location(), uB->position()) <= MEETING_SIGHT_RANGE) {
                                met = true; break;
                            }
                        }
                    }
                }
                if (met) {
                    diplomacy.meetPlayers(pidA, pidB, turn);
                }
            }
        }

        // Check victory
        aoc::sim::VictoryResult vr = aoc::sim::checkVictoryConditions(
            gameState, static_cast<aoc::TurnNumber>(turn), static_cast<aoc::TurnNumber>(turns));
        if (vr.type != aoc::sim::VictoryType::None) {
            break;
        }

        turnManager.beginNewTurn();
    }

    // Extract final scores
    for (int32_t p = 0; p < playerCount; ++p) {
        const aoc::game::Player* player = gameState.player(static_cast<aoc::PlayerId>(p));
        if (player != nullptr) {
            result.eraVP[static_cast<std::size_t>(p)] = player->victoryTracker().eraVictoryPoints;
            result.compositeCSI[static_cast<std::size_t>(p)] = player->victoryTracker().compositeCSI;
        }
    }
    result.valid = true;
    return result;
}

float evaluateFitness(const Individual& individual,
                       int32_t gamesPerEval,
                       int32_t turnsPerGame,
                       int32_t playerCount,
                       uint64_t baseSeed) {
    float totalScore = 0.0f;
    int32_t validGames = 0;

    for (int32_t game = 0; game < gamesPerEval; ++game) {
        uint64_t gameSeed = baseSeed + static_cast<uint64_t>(game) * 7919;
        SimulationResult simResult = runSimulation(turnsPerGame, playerCount, gameSeed);

        if (!simResult.valid || simResult.eraVP.empty()) {
            continue;
        }

        // Gene quality score: reward parameters that correlate with winning
        float geneQuality = 0.0f;
        for (int32_t i = 0; i < NUM_PARAMS; ++i) {
            float gene = individual.genes[static_cast<std::size_t>(i)];
            // Reward expansion, science, economy focus
            if (i == 1 || i == 2 || i == 4) {  // expansionism, scienceFocus, economicFocus
                geneQuality += gene * 0.15f;
            } else if (i == 0) {  // militaryAggression — moderate is best
                geneQuality += (1.5f - std::abs(gene - 1.2f)) * 0.1f;
            } else if (i == 15 || i == 18) {  // prodSettlers, prodBuildings
                geneQuality += gene * 0.1f;
            } else if (i == 22) {  // warDeclarationThreshold
                geneQuality += gene * 0.05f;
            }
        }

        // Game outcome score: reward score spread (decisive winners = working AI)
        int32_t maxVP = *std::max_element(simResult.eraVP.begin(), simResult.eraVP.end());
        int32_t minVP = *std::min_element(simResult.eraVP.begin(), simResult.eraVP.end());
        float scoreRange = (maxVP > 0)
            ? static_cast<float>(maxVP - minVP) / static_cast<float>(maxVP)
            : 0.0f;

        totalScore += geneQuality + scoreRange * 0.5f;
        ++validGames;
    }

    return (validGames > 0) ? totalScore / static_cast<float>(validGames) : 0.0f;
}

void evaluatePopulation(std::vector<Individual>& population,
                         const GAConfig& config,
                         uint64_t baseSeed) {
    // Collect indices of individuals that need evaluation
    std::vector<std::size_t> toEvaluate;
    for (std::size_t i = 0; i < population.size(); ++i) {
        if (population[i].gamesPlayed == 0) {
            toEvaluate.push_back(i);
        }
    }

    if (toEvaluate.empty()) {
        return;
    }

    int32_t threadCount = config.threadCount;
    if (threadCount <= 0) {
        threadCount = static_cast<int32_t>(std::thread::hardware_concurrency());
        if (threadCount <= 0) { threadCount = 4; }
        // Leave one core free for the OS
        threadCount = std::max(1, threadCount - 1);
    }

    // Limit threads to work items
    threadCount = std::min(threadCount, static_cast<int32_t>(toEvaluate.size()));

    if (threadCount <= 1) {
        // Sequential fallback
        for (std::size_t idx : toEvaluate) {
            uint64_t individualSeed = baseSeed + idx * 104729;
            population[idx].fitness = evaluateFitness(
                population[idx], config.gamesPerEval, config.turnsPerGame,
                config.playerCount, individualSeed);
            population[idx].gamesPlayed = config.gamesPerEval;
        }
        return;
    }

    // Thread pool with counting semaphore
    std::counting_semaphore<> semaphore(threadCount);
    std::mutex resultMutex;
    std::vector<std::jthread> threads;
    threads.reserve(toEvaluate.size());

    for (std::size_t idx : toEvaluate) {
        semaphore.acquire();  // Block if pool is full

        threads.emplace_back([&population, &config, &semaphore, &resultMutex,
                               idx, baseSeed]() {
            uint64_t individualSeed = baseSeed + idx * 104729;
            float fitness = evaluateFitness(
                population[idx], config.gamesPerEval, config.turnsPerGame,
                config.playerCount, individualSeed);

            {
                std::lock_guard<std::mutex> lock(resultMutex);
                population[idx].fitness = fitness;
                population[idx].gamesPlayed = config.gamesPerEval;
            }

            semaphore.release();
        });
    }

    // jthreads auto-join on destruction
}

} // namespace aoc::ga

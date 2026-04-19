/**
 * @file FitnessEvaluator.cpp
 * @brief Embedded simulation runner for GA fitness evaluation.
 *
 * Each simulation creates its own GameState, grid, RNG, and AI controllers.
 * No shared mutable state — fully thread-safe.
 */

#include "FitnessEvaluator.hpp"
#include "ThreadPool.hpp"

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
#include "aoc/simulation/ai/LeaderPersonality.hpp"
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
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <future>
#include <numeric>
#include <random>
#include <span>
#include <thread>
#include <vector>

namespace aoc::ga {

SimulationResult runSimulation(int32_t turns, int32_t playerCount, uint64_t seed,
                                const std::atomic<bool>* stopFlag,
                                std::span<const Individual* const> overrides,
                                aoc::map::MapType mapType) {
    // Per-player personality overrides: install one LeaderPersonalityDef on
    // each player's civId (civId = p % CIV_COUNT). Storage is local to this
    // call so the setLeaderPersonalityOverride pointer stays valid for the
    // full sim. Reserve up-front so push_back never reallocates and
    // invalidates pointers handed to the override table.
    //
    // RAII guard clears every installed override on any return path
    // (including stopFlag abort) so a pooled worker thread starts clean
    // for its next sim.
    std::vector<aoc::sim::LeaderPersonalityDef> overrideDefs;
    overrideDefs.reserve(overrides.size());
    std::vector<aoc::sim::CivId> installedCivs;
    installedCivs.reserve(overrides.size());

    struct OverrideScope {
        std::vector<aoc::sim::CivId>* civs;
        ~OverrideScope() {
            if (civs == nullptr) { return; }
            for (aoc::sim::CivId c : *civs) {
                aoc::sim::setLeaderPersonalityOverride(c, nullptr);
            }
        }
    } scope{&installedCivs};

    const std::size_t slotCount =
        std::min(overrides.size(), static_cast<std::size_t>(playerCount));
    for (std::size_t i = 0; i < slotCount; ++i) {
        if (overrides[i] == nullptr) { continue; }
        aoc::sim::CivId civId = static_cast<aoc::sim::CivId>(i % aoc::sim::CIV_COUNT);
        overrideDefs.push_back(aoc::sim::LEADER_PERSONALITIES[civId]);
        overrideDefs.back().behavior = overrides[i]->toBehavior();
        aoc::sim::setLeaderPersonalityOverride(civId, &overrideDefs.back());
        installedCivs.push_back(civId);
    }

    SimulationResult result{};
    const std::size_t pc = static_cast<std::size_t>(playerCount);
    result.eraVP.resize(pc, 0);
    result.compositeCSI.resize(pc, 0.0f);
    result.treasury.resize(pc, 0);
    result.cityCount.resize(pc, 0);
    result.peakCityCount.resize(pc, 0);
    result.population.resize(pc, 0);
    result.gdp.resize(pc, 0.0f);
    result.avgHappiness.resize(pc, 0.0f);
    result.totalIncome.resize(pc, 0.0f);
    result.totalExpense.resize(pc, 0.0f);

    aoc::map::HexGrid grid;
    aoc::Random rng(seed);

    // Generate map
    aoc::map::MapGenerator::Config mapConfig{};
    mapConfig.width = 80;
    mapConfig.height = 52;
    mapConfig.seed = rng.next();
    mapConfig.mapType = mapType;
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
        // Fast SIGINT/SIGTERM abort: check once per turn. Relaxed load is
        // fine — late by one turn is acceptable for a shutdown path.
        if (stopFlag != nullptr && stopFlag->load(std::memory_order_relaxed)) {
            result.valid = false;
            return result;
        }

        turnCtx.currentTurn = static_cast<aoc::TurnNumber>(turn);
        eventLog.clear();

        aoc::sim::processTurn(turnCtx);

        // Track peak city count (survival metric)
        for (int32_t p = 0; p < playerCount; ++p) {
            const aoc::game::Player* player =
                gameState.player(static_cast<aoc::PlayerId>(p));
            if (player == nullptr) { continue; }
            const int32_t current = player->cityCount();
            int32_t& peak = result.peakCityCount[static_cast<std::size_t>(p)];
            if (current > peak) { peak = current; }
        }

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
            result.victoryType = vr.type;
            result.winner      = vr.winner;
            break;
        }

        turnManager.beginNewTurn();
    }

    // Extract final scores + economic metrics for fitness evaluation
    for (int32_t p = 0; p < playerCount; ++p) {
        const std::size_t idx = static_cast<std::size_t>(p);
        const aoc::game::Player* player = gameState.player(static_cast<aoc::PlayerId>(p));
        if (player == nullptr) { continue; }

        result.eraVP[idx]        = player->victoryTracker().eraVictoryPoints;
        result.compositeCSI[idx] = player->victoryTracker().compositeCSI;
        result.treasury[idx]     = static_cast<int32_t>(player->treasury());
        result.cityCount[idx]    = player->cityCount();
        result.gdp[idx]          = static_cast<float>(player->monetary().gdp);

        int32_t popSum = 0;
        float happySum = 0.0f;
        int32_t happyN = 0;
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            popSum += city->population();
            happySum += city->happiness().happiness;
            ++happyN;
        }
        result.population[idx]   = popSum;
        result.avgHappiness[idx] = (happyN > 0) ? (happySum / static_cast<float>(happyN)) : 0.0f;

        aoc::sim::EconomicBreakdown bd =
            aoc::sim::computeEconomicBreakdown(*player, grid);
        result.totalIncome[idx]  = static_cast<float>(bd.totalIncome);
        result.totalExpense[idx] = static_cast<float>(bd.totalExpense);
    }
    result.valid = true;
    return result;
}

namespace {

struct GameScore {
    float score = 0.0f;
    /// Victory type that ended the game (None if timed out).
    aoc::sim::VictoryType victoryType = aoc::sim::VictoryType::None;
    /// True if Player 0 (the evaluated subject) was the winner.
    bool subjectWon = false;
    bool valid = false;
};

/// Build the per-player override span for one game task based on
/// `config.opponentMode`. Slot 0 always points to the individual being
/// evaluated; slots 1..N-1 are filled according to the mode.
///
/// `sampleSeed` seeds a local RNG so opponent selection is reproducible
/// per (generation, individual, game) triple but still varies across
/// those dimensions, giving individuals diverse opponent distributions.
void buildOverrides(const Individual& subject,
                    const std::vector<Individual>& population,
                    std::size_t subjectIdx,
                    const std::vector<Individual>* hallOfFame,
                    OpponentMode mode,
                    int32_t playerCount,
                    uint64_t sampleSeed,
                    std::vector<const Individual*>& outOverrides) {
    outOverrides.assign(static_cast<std::size_t>(playerCount), nullptr);
    outOverrides[0] = &subject;
    if (playerCount <= 1) { return; }

    std::mt19937_64 rng(sampleSeed);

    auto sampleFromCoEvolve = [&]() -> const Individual* {
        if (population.size() <= 1) { return nullptr; }
        std::uniform_int_distribution<std::size_t> dist(0, population.size() - 1);
        for (int32_t tries = 0; tries < 8; ++tries) {
            std::size_t pick = dist(rng);
            if (pick != subjectIdx) { return &population[pick]; }
        }
        // Fallback: return any (including subject is fine; self-play is valid).
        return &population[(subjectIdx + 1) % population.size()];
    };

    auto sampleFromHoF = [&]() -> const Individual* {
        if (hallOfFame != nullptr && !hallOfFame->empty()) {
            std::uniform_int_distribution<std::size_t> dist(0, hallOfFame->size() - 1);
            return &(*hallOfFame)[dist(rng)];
        }
        // No HoF yet: fall back to the current-population best by fitness.
        if (population.empty()) { return nullptr; }
        std::size_t best = 0;
        for (std::size_t i = 1; i < population.size(); ++i) {
            if (population[i].fitness > population[best].fitness) { best = i; }
        }
        return &population[best];
    };

    switch (mode) {
        case OpponentMode::Fixed:
            // All nullptr -> civ-type defaults.
            break;
        case OpponentMode::CoEvolve: {
            // Fill with random distinct individuals from the population.
            std::vector<std::size_t> idxs;
            idxs.reserve(population.size());
            for (std::size_t i = 0; i < population.size(); ++i) {
                if (i != subjectIdx) { idxs.push_back(i); }
            }
            std::shuffle(idxs.begin(), idxs.end(), rng);
            for (int32_t p = 1; p < playerCount; ++p) {
                const std::size_t k = static_cast<std::size_t>(p - 1);
                if (k < idxs.size()) {
                    outOverrides[static_cast<std::size_t>(p)] = &population[idxs[k]];
                } else {
                    // More slots than pop-1: wrap around.
                    outOverrides[static_cast<std::size_t>(p)] =
                        &population[idxs[k % std::max<std::size_t>(1, idxs.size())]];
                }
            }
            break;
        }
        case OpponentMode::Champion:
            for (int32_t p = 1; p < playerCount; ++p) {
                outOverrides[static_cast<std::size_t>(p)] = sampleFromHoF();
            }
            break;
        case OpponentMode::Mixed: {
            std::uniform_int_distribution<int32_t> modeDist(0, 2);
            for (int32_t p = 1; p < playerCount; ++p) {
                const int32_t pick = modeDist(rng);
                const Individual* opp = nullptr;
                if (pick == 0) {
                    opp = nullptr; // Fixed: civ default
                } else if (pick == 1) {
                    opp = sampleFromCoEvolve();
                } else {
                    opp = sampleFromHoF();
                }
                outOverrides[static_cast<std::size_t>(p)] = opp;
            }
            break;
        }
    }
}

/// Score the full SimulationResult for Player 0 using the outcome-component
/// formula. Extracted so rank-based future work can share the math.
float playerOutcomeScore(const SimulationResult& simResult,
                         std::size_t playerIdx, int32_t turns) {
    const int32_t myVP = simResult.eraVP[playerIdx];
    int32_t bestRivalVP = 0;
    for (std::size_t i = 0; i < simResult.eraVP.size(); ++i) {
        if (i == playerIdx) { continue; }
        if (simResult.eraVP[i] > bestRivalVP) { bestRivalVP = simResult.eraVP[i]; }
    }
    const float denomVP = static_cast<float>(std::max(1, std::max(myVP, bestRivalVP)));
    const float relativeWin = static_cast<float>(myVP - bestRivalVP) / denomVP;

    const float treasuryF = static_cast<float>(simResult.treasury[playerIdx]);
    const float turnsF    = static_cast<float>(std::max(1, turns));
    float economicHealth  = treasuryF / (turnsF * 5.0f);
    economicHealth = std::max(-1.0f, std::min(1.0f, economicHealth));

    const int32_t peak = simResult.peakCityCount[playerIdx];
    const int32_t held = simResult.cityCount[playerIdx];
    const float survival = (peak > 0)
        ? static_cast<float>(held) / static_cast<float>(peak)
        : 0.0f;

    const float income  = simResult.totalIncome[playerIdx];
    const float expense = simResult.totalExpense[playerIdx];
    float balancedFlow;
    if (income <= 0.0f) {
        balancedFlow = 0.0f;
    } else {
        const float ratio = (income - expense) / income;
        balancedFlow = std::max(0.0f, std::min(1.0f, 0.5f + 0.5f * ratio));
    }

    const float rawHappy  = simResult.avgHappiness[playerIdx];
    const float happiness = std::max(0.0f, std::min(1.0f, (rawHappy + 5.0f) / 10.0f));

    const bool eliminated = (peak > 0 && held == 0);
    if (eliminated) { return -1.0f; }

    return 1.0f  * relativeWin
         + 0.25f * economicHealth
         + 0.25f * survival
         + 0.20f * balancedFlow
         + 0.15f * happiness;
}

/// Single-game fitness contribution. Runs one headless sim with `overrides`
/// installed (slot 0 = individual being evaluated, slots 1..N-1 = opponents
/// per OpponentMode) and returns Player 0's outcome-component sum.
GameScore scoreOneGame(std::span<const Individual* const> overrides,
                       const GAConfig& config,
                       int32_t game, uint64_t baseSeed) {
    GameScore out{};
    if (config.stopFlag != nullptr
        && config.stopFlag->load(std::memory_order_relaxed)) {
        return out;
    }

    const uint64_t gameSeed = baseSeed + static_cast<uint64_t>(game) * 7919;
    const int32_t turns = config.turnsList.empty()
        ? config.turnsPerGame
        : config.turnsList[static_cast<std::size_t>(game)
                            % config.turnsList.size()];
    const int32_t playerCount = config.playersList.empty()
        ? config.playerCount
        : config.playersList[static_cast<std::size_t>(game)
                              % config.playersList.size()];
    const aoc::map::MapType mapType = config.mapsList.empty()
        ? aoc::map::MapType::Realistic
        : config.mapsList[static_cast<std::size_t>(game)
                           % config.mapsList.size()];

    SimulationResult simResult = runSimulation(turns, playerCount, gameSeed,
                                                config.stopFlag, overrides, mapType);
    if (!simResult.valid || simResult.eraVP.empty()) { return out; }

    constexpr std::size_t P0 = 0;
    if (simResult.eraVP.size() <= P0) { return out; }
    // Outcome-driven fitness for Player 0 (subject being evaluated).
    //   relativeWin  : EraVP vs best rival, [-1, 1]. Dominant.
    //   economicHealth : treasury / (turns * 5), clamped.
    //   survival     : cities held / peak cities.
    //   balancedFlow : 0.5 + 0.5 * (income - expense)/income.
    //   happiness    : avg happiness shifted/clamped to [0, 1].
    out.score       = playerOutcomeScore(simResult, P0, turns);
    out.victoryType = simResult.victoryType;
    out.subjectWon  = (simResult.winner == static_cast<aoc::PlayerId>(P0));
    out.valid       = true;
    return out;
}

} // namespace

float evaluateFitness(const Individual& individual,
                       const GAConfig& config,
                       uint64_t baseSeed) {
    // Standalone API: always uses Fixed opponents (no co-evolve). Callers
    // wanting co-evolution must go through evaluatePopulation.
    std::array<const Individual*, 1> overrides{&individual};
    std::span<const Individual* const> span(overrides.data(), overrides.size());

    float totalScore = 0.0f;
    int32_t validGames = 0;
    for (int32_t game = 0; game < config.gamesPerEval; ++game) {
        if (config.stopFlag != nullptr
            && config.stopFlag->load(std::memory_order_relaxed)) { break; }
        GameScore s = scoreOneGame(span, config, game, baseSeed);
        if (!s.valid) { continue; }
        totalScore += s.score;
        ++validGames;
    }
    return (validGames > 0) ? totalScore / static_cast<float>(validGames) : 0.0f;
}

void evaluatePopulation(std::vector<Individual>& population,
                         const GAConfig& config,
                         uint64_t baseSeed,
                         ThreadPool* pool,
                         const std::vector<Individual>* hallOfFame) {
    std::vector<std::size_t> toEvaluate;
    for (std::size_t i = 0; i < population.size(); ++i) {
        if (population[i].gamesPlayed == 0) {
            toEvaluate.push_back(i);
        }
    }
    if (toEvaluate.empty()) { return; }

    const int32_t totalGames =
        static_cast<int32_t>(toEvaluate.size()) * config.gamesPerEval;
    std::atomic<int32_t> completed{0};
    std::chrono::steady_clock::time_point evalStart =
        std::chrono::steady_clock::now();

    auto renderProgress = [&](bool finalLine) {
        int32_t done = completed.load(std::memory_order_acquire);
        double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - evalStart).count();
        double rate = (elapsed > 0.0) ? static_cast<double>(done) / elapsed : 0.0;
        double etaSec = (rate > 0.0 && done < totalGames)
            ? static_cast<double>(totalGames - done) / rate
            : 0.0;
        constexpr int32_t BAR_W = 24;
        int32_t filled = (totalGames > 0)
            ? (done * BAR_W + totalGames / 2) / totalGames
            : BAR_W;
        char bar[BAR_W + 1];
        for (int32_t i = 0; i < BAR_W; ++i) {
            bar[i] = (i < filled) ? '#' : '.';
        }
        bar[BAR_W] = '\0';
        double pct = (totalGames > 0)
            ? 100.0 * static_cast<double>(done) / static_cast<double>(totalGames)
            : 100.0;
        std::fprintf(stderr,
            "\r  [eval %s] %d/%d games (%.1f%%) %.1fs  %.2f games/s  ETA %.0fs   %s",
            bar, done, totalGames, pct, elapsed, rate, etaSec,
            finalLine ? "\n" : "");
        std::fflush(stderr);
    };

    auto stopRequested = [&config]() {
        return config.stopFlag != nullptr
               && config.stopFlag->load(std::memory_order_relaxed);
    };

    // Per-task scorer: build the opponent override slot set for (idx, g)
    // and run one game. Inlined as a lambda so both the sequential and
    // pooled paths share the same logic.
    auto runTask = [&](std::size_t idx, int32_t g, uint64_t individualSeed) {
        // sampleSeed differs per (gen, individual, game) so opponent draws
        // are reproducible AND varied across the eval.
        const uint64_t sampleSeed = individualSeed
                                  + static_cast<uint64_t>(g) * 2654435761ULL
                                  + 0x9E3779B97F4A7C15ULL;
        std::vector<const Individual*> overrides;
        buildOverrides(population[idx], population, idx, hallOfFame,
                       config.opponentMode,
                       (config.playersList.empty()
                          ? config.playerCount
                          : config.playersList[static_cast<std::size_t>(g)
                                               % config.playersList.size()]),
                       sampleSeed, overrides);
        std::span<const Individual* const> span(overrides.data(), overrides.size());
        return scoreOneGame(span, config, g, individualSeed);
    };

    // Per-individual GameScore storage. Collected first so balance-winrate
    // mode can compute a generation-wide rareness histogram before fitness
    // is finalized.
    std::vector<std::vector<GameScore>> perIndividualScores(toEvaluate.size());

    // Sequential fallback: no pool provided.
    if (pool == nullptr) {
        renderProgress(false);
        for (std::size_t k = 0; k < toEvaluate.size(); ++k) {
            if (stopRequested()) { break; }
            const std::size_t idx = toEvaluate[k];
            const uint64_t individualSeed = baseSeed + idx * 104729;
            perIndividualScores[k].reserve(
                static_cast<std::size_t>(config.gamesPerEval));
            for (int32_t g = 0; g < config.gamesPerEval; ++g) {
                if (stopRequested()) { break; }
                perIndividualScores[k].push_back(runTask(idx, g, individualSeed));
                completed.fetch_add(1, std::memory_order_release);
                renderProgress(false);
            }
        }
        renderProgress(true);
    } else {
        std::fprintf(stderr, "  [eval] %d individuals x %d games = %d tasks, %zu workers"
                             "  (opponents: %s)%s\n",
                     static_cast<int32_t>(toEvaluate.size()),
                     config.gamesPerEval, totalGames, pool->size(),
                     opponentModeName(config.opponentMode),
                     config.balanceWinrate ? "  [balance-winrate]" : "");
        renderProgress(false);

        std::jthread watcher([&renderProgress](std::stop_token stop) {
            while (!stop.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                if (stop.stop_requested()) { break; }
                renderProgress(false);
            }
        });

        // Flatten to game-level tasks: (pop × gamesPerEval) futures submitted
        // up-front to the pool. Workers pull tasks as they finish, so straggler
        // variance averages out across the larger task count.
        //
        // Opponent sampling happens INSIDE the worker via buildOverrides, so
        // every task reads the current (immutable during eval) population and
        // hallOfFame pointers. Both live on the main stack and are not mutated
        // until evaluatePopulation returns.
        struct IndivFutures {
            std::vector<std::future<GameScore>> futures;
        };
        std::vector<IndivFutures> perIndividual(toEvaluate.size());

        for (std::size_t k = 0; k < toEvaluate.size(); ++k) {
            const std::size_t idx = toEvaluate[k];
            const uint64_t individualSeed = baseSeed + idx * 104729;
            perIndividual[k].futures.reserve(
                static_cast<std::size_t>(config.gamesPerEval));
            for (int32_t g = 0; g < config.gamesPerEval; ++g) {
                if (stopRequested()) { break; }
                perIndividual[k].futures.push_back(
                    pool->submit([&runTask, idx, g, individualSeed, &completed] {
                        GameScore s = runTask(idx, g, individualSeed);
                        completed.fetch_add(1, std::memory_order_release);
                        return s;
                    }));
            }
        }

        // Reduce per-individual: wait on that individual's game futures.
        for (std::size_t k = 0; k < toEvaluate.size(); ++k) {
            perIndividualScores[k].reserve(perIndividual[k].futures.size());
            for (std::future<GameScore>& f : perIndividual[k].futures) {
                perIndividualScores[k].push_back(f.get());
            }
        }

        watcher.request_stop();
        watcher.join();
        renderProgress(true);
    }

    // Balance-winrate histogram: count subject-wins per victory type across
    // the whole generation. Rareness[T] = gentle inverse frequency, scaled
    // so the rarest type receives up to config.balanceBonus and the most
    // common receives ~0.
    //
    // Also track ALL game endings (regardless of winner) for diagnostic output
    // — this exposes whether non-Score/Culture conditions ever actually fire,
    // independent of whether the evaluated subject happened to win.
    constexpr std::size_t VT_COUNT = 8;  // Must match VictoryType enum size.
    std::array<int32_t, VT_COUNT> winsByType{};
    std::array<int32_t, VT_COUNT> allEndings{};
    int32_t totalSubjectWins = 0;
    int32_t totalValidGames  = 0;
    for (const std::vector<GameScore>& games : perIndividualScores) {
        for (const GameScore& s : games) {
            if (!s.valid) { continue; }
            ++totalValidGames;
            const std::size_t t = static_cast<std::size_t>(s.victoryType);
            if (t < VT_COUNT) { ++allEndings[t]; }
            if (config.balanceWinrate && s.subjectWon && t < VT_COUNT) {
                ++winsByType[t];
                ++totalSubjectWins;
            }
        }
    }

    auto rarenessFor = [&](aoc::sim::VictoryType vt) -> float {
        if (!config.balanceWinrate || totalSubjectWins <= 0) { return 0.0f; }
        const std::size_t t = static_cast<std::size_t>(vt);
        if (t >= VT_COUNT) { return 0.0f; }
        const float freq = static_cast<float>(winsByType[t])
                          / static_cast<float>(totalSubjectWins);
        // 1 - freq in [0, 1): rarest near 1, dominant near 0.
        return (1.0f - freq) * config.balanceBonus;
    };

    // Finalize fitness from collected scores. Adds per-win rareness bonus
    // when balance-winrate is active so a rare-type winner outranks a
    // dominant-type winner with equal base outcome.
    for (std::size_t k = 0; k < toEvaluate.size(); ++k) {
        const std::size_t idx = toEvaluate[k];
        float total = 0.0f;
        int32_t valid = 0;
        for (const GameScore& s : perIndividualScores[k]) {
            if (!s.valid) { continue; }
            float gameScore = s.score;
            if (s.subjectWon) {
                gameScore += rarenessFor(s.victoryType);
            }
            total += gameScore;
            ++valid;
        }
        population[idx].fitness = (valid > 0)
            ? total / static_cast<float>(valid) : 0.0f;
        population[idx].gamesPlayed = config.gamesPerEval;
    }

    static const char* VT_NAMES[VT_COUNT] = {
        "None", "Score", "Integration", "LastStanding",
        "Science", "Domination", "Culture", "Religion"
    };
    if (config.balanceWinrate && totalSubjectWins > 0) {
        std::fprintf(stderr, "  [balance] subject wins:");
        for (std::size_t t = 1; t < VT_COUNT; ++t) {
            if (winsByType[t] > 0) {
                std::fprintf(stderr, " %s=%d", VT_NAMES[t], winsByType[t]);
            }
        }
        std::fprintf(stderr, "  (total=%d)\n", totalSubjectWins);
    }
    if (totalValidGames > 0) {
        std::fprintf(stderr, "  [endings] all %d games:", totalValidGames);
        int32_t reported = 0;
        for (std::size_t t = 0; t < VT_COUNT; ++t) {
            if (allEndings[t] > 0) {
                std::fprintf(stderr, " %s=%d", VT_NAMES[t], allEndings[t]);
                reported += allEndings[t];
            }
        }
        const int32_t unfinished = totalValidGames - reported;
        if (unfinished > 0) {
            std::fprintf(stderr, " NoWinner=%d", unfinished);
        }
        std::fprintf(stderr, "\n");
    }
}

} // namespace aoc::ga

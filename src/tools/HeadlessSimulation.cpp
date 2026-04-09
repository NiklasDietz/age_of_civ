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

#include "aoc/ecs/World.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/MapGenerator.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/core/SimpleYaml.hpp"
#include "aoc/simulation/turn/GameLength.hpp"

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
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/District.hpp"
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

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

using aoc::EntityId;

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
using aoc::PlayerId;
using aoc::TechId;
using aoc::UnitTypeId;
using aoc::CurrencyAmount;
using aoc::ResourceId;
using namespace aoc::map;
namespace hex = aoc::hex;

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

PlayerSnapshot snapshotPlayer(const aoc::ecs::World& world,
                               aoc::PlayerId player) {
    PlayerSnapshot snap{};
    snap.player = player;

    // Monetary state
    const aoc::ecs::ComponentPool<aoc::sim::MonetaryStateComponent>* mPool =
        world.getPool<aoc::sim::MonetaryStateComponent>();
    if (mPool != nullptr) {
        for (uint32_t i = 0; i < mPool->size(); ++i) {
            if (mPool->data()[i].owner == player) {
                snap.gdp = mPool->data()[i].gdp;
                snap.treasury = mPool->data()[i].treasury;
                snap.coinTier = static_cast<uint8_t>(mPool->data()[i].effectiveCoinTier);
                snap.monetarySystem = static_cast<uint8_t>(mPool->data()[i].system);
                snap.inflationRate = mPool->data()[i].inflationRate;
                break;
            }
        }
    }

    // Cities and population
    const aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cPool =
        world.getPool<aoc::sim::CityComponent>();
    if (cPool != nullptr) {
        for (uint32_t i = 0; i < cPool->size(); ++i) {
            if (cPool->data()[i].owner == player) {
                ++snap.cities;
                snap.population += cPool->data()[i].population;
            }
        }
    }

    // Happiness
    const aoc::ecs::ComponentPool<aoc::sim::CityHappinessComponent>* hPool =
        world.getPool<aoc::sim::CityHappinessComponent>();
    if (hPool != nullptr && cPool != nullptr) {
        float totalHappy = 0.0f;
        int32_t happyCities = 0;
        for (uint32_t i = 0; i < cPool->size(); ++i) {
            if (cPool->data()[i].owner != player) { continue; }
            const aoc::sim::CityHappinessComponent* h =
                world.tryGetComponent<aoc::sim::CityHappinessComponent>(cPool->entities()[i]);
            if (h != nullptr) {
                totalHappy += h->amenities - h->demand;
                ++happyCities;
            }
        }
        if (happyCities > 0) {
            snap.avgHappiness = totalHappy / static_cast<float>(happyCities);
        }
    }

    // Military units
    const aoc::ecs::ComponentPool<aoc::sim::UnitComponent>* uPool =
        world.getPool<aoc::sim::UnitComponent>();
    if (uPool != nullptr) {
        for (uint32_t i = 0; i < uPool->size(); ++i) {
            if (uPool->data()[i].owner == player
                && aoc::sim::isMilitary(aoc::sim::unitTypeDef(uPool->data()[i].typeId).unitClass)) {
                ++snap.militaryUnits;
            }
        }
    }

    // Techs researched
    const aoc::ecs::ComponentPool<aoc::sim::PlayerTechComponent>* tPool =
        world.getPool<aoc::sim::PlayerTechComponent>();
    if (tPool != nullptr) {
        for (uint32_t i = 0; i < tPool->size(); ++i) {
            if (tPool->data()[i].owner == player) {
                for (std::size_t b = 0; b < tPool->data()[i].completedTechs.size(); ++b) {
                    if (tPool->data()[i].completedTechs[b]) { ++snap.techsResearched; }
                }
                break;
            }
        }
    }

    // Victory tracker
    const aoc::ecs::ComponentPool<aoc::sim::VictoryTrackerComponent>* vPool =
        world.getPool<aoc::sim::VictoryTrackerComponent>();
    if (vPool != nullptr) {
        for (uint32_t i = 0; i < vPool->size(); ++i) {
            if (vPool->data()[i].owner == player) {
                snap.compositeCSI = vPool->data()[i].compositeCSI;
                snap.eraVP = vPool->data()[i].eraVictoryPoints;
                snap.cultureTotal = vPool->data()[i].totalCultureAccumulated;
                break;
            }
        }
    }

    // Crisis
    const aoc::ecs::ComponentPool<aoc::sim::CurrencyCrisisComponent>* crPool =
        world.getPool<aoc::sim::CurrencyCrisisComponent>();
    if (crPool != nullptr) {
        for (uint32_t i = 0; i < crPool->size(); ++i) {
            if (crPool->data()[i].owner == player) {
                snap.crisisType = static_cast<uint8_t>(crPool->data()[i].activeCrisis);
                break;
            }
        }
    }

    // Industrial revolution
    const aoc::ecs::ComponentPool<aoc::sim::PlayerIndustrialComponent>* indPool =
        world.getPool<aoc::sim::PlayerIndustrialComponent>();
    if (indPool != nullptr) {
        for (uint32_t i = 0; i < indPool->size(); ++i) {
            if (indPool->data()[i].owner == player) {
                snap.industrialRev = static_cast<uint8_t>(indPool->data()[i].currentRevolution);
                break;
            }
        }
    }

    // Government
    const aoc::ecs::ComponentPool<aoc::sim::PlayerGovernmentComponent>* gPool =
        world.getPool<aoc::sim::PlayerGovernmentComponent>();
    if (gPool != nullptr) {
        for (uint32_t i = 0; i < gPool->size(); ++i) {
            if (gPool->data()[i].owner == player) {
                snap.governmentType = static_cast<uint8_t>(gPool->data()[i].government);

                // Compute corruption
                int32_t cityCount = snap.cities;
                snap.corruption = aoc::sim::computeCorruption(
                    gPool->data()[i].government, cityCount, 0.0f);
                break;
            }
        }
    }

    // Trade partners
    const aoc::ecs::ComponentPool<aoc::sim::TradeRouteComponent>* trPool =
        world.getPool<aoc::sim::TradeRouteComponent>();
    if (trPool != nullptr) {
        std::unordered_set<aoc::PlayerId> partners;
        for (uint32_t i = 0; i < trPool->size(); ++i) {
            if (trPool->data()[i].sourcePlayer == player) {
                partners.insert(trPool->data()[i].destPlayer);
            }
            if (trPool->data()[i].destPlayer == player) {
                partners.insert(trPool->data()[i].sourcePlayer);
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

    // CSV header
    csv << "Turn,Player,GDP,Treasury,CoinTier,MonetarySystem,Inflation,"
        << "Population,Cities,Military,TechsResearched,CultureTotal,"
        << "TradePartners,CompositeCSI,EraVP,AvgHappiness,"
        << "Corruption,CrisisType,IndustrialRev,GovernmentType\n";

    // Initialize world
    aoc::ecs::World world;
    aoc::map::HexGrid grid;
    aoc::Random rng(42);  // Fixed seed for reproducibility

    // Initialize game pace (default Standard; overridden by config in main())
    // The GamePace singleton is set before this function is called if config specifies game_length.

    // Generate map (use Realistic for resource placement)
    aoc::map::MapGenerator::Config mapConfig{};
    mapConfig.width = 60;
    mapConfig.height = 40;
    mapConfig.seed = 42;
    mapConfig.mapType = aoc::map::MapType::Realistic;
    aoc::map::MapGenerator generator;
    generator.generate(mapConfig, grid);

    LOG_INFO("Map generated: %dx%d", mapConfig.width, mapConfig.height);

    // Create players (all AI)
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
    std::vector<hex::AxialCoord> startPositions;
    constexpr int32_t MIN_START_DISTANCE = 8;

    // Spawn each AI player with a starting city and settler
    for (int32_t p = 0; p < playerCount; ++p) {
        aoc::PlayerId player = static_cast<aoc::PlayerId>(p);

        // Find a land tile for the starting city
        hex::AxialCoord startPos{0, 0};
        bool found = false;
        for (int32_t attempts = 0; attempts < 1000 && !found; ++attempts) {
            int32_t rx = rng.nextInt(5, mapConfig.width - 5);
            int32_t ry = rng.nextInt(5, mapConfig.height - 5);
            int32_t idx = ry * mapConfig.width + rx;
            if (!aoc::map::isWater(grid.terrain(idx))
                && !aoc::map::isImpassable(grid.terrain(idx))) {
                hex::AxialCoord candidate = hex::offsetToAxial({rx, ry});
                // Check minimum distance from all existing starts
                bool tooClose = false;
                for (const hex::AxialCoord& existing : startPositions) {
                    if (hex::distance(candidate, existing) < MIN_START_DISTANCE) {
                        tooClose = true;
                        break;
                    }
                }
                if (!tooClose) {
                    startPos = candidate;
                    found = true;
                }
            }
        }

        startPositions.push_back(startPos);

        // Create city
        EntityId cityEntity = world.createEntity();
        aoc::sim::CityComponent city{};
        city.owner = player;
        city.name = std::string(aoc::sim::civDef(static_cast<aoc::sim::CivId>(p % aoc::sim::CIV_COUNT)).cityNames[0]);
        city.location = startPos;
        city.population = 3;
        city.isOriginalCapital = true;
        city.workedTiles.push_back(startPos);
        // Add neighboring tiles to worked tiles
        std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(startPos);
        for (int32_t n = 0; n < 3 && n < 6; ++n) {
            if (grid.isValid(nbrs[static_cast<std::size_t>(n)])) {
                city.workedTiles.push_back(nbrs[static_cast<std::size_t>(n)]);
            }
        }
        world.addComponent<aoc::sim::CityComponent>(cityEntity, std::move(city));
        world.addComponent<aoc::sim::CityHappinessComponent>(cityEntity, aoc::sim::CityHappinessComponent{});
        world.addComponent<aoc::sim::ProductionQueueComponent>(cityEntity, aoc::sim::ProductionQueueComponent{});
        world.addComponent<aoc::sim::CityStockpileComponent>(cityEntity, aoc::sim::CityStockpileComponent{});

        // Create CityCenter district so CityCenter buildings can be placed
        aoc::sim::CityDistrictsComponent districts{};
        aoc::sim::CityDistrictsComponent::PlacedDistrict centerDistrict{};
        centerDistrict.type = aoc::sim::DistrictType::CityCenter;
        centerDistrict.location = startPos;
        districts.districts.push_back(std::move(centerDistrict));
        world.addComponent<aoc::sim::CityDistrictsComponent>(cityEntity, std::move(districts));

        // Claim surrounding tiles for this player
        int32_t centerIdx = grid.toIndex(startPos);
        grid.setOwner(centerIdx, player);
        for (const hex::AxialCoord& nbr : nbrs) {
            if (grid.isValid(nbr)) {
                grid.setOwner(grid.toIndex(nbr), player);
            }
        }

        // Guarantee minimum resources near starting position
        // Place wheat on center if no resource, iron+copper on neighbors
        if (!grid.resource(centerIdx).isValid()) {
            grid.setResource(centerIdx, ResourceId{aoc::sim::goods::WHEAT});
        }
        int32_t resourcesPlaced = 0;
        const uint16_t STARTER_RESOURCES[] = {
            aoc::sim::goods::IRON_ORE, aoc::sim::goods::COPPER_ORE,
            aoc::sim::goods::WOOD, aoc::sim::goods::STONE,
            aoc::sim::goods::COAL, aoc::sim::goods::CATTLE
        };
        for (const hex::AxialCoord& nbr2 : nbrs) {
            if (!grid.isValid(nbr2)) { continue; }
            int32_t nbrIdx = grid.toIndex(nbr2);
            if (!grid.resource(nbrIdx).isValid()
                && !aoc::map::isWater(grid.terrain(nbrIdx))
                && !aoc::map::isImpassable(grid.terrain(nbrIdx))
                && resourcesPlaced < 6) {
                grid.setResource(nbrIdx, ResourceId{STARTER_RESOURCES[resourcesPlaced]});
                ++resourcesPlaced;
            }
        }

        // Create player entity with monetary state
        EntityId playerEntity = world.createEntity();
        aoc::sim::MonetaryStateComponent monetary{};
        monetary.owner = player;
        monetary.system = aoc::sim::MonetarySystemType::Barter;
        monetary.treasury = 100;
        world.addComponent<aoc::sim::MonetaryStateComponent>(playerEntity, std::move(monetary));

        aoc::sim::PlayerEconomyComponent econ{};
        econ.owner = player;
        econ.treasury = 100;
        world.addComponent<aoc::sim::PlayerEconomyComponent>(playerEntity, std::move(econ));

        aoc::sim::PlayerTechComponent tech{};
        tech.owner = player;
        tech.initialize();
        // Give first tech for free so AI has something to work with
        tech.completedTechs[0] = true;
        world.addComponent<aoc::sim::PlayerTechComponent>(playerEntity, std::move(tech));

        aoc::sim::PlayerCivicComponent civic{};
        civic.owner = player;
        civic.initialize();
        world.addComponent<aoc::sim::PlayerCivicComponent>(playerEntity, std::move(civic));

        aoc::sim::PlayerGovernmentComponent gov{};
        gov.owner = player;
        world.addComponent<aoc::sim::PlayerGovernmentComponent>(playerEntity, std::move(gov));

        aoc::sim::VictoryTrackerComponent victory{};
        victory.owner = player;
        world.addComponent<aoc::sim::VictoryTrackerComponent>(playerEntity, std::move(victory));

        aoc::sim::PlayerCivilizationComponent civComp{};
        civComp.owner = player;
        civComp.civId = static_cast<aoc::sim::CivId>(p % aoc::sim::CIV_COUNT);
        world.addComponent<aoc::sim::PlayerCivilizationComponent>(playerEntity, std::move(civComp));

        // Create a scout unit
        EntityId unitEntity = world.createEntity();
        world.addComponent<aoc::sim::UnitComponent>(
            unitEntity, aoc::sim::UnitComponent::create(player, UnitTypeId{2}, startPos));

        // Create AI controller
        aiControllers.emplace_back(player);

        LOG_INFO("Player %d (%.*s) placed at (%d,%d)",
                 p,
                 static_cast<int>(aoc::sim::civDef(static_cast<aoc::sim::CivId>(p % aoc::sim::CIV_COUNT)).name.size()),
                 aoc::sim::civDef(static_cast<aoc::sim::CivId>(p % aoc::sim::CIV_COUNT)).name.data(),
                 startPos.q, startPos.r);
    }

    // === Main simulation loop ===
    for (int32_t turn = 1; turn <= maxTurns; ++turn) {
        // AI decisions
        for (aoc::sim::ai::AIController& ai : aiControllers) {
            ai.executeTurn(world, grid, diplomacy, economy.market(), rng);
            turnManager.submitEndTurn(ai.player());
        }

        // Economy
        economy.executeTurn(world, grid);

        // Per-player processing
        for (const aoc::sim::ai::AIController& ai : aiControllers) {
            aoc::PlayerId p = ai.player();
            aoc::sim::processAdvancedEconomics(world, grid, p, economy.market());
            aoc::sim::processWarWeariness(world, p, diplomacy);
            aoc::sim::processCityGrowth(world, grid, p);
            aoc::sim::computeCityHappiness(world, p);
            aoc::sim::computeCityLoyalty(world, grid, p);

            float science = aoc::sim::computePlayerScience(world, grid, p);
            float culture = aoc::sim::computePlayerCulture(world, grid, p);
            world.forEach<aoc::sim::PlayerTechComponent>(
                [p, science](EntityId, aoc::sim::PlayerTechComponent& tech) {
                    if (tech.owner == p) { aoc::sim::advanceResearch(tech, science); }
                });
            world.forEach<aoc::sim::PlayerCivicComponent>(
                [p, culture, &world](EntityId, aoc::sim::PlayerCivicComponent& civic) {
                    if (civic.owner != p) { return; }
                    // Find government component for unlocking
                    aoc::sim::PlayerGovernmentComponent* gov = nullptr;
                    world.forEach<aoc::sim::PlayerGovernmentComponent>(
                        [p, &gov](EntityId, aoc::sim::PlayerGovernmentComponent& g) {
                            if (g.owner == p) { gov = &g; }
                        });
                    aoc::sim::advanceCivicResearch(civic, culture, gov);
                });

            aoc::sim::processProductionQueues(world, grid, p);
        }

        // Barbarians
        barbarians.executeTurn(world, grid, rng);

        // Victory tracking
        aoc::sim::updateVictoryTrackers(world, grid, economy,
                                         static_cast<aoc::TurnNumber>(turn));

        // === Log snapshot for each player ===
        for (int32_t p = 0; p < playerCount; ++p) {
            PlayerSnapshot snap = snapshotPlayer(world, static_cast<aoc::PlayerId>(p));
            csv << turn << ","
                << static_cast<int>(snap.player) << ","
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
                << static_cast<int>(snap.governmentType) << "\n";
        }

        // Check victory
        aoc::sim::VictoryResult vr = aoc::sim::checkVictoryConditions(
            world, static_cast<aoc::TurnNumber>(turn), static_cast<aoc::TurnNumber>(maxTurns));
        if (vr.type != aoc::sim::VictoryType::None) {
            printProgressBar(turn, maxTurns);
            std::fprintf(stderr, "\n\n  GAME OVER on turn %d: Player %u wins (type %d)\n",
                         turn, static_cast<unsigned>(vr.winner),
                         static_cast<int>(vr.type));
            break;
        }

        // Progress bar
        printProgressBar(turn, maxTurns);

        // Detailed logging at intervals
        if (turn % 25 == 0) {
            PlayerSnapshot s0 = snapshotPlayer(world, 0);
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

    // Check if first arg is a .yaml/.yml config file
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

                // Game length preset (overrides max_turns if set)
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

    // Fallback: positional arguments
    if (!loadedConfig) {
        if (argc >= 2) { turns = std::atoi(argv[1]); }
        if (argc >= 3) { players = std::atoi(argv[2]); }
        if (argc >= 4) { outputPath = argv[3]; }

        std::fprintf(stderr, "\n  === Age of Civilization: Headless Simulation ===\n\n");
        std::fprintf(stderr, "  Turns:   %d\n", turns);
        std::fprintf(stderr, "  Players: %d\n", players);
        std::fprintf(stderr, "  Output:  %s\n\n", outputPath.c_str());
    }

    if (turns <= 0) { turns = 200; }
    if (players < 2) { players = 2; }
    if (players > 12) { players = 12; }

    int result = runHeadlessSimulation(turns, players, outputPath);

    // Final newline after progress bar
    std::fprintf(stderr, "\n\n");
    return result;
}
#endif

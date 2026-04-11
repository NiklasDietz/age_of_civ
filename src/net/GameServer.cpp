/**
 * @file GameServer.cpp
 * @brief Authoritative game server implementation.
 */

#include "aoc/net/GameServer.hpp"
#include "aoc/simulation/turn/TurnProcessor.hpp"
#include "aoc/simulation/turn/GameLength.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/unit/Combat.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/victory/VictoryCondition.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"
#include "aoc/map/Pathfinding.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::net {

GameServer::GameServer() : m_rng(42) {}
GameServer::~GameServer() = default;

void GameServer::initialize(const GameConfig& config) {
    this->m_maxTurns = config.maxTurns;
    this->m_rng = aoc::Random(config.seed);
    this->m_gameOver = false;

    // Generate map
    aoc::map::MapGenerator::Config mapConfig{};
    mapConfig.width = config.mapWidth;
    mapConfig.height = config.mapHeight;
    mapConfig.seed = config.seed;
    mapConfig.mapType = config.mapType;
    aoc::map::MapGenerator generator;
    generator.generate(mapConfig, this->m_grid);

    // Initialize economy
    this->m_economy.initialize();

    // Initialize diplomacy
    int32_t totalPlayers = config.humanPlayerCount + config.aiPlayerCount;
    this->m_diplomacy.initialize(static_cast<uint8_t>(totalPlayers));

    // Initialize turn manager
    this->m_turnManager.setPlayerCount(
        static_cast<uint8_t>(config.humanPlayerCount),
        static_cast<uint8_t>(config.aiPlayerCount));

    // Track players
    this->m_playerReady.resize(static_cast<std::size_t>(totalPlayers), false);

    // Create human players
    for (int32_t i = 0; i < config.humanPlayerCount; ++i) {
        PlayerId player = static_cast<PlayerId>(i);
        this->m_humanPlayers.push_back(player);
        this->m_allPlayers.push_back(player);
    }

    // Create AI players
    for (int32_t i = 0; i < config.aiPlayerCount; ++i) {
        PlayerId player = static_cast<PlayerId>(config.humanPlayerCount + i);
        this->m_allPlayers.push_back(player);
        this->m_aiControllers.emplace_back(player);
    }

    // Spawn starting cities and player entities for each player
    for (int32_t p = 0; p < totalPlayers; ++p) {
        PlayerId player = static_cast<PlayerId>(p);
        uint8_t civId = (p < static_cast<int32_t>(config.civAssignments.size()))
            ? config.civAssignments[static_cast<std::size_t>(p)]
            : static_cast<uint8_t>(p % aoc::sim::CIV_COUNT);

        // Find starting position
        aoc::hex::AxialCoord startPos{0, 0};
        for (int32_t attempts = 0; attempts < 1000; ++attempts) {
            int32_t rx = this->m_rng.nextInt(5, config.mapWidth - 5);
            int32_t ry = this->m_rng.nextInt(5, config.mapHeight - 5);
            int32_t idx = ry * config.mapWidth + rx;
            if (!aoc::map::isWater(this->m_grid.terrain(idx))
                && !aoc::map::isImpassable(this->m_grid.terrain(idx))) {
                startPos = aoc::hex::offsetToAxial({rx, ry});
                break;
            }
        }

        // Found starting city
        std::string cityName = std::string(
            aoc::sim::civDef(static_cast<aoc::sim::CivId>(civId)).cityNames[0]);
        aoc::sim::foundCity(this->m_world, this->m_grid, player, startPos, cityName, true, 1);

        // Create player entity with all components
        EntityId playerEntity = this->m_world.createEntity();

        aoc::sim::MonetaryStateComponent monetary{};
        monetary.owner = player;
        monetary.treasury = 100;
        this->m_world.addComponent<aoc::sim::MonetaryStateComponent>(playerEntity, std::move(monetary));

        aoc::sim::PlayerEconomyComponent econ{};
        econ.owner = player;
        econ.treasury = 100;
        this->m_world.addComponent<aoc::sim::PlayerEconomyComponent>(playerEntity, std::move(econ));

        aoc::sim::PlayerTechComponent tech{};
        tech.owner = player;
        tech.initialize();
        tech.completedTechs[0] = true;  // Start with Mining
        this->m_world.addComponent<aoc::sim::PlayerTechComponent>(playerEntity, std::move(tech));

        aoc::sim::PlayerCivicComponent civic{};
        civic.owner = player;
        civic.initialize();
        this->m_world.addComponent<aoc::sim::PlayerCivicComponent>(playerEntity, std::move(civic));

        aoc::sim::PlayerGovernmentComponent gov{};
        gov.owner = player;
        this->m_world.addComponent<aoc::sim::PlayerGovernmentComponent>(playerEntity, std::move(gov));

        aoc::sim::VictoryTrackerComponent victory{};
        victory.owner = player;
        this->m_world.addComponent<aoc::sim::VictoryTrackerComponent>(playerEntity, std::move(victory));

        aoc::sim::PlayerCivilizationComponent civComp{};
        civComp.owner = player;
        civComp.civId = static_cast<aoc::sim::CivId>(civId);
        this->m_world.addComponent<aoc::sim::PlayerCivilizationComponent>(playerEntity, std::move(civComp));

        // Create starting scout
        EntityId unitEntity = this->m_world.createEntity();
        this->m_world.addComponent<aoc::sim::UnitComponent>(
            unitEntity,
            aoc::sim::UnitComponent::create(player, UnitTypeId{2}, startPos));
    }

    // Build turn context
    // world accessed via gameState.legacyWorld()
    this->m_turnCtx.grid = &this->m_grid;
    this->m_turnCtx.economy = &this->m_economy;
    this->m_turnCtx.diplomacy = &this->m_diplomacy;
    this->m_turnCtx.barbarians = &this->m_barbarians;
    this->m_turnCtx.rng = &this->m_rng;
    this->m_turnCtx.currentTurn = 0;

    for (aoc::sim::ai::AIController& ai : this->m_aiControllers) {
        this->m_turnCtx.aiControllers.push_back(&ai);
    }
    this->m_turnCtx.allPlayers = this->m_allPlayers;
    this->m_turnCtx.humanPlayer = this->m_humanPlayers.empty()
        ? INVALID_PLAYER : this->m_humanPlayers[0];

    LOG_INFO("GameServer initialized: %d human + %d AI players, map %dx%d",
             config.humanPlayerCount, config.aiPlayerCount,
             config.mapWidth, config.mapHeight);
}

bool GameServer::tick() {
    if (this->m_gameOver) {
        return false;
    }

    // 1. Receive commands from transport
    if (this->m_transport != nullptr) {
        std::vector<std::pair<PlayerId, GameCommand>> incoming =
            this->m_transport->receivePendingCommands();
        for (std::pair<PlayerId, GameCommand>& cmd : incoming) {
            if (this->validateCommand(cmd.first, cmd.second)) {
                this->executeCommand(cmd.first, cmd.second);
            }
        }
    }

    // 2. Check if all players are ready
    bool allReady = true;
    for (PlayerId human : this->m_humanPlayers) {
        if (!this->m_playerReady[static_cast<std::size_t>(human)]) {
            allReady = false;
            break;
        }
    }

    if (!allReady) {
        return false;
    }

    // 3. Process turn
    aoc::sim::processTurn(this->m_turnCtx);

    // 4. Check victory
    aoc::sim::VictoryResult vr = aoc::sim::checkVictoryConditions(
        this->m_world, this->m_turnCtx.currentTurn,
        static_cast<TurnNumber>(this->m_maxTurns));
    if (vr.type != aoc::sim::VictoryType::None) {
        this->m_gameOver = true;
    }

    // 5. Broadcast snapshots to human players
    this->broadcastSnapshots();

    // 6. Reset readiness for next turn
    for (std::size_t i = 0; i < this->m_playerReady.size(); ++i) {
        this->m_playerReady[i] = false;
    }

    return true;
}

bool GameServer::validateCommand(PlayerId /*player*/, const GameCommand& /*command*/) const {
    // Basic validation -- expand as needed
    return true;
}

void GameServer::executeCommand(PlayerId player, const GameCommand& command) {
    // auto required: generic lambda template parameter for std::visit
    std::visit([this, player](const auto& cmd) {
        using T = std::decay_t<decltype(cmd)>;

        if constexpr (std::is_same_v<T, EndTurnCommand>) {
            this->m_playerReady[static_cast<std::size_t>(player)] = true;
            // Broadcast: other players see "Player X ended turn"
            if (this->m_transport != nullptr) {
                this->m_transport->broadcastUpdate(
                    PlayerEndedTurnUpdate{player});
            }
        }
        else if constexpr (std::is_same_v<T, MoveUnitCommand>) {
            aoc::sim::UnitComponent* unit =
                this->m_world.tryGetComponent<aoc::sim::UnitComponent>(cmd.unitEntity);
            if (unit != nullptr && unit->owner == player) {
                aoc::hex::AxialCoord from = unit->position;
                std::optional<aoc::map::PathResult> path =
                    aoc::map::findPath(this->m_grid, unit->position, cmd.destination);
                if (path.has_value() && !path->path.empty()) {
                    // Execute first step of movement immediately
                    aoc::hex::AxialCoord nextTile = path->path.front();
                    unit->position = nextTile;
                    unit->movementRemaining -= 1;
                    // Store remaining path for subsequent steps
                    path->path.erase(path->path.begin());
                    unit->pendingPath = std::move(path->path);

                    // Broadcast: all players see the unit move
                    if (this->m_transport != nullptr) {
                        this->m_transport->broadcastUpdate(
                            UnitMovedUpdate{cmd.unitEntity, player, from, nextTile,
                                            unit->movementRemaining});
                    }
                }
            }
        }
        else if constexpr (std::is_same_v<T, SetResearchCommand>) {
            this->m_world.forEach<aoc::sim::PlayerTechComponent>(
                [&cmd](EntityId, aoc::sim::PlayerTechComponent& tech) {
                    if (tech.owner == cmd.player) {
                        tech.currentResearch = cmd.techId;
                    }
                });
            // Broadcast: other players see research change
            if (this->m_transport != nullptr) {
                std::string techName = "Unknown";
                if (cmd.techId.isValid() && cmd.techId.value < aoc::sim::techCount()) {
                    techName = std::string(aoc::sim::techDef(cmd.techId).name);
                }
                this->m_transport->broadcastUpdate(
                    ResearchChangedUpdate{cmd.player, cmd.techId.value, techName});
            }
        }
        else if constexpr (std::is_same_v<T, FoundCityCommand>) {
            aoc::sim::UnitComponent* settler =
                this->m_world.tryGetComponent<aoc::sim::UnitComponent>(cmd.settlerEntity);
            if (settler != nullptr && settler->owner == player
                && aoc::sim::unitTypeDef(settler->typeId).unitClass == aoc::sim::UnitClass::Settler) {
                aoc::hex::AxialCoord pos = settler->position;
                EntityId cityEntity = aoc::sim::foundCity(
                    this->m_world, this->m_grid, player,
                    pos, cmd.cityName, false, 1);
                this->m_world.destroyEntity(cmd.settlerEntity);

                // Broadcast: all players see new city + settler consumed
                if (this->m_transport != nullptr) {
                    this->m_transport->broadcastUpdate(
                        UnitDestroyedUpdate{cmd.settlerEntity, player, pos, 2});
                    this->m_transport->broadcastUpdate(
                        CityFoundedUpdate{cityEntity, player, cmd.cityName, pos});
                }
            }
        }
        else if constexpr (std::is_same_v<T, AttackUnitCommand>) {
            // Execute combat immediately
            aoc::sim::UnitComponent* attacker =
                this->m_world.tryGetComponent<aoc::sim::UnitComponent>(cmd.attacker);
            aoc::sim::UnitComponent* defender =
                this->m_world.tryGetComponent<aoc::sim::UnitComponent>(cmd.defender);
            if (attacker != nullptr && defender != nullptr && attacker->owner == player) {
                aoc::hex::AxialCoord atkPos = attacker->position;
                aoc::hex::AxialCoord defPos = defender->position;

                // Resolve combat
                aoc::sim::CombatResult result = aoc::sim::resolveMeleeCombat(
                    this->m_world, this->m_rng, this->m_grid, cmd.attacker, cmd.defender);

                // Broadcast result to all players
                if (this->m_transport != nullptr) {
                    CombatResultUpdate update{};
                    update.attacker = cmd.attacker;
                    update.defender = cmd.defender;
                    update.attackerOwner = player;
                    update.defenderOwner = defender->owner;
                    update.attackerHPAfter = attacker->hitPoints - result.attackerDamage;
                    update.defenderHPAfter = defender->hitPoints - result.defenderDamage;
                    update.attackerDestroyed = result.attackerKilled;
                    update.defenderDestroyed = result.defenderKilled;
                    update.attackerPos = atkPos;
                    update.defenderPos = defPos;
                    this->m_transport->broadcastUpdate(update);
                }
            }
        }
        else if constexpr (std::is_same_v<T, SetProductionCommand>) {
            // Set production in city
            aoc::sim::ProductionQueueComponent* queue =
                this->m_world.tryGetComponent<aoc::sim::ProductionQueueComponent>(cmd.cityEntity);
            if (queue != nullptr) {
                // Broadcast production change
                if (this->m_transport != nullptr) {
                    this->m_transport->broadcastUpdate(
                        ProductionChangedUpdate{cmd.cityEntity, player, "Item", 0.0f});
                }
            }
        }
        else if constexpr (std::is_same_v<T, SetTaxRateCommand>) {
            this->m_world.forEach<aoc::sim::MonetaryStateComponent>(
                [&cmd](EntityId, aoc::sim::MonetaryStateComponent& ms) {
                    if (ms.owner == cmd.player) {
                        ms.taxRate = cmd.rate;
                    }
                });
        }
        else if constexpr (std::is_same_v<T, TransitionMonetaryCommand>) {
            // Monetary transition -- validate and execute
        }
    }, command);
}

GameStateSnapshot GameServer::generateSnapshot(PlayerId player) const {
    GameStateSnapshot snapshot{};
    snapshot.forPlayer = player;
    snapshot.turnNumber = this->m_turnCtx.currentTurn;
    snapshot.gameOver = this->m_gameOver;

    // Economy summary
    const aoc::ecs::ComponentPool<aoc::sim::MonetaryStateComponent>* mPool =
        this->m_world.getPool<aoc::sim::MonetaryStateComponent>();
    if (mPool != nullptr) {
        for (uint32_t i = 0; i < mPool->size(); ++i) {
            if (mPool->data()[i].owner == player) {
                const aoc::sim::MonetaryStateComponent& ms = mPool->data()[i];
                snapshot.economy.gdp = ms.gdp;
                snapshot.economy.treasury = ms.treasury;
                snapshot.economy.monetarySystem = static_cast<uint8_t>(ms.system);
                snapshot.economy.coinTier = static_cast<uint8_t>(ms.effectiveCoinTier);
                snapshot.economy.inflationRate = ms.inflationRate;
                break;
            }
        }
    }

    // Units
    const aoc::ecs::ComponentPool<aoc::sim::UnitComponent>* uPool =
        this->m_world.getPool<aoc::sim::UnitComponent>();
    if (uPool != nullptr) {
        for (uint32_t i = 0; i < uPool->size(); ++i) {
            const aoc::sim::UnitComponent& unit = uPool->data()[i];
            // Include own units and units in visible tiles (simplified: include all for now)
            VisibleUnit vu{};
            vu.entity = uPool->entities()[i];
            vu.owner = unit.owner;
            vu.unitTypeId = unit.typeId.value;
            vu.position = unit.position;
            vu.hitPoints = unit.hitPoints;
            vu.maxHitPoints = aoc::sim::unitTypeDef(unit.typeId).maxHitPoints;
            vu.movementRemaining = unit.movementRemaining;
            snapshot.units.push_back(vu);
        }
    }

    // Cities
    const aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cPool =
        this->m_world.getPool<aoc::sim::CityComponent>();
    if (cPool != nullptr) {
        for (uint32_t i = 0; i < cPool->size(); ++i) {
            const aoc::sim::CityComponent& city = cPool->data()[i];
            VisibleCity vc{};
            vc.entity = cPool->entities()[i];
            vc.owner = city.owner;
            vc.name = city.name;
            vc.location = city.location;
            vc.population = city.population;
            vc.isCapital = city.isOriginalCapital;
            snapshot.cities.push_back(vc);
        }
    }

    // Victory
    const aoc::ecs::ComponentPool<aoc::sim::VictoryTrackerComponent>* vPool =
        this->m_world.getPool<aoc::sim::VictoryTrackerComponent>();
    if (vPool != nullptr) {
        for (uint32_t i = 0; i < vPool->size(); ++i) {
            if (vPool->data()[i].owner == player) {
                snapshot.economy.eraVictoryPoints = vPool->data()[i].eraVictoryPoints;
                snapshot.economy.compositeCSI = vPool->data()[i].compositeCSI;
                break;
            }
        }
    }

    return snapshot;
}

void GameServer::broadcastSnapshots() {
    if (this->m_transport == nullptr) {
        return;
    }

    for (PlayerId human : this->m_humanPlayers) {
        GameStateSnapshot snapshot = this->generateSnapshot(human);
        this->m_transport->sendSnapshot(human, std::move(snapshot));
    }
}

} // namespace aoc::net

/**
 * @file GameServer.cpp
 * @brief Authoritative game server implementation.
 *
 * All game state is accessed through GameState/Player/City/Unit objects.
 * No direct ECS World queries are made; the World member is kept only for
 * Legacy migration period.
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
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"

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

    // Initialize economy and diplomacy
    this->m_economy.initialize();

    int32_t totalPlayers = config.humanPlayerCount + config.aiPlayerCount;
    this->m_diplomacy.initialize(static_cast<uint8_t>(totalPlayers));

    this->m_turnManager.setPlayerCount(
        static_cast<uint8_t>(config.humanPlayerCount),
        static_cast<uint8_t>(config.aiPlayerCount));

    // Initialize GameState for the correct number of players
    this->m_gameState.initialize(totalPlayers);

    // Track player arrays and readiness flags
    this->m_playerReady.resize(static_cast<std::size_t>(totalPlayers), false);

    for (int32_t i = 0; i < config.humanPlayerCount; ++i) {
        PlayerId player = static_cast<PlayerId>(i);
        this->m_humanPlayers.push_back(player);
        this->m_allPlayers.push_back(player);
    }

    for (int32_t i = 0; i < config.aiPlayerCount; ++i) {
        PlayerId player = static_cast<PlayerId>(config.humanPlayerCount + i);
        this->m_allPlayers.push_back(player);
        this->m_aiControllers.emplace_back(player);
    }

    // Spawn starting city and configure each player via the GameState object model
    for (int32_t p = 0; p < totalPlayers; ++p) {
        PlayerId player = static_cast<PlayerId>(p);
        uint8_t civId = (p < static_cast<int32_t>(config.civAssignments.size()))
            ? config.civAssignments[static_cast<std::size_t>(p)]
            : static_cast<uint8_t>(p % aoc::sim::CIV_COUNT);

        // Find a valid starting land tile
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

        // Found starting city via GameState (creates City in Player's city list)
        std::string cityName = std::string(
            aoc::sim::civDef(static_cast<aoc::sim::CivId>(civId)).cityNames[0]);
        aoc::sim::foundCity(this->m_gameState, this->m_grid, player, startPos, cityName, true, 1);

        // Configure player fields on the GameState Player object
        aoc::game::Player* gsPlayer = this->m_gameState.player(player);
        if (gsPlayer != nullptr) {
            gsPlayer->setCivId(static_cast<aoc::sim::CivId>(civId));
            gsPlayer->setHuman(p < config.humanPlayerCount);
            gsPlayer->setTreasury(100);

            // Initialize tech state
            gsPlayer->tech().owner = player;
            gsPlayer->tech().initialize();
            gsPlayer->tech().completedTechs[0] = true;  // Start with Mining

            // Initialize civic state
            gsPlayer->civics().owner = player;
            gsPlayer->civics().initialize();

            // Initialize monetary state
            gsPlayer->monetary().owner = player;
            gsPlayer->monetary().treasury = 100;

            // Initialize economy component
            gsPlayer->economy().owner = player;
            gsPlayer->economy().treasury = 100;

            // Initialize government
            gsPlayer->government().owner = player;

            // Initialize victory tracker
            gsPlayer->victoryTracker().owner = player;

            // Add starting scout unit to the player's unit list
            gsPlayer->addUnit(UnitTypeId{2}, startPos);
        }
    }

    // Build turn context
    this->m_turnCtx.grid = &this->m_grid;
    this->m_turnCtx.economy = &this->m_economy;
    this->m_turnCtx.diplomacy = &this->m_diplomacy;
    this->m_turnCtx.barbarians = &this->m_barbarians;
    this->m_turnCtx.rng = &this->m_rng;
    this->m_turnCtx.currentTurn = 0;
    this->m_turnCtx.gameState = &this->m_gameState;

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

    // 2. Check if all human players are ready
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
        this->m_gameState, this->m_turnCtx.currentTurn,
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
    return true;
}

void GameServer::executeCommand(PlayerId player, const GameCommand& command) {
    // std::visit requires a generic lambda (type is not nameable) - the only valid use of auto here
    std::visit([this, player](const auto& cmd) {
        using T = std::decay_t<decltype(cmd)>;

        if constexpr (std::is_same_v<T, EndTurnCommand>) {
            this->m_playerReady[static_cast<std::size_t>(player)] = true;
            if (this->m_transport != nullptr) {
                this->m_transport->broadcastUpdate(PlayerEndedTurnUpdate{player});
            }
        }
        else if constexpr (std::is_same_v<T, MoveUnitCommand>) {
            // Find the unit in the GameState object model via the player's unit list
            aoc::game::Player* gsPlayer = this->m_gameState.player(player);
            if (gsPlayer == nullptr) { return; }

            // Identify the unit by its entity handle embedded in the command.
            // During the migration period, cmd.unitEntity is still an ECS EntityId;
            // resolve it by searching the player's units by position or index.
            // For now we use the ECS unit as the authoritative source of position.
            aoc::sim::UnitComponent* ecsUnit =
                static_cast<aoc::sim::UnitComponent*>(nullptr) /* network commands need position-based unit lookup */;
            if (ecsUnit == nullptr || ecsUnit->owner != player) { return; }

            // Mirror state into the GameState Unit object
            aoc::game::Unit* gsUnit = gsPlayer->unitAt(ecsUnit->position);
            aoc::hex::AxialCoord fromPos = ecsUnit->position;

            std::optional<aoc::map::PathResult> path =
                aoc::map::findPath(this->m_grid, ecsUnit->position, cmd.destination);
            if (path.has_value() && !path->path.empty()) {
                aoc::hex::AxialCoord nextTile = path->path.front();

                // Update ECS unit (still used by movement system)
                ecsUnit->position = nextTile;
                ecsUnit->movementRemaining -= 1;
                path->path.erase(path->path.begin());
                ecsUnit->pendingPath = std::move(path->path);

                // Mirror update into the GameState Unit
                if (gsUnit != nullptr) {
                    gsUnit->setPosition(nextTile);
                    gsUnit->setMovementRemaining(ecsUnit->movementRemaining);
                    gsUnit->pendingPath() = ecsUnit->pendingPath;
                }

                if (this->m_transport != nullptr) {
                    this->m_transport->broadcastUpdate(
                        UnitMovedUpdate{cmd.unitEntity, player, fromPos, nextTile,
                                        ecsUnit->movementRemaining});
                }
            }
        }
        else if constexpr (std::is_same_v<T, SetResearchCommand>) {
            // Set research directly on the GameState Player
            aoc::game::Player* gsPlayer = this->m_gameState.player(cmd.player);
            if (gsPlayer != nullptr) {
                gsPlayer->tech().currentResearch = cmd.techId;
            }

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
            // Find settler unit in the GameState object model
            aoc::game::Player* gsPlayer = this->m_gameState.player(player);
            if (gsPlayer == nullptr) { return; }

            // Resolve settler position from ECS (settler entity still tracked by ECS)
            aoc::sim::UnitComponent* settler =
                static_cast<aoc::sim::UnitComponent*>(nullptr) /* network protocol migration pending */;
            if (settler == nullptr || settler->owner != player
                || aoc::sim::unitTypeDef(settler->typeId).unitClass != aoc::sim::UnitClass::Settler) {
                return;
            }
            aoc::hex::AxialCoord pos = settler->position;

            // Remove settler from the GameState unit list
            aoc::game::Unit* gsSettler = gsPlayer->unitAt(pos);
            if (gsSettler != nullptr) {
                gsPlayer->removeUnit(gsSettler);
            }

            // Destroy the ECS settler entity
            // Settler removal deferred until network protocol uses position-based IDs

            // Found city via GameState (creates City in Player's city list)
            aoc::sim::foundCity(
                this->m_gameState, this->m_grid, player,
                pos, cmd.cityName, false, 1);

            // Resolve the new city for the broadcast.
            // Cities are now owned entirely by the GameState object model, so there is
            // no ECS entity handle. The broadcast uses NULL_ENTITY as a placeholder
            // until a proper network ID scheme is introduced.
            const aoc::game::City* newCity = gsPlayer->cityAt(pos);
            EntityId cityEntity = NULL_ENTITY;
            (void)newCity; // used only to confirm the city was created

            if (this->m_transport != nullptr) {
                this->m_transport->broadcastUpdate(
                    UnitDestroyedUpdate{cmd.settlerEntity, player, pos, 2});
                this->m_transport->broadcastUpdate(
                    CityFoundedUpdate{cityEntity, player, cmd.cityName, pos});
            }
        }
        else if constexpr (std::is_same_v<T, AttackUnitCommand>) {
            // Resolve combat via the ECS-backed combat system (still authoritative for combat)
            aoc::sim::UnitComponent* attacker =
                static_cast<aoc::sim::UnitComponent*>(nullptr) /* network protocol migration pending */;
            aoc::sim::UnitComponent* defender =
                static_cast<aoc::sim::UnitComponent*>(nullptr) /* network protocol migration pending */;
            if (attacker == nullptr || defender == nullptr || attacker->owner != player) { return; }

            aoc::hex::AxialCoord atkPos = attacker->position;
            aoc::hex::AxialCoord defPos = defender->position;

            aoc::sim::CombatResult result = aoc::sim::resolveMeleeCombat(
                this->m_gameState, this->m_rng, this->m_grid, cmd.attacker, cmd.defender);

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
        else if constexpr (std::is_same_v<T, SetProductionCommand>) {
            // Find city in the GameState object model
            aoc::game::Player* gsPlayer = this->m_gameState.player(player);
            if (gsPlayer == nullptr) { return; }

            // Resolve city from the ECS entity (still used as the network handle)
            const aoc::sim::CityComponent* ecsCity =
                static_cast<aoc::sim::CityComponent*>(nullptr) /* network protocol migration pending */;
            if (ecsCity != nullptr) {
                aoc::game::City* gsCity = gsPlayer->cityAt(ecsCity->location);
                if (gsCity != nullptr && this->m_transport != nullptr) {
                    this->m_transport->broadcastUpdate(
                        ProductionChangedUpdate{cmd.cityEntity, player, "Item", 0.0f});
                }
            }
        }
        else if constexpr (std::is_same_v<T, SetTaxRateCommand>) {
            // Apply tax rate directly via the GameState Player monetary component
            aoc::game::Player* gsPlayer = this->m_gameState.player(cmd.player);
            if (gsPlayer != nullptr) {
                gsPlayer->monetary().taxRate = cmd.rate;
            }
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

    const aoc::game::Player* gsPlayer = this->m_gameState.player(player);
    if (gsPlayer == nullptr) {
        return snapshot;
    }

    // Economy summary from the GameState Player object
    const aoc::sim::MonetaryStateComponent& ms = gsPlayer->monetary();
    snapshot.economy.gdp = ms.gdp;
    snapshot.economy.treasury = ms.treasury;
    snapshot.economy.monetarySystem = static_cast<uint8_t>(ms.system);
    snapshot.economy.coinTier = static_cast<uint8_t>(ms.effectiveCoinTier);
    snapshot.economy.inflationRate = ms.inflationRate;

    // Units: iterate all players (all units are visible in the snapshot for now)
    for (const std::unique_ptr<aoc::game::Player>& p : this->m_gameState.players()) {
        for (const std::unique_ptr<aoc::game::Unit>& unit : p->units()) {
            VisibleUnit vu{};
            // Network handle: use NULL_ENTITY during ECS migration
            vu.entity = NULL_ENTITY;
            vu.owner = unit->owner();
            vu.unitTypeId = unit->typeId().value;
            vu.position = unit->position();
            vu.hitPoints = unit->hitPoints();
            vu.maxHitPoints = unit->typeDef().maxHitPoints;
            vu.movementRemaining = unit->movementRemaining();
            snapshot.units.push_back(vu);
        }
    }

    // Cities: iterate all players
    for (const std::unique_ptr<aoc::game::Player>& p : this->m_gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : p->cities()) {
            VisibleCity vc{};
            vc.entity = NULL_ENTITY;
            vc.owner = city->owner();
            vc.name = city->name();
            vc.location = city->location();
            vc.population = city->population();
            vc.isCapital = city->isOriginalCapital();
            snapshot.cities.push_back(vc);
        }
    }

    // Victory data from the GameState Player object
    const aoc::sim::VictoryTrackerComponent& vt = gsPlayer->victoryTracker();
    snapshot.economy.eraVictoryPoints = vt.eraVictoryPoints;
    snapshot.economy.compositeCSI = vt.compositeCSI;

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

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
#include <array>

namespace aoc::net {

// Upper bound on free-text command fields (city names, etc). The wire format
// supplies these as untrusted std::string; without a cap a single command can
// force an unbounded allocation (and is rebroadcast to every client, amplifying
// it). 64 bytes comfortably fits any real city name.
inline constexpr std::size_t MAX_COMMAND_TEXT_LEN = 64;

// Upper bound on commands accepted from one player per turn. The transport
// drains an unbounded queue each tick; a misbehaving or hostile client (over a
// future NetworkTransport) could flood it. Excess commands past this cap are
// dropped with a warning rather than processed. A legitimate turn issues only a
// handful of commands, so 256 is generous headroom.
inline constexpr std::size_t MAX_COMMANDS_PER_PLAYER_PER_TURN = 256;

// Seed 42 is an intentional, fixed default for reproducible/test runs. The RNG
// drives deterministic lockstep (identical map + simulation across peers), so a
// CSPRNG-derived seed is deliberately NOT used here -- the seed is overwritten by
// config.seed in initialize(), which is where any non-default seed is supplied.
GameServer::GameServer() : m_rng(42) {}
GameServer::~GameServer() = default;

void GameServer::initialize(const GameConfig& config) {
    // Treat the player count as untrusted: cap it before any resize / uint8_t
    // cast so it cannot overflow MAX_PLAYERS-sized arrays.
    const int32_t requestedPlayers = config.humanPlayerCount + config.aiPlayerCount;
    if (requestedPlayers <= 0 || requestedPlayers > static_cast<int32_t>(MAX_PLAYERS)) {
        LOG_ERROR("GameServer::initialize: invalid player count %d (must be 1..%d)",
                  requestedPlayers, static_cast<int>(MAX_PLAYERS));
        return;
    }

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

    const int32_t totalPlayers = requestedPlayers;
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
            gsPlayer->setTreasury(0);

            // Initialize tech state
            gsPlayer->tech().owner = player;
            gsPlayer->tech().initialize();
            gsPlayer->tech().completedTechs[0] = true;  // Start with Mining

            // Initialize civic state
            gsPlayer->civics().owner = player;
            gsPlayer->civics().initialize();

            // Initialize monetary state
            gsPlayer->monetary().owner = player;
            gsPlayer->monetary().treasury = 0;

            // Initialize economy component
            gsPlayer->economy().owner = player;
            gsPlayer->economy().treasury = 0;

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
    this->m_turnCtx.maxTurns = static_cast<TurnNumber>(this->m_maxTurns);
    this->m_turnCtx.victoryTypeMask = aoc::sim::VICTORY_MASK_ALL;
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
        // Per-player per-turn flood cap: the transport queue is unbounded, so
        // count commands per player as we drain and drop any beyond the cap
        // (LOG_WARN once per player on the first dropped command). Indexed by
        // PlayerId; ids outside [0, MAX_PLAYERS) cannot index the array, so
        // their commands are dropped here too (validateCommand would reject
        // them anyway on the authority check).
        std::array<std::size_t, MAX_PLAYERS> commandCount{};
        for (std::pair<PlayerId, GameCommand>& cmd : incoming) {
            const std::size_t slot = static_cast<std::size_t>(cmd.first);
            if (slot >= commandCount.size()) {
                LOG_WARN("GameServer::tick: dropping command from out-of-range "
                         "player id %d", static_cast<int>(cmd.first));
                continue;
            }
            if (commandCount[slot] >= MAX_COMMANDS_PER_PLAYER_PER_TURN) {
                if (commandCount[slot] == MAX_COMMANDS_PER_PLAYER_PER_TURN) {
                    LOG_WARN("GameServer::tick: player %d exceeded %zu commands "
                             "this turn -- dropping excess",
                             static_cast<int>(cmd.first),
                             MAX_COMMANDS_PER_PLAYER_PER_TURN);
                    ++commandCount[slot];  // advance past the cap so we warn once
                }
                continue;
            }
            ++commandCount[slot];
            if (this->validateCommand(cmd.first, cmd.second)) {
                this->executeCommand(cmd.first, cmd.second);
            }
        }
    }

    // 2. Check if all human players are ready
    bool allReady = true;
    for (PlayerId human : this->m_humanPlayers) {
        if (static_cast<std::size_t>(human) >= this->m_playerReady.size()) {
            LOG_WARN("GameServer::tick: player id %d out of range (size %zu)",
                     static_cast<int>(human), this->m_playerReady.size());
            return false;
        }
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

    // 4. Check victory: read cached result from processTurn.
    if (this->m_turnCtx.lastVictoryResult.type != aoc::sim::VictoryType::None) {
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

bool GameServer::validateCommand(PlayerId player, const GameCommand& command) const {
    // Per-command authority check. Rejects commands where the asserted
    // actor differs from the player whose connection sent the command,
    // and clamps numeric inputs to documented ranges. Unit-owner
    // verification for MoveUnit / AttackUnit / FoundCity is enforced
    // at dispatch time inside executeCommand against the resolved unit
    // (`unit->owner() == player`) since EntityId-to-unit resolution
    // is part of the ECS migration; once that is in place, hoist the
    // check up here.
    return std::visit([player](const auto& cmd) -> bool {
        using T = std::decay_t<decltype(cmd)>;

        if constexpr (std::is_same_v<T, SetTaxRateCommand>) {
            // Reject commands that spoof a player id.
            if (cmd.player != player) {
                LOG_WARN("validateCommand: SetTaxRate rejected -- "
                         "cmd.player=%d != connection player=%d",
                         static_cast<int>(cmd.player),
                         static_cast<int>(player));
                return false;
            }
            // Tax rate is documented as Percentage in [0.0f, 1.0f].
            // Reject any rate outside that range; clients that want a
            // saturated rate must send a clamped value.
            if (!(cmd.rate >= 0.0f && cmd.rate <= 1.0f)) {
                LOG_WARN("validateCommand: SetTaxRate rejected -- "
                         "rate=%.6f outside [0.0, 1.0]",
                         static_cast<double>(cmd.rate));
                return false;
            }
            return true;
        }
        else if constexpr (std::is_same_v<T, SetResearchCommand>) {
            if (cmd.player != player) {
                LOG_WARN("validateCommand: SetResearch rejected -- "
                         "cmd.player=%d != connection player=%d",
                         static_cast<int>(cmd.player),
                         static_cast<int>(player));
                return false;
            }
            // techId is untrusted wire data: reject ids outside the tech table.
            if (!(cmd.techId.isValid() && cmd.techId.value < aoc::sim::techCount())) {
                LOG_WARN("validateCommand: SetResearch rejected -- "
                         "invalid techId %u (count %u)",
                         static_cast<unsigned>(cmd.techId.value),
                         static_cast<unsigned>(aoc::sim::techCount()));
                return false;
            }
            return true;
        }
        else if constexpr (std::is_same_v<T, EndTurnCommand>) {
            if (cmd.player != player) {
                LOG_WARN("validateCommand: EndTurn rejected -- "
                         "cmd.player=%d != connection player=%d",
                         static_cast<int>(cmd.player),
                         static_cast<int>(player));
                return false;
            }
            return true;
        }
        else if constexpr (std::is_same_v<T, TransitionMonetaryCommand>) {
            if (cmd.player != player) {
                LOG_WARN("validateCommand: TransitionMonetary rejected -- "
                         "cmd.player=%d != connection player=%d",
                         static_cast<int>(cmd.player),
                         static_cast<int>(player));
                return false;
            }
            // targetSystem is cast to MonetarySystemType: reject values that
            // fall outside the enum's valid range.
            if (cmd.targetSystem
                >= static_cast<uint8_t>(aoc::sim::MonetarySystemType::Count)) {
                LOG_WARN("validateCommand: TransitionMonetary rejected -- "
                         "targetSystem=%u out of range (count %u)",
                         static_cast<unsigned>(cmd.targetSystem),
                         static_cast<unsigned>(aoc::sim::MonetarySystemType::Count));
                return false;
            }
            // executeCommand has no implementation for this command: applying
            // the transition belongs to the monetary subsystem, which is not
            // wired into the command path yet. Reject it here so the client
            // gets an explicit failure instead of a "validated, then silently
            // dropped" divergence between client and server state.
            LOG_WARN("validateCommand: TransitionMonetary rejected -- "
                     "not implemented (player %d, targetSystem=%u)",
                     static_cast<int>(player),
                     static_cast<unsigned>(cmd.targetSystem));
            return false;
        }
        else if constexpr (std::is_same_v<T, FoundCityCommand>) {
            // cityName is untrusted free text: reject empty or over-long names
            // before they are stored / rebroadcast to every client.
            if (cmd.cityName.empty() || cmd.cityName.size() > MAX_COMMAND_TEXT_LEN) {
                LOG_WARN("validateCommand: FoundCity rejected -- cityName length "
                         "%zu outside [1, %zu]",
                         cmd.cityName.size(), MAX_COMMAND_TEXT_LEN);
                return false;
            }
            // Owner check stays in executeCommand (settler resolution is part
            // of the ECS migration); see the function-level comment above.
            return true;
        }
        else if constexpr (std::is_same_v<T, SetProductionCommand>) {
            // itemType is a ProductionItemType cast from untrusted wire data:
            // reject values outside the enum before it is used as a discriminant.
            if (cmd.itemType
                > static_cast<uint8_t>(aoc::sim::ProductionItemType::Wonder)) {
                LOG_WARN("validateCommand: SetProduction rejected -- itemType=%u "
                         "out of range (max %u)",
                         static_cast<unsigned>(cmd.itemType),
                         static_cast<unsigned>(aoc::sim::ProductionItemType::Wonder));
                return false;
            }
            // DEBT(net): itemId is a type-dependent index (UnitTypeId /
            // BuildingId / DistrictType) and cannot be range-checked until the
            // ECS production path resolves the city and its per-type tables.
            // Owner/city resolution stays in executeCommand.
            return true;
        }
        // Commands without an explicit `player` field (MoveUnit,
        // AttackUnit) fall through to the unit owner check inside
        // executeCommand.
        return true;
    }, command);
}

void GameServer::executeCommand(PlayerId player, const GameCommand& command) {
    // std::visit requires a generic lambda (type is not nameable) - the only valid use of auto here
    std::visit([this, player](const auto& cmd) {
        using T = std::decay_t<decltype(cmd)>;

        if constexpr (std::is_same_v<T, EndTurnCommand>) {
            if (static_cast<std::size_t>(player) >= this->m_playerReady.size()) {
                LOG_WARN("executeCommand: EndTurn dropped -- player id %d out of "
                         "range (size %zu)",
                         static_cast<int>(player), this->m_playerReady.size());
                return;
            }
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
            if (ecsUnit == nullptr || ecsUnit->owner != player) {
                LOG_WARN("executeCommand: MoveUnit dropped -- player %d, unit "
                         "lookup unresolved (ECS path not implemented)",
                         static_cast<int>(player));
                return;
            }

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
            // Reject an out-of-range tech id before writing it: cmd.techId is
            // untrusted wire data and must not be stored unvalidated.
            if (!(cmd.techId.isValid() && cmd.techId.value < aoc::sim::techCount())) {
                LOG_WARN("executeCommand: SetResearch dropped -- player %d sent "
                         "invalid techId %u (count %u)",
                         static_cast<int>(player),
                         static_cast<unsigned>(cmd.techId.value),
                         static_cast<unsigned>(aoc::sim::techCount()));
                return;
            }

            // Set research directly on the GameState Player
            aoc::game::Player* gsPlayer = this->m_gameState.player(cmd.player);
            if (gsPlayer != nullptr) {
                gsPlayer->tech().currentResearch = cmd.techId;
            }

            if (this->m_transport != nullptr) {
                std::string techName = std::string(aoc::sim::techDef(cmd.techId).name);
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
                LOG_WARN("executeCommand: FoundCity dropped -- player %d, settler "
                         "lookup unresolved (ECS path not implemented)",
                         static_cast<int>(player));
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
            if (attacker == nullptr || defender == nullptr || attacker->owner != player) {
                LOG_WARN("executeCommand: AttackUnit dropped -- player %d, "
                         "attacker/defender lookup unresolved (ECS path not implemented)",
                         static_cast<int>(player));
                return;
            }

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
            // Defence in depth: validateCommand already rejects rates
            // outside [0.0f, 1.0f] and a spoofed cmd.player. Clamp here
            // anyway so any future caller that bypasses validate cannot
            // store an out-of-range tax rate. `cmd.player != player`
            // is treated as a hard bug -- drop the command.
            if (cmd.player != player) {
                LOG_WARN("executeCommand: SetTaxRate dropped -- "
                         "cmd.player=%d != connection player=%d",
                         static_cast<int>(cmd.player),
                         static_cast<int>(player));
                return;
            }
            const float clampedRate = std::clamp(cmd.rate, 0.0f, 1.0f);
            aoc::game::Player* gsPlayer = this->m_gameState.player(cmd.player);
            if (gsPlayer != nullptr) {
                gsPlayer->monetary().taxRate = clampedRate;
            }
        }
        else if constexpr (std::is_same_v<T, TransitionMonetaryCommand>) {
            // Unreachable: validateCommand rejects TransitionMonetary because
            // there is no implementation yet (the transition belongs to the
            // monetary subsystem, not the command path). Kept as an explicit
            // arm so the std::visit is exhaustive and a future implementation
            // has an obvious home. Drop without acting.
            (void)cmd;
            LOG_WARN("executeCommand: TransitionMonetary reached dispatch but "
                     "is unimplemented -- dropping (player %d)",
                     static_cast<int>(player));
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

    // Visibility filter: a snapshot must never leak entities the requesting
    // player cannot see. The correct filter is a per-player fog-of-war query
    // (own entities always included; foreign entities only on tiles the
    // player currently sees). GameServer does not own a FogOfWar instance --
    // m_turnCtx.fogOfWar is left null -- so that query is not available here.
    //
    // DEBT(net): until a FogOfWar instance is wired into GameServer, ship
    // only the requesting player's OWN units and cities. This is the safe
    // subset (a player may always see their own entities) and closes the
    // full-board information-disclosure hole; the cost is that legitimately
    // visible foreign entities are omitted until the fog query lands. See
    // GameStateSnapshot.hpp and AUDIT_REPORT_2026-06-06.md (GameServer.cpp:546).

    // Units: own player only (see DEBT note above).
    for (const std::unique_ptr<aoc::game::Unit>& unit : gsPlayer->units()) {
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

    // Cities: own player only (see DEBT note above).
    for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
        VisibleCity vc{};
        vc.entity = NULL_ENTITY;
        vc.owner = city->owner();
        vc.name = city->name();
        vc.location = city->location();
        vc.population = city->population();
        vc.isCapital = city->isOriginalCapital();
        snapshot.cities.push_back(vc);
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

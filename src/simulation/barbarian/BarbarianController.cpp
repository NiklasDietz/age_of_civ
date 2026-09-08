/**
 * @file BarbarianController.cpp
 * @brief Barbarian encampment spawning, unit spawning, and AI movement.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/barbarian/BarbarianController.hpp"
#include "aoc/simulation/barbarian/BarbarianClans.hpp"
#include "aoc/simulation/unit/Combat.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/event/VisibilityEvents.hpp"
#include "aoc/simulation/turn/TurnEventLog.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/simulation/city/CitySiege.hpp"
#include "aoc/core/Log.hpp"

#include <array>
#include <cassert>
#include <vector>

namespace aoc::sim {

// ============================================================================
// Constants
// ============================================================================

static constexpr int32_t ENCAMPMENT_SPAWN_INTERVAL  = 15;
static constexpr int32_t MAX_ENCAMPMENTS            = 3;
static constexpr int32_t MIN_DISTANCE_FROM_CITY     = 7;
static constexpr int32_t SPAWN_COOLDOWN_TURNS       = 3;
static constexpr int32_t MAX_NEARBY_BARBARIAN_UNITS = 3;
static constexpr int32_t AGGRO_RANGE                = 3;

// ============================================================================
// Helpers
// ============================================================================

/// Count barbarian units within a given radius of a position.
static int32_t countBarbarianUnitsNear(const aoc::game::GameState& gameState,
                                        const aoc::map::HexGrid& grid,
                                        hex::AxialCoord center,
                                        int32_t radius) {
    const aoc::game::Player* barbPlayer = gameState.player(BARBARIAN_PLAYER);
    if (barbPlayer == nullptr) {
        return 0;
    }

    int32_t count = 0;
    for (const std::unique_ptr<aoc::game::Unit>& unit : barbPlayer->units()) {
        if (grid.distance(unit->position(), center) <= radius) {
            ++count;
        }
    }
    return count;
}

/// Find the closest non-barbarian unit within a given range.
/// Returns a pointer to the unit, or nullptr if none found.
static aoc::game::Unit* findNearestTarget(const aoc::game::GameState& gameState,
                                           const aoc::map::HexGrid& grid,
                                           hex::AxialCoord position,
                                           int32_t range) {
    aoc::game::Unit* closest = nullptr;
    int32_t bestDist = range + 1;

    auto scan = [&](const aoc::game::Player* player) {
        if (player == nullptr || player->id() == BARBARIAN_PLAYER) {
            return;
        }
        for (const std::unique_ptr<aoc::game::Unit>& unit : player->units()) {
            int32_t dist = grid.distance(unit->position(), position);
            if (dist <= range && dist < bestDist) {
                bestDist = dist;
                closest  = unit.get();
            }
        }
    };

    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        scan(player.get());
    }
    // players() is the major seats only, by contract. City-states were
    // therefore invisible to raiders, who walked past them to reach a major
    // civ's units.
    for (const std::unique_ptr<aoc::game::Player>& cityState : gameState.cityStatePlayers()) {
        scan(cityState.get());
    }
    return closest;
}

/// The closest unit belonging to `onlyOwner` within `range`, or null.
/// Used when a clan has been hired to go after one particular civ.
static aoc::game::Unit* findNearestTargetOf(const aoc::game::GameState& gameState,
                                            const aoc::map::HexGrid& grid,
                                            hex::AxialCoord position, int32_t range,
                                            PlayerId onlyOwner) {
    const aoc::game::Player* owner = gameState.player(onlyOwner);
    if (owner == nullptr) {
        return nullptr;
    }
    aoc::game::Unit* closest = nullptr;
    int32_t bestDist         = range + 1;
    for (const std::unique_ptr<aoc::game::Unit>& unit : owner->units()) {
        const int32_t dist = grid.distance(unit->position(), position);
        if (dist <= range && dist < bestDist) {
            bestDist = dist;
            closest  = unit.get();
        }
    }
    return closest;
}

/// The closest non-barbarian city within `range`, or null.
///
/// Barbarians could not attack a city at all: the raid loop only ever looked
/// for units, and neither resolveAttackOnCity nor pressIntoCity was named
/// anywhere in this file. A camp beside an undefended town simply ignored it.
static aoc::game::City* findNearestCityTarget(const aoc::game::GameState& gameState,
                                              const aoc::map::HexGrid& grid,
                                              hex::AxialCoord position, int32_t range,
                                              int32_t& bestDistOut) {
    aoc::game::City* closest = nullptr;
    bestDistOut              = range + 1;

    auto scan = [&](const aoc::game::Player* player) {
        if (player == nullptr || player->id() == BARBARIAN_PLAYER) {
            return;
        }
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            if (city == nullptr) {
                continue;
            }
            const int32_t dist = grid.distance(city->location(), position);
            if (dist <= range && dist < bestDistOut) {
                bestDistOut = dist;
                closest     = city.get();
            }
        }
    };

    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        scan(player.get());
    }
    for (const std::unique_ptr<aoc::game::Player>& cityState : gameState.cityStatePlayers()) {
        scan(cityState.get());
    }
    return closest;
}

/// H5.7: highest era reached by any non-barbarian player.
static int32_t leadingPlayerEra(const aoc::game::GameState& gameState) {
    int32_t best = -1;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        if (player->id() == BARBARIAN_PLAYER) { continue; }
        const int32_t era = static_cast<int32_t>(player->era().currentEra.value);
        if (era > best) { best = era; }
    }
    return best;
}

/// Check if any city from any player is within the given distance of a tile.
static bool isTooCloseToCity(const aoc::game::GameState& gameState,
                              const aoc::map::HexGrid& grid,
                              hex::AxialCoord tile,
                              int32_t minDistance) {
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            if (grid.distance(city->location(), tile) < minDistance) {
                return true;
            }
        }
    }
    for (const std::unique_ptr<aoc::game::Player>& cityState : gameState.cityStatePlayers()) {
        for (const std::unique_ptr<aoc::game::City>& city : cityState->cities()) {
            if (grid.distance(city->location(), tile) < minDistance) {
                return true;
            }
        }
    }
    return false;
}

// ============================================================================
// BarbarianController
// ============================================================================

void BarbarianController::removeEncampment(std::size_t index) {
    if (index >= this->m_encampments.size()) { return; }
    if (index + 1 != this->m_encampments.size()) {
        this->m_encampments[index] = this->m_encampments.back();
    }
    this->m_encampments.pop_back();
}

/// H5.6: a camp is "destroyed" when a non-barbarian unit stands on its tile.
/// Mirrors Civ-style clearance (step onto camp after killing defender).
/// Returns the overrunning unit's owner, or INVALID_PLAYER when the camp stands.
static PlayerId campOverrunBy(const aoc::game::GameState& gameState, hex::AxialCoord tile) {
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::Unit>& unit : player->units()) {
            if (unit->position() == tile) { return player->id(); }
        }
    }
    return INVALID_PLAYER;
}

void BarbarianController::executeTurn(aoc::game::GameState& gameState,
                                       aoc::map::HexGrid& grid,
                                       aoc::Random& rng,
                                       TurnEventLog* eventLog) {
    ++this->m_turnCounter;

    // Bribes and hires run out. Nothing ticked these before, because nothing
    // ever set them: the clan list was empty for the whole game.
    for (BarbarianClanComponent& clan : gameState.barbarianClans()) {
        if (clan.isBribed && --clan.bribeTurnsLeft <= 0) {
            clan.isBribed       = false;
            clan.bribeTurnsLeft = 0;
        }
        if (clan.hiredBy != INVALID_PLAYER && --clan.hireTurnsLeft <= 0) {
            clan.hiredBy     = INVALID_PLAYER;
            clan.hiredTarget = INVALID_PLAYER;
            clan.hireTurnsLeft = 0;
        }
    }

    // Restore movement points for all barbarian-owned units.
    refreshMovement(gameState, BARBARIAN_PLAYER);

    // H5.6: purge destroyed encampments before spawning so MAX_ENCAMPMENTS
    // reflects live camps only. Iterate backwards because removeEncampment
    // swap-pops. Clearing a camp pays the Civ-style reward; the clan strength
    // grows with the game clock until camps carry a clan of their own.
    for (std::size_t i = this->m_encampments.size(); i-- > 0; ) {
        const hex::AxialCoord campTile = this->m_encampments[i].location;
        const PlayerId clearer         = campOverrunBy(gameState, campTile);
        if (clearer == INVALID_PLAYER) {
            continue;
        }
        const int32_t reward = encampmentDestroyReward(1 + this->m_turnCounter / 50);
        if (aoc::game::Player* clearerPlayer = gameState.player(clearer); clearerPlayer != nullptr) {
            clearerPlayer->addGold(reward);
        }
        if (eventLog != nullptr) {
            eventLog->record(TurnEventType::BarbarianCampCleared, clearer, BARBARIAN_PLAYER, reward,
                             0, "Barbarian encampment cleared");
        }
        LOG_INFO("Barbarian encampment at (%d,%d) cleared by player %u for %d gold",
                 campTile.q, campTile.r, static_cast<unsigned>(clearer), reward);
        this->removeEncampment(i);
    }

    this->spawnEncampments(gameState, grid, rng, eventLog);
    this->spawnUnitsFromEncampments(gameState, grid, rng);
    this->moveBarbarianUnits(gameState, grid, rng);
}

void BarbarianController::spawnEncampments(aoc::game::GameState& gameState,
                                            const aoc::map::HexGrid& grid,
                                            aoc::Random& rng,
                                            TurnEventLog* eventLog) {
    if (this->m_turnCounter % ENCAMPMENT_SPAWN_INTERVAL != 0) {
        return;
    }

    if (static_cast<int32_t>(this->m_encampments.size()) >= MAX_ENCAMPMENTS) {
        return;
    }

    aoc::game::Player* barbPlayer = gameState.player(BARBARIAN_PLAYER);
    if (barbPlayer == nullptr) {
        return;
    }

    // Try several random positions to find a valid encampment site.
    constexpr int32_t MAX_ATTEMPTS = 50;
    for (int32_t attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
        int32_t col = rng.nextInt(0, grid.width() - 1);
        int32_t row = rng.nextInt(0, grid.height() - 1);
        hex::AxialCoord candidate = hex::offsetToAxial({col, row});
        int32_t index = grid.toIndex(candidate);

        // Must be passable land.
        aoc::map::TerrainType terrain = grid.terrain(index);
        if (aoc::map::isWater(terrain) || aoc::map::isImpassable(terrain)) {
            continue;
        }

        // Must be unowned.
        if (grid.owner(index) != INVALID_PLAYER) {
            continue;
        }

        // Must be far from any city.
        if (isTooCloseToCity(gameState, grid, candidate, MIN_DISTANCE_FROM_CITY)) {
            continue;
        }

        // Place the encampment.
        // The founding warrior below is this camp's first spawn, so the camp
        // starts on cooldown instead of spawning a second unit the same turn.
        BarbarianEncampmentComponent camp{};
        camp.location      = candidate;
        camp.spawnCooldown = SPAWN_COOLDOWN_TURNS;
        camp.unitsSpawned  = 1;

        // Give the camp a clan. GameState::barbarianClans() was never populated,
        // so BarbarianClanComponent -- its type, strength, isBribed, hiredBy --
        // was dead in its entirety, and bribeClan and hireClan, both fully
        // implemented, had no caller anywhere.
        {
            std::vector<BarbarianClanComponent>& clans = gameState.barbarianClans();
            BarbarianClanComponent clan{};
            clan.clanId = static_cast<uint8_t>(clans.size() % BARBARIAN_CLAN_COUNT);
            clan.clanType = BARBARIAN_CLAN_DEFS[clan.clanId].type;
            // Strength tracks the age: a late-game warband is a real threat, an
            // ancient one is a nuisance. Also what bribe and hire cost scale on.
            clan.strength = 1 + this->m_turnCounter / 50;
            camp.clanIndex = static_cast<int32_t>(clans.size());
            clans.push_back(clan);
            LOG_INFO("Barbarian camp at (%d,%d) founded by the %.*s (strength %d)",
                     candidate.q, candidate.r,
                     static_cast<int>(BARBARIAN_CLAN_DEFS[clan.clanId].name.size()),
                     BARBARIAN_CLAN_DEFS[clan.clanId].name.data(), clan.strength);
        }
        this->m_encampments.push_back(camp);

        // Spawn the founding warrior at the encampment.
        barbPlayer->addUnit(
            barbarianSpawnUnit(this->m_turnCounter, leadingPlayerEra(gameState)),
            candidate);

        {
            VisibilityEvent ev{};
            ev.type = VisibilityEventType::BarbarianCampSighted;
            ev.location = candidate;
            ev.actor = INVALID_PLAYER;
            gameState.visibilityBus().emit(ev);
        }

        if (eventLog != nullptr) {
            eventLog->record(TurnEventType::BarbarianCampSpawned, BARBARIAN_PLAYER, INVALID_PLAYER,
                             candidate.q, candidate.r, "Barbarian encampment spawned");
        }
        LOG_INFO("Barbarian encampment spawned at (%d,%d)", candidate.q, candidate.r);
        return;
    }
}

void BarbarianController::spawnUnitsFromEncampments(aoc::game::GameState& gameState,
                                                     const aoc::map::HexGrid& grid,
                                                     aoc::Random& rng) {
    (void)grid;
    (void)rng;

    aoc::game::Player* barbPlayer = gameState.player(BARBARIAN_PLAYER);
    if (barbPlayer == nullptr) {
        return;
    }

    for (BarbarianEncampmentComponent& camp : this->m_encampments) {
        if (camp.spawnCooldown > 0) {
            --camp.spawnCooldown;
            continue;
        }

        // Check if there are already enough barbarian units near this camp.
        if (countBarbarianUnitsNear(gameState, grid, camp.location, 4) >= MAX_NEARBY_BARBARIAN_UNITS) {
            continue;
        }

        // Spawn a warrior at the encampment location.
        barbPlayer->addUnit(
            barbarianSpawnUnit(this->m_turnCounter, leadingPlayerEra(gameState)),
            camp.location);

        camp.spawnCooldown = SPAWN_COOLDOWN_TURNS;
        ++camp.unitsSpawned;

        LOG_INFO("Barbarian warrior spawned at encampment (%d,%d)",
                 camp.location.q, camp.location.r);
    }
}

void BarbarianController::moveBarbarianUnits(aoc::game::GameState& gameState,
                                              aoc::map::HexGrid& grid,
                                              aoc::Random& rng) {
    aoc::game::Player* barbPlayer = gameState.player(BARBARIAN_PLAYER);
    if (barbPlayer == nullptr) {
        return;
    }

    // Collect raw pointers so we can mutate the player's unit list during the loop
    // without iterator invalidation (units may die from counter-attacks).
    std::vector<aoc::game::Unit*> barbarianUnits;
    barbarianUnits.reserve(static_cast<std::size_t>(barbPlayer->unitCount()));
    for (const std::unique_ptr<aoc::game::Unit>& unit : barbPlayer->units()) {
        barbarianUnits.push_back(unit.get());
    }

    for (aoc::game::Unit* unit : barbarianUnits) {
        if (unit->isDead()) {
            continue;
        }
        if (unit->movementRemaining() <= 0) {
            continue;
        }

        // Which clan answers for this unit: the one holding the nearest camp.
        // Units carry no clan of their own, and the camp they operate out of is
        // the honest association.
        const BarbarianClanComponent* clan = nullptr;
        {
            int32_t bestDist = std::numeric_limits<int32_t>::max();
            for (const BarbarianEncampmentComponent& camp : this->m_encampments) {
                if (camp.clanIndex < 0 ||
                    static_cast<std::size_t>(camp.clanIndex) >= gameState.barbarianClans().size()) {
                    continue;
                }
                const int32_t d = grid.distance(camp.location, unit->position());
                if (d < bestDist) {
                    bestDist = d;
                    clan     = &gameState.barbarianClans()[static_cast<std::size_t>(camp.clanIndex)];
                }
            }
        }

        // A bribed clan stands down. This is what the gold buys, and until the
        // clan list was populated isBribed was written by nobody and read by
        // nobody, so bribeClan -- fully implemented -- bought nothing.
        if (clan != nullptr && clan->isBribed) {
            continue;
        }

        // Look for a nearby non-barbarian unit to attack. A hired clan looks
        // for its employer's enemy first and only falls back to whoever is
        // nearest, which is what the hire is paying for.
        aoc::game::Unit* target = nullptr;
        if (clan != nullptr && clan->hiredTarget != INVALID_PLAYER) {
            target = findNearestTargetOf(gameState, grid, unit->position(), AGGRO_RANGE,
                                         clan->hiredTarget);
        }
        if (target == nullptr) {
            target = findNearestTarget(gameState, grid, unit->position(), AGGRO_RANGE);
        }
        const int32_t unitDist =
            (target != nullptr) ? grid.distance(unit->position(), target->position())
                                : AGGRO_RANGE + 1;

        // And for a city. A raider that ignored towns entirely was the whole
        // of the barbarian threat model: camps spawned, wandered, and bounced
        // off garrisons without ever menacing what the garrison was guarding.
        int32_t cityDist          = AGGRO_RANGE + 1;
        aoc::game::City* cityTarget =
            findNearestCityTarget(gameState, grid, unit->position(), AGGRO_RANGE, cityDist);

        // A defender in the field is the nearer threat on a tie: cutting it
        // down first is how a raid gets to the walls at all.
        if (cityTarget != nullptr && cityDist < unitDist) {
            if (cityDist == 1) {
                [[maybe_unused]] const CityAttackResult res = resolveAttackOnCity(
                    gameState, rng, grid, *unit, *cityTarget, gameState.currentTurn());
                continue;
            }
            // Close on the city.
            const std::array<hex::AxialCoord, 6> cityNbrs = hex::neighbors(unit->position());
            hex::AxialCoord towardCity                    = unit->position();
            int32_t bestCityDist                          = cityDist;
            for (const hex::AxialCoord& nbr : cityNbrs) {
                if (!grid.isValid(nbr)) { continue; }
                if (grid.movementCost(grid.toIndex(nbr)) == 0) { continue; }
                const int32_t d = grid.distance(nbr, cityTarget->location());
                if (d < bestCityDist) {
                    bestCityDist = d;
                    towardCity   = nbr;
                }
            }
            if (!(towardCity == unit->position())) {
                const int32_t moveCost = grid.movementCost(grid.toIndex(towardCity));
                if (unit->movementRemaining() >= moveCost) {
                    unit->setPosition(towardCity);
                    unit->setMovementRemaining(unit->movementRemaining() - moveCost);
                }
            }
            continue;
        }

        if (target != nullptr && !target->isDead()) {
            int32_t dist = grid.distance(unit->position(), target->position());

            if (dist == 1) {
                // Adjacent: resolve melee combat. Use the Unit& overload —
                // `unit` (barbarian attacker) and `target` (defender) are both
                // live, non-null here (unit guarded by isDead() above; target
                // by the `!= nullptr && !isDead()` check). The EntityId overload
                // with NULL_ENTITY is a no-op and was silently dropping every
                // barbarian melee attack.
                resolveMeleeCombat(gameState, rng, grid, *unit, *target);
                continue;
            }

            // Move toward the target.
            const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(unit->position());
            hex::AxialCoord bestMove = unit->position();
            int32_t bestDist = dist;

            for (const hex::AxialCoord& nbr : nbrs) {
                if (!grid.isValid(nbr)) {
                    continue;
                }
                int32_t cost = grid.movementCost(grid.toIndex(nbr));
                if (cost == 0) {
                    continue;
                }
                int32_t nbrDist = grid.distance(nbr, target->position());
                if (nbrDist < bestDist) {
                    bestDist = nbrDist;
                    bestMove = nbr;
                }
            }

            if (!(bestMove == unit->position())) {
                int32_t moveCost = grid.movementCost(grid.toIndex(bestMove));
                if (unit->movementRemaining() >= moveCost) {
                    unit->setPosition(bestMove);
                    unit->setMovementRemaining(unit->movementRemaining() - moveCost);
                }
            }
        } else {
            // Random patrol: pick a random passable neighbour.
            const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(unit->position());
            std::vector<hex::AxialCoord> passableNeighbors;
            for (const hex::AxialCoord& nbr : nbrs) {
                if (!grid.isValid(nbr)) {
                    continue;
                }
                int32_t cost = grid.movementCost(grid.toIndex(nbr));
                if (cost > 0 && unit->movementRemaining() >= cost) {
                    passableNeighbors.push_back(nbr);
                }
            }

            if (!passableNeighbors.empty()) {
                int32_t idx = rng.nextInt(0, static_cast<int32_t>(passableNeighbors.size()) - 1);
                hex::AxialCoord chosen = passableNeighbors[static_cast<std::size_t>(idx)];
                int32_t moveCost = grid.movementCost(grid.toIndex(chosen));
                unit->setPosition(chosen);
                unit->setMovementRemaining(unit->movementRemaining() - moveCost);
            }
        }
    }
}

} // namespace aoc::sim

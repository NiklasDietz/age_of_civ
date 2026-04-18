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
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/HexCoord.hpp"
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

    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        if (player->id() == BARBARIAN_PLAYER) {
            continue;
        }
        for (const std::unique_ptr<aoc::game::Unit>& unit : player->units()) {
            int32_t dist = grid.distance(unit->position(), position);
            if (dist <= range && dist < bestDist) {
                bestDist = dist;
                closest  = unit.get();
            }
        }
    }
    return closest;
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
    return false;
}

// ============================================================================
// BarbarianController
// ============================================================================

void BarbarianController::executeTurn(aoc::game::GameState& gameState,
                                       const aoc::map::HexGrid& grid,
                                       aoc::Random& rng) {
    ++this->m_turnCounter;

    // Restore movement points for all barbarian-owned units.
    refreshMovement(gameState, BARBARIAN_PLAYER);

    this->spawnEncampments(gameState, grid, rng);
    this->spawnUnitsFromEncampments(gameState, grid, rng);
    this->moveBarbarianUnits(gameState, grid, rng);
}

void BarbarianController::spawnEncampments(aoc::game::GameState& gameState,
                                            const aoc::map::HexGrid& grid,
                                            aoc::Random& rng) {
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
        BarbarianEncampmentComponent camp{};
        camp.location      = candidate;
        camp.spawnCooldown = 0;
        camp.unitsSpawned  = 0;
        this->m_encampments.push_back(camp);

        // Also spawn an initial warrior at the encampment.
        barbPlayer->addUnit(barbarianSpawnUnit(this->m_turnCounter), candidate);

        {
            VisibilityEvent ev{};
            ev.type = VisibilityEventType::BarbarianCampSighted;
            ev.location = candidate;
            ev.actor = INVALID_PLAYER;
            gameState.visibilityBus().emit(ev);
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
        barbPlayer->addUnit(barbarianSpawnUnit(this->m_turnCounter), camp.location);

        camp.spawnCooldown = SPAWN_COOLDOWN_TURNS;
        ++camp.unitsSpawned;

        LOG_INFO("Barbarian warrior spawned at encampment (%d,%d)",
                 camp.location.q, camp.location.r);
    }
}

void BarbarianController::moveBarbarianUnits(aoc::game::GameState& gameState,
                                              const aoc::map::HexGrid& grid,
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

        // Look for a nearby non-barbarian unit to attack.
        aoc::game::Unit* target = findNearestTarget(gameState, grid, unit->position(), AGGRO_RANGE);

        if (target != nullptr && !target->isDead()) {
            int32_t dist = grid.distance(unit->position(), target->position());

            if (dist == 1) {
                // Adjacent: resolve melee combat.
                // Combat.hpp takes EntityId; a placeholder NULL_ENTITY is passed here
                // until the combat subsystem is fully migrated to the object model.
                // Combat API uses Unit& overloads.
                resolveMeleeCombat(gameState, rng, grid, NULL_ENTITY, NULL_ENTITY);
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

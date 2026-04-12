/**
 * @file AISettlerController.cpp
 * @brief AI settler management: city location scoring, settler movement,
 *        and city founding.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/ai/AISettlerController.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/turn/TurnProcessor.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"

namespace aoc::sim::ai {

// ============================================================================
// Helper: Score a potential city location for settler placement.
// ============================================================================

static float scoreCityLocation(aoc::hex::AxialCoord pos,
                               const aoc::map::HexGrid& grid,
                               const aoc::game::GameState& gameState,
                               PlayerId player) {
    if (!grid.isValid(pos)) {
        return -1000.0f;
    }
    const int32_t centerIdx = grid.toIndex(pos);

    if (aoc::map::isWater(grid.terrain(centerIdx)) ||
        aoc::map::isImpassable(grid.terrain(centerIdx))) {
        return -1000.0f;
    }

    if (grid.owner(centerIdx) != INVALID_PLAYER) {
        return -500.0f;
    }

    float score = 0.0f;

    const std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(pos);
    int32_t coastCount = 0;
    for (const aoc::hex::AxialCoord& nbr : nbrs) {
        if (!grid.isValid(nbr)) {
            continue;
        }
        const int32_t nbrIdx = grid.toIndex(nbr);
        const aoc::map::TileYield yield = grid.tileYield(nbrIdx);
        score += static_cast<float>(yield.food) * 2.0f;
        score += static_cast<float>(yield.production) * 1.5f;
        score += static_cast<float>(yield.gold) * 1.0f;

        if (aoc::map::isWater(grid.terrain(nbrIdx))) {
            ++coastCount;
        }

        if (grid.resource(nbrIdx).isValid()) {
            score += 3.0f;
        }
    }

    if (coastCount > 0 && coastCount <= 3) {
        score += 4.0f;
    }

    // Penalise positions that are too close to existing cities or too far from own cities
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : p->cities()) {
            const int32_t dist = aoc::hex::distance(pos, city->location());

            if (city->owner() == player) {
                if (dist < 4) {
                    // Reduced from -50 to -20: still discourages very close placement
                    // but doesn't make the whole region unviable for expansion.
                    score -= 20.0f;
                } else if (dist > 8) {
                    score -= static_cast<float>(dist - 8) * 2.0f;
                } else if (dist >= 4 && dist <= 6) {
                    score += 5.0f;
                }
            } else {
                if (dist < 4) {
                    score -= 30.0f;
                }
            }
        }
    }

    const aoc::map::TileYield centerYield = grid.tileYield(centerIdx);
    score += static_cast<float>(centerYield.food) * 1.5f;
    score += static_cast<float>(centerYield.production) * 1.0f;

    return score;
}

// ============================================================================
// Constructor
// ============================================================================

AISettlerController::AISettlerController(PlayerId player, aoc::ui::AIDifficulty difficulty)
    : m_player(player)
    , m_difficulty(difficulty)
{
}

// ============================================================================
// Settler actions
// ============================================================================

void AISettlerController::executeSettlerActions(aoc::game::GameState& gameState,
                                                aoc::map::HexGrid& grid) {
    aoc::game::Player* gsPlayer = gameState.player(this->m_player);
    if (gsPlayer == nullptr) {
        return;
    }

    // Snapshot settler units (founding destroys the unit)
    struct SettlerSnapshot {
        aoc::game::Unit*     ptr;
        aoc::hex::AxialCoord position;
        int32_t              movementRemaining;
    };
    std::vector<SettlerSnapshot> settlers;
    for (const std::unique_ptr<aoc::game::Unit>& u : gsPlayer->units()) {
        if (unitTypeDef(u->typeId()).unitClass == UnitClass::Settler) {
            settlers.push_back({u.get(), u->position(), u->movementRemaining()});
        }
    }

    for (const SettlerSnapshot& snap : settlers) {
        if (snap.ptr->isDead()) {
            continue;
        }

        // Score candidate locations in a radius around the settler
        float bestScore = -999.0f;
        aoc::hex::AxialCoord bestLocation = snap.position;

        std::vector<aoc::hex::AxialCoord> candidates;
        candidates.reserve(200);
        aoc::hex::spiral(snap.position, 8, std::back_inserter(candidates));

        for (const aoc::hex::AxialCoord& candidate : candidates) {
            if (!grid.isValid(candidate)) {
                continue;
            }
            const float locationScore = scoreCityLocation(
                candidate, grid, gameState, this->m_player);
            if (locationScore > bestScore) {
                bestScore = locationScore;
                bestLocation = candidate;
            }
        }

        // Found city if at the best location and score is acceptable
        if (bestLocation == snap.position && bestScore > -100.0f) {
            const std::string aiCityName = getNextCityName(gameState, this->m_player);
            foundCity(gameState, grid, this->m_player, snap.position, aiCityName);
            gsPlayer->removeUnit(snap.ptr);

            LOG_INFO("AI %u Founded city at (%d,%d) score=%.1f",
                     static_cast<unsigned>(this->m_player),
                     snap.position.q, snap.position.r,
                     static_cast<double>(bestScore));
            continue;
        }

        // Move toward the best location
        if (bestLocation != snap.position && snap.ptr->movementRemaining() > 0) {
            orderUnitMove(*snap.ptr, bestLocation, grid);
            moveUnitAlongPath(gameState, *snap.ptr, grid);
        }
    }
}

} // namespace aoc::sim::ai

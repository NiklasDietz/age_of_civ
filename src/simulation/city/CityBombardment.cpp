/**
 * @file CityBombardment.cpp
 * @brief City ranged bombardment + destructible wall + siege implementation.
 */

#include "aoc/simulation/city/CityBombardment.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

// ============================================================================
// Wall auto-detection from buildings
// ============================================================================

/// Detect wall tier from the city's built buildings.
/// BuildingId 17 = Ancient Walls, 18 = Medieval Walls, 24 = Renaissance Walls
static WallTier detectWallTier(const aoc::game::City& city) {
    if (city.hasBuilding(BuildingId{24})) { return WallTier::Renaissance; }
    if (city.hasBuilding(BuildingId{18})) { return WallTier::Medieval; }
    if (city.hasBuilding(BuildingId{17})) { return WallTier::Ancient; }
    return WallTier::None;
}

/// Ensure wall state matches built buildings (called each turn).
static void syncWallState(aoc::game::City& city) {
    const WallTier currentTier = detectWallTier(city);
    if (currentTier != city.walls().tier) {
        // Upgraded or built new walls — set new tier, heal to full
        city.walls().setTier(currentTier);
    }
}

// ============================================================================
// Bombardment processing
// ============================================================================

void processCityBombardment(aoc::game::GameState& gameState,
                             const aoc::map::HexGrid& grid,
                             PlayerId player, aoc::Random& rng) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return; }

    for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
        // Sync wall state from buildings
        syncWallState(*city);

        CityWallState& walls = city->walls();
        // Encampment district adds a ranged-attack source independent of walls.
        // Strength scales with Barracks presence. Range 2 base, +1 with Barracks.
        const bool hasEncampment = city->districts().hasDistrict(DistrictType::Encampment);
        const bool hasBarracks   = city->hasBuilding(BuildingId{18});
        float encampStrength = hasEncampment ? (hasBarracks ? 32.0f : 22.0f) : 0.0f;
        int32_t encampRange  = hasEncampment ? (hasBarracks ? 3 : 2) : 0;

        if (!walls.hasWalls() || !walls.isIntact()) {
            // No walls or walls destroyed — normally can't shoot.
            // Repair walls if no enemy within 3 tiles.
            if (walls.hasWalls() && walls.currentHP < walls.maxHP) {
                bool enemyNearby = false;
                for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
                    if (other->id() == player) { continue; }
                    for (const std::unique_ptr<aoc::game::Unit>& unit : other->units()) {
                        if (isMilitary(unit->typeDef().unitClass)
                            && grid.distance(unit->position(), city->location()) <= 3) {
                            enemyNearby = true;
                            break;
                        }
                    }
                    if (enemyNearby) { break; }
                }
                if (!enemyNearby) {
                    walls.repair();
                }
            }
            // Encampment district still shoots even without walls.
            if (!hasEncampment) { continue; }
        }

        // Base ranged attack: walls (if intact) OR encampment. Take the stronger
        // of the two so an Encampment in a walled city still hits hard.
        float bombardStrength = 0.0f;
        int32_t attackRange = 0;
        if (walls.hasWalls() && walls.isIntact()) {
            bombardStrength = static_cast<float>(walls.rangedStrength);
            attackRange     = walls.range;
        }
        if (hasEncampment && encampStrength > bombardStrength) {
            bombardStrength = encampStrength;
            attackRange     = std::max(attackRange, encampRange);
        } else if (hasEncampment) {
            attackRange = std::max(attackRange, encampRange);
        }

        // Find the weakest enemy unit within range
        aoc::game::Unit* bestTarget = nullptr;
        aoc::game::Player* targetOwner = nullptr;
        int32_t weakestHP = std::numeric_limits<int32_t>::max();

        for (const std::unique_ptr<aoc::game::Player>& otherPlayer : gameState.players()) {
            if (otherPlayer->id() == player) { continue; }
            for (const std::unique_ptr<aoc::game::Unit>& unit : otherPlayer->units()) {
                if (!isMilitary(unit->typeDef().unitClass)) { continue; }
                const int32_t dist = grid.distance(unit->position(), city->location());
                if (dist > 0 && dist <= attackRange && unit->hitPoints() < weakestHP) {
                    weakestHP = unit->hitPoints();
                    bestTarget = unit.get();
                    targetOwner = otherPlayer.get();
                }
            }
        }

        if (bestTarget == nullptr || targetOwner == nullptr) {
            // No enemies in range — repair walls
            walls.repair();
            continue;
        }

        // Compute damage: wall strength vs unit defense
        const float defStrength = static_cast<float>(bestTarget->typeDef().combatStrength);
        const float ratio = bombardStrength / std::max(defStrength, 1.0f);
        const float randomFactor = 0.8f + rng.nextFloat() * 0.4f;
        const int32_t damage = std::clamp(
            static_cast<int32_t>(30.0f * ratio * randomFactor), 1, 80);

        bestTarget->setHitPoints(bestTarget->hitPoints() - damage);

        LOG_INFO("City %s (walls %d/%d HP) bombarded %.*s for %d damage (HP: %d/%d)",
                 city->name().c_str(),
                 walls.currentHP, walls.maxHP,
                 static_cast<int>(bestTarget->typeDef().name.size()),
                 bestTarget->typeDef().name.data(),
                 damage, bestTarget->hitPoints(), bestTarget->typeDef().maxHitPoints);

        if (bestTarget->isDead()) {
            targetOwner->removeUnit(bestTarget);
            LOG_INFO("City bombardment destroyed enemy unit");
        }
    }
}

// ============================================================================
// Siege functions
// ============================================================================

bool canCaptureCity(const aoc::game::City& city) {
    // City can be captured if it has no walls OR walls are destroyed (HP = 0)
    if (!city.walls().hasWalls()) { return true; }
    return city.walls().currentHP <= 0;
}

int32_t dealSiegeDamage(aoc::game::City& city, const aoc::game::Unit& attacker) {
    CityWallState& walls = city.walls();
    if (!walls.hasWalls() || walls.currentHP <= 0) {
        return 0;  // No walls to damage
    }

    // Bombard/Artillery class: full strength as wall damage
    // Melee/other: 15% of combat strength as wall damage
    const UnitTypeDef& def = attacker.typeDef();
    int32_t siegeDamage = 0;

    if (def.unitClass == UnitClass::Artillery) {
        // Full siege: ranged strength + combat strength
        siegeDamage = def.rangedStrength + def.combatStrength / 2;
    } else if (def.unitClass == UnitClass::Melee || def.unitClass == UnitClass::Cavalry
               || def.unitClass == UnitClass::Armor) {
        // Melee against walls: 15% effectiveness
        siegeDamage = std::max(1, def.combatStrength * 15 / 100);
    } else {
        // Other classes: 10%
        siegeDamage = std::max(1, def.combatStrength * 10 / 100);
    }

    const int32_t actualDamage = walls.takeDamage(siegeDamage);

    if (walls.currentHP <= 0) {
        LOG_INFO("Walls of %s destroyed! City is now vulnerable to capture.",
                 city.name().c_str());
    }

    return actualDamage;
}

} // namespace aoc::sim

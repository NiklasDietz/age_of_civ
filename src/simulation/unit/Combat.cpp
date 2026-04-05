/**
 * @file Combat.cpp
 * @brief Lanchester-based combat resolution.
 */

#include "aoc/simulation/unit/Combat.hpp"
#include "aoc/simulation/barbarian/BarbarianController.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/ecs/World.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

float terrainDefenseModifier(const aoc::map::HexGrid& grid, hex::AxialCoord position) {
    if (!grid.isValid(position)) {
        return 1.0f;
    }
    int32_t index = grid.toIndex(position);
    aoc::map::FeatureType feature = grid.feature(index);

    float modifier = 1.0f;
    if (feature == aoc::map::FeatureType::Hills) {
        modifier += 0.3f;    // +30% defense on hills
    }
    if (feature == aoc::map::FeatureType::Forest) {
        modifier += 0.25f;   // +25% in forest
    }
    if (feature == aoc::map::FeatureType::Jungle) {
        modifier += 0.25f;
    }
    return modifier;
}

int32_t countAdjacentFriendlies(const aoc::ecs::World& world,
                                 hex::AxialCoord position,
                                 PlayerId friendlyPlayer) {
    const aoc::ecs::ComponentPool<UnitComponent>* pool = world.getPool<UnitComponent>();
    if (pool == nullptr) {
        return 0;
    }

    std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(position);
    int32_t count = 0;

    for (uint32_t i = 0; i < pool->size(); ++i) {
        const UnitComponent& unit = pool->data()[i];
        if (unit.owner != friendlyPlayer) {
            continue;
        }
        for (const hex::AxialCoord& nbr : nbrs) {
            if (unit.position == nbr) {
                ++count;
                break;
            }
        }
    }
    return count;
}

namespace {

/// Core damage formula: modified Lanchester-style.
/// damage = 30 * (attackStrength / defenseStrength) * randomFactor
int32_t computeDamage(float attackStrength, float defenseStrength, aoc::Random& rng) {
    if (defenseStrength < 0.01f) {
        return 100;  // Instant kill if no defense
    }

    float ratio = attackStrength / defenseStrength;
    // Random factor: 0.8 to 1.2
    float randomFactor = 0.8f + rng.nextFloat() * 0.4f;
    float baseDamage = 30.0f * ratio * randomFactor;

    return std::clamp(static_cast<int32_t>(baseDamage), 0, 100);
}

} // anonymous namespace

CombatResult resolveMeleeCombat(aoc::ecs::World& world,
                                 aoc::Random& rng,
                                 const aoc::map::HexGrid& grid,
                                 EntityId attacker,
                                 EntityId defender) {
    UnitComponent& atkUnit = world.getComponent<UnitComponent>(attacker);
    UnitComponent& defUnit = world.getComponent<UnitComponent>(defender);
    const UnitTypeDef& atkDef = unitTypeDef(atkUnit.typeId);
    const UnitTypeDef& defDef = unitTypeDef(defUnit.typeId);

    // Effective strengths
    float atkStrength = static_cast<float>(atkDef.combatStrength);
    float defStrength = static_cast<float>(defDef.combatStrength);

    // Embarked units fight at 50% strength
    if (atkUnit.state == UnitState::Embarked) {
        atkStrength *= 0.5f;
    }
    if (defUnit.state == UnitState::Embarked) {
        defStrength *= 0.5f;
    }

    // Health modifier: damaged units fight worse
    float atkHealthMod = static_cast<float>(atkUnit.hitPoints) / static_cast<float>(atkDef.maxHitPoints);
    float defHealthMod = static_cast<float>(defUnit.hitPoints) / static_cast<float>(defDef.maxHitPoints);
    atkStrength *= atkHealthMod;
    defStrength *= defHealthMod;

    // Terrain defense bonus for defender
    float terrainMod = terrainDefenseModifier(grid, defUnit.position);
    defStrength *= terrainMod;

    // Flanking bonus: +10% per adjacent friendly for attacker
    int32_t flanking = countAdjacentFriendlies(world, defUnit.position, atkUnit.owner);
    atkStrength *= 1.0f + static_cast<float>(flanking) * 0.10f;

    // Fortification bonus
    if (defUnit.state == UnitState::Fortified) {
        defStrength *= 1.25f;
    }

    // Calculate damage
    CombatResult result{};
    result.defenderDamage = computeDamage(atkStrength, defStrength, rng);
    result.attackerDamage = computeDamage(defStrength, atkStrength, rng);

    // Attacker takes slightly less damage (aggressor advantage)
    result.attackerDamage = result.attackerDamage * 8 / 10;

    // Apply damage
    defUnit.hitPoints -= result.defenderDamage;
    atkUnit.hitPoints -= result.attackerDamage;

    result.defenderKilled = (defUnit.hitPoints <= 0);
    result.attackerKilled = (atkUnit.hitPoints <= 0);

    // XP: base 5, bonus for killing
    result.attackerXpGained = 5;
    result.defenderXpGained = 4;
    if (result.defenderKilled) {
        result.attackerXpGained += 10;
    }
    if (result.attackerKilled) {
        result.defenderXpGained += 10;
    }

    // If defender died and attacker survived, move attacker to defender's tile
    if (result.defenderKilled && !result.attackerKilled) {
        atkUnit.position = defUnit.position;
        atkUnit.movementRemaining = 0;  // Melee attack ends movement
    }

    // Clean up dead units
    hex::AxialCoord defenderTile = defUnit.position;
    PlayerId attackerOwner = atkUnit.owner;

    if (result.defenderKilled) {
        world.destroyEntity(defender);
    }
    if (result.attackerKilled) {
        world.destroyEntity(attacker);
    }

    // If defender was killed and attacker survived, check for barbarian encampment
    // on the tile the attacker moved onto.
    if (result.defenderKilled && !result.attackerKilled && attackerOwner != BARBARIAN_PLAYER) {
        aoc::ecs::ComponentPool<BarbarianEncampmentComponent>* campPool =
            world.getPool<BarbarianEncampmentComponent>();
        if (campPool != nullptr) {
            for (uint32_t ci = 0; ci < campPool->size(); ++ci) {
                if (campPool->data()[ci].location == defenderTile) {
                    EntityId campEntity = campPool->entities()[ci];

                    // Award gold to the attacking player
                    aoc::ecs::ComponentPool<PlayerEconomyComponent>* econPool =
                        world.getPool<PlayerEconomyComponent>();
                    if (econPool != nullptr) {
                        for (uint32_t ei = 0; ei < econPool->size(); ++ei) {
                            if (econPool->data()[ei].owner == attackerOwner) {
                                econPool->data()[ei].treasury += 25;
                                LOG_INFO("Player %u earned 25 gold from clearing barbarian encampment",
                                         static_cast<unsigned>(attackerOwner));
                                break;
                            }
                        }
                    }

                    world.destroyEntity(campEntity);
                    LOG_INFO("Barbarian encampment at (%d,%d) destroyed in combat",
                             defenderTile.q, defenderTile.r);
                    break;
                }
            }
        }
    }

    LOG_INFO("Atk took %d dmg, Def took %d dmg%s%s",
             result.attackerDamage, result.defenderDamage,
             result.attackerKilled ? " (attacker killed)" : "",
             result.defenderKilled ? " (defender killed)" : "");

    return result;
}

CombatResult resolveRangedCombat(aoc::ecs::World& world,
                                  aoc::Random& rng,
                                  const aoc::map::HexGrid& grid,
                                  EntityId attacker,
                                  EntityId defender) {
    UnitComponent& atkUnit = world.getComponent<UnitComponent>(attacker);
    UnitComponent& defUnit = world.getComponent<UnitComponent>(defender);
    const UnitTypeDef& atkDef = unitTypeDef(atkUnit.typeId);
    const UnitTypeDef& defDef = unitTypeDef(defUnit.typeId);

    // Ranged uses rangedStrength for attack
    float atkStrength = static_cast<float>(atkDef.rangedStrength);
    float defStrength = static_cast<float>(defDef.combatStrength);

    float atkHealthMod = static_cast<float>(atkUnit.hitPoints) / static_cast<float>(atkDef.maxHitPoints);
    float defHealthMod = static_cast<float>(defUnit.hitPoints) / static_cast<float>(defDef.maxHitPoints);
    atkStrength *= atkHealthMod;
    defStrength *= defHealthMod;

    float terrainMod = terrainDefenseModifier(grid, defUnit.position);
    defStrength *= terrainMod;

    CombatResult result{};
    // Ranged: attacker deals damage but takes none (no retaliation)
    result.defenderDamage = computeDamage(atkStrength, defStrength, rng);
    result.attackerDamage = 0;

    defUnit.hitPoints -= result.defenderDamage;
    result.defenderKilled = (defUnit.hitPoints <= 0);

    result.attackerXpGained = 3;
    result.defenderXpGained = 2;
    if (result.defenderKilled) {
        result.attackerXpGained += 8;
        world.destroyEntity(defender);
    }

    return result;
}

} // namespace aoc::sim

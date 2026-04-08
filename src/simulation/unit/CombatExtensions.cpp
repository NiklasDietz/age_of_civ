/**
 * @file CombatExtensions.cpp
 * @brief Corps/Armies, nuclear weapons, and air combat mechanics.
 */

#include "aoc/simulation/unit/CombatExtensions.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

// ============================================================================
// Corps / Armies
// ============================================================================

ErrorCode formCorps(aoc::ecs::World& world,
                     EntityId targetUnit, EntityId sourceUnit) {
    UnitComponent* target = world.tryGetComponent<UnitComponent>(targetUnit);
    UnitComponent* source = world.tryGetComponent<UnitComponent>(sourceUnit);
    if (target == nullptr || source == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    // Same type and owner
    if (target->typeId != source->typeId || target->owner != source->owner) {
        return ErrorCode::InvalidArgument;
    }

    // Must be adjacent
    if (hex::distance(target->position, source->position) > 1) {
        return ErrorCode::InvalidArgument;
    }

    // Check current formation -- target must be Single
    UnitFormationComponent* formation =
        world.tryGetComponent<UnitFormationComponent>(targetUnit);
    if (formation != nullptr && formation->level != FormationLevel::Single) {
        return ErrorCode::InvalidArgument;
    }

    // Create formation component
    if (formation == nullptr) {
        UnitFormationComponent newFormation{};
        world.addComponent<UnitFormationComponent>(targetUnit, std::move(newFormation));
        formation = world.tryGetComponent<UnitFormationComponent>(targetUnit);
    }
    if (formation != nullptr) {
        bool naval = isNaval(unitTypeDef(target->typeId).unitClass);
        formation->level = naval ? FormationLevel::Fleet : FormationLevel::Corps;
        formation->unitsInFormation = 2;
    }

    // Heal the combined unit (take the better HP)
    target->hitPoints = std::max(target->hitPoints, source->hitPoints);

    // Destroy the source unit
    world.destroyEntity(sourceUnit);

    LOG_INFO("Formed %s from two %.*s units",
             isNaval(unitTypeDef(target->typeId).unitClass) ? "Fleet" : "Corps",
             static_cast<int>(unitTypeDef(target->typeId).name.size()),
             unitTypeDef(target->typeId).name.data());

    return ErrorCode::Ok;
}

ErrorCode formArmy(aoc::ecs::World& world,
                    EntityId corpsUnit, EntityId sourceUnit) {
    UnitComponent* corps = world.tryGetComponent<UnitComponent>(corpsUnit);
    UnitComponent* source = world.tryGetComponent<UnitComponent>(sourceUnit);
    if (corps == nullptr || source == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    if (corps->typeId != source->typeId || corps->owner != source->owner) {
        return ErrorCode::InvalidArgument;
    }

    if (hex::distance(corps->position, source->position) > 1) {
        return ErrorCode::InvalidArgument;
    }

    UnitFormationComponent* formation =
        world.tryGetComponent<UnitFormationComponent>(corpsUnit);
    if (formation == nullptr || formation->unitsInFormation != 2) {
        return ErrorCode::InvalidArgument;
    }

    bool naval = isNaval(unitTypeDef(corps->typeId).unitClass);
    formation->level = naval ? FormationLevel::Armada : FormationLevel::Army;
    formation->unitsInFormation = 3;

    corps->hitPoints = std::max(corps->hitPoints, source->hitPoints);
    world.destroyEntity(sourceUnit);

    LOG_INFO("Formed %s!", naval ? "Armada" : "Army");

    return ErrorCode::Ok;
}

// ============================================================================
// Nuclear Weapons
// ============================================================================

ErrorCode launchNuclearStrike(aoc::ecs::World& world,
                              aoc::map::HexGrid& grid,
                              EntityId launcherEntity,
                              hex::AxialCoord targetTile,
                              NukeType type) {
    // Determine blast radius and population damage
    int32_t blastRadius = (type == NukeType::ThermonuclearDevice) ? 2 : 1;
    float populationDamage = (type == NukeType::ThermonuclearDevice) ? 0.75f : 0.50f;

    const UnitComponent* launcher = world.tryGetComponent<UnitComponent>(launcherEntity);
    PlayerId launcherOwner = (launcher != nullptr) ? launcher->owner : INVALID_PLAYER;

    LOG_INFO("NUCLEAR STRIKE at (%d,%d)! Blast radius %d, %d%% population damage",
             targetTile.q, targetTile.r, blastRadius,
             static_cast<int>(populationDamage * 100.0f));

    // Collect all tiles in blast radius
    std::vector<hex::AxialCoord> blastTiles;
    blastTiles.push_back(targetTile);
    if (blastRadius >= 1) {
        std::array<hex::AxialCoord, 6> ring1 = hex::neighbors(targetTile);
        for (const hex::AxialCoord& t : ring1) {
            if (grid.isValid(t)) {
                blastTiles.push_back(t);
            }
        }
    }
    if (blastRadius >= 2) {
        // Ring 2: neighbors of ring 1 not already included
        std::vector<hex::AxialCoord> ring2;
        for (const hex::AxialCoord& r1 : blastTiles) {
            std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(r1);
            for (const hex::AxialCoord& n : nbrs) {
                if (!grid.isValid(n)) { continue; }
                bool alreadyIncluded = false;
                for (const hex::AxialCoord& existing : blastTiles) {
                    if (existing == n) { alreadyIncluded = true; break; }
                }
                if (!alreadyIncluded) {
                    for (const hex::AxialCoord& existing : ring2) {
                        if (existing == n) { alreadyIncluded = true; break; }
                    }
                }
                if (!alreadyIncluded) {
                    ring2.push_back(n);
                }
            }
        }
        for (const hex::AxialCoord& t : ring2) {
            blastTiles.push_back(t);
        }
    }

    // Destroy all units in blast zone
    aoc::ecs::ComponentPool<UnitComponent>* unitPool = world.getPool<UnitComponent>();
    if (unitPool != nullptr) {
        std::vector<EntityId> toDestroy;
        for (uint32_t i = 0; i < unitPool->size(); ++i) {
            const UnitComponent& unit = unitPool->data()[i];
            for (const hex::AxialCoord& blastTile : blastTiles) {
                if (unit.position == blastTile) {
                    toDestroy.push_back(unitPool->entities()[i]);
                    break;
                }
            }
        }
        for (EntityId e : toDestroy) {
            world.destroyEntity(e);
        }
    }

    // Damage cities in blast zone
    aoc::ecs::ComponentPool<CityComponent>* cityPool = world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            CityComponent& city = cityPool->data()[i];
            for (const hex::AxialCoord& blastTile : blastTiles) {
                if (city.location == blastTile) {
                    int32_t popLoss = static_cast<int32_t>(
                        static_cast<float>(city.population) * populationDamage);
                    city.population = std::max(1, city.population - popLoss);
                    LOG_INFO("City %s lost %d population from nuclear strike",
                             city.name.c_str(), popLoss);
                    break;
                }
            }
        }
    }

    // Apply Fallout to all blast tiles
    for (const hex::AxialCoord& tile : blastTiles) {
        int32_t idx = grid.toIndex(tile);
        // Remove improvements
        grid.setImprovement(idx, aoc::map::ImprovementType::None);
        // Remove features (forest burns, etc.)
        grid.setFeature(idx, aoc::map::FeatureType::None);
    }

    // Add grievance with ALL civilizations (+50 each)
    // Nuclear strike is the worst possible grievance: use ConqueredCity type
    // with maximum severity to represent the global outrage.
    aoc::ecs::ComponentPool<PlayerGrievanceComponent>* grievancePool =
        world.getPool<PlayerGrievanceComponent>();
    if (grievancePool != nullptr && launcherOwner != INVALID_PLAYER) {
        for (uint32_t i = 0; i < grievancePool->size(); ++i) {
            PlayerGrievanceComponent& pg = grievancePool->data()[i];
            if (pg.owner != launcherOwner) {
                Grievance g{};
                g.type = GrievanceType::ConqueredCity;  // Worst available type
                g.against = launcherOwner;
                g.severity = 50;
                g.turnsRemaining = 100;
                pg.grievances.push_back(g);
            }
        }
    }

    // Destroy the launcher unit (consumed)
    if (launcher != nullptr) {
        world.destroyEntity(launcherEntity);
    }

    return ErrorCode::Ok;
}

// ============================================================================
// Air Combat
// ============================================================================

ErrorCode executeBombingRun(aoc::ecs::World& world,
                            aoc::map::HexGrid& grid,
                            EntityId bomberEntity,
                            hex::AxialCoord targetTile) {
    UnitComponent* bomber = world.tryGetComponent<UnitComponent>(bomberEntity);
    AirUnitComponent* air = world.tryGetComponent<AirUnitComponent>(bomberEntity);
    if (bomber == nullptr || air == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    if (air->sortiesRemaining <= 0) {
        return ErrorCode::InvalidArgument;  // No sorties left
    }

    // Range check
    hex::AxialCoord basePos{0, 0};
    const CityComponent* base = world.tryGetComponent<CityComponent>(air->baseCity);
    if (base != nullptr) {
        basePos = base->location;
    }
    if (hex::distance(basePos, targetTile) > air->operationalRange) {
        return ErrorCode::InvalidArgument;  // Out of range
    }

    // Check for interception by enemy fighters
    aoc::ecs::ComponentPool<AirUnitComponent>* airPool = world.getPool<AirUnitComponent>();
    if (airPool != nullptr) {
        for (uint32_t i = 0; i < airPool->size(); ++i) {
            AirUnitComponent& interceptor = airPool->data()[i];
            if (!interceptor.isIntercepting) { continue; }

            const UnitComponent* intUnit = world.tryGetComponent<UnitComponent>(airPool->entities()[i]);
            if (intUnit == nullptr || intUnit->owner == bomber->owner) { continue; }

            if (attemptInterception(world, airPool->entities()[i], bomberEntity)) {
                // Bomber was intercepted -- take damage, abort mission
                bomber->hitPoints -= 30;
                if (bomber->hitPoints <= 0) {
                    world.destroyEntity(bomberEntity);
                    return ErrorCode::Ok;  // Bomber destroyed
                }
                break;
            }
        }
    }

    // Execute bombing
    if (!grid.isValid(targetTile)) {
        return ErrorCode::InvalidArgument;
    }
    int32_t targetIdx = grid.toIndex(targetTile);

    // Damage improvements
    aoc::map::ImprovementType imp = grid.improvement(targetIdx);
    if (imp != aoc::map::ImprovementType::None && imp != aoc::map::ImprovementType::Road) {
        grid.setImprovement(targetIdx, aoc::map::ImprovementType::None);
        LOG_INFO("Bombing destroyed improvement at (%d,%d)", targetTile.q, targetTile.r);
    }

    // Damage units on the tile
    aoc::ecs::ComponentPool<UnitComponent>* unitPool = world.getPool<UnitComponent>();
    if (unitPool != nullptr) {
        for (uint32_t i = 0; i < unitPool->size(); ++i) {
            UnitComponent& unit = unitPool->data()[i];
            if (unit.position == targetTile && unit.owner != bomber->owner) {
                int32_t bombDamage = unitTypeDef(bomber->typeId).rangedStrength;
                unit.hitPoints -= bombDamage;
                if (unit.hitPoints <= 0) {
                    world.destroyEntity(unitPool->entities()[i]);
                }
                break;
            }
        }
    }

    --air->sortiesRemaining;
    return ErrorCode::Ok;
}

bool attemptInterception(aoc::ecs::World& world,
                         EntityId interceptorEntity,
                         EntityId targetAirUnit) {
    UnitComponent* interceptor = world.tryGetComponent<UnitComponent>(interceptorEntity);
    AirUnitComponent* intAir = world.tryGetComponent<AirUnitComponent>(interceptorEntity);
    UnitComponent* target = world.tryGetComponent<UnitComponent>(targetAirUnit);
    AirUnitComponent* targetAir = world.tryGetComponent<AirUnitComponent>(targetAirUnit);

    if (interceptor == nullptr || intAir == nullptr
        || target == nullptr || targetAir == nullptr) {
        return false;
    }

    if (intAir->sortiesRemaining <= 0 || !intAir->isIntercepting) {
        return false;
    }

    // Check range: interceptor must be able to reach the target's position
    hex::AxialCoord intBase{0, 0};
    const CityComponent* base = world.tryGetComponent<CityComponent>(intAir->baseCity);
    if (base != nullptr) {
        intBase = base->location;
    }
    if (hex::distance(intBase, target->position) > intAir->operationalRange) {
        return false;
    }

    // Interception success: deal damage to target based on combat strength
    int32_t intStrength = unitTypeDef(interceptor->typeId).combatStrength;
    target->hitPoints -= intStrength / 2;

    // Interceptor uses a sortie
    --intAir->sortiesRemaining;

    LOG_INFO("Fighter intercepted enemy air unit, dealt %d damage", intStrength / 2);

    return true;
}

void resetAirSorties(aoc::ecs::World& world, PlayerId player) {
    aoc::ecs::ComponentPool<AirUnitComponent>* airPool =
        world.getPool<AirUnitComponent>();
    if (airPool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < airPool->size(); ++i) {
        const UnitComponent* unit =
            world.tryGetComponent<UnitComponent>(airPool->entities()[i]);
        if (unit != nullptr && unit->owner == player) {
            airPool->data()[i].sortiesRemaining = airPool->data()[i].maxSorties;
        }
    }
}

} // namespace aoc::sim

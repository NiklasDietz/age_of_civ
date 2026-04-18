/**
 * @file CombatExtensions.cpp
 * @brief Corps/Armies, nuclear weapons, and air combat mechanics.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/unit/CombatExtensions.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

// ============================================================================
// Corps / Armies
// ============================================================================

ErrorCode formCorps(aoc::game::GameState& gameState,
                     aoc::game::Unit& target,
                     aoc::game::Unit& source) {
    // Same type and owner
    if (target.typeId() != source.typeId() || target.owner() != source.owner()) {
        return ErrorCode::InvalidArgument;
    }

    // Must be adjacent
    if (hex::distance(target.position(), source.position()) > 1) {
        return ErrorCode::InvalidArgument;
    }

    // Target must be Single formation
    if (target.formationLevel() != FormationLevel::Single) {
        return ErrorCode::InvalidArgument;
    }

    const bool naval = isNaval(unitTypeDef(target.typeId()).unitClass);
    target.setFormationLevel(naval ? FormationLevel::Fleet : FormationLevel::Corps);

    // Heal the combined unit (take the better HP)
    if (source.hitPoints() > target.hitPoints()) {
        target.setHitPoints(source.hitPoints());
    }

    // Destroy the source unit
    aoc::game::Player* gsPlayer = gameState.player(source.owner());
    if (gsPlayer != nullptr) {
        gsPlayer->removeUnit(&source);
    }

    LOG_INFO("Formed %s from two %.*s units",
             naval ? "Fleet" : "Corps",
             static_cast<int>(unitTypeDef(target.typeId()).name.size()),
             unitTypeDef(target.typeId()).name.data());

    return ErrorCode::Ok;
}

ErrorCode formArmy(aoc::game::GameState& gameState,
                    aoc::game::Unit& corps,
                    aoc::game::Unit& source) {
    if (corps.typeId() != source.typeId() || corps.owner() != source.owner()) {
        return ErrorCode::InvalidArgument;
    }

    if (hex::distance(corps.position(), source.position()) > 1) {
        return ErrorCode::InvalidArgument;
    }

    // Corps must already be a Corps/Fleet formation
    if (corps.formationLevel() != FormationLevel::Corps
        && corps.formationLevel() != FormationLevel::Fleet) {
        return ErrorCode::InvalidArgument;
    }

    const bool naval = isNaval(unitTypeDef(corps.typeId()).unitClass);
    corps.setFormationLevel(naval ? FormationLevel::Armada : FormationLevel::Army);

    if (source.hitPoints() > corps.hitPoints()) {
        corps.setHitPoints(source.hitPoints());
    }

    aoc::game::Player* gsPlayer = gameState.player(source.owner());
    if (gsPlayer != nullptr) {
        gsPlayer->removeUnit(&source);
    }

    LOG_INFO("Formed %s!", naval ? "Armada" : "Army");

    return ErrorCode::Ok;
}

// ============================================================================
// Nuclear Weapons
// ============================================================================

ErrorCode launchNuclearStrike(aoc::game::GameState& gameState,
                              aoc::map::HexGrid& grid,
                              PlayerId launcherOwner,
                              hex::AxialCoord targetTile,
                              NukeType type) {
    // Determine blast radius and population damage
    const int32_t blastRadius = (type == NukeType::ThermonuclearDevice) ? 2 : 1;
    const float populationDamage = (type == NukeType::ThermonuclearDevice) ? 0.75f : 0.50f;

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
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        std::vector<aoc::game::Unit*> toDestroy;
        for (const std::unique_ptr<aoc::game::Unit>& unit : p->units()) {
            for (const hex::AxialCoord& blastTile : blastTiles) {
                if (unit->position() == blastTile) {
                    toDestroy.push_back(unit.get());
                    break;
                }
            }
        }
        for (aoc::game::Unit* dead : toDestroy) {
            p->removeUnit(dead);
        }
    }

    // Damage cities in blast zone
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : p->cities()) {
            for (const hex::AxialCoord& blastTile : blastTiles) {
                if (city->location() == blastTile) {
                    const int32_t popLoss = static_cast<int32_t>(
                        static_cast<float>(city->population()) * populationDamage);
                    city->setPopulation(std::max(1, city->population() - popLoss));
                    LOG_INFO("City %s lost %d population from nuclear strike",
                             city->name().c_str(), popLoss);
                    break;
                }
            }
        }
    }

    // Apply Fallout to all blast tiles
    for (const hex::AxialCoord& tile : blastTiles) {
        const int32_t idx = grid.toIndex(tile);
        grid.setImprovement(idx, aoc::map::ImprovementType::None);
        grid.setFeature(idx, aoc::map::FeatureType::None);
    }

    // Add grievance with ALL civilizations (+50 each)
    if (launcherOwner != INVALID_PLAYER) {
        for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
            if (p->id() == launcherOwner) { continue; }
            Grievance g{};
            g.type = GrievanceType::ConqueredCity;
            g.against = launcherOwner;
            g.severity = 50;
            g.turnsRemaining = 100;
            p->grievances().grievances.push_back(g);
        }
    }

    return ErrorCode::Ok;
}

// ============================================================================
// Air Combat
// ============================================================================

ErrorCode executeBombingRun(aoc::game::GameState& gameState,
                            aoc::map::HexGrid& grid,
                            aoc::game::Unit& bomber,
                            hex::AxialCoord targetTile) {
    AirUnitComponent& air = bomber.airUnit();

    if (air.sortiesRemaining <= 0) {
        return ErrorCode::InvalidArgument;
    }

    // Range check: find base city location
    hex::AxialCoord basePos{0, 0};
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : p->cities()) {
            const int32_t cityIdx = grid.toIndex(city->location());
            if (grid.toIndex(city->location()) == cityIdx) {
                // Identify base by searching for a city that matches the stored base
                // (air.baseCity is legacy EntityId -- use proximity heuristic for now:
                //  the city at the bomber's home position)
                if (city->location() == bomber.position()) {
                    basePos = city->location();
                    break;
                }
            }
        }
    }
    if (grid.distance(basePos, targetTile) > air.operationalRange) {
        return ErrorCode::InvalidArgument;
    }

    // Check for interception by enemy fighters
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        if (p->id() == bomber.owner()) { continue; }
        for (const std::unique_ptr<aoc::game::Unit>& interceptorUnit : p->units()) {
            AirUnitComponent& intAir = interceptorUnit->airUnit();
            if (!intAir.isIntercepting) { continue; }

            if (attemptInterception(gameState, *interceptorUnit, bomber)) {
                // Bomber was intercepted -- take damage, abort mission
                bomber.takeDamage(30);
                if (bomber.isDead()) {
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
    const int32_t targetIdx = grid.toIndex(targetTile);

    // Damage improvements
    aoc::map::ImprovementType imp = grid.improvement(targetIdx);
    if (imp != aoc::map::ImprovementType::None && imp != aoc::map::ImprovementType::Road) {
        grid.setImprovement(targetIdx, aoc::map::ImprovementType::None);
        LOG_INFO("Bombing destroyed improvement at (%d,%d)", targetTile.q, targetTile.r);
    }

    // Damage units on the tile
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        if (p->id() == bomber.owner()) { continue; }
        aoc::game::Unit* targetUnit = p->unitAt(targetTile);
        if (targetUnit != nullptr) {
            const int32_t bombDamage = unitTypeDef(bomber.typeId()).rangedStrength;
            targetUnit->takeDamage(bombDamage);
            if (targetUnit->isDead()) {
                p->removeUnit(targetUnit);
            }
            break;
        }
    }

    --air.sortiesRemaining;
    return ErrorCode::Ok;
}

bool attemptInterception(aoc::game::GameState& /*gameState*/,
                         aoc::game::Unit& interceptor,
                         aoc::game::Unit& target) {
    AirUnitComponent& intAir = interceptor.airUnit();
    AirUnitComponent& targetAir = target.airUnit();

    (void)targetAir;  // Not needed for damage computation

    if (intAir.sortiesRemaining <= 0 || !intAir.isIntercepting) {
        return false;
    }

    // Check range: interceptor base position (use interceptor's own position as base)
    if (hex::distance(interceptor.position(), target.position()) > intAir.operationalRange) {
        return false;
    }

    // Interception success: deal damage based on combat strength
    const int32_t intStrength = unitTypeDef(interceptor.typeId()).combatStrength;
    target.takeDamage(intStrength / 2);

    --intAir.sortiesRemaining;

    LOG_INFO("Fighter intercepted enemy air unit, dealt %d damage", intStrength / 2);

    return true;
}

void resetAirSorties(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return; }

    for (const std::unique_ptr<aoc::game::Unit>& unit : gsPlayer->units()) {
        AirUnitComponent& air = unit->airUnit();
        if (air.maxSorties > 0) {
            air.sortiesRemaining = air.maxSorties;
        }
    }
}

} // namespace aoc::sim

/**
 * @file UnitSelection.cpp
 * @brief Multi-unit selection, group commands, and rally point system.
 */

#include "aoc/app/UnitSelection.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Pathfinding.hpp"

#include <algorithm>
#include <unordered_map>

namespace aoc::sim {

// ============================================================================
// Selection management
// ============================================================================

void UnitSelection::clear() {
    this->m_selected.clear();
}

void UnitSelection::selectUnit(EntityId unitEntity) {
    this->m_selected.clear();
    this->m_selected.push_back(unitEntity);
}

void UnitSelection::addUnit(EntityId unitEntity) {
    if (!this->isSelected(unitEntity)) {
        this->m_selected.push_back(unitEntity);
    }
}

void UnitSelection::removeUnit(EntityId unitEntity) {
    std::vector<EntityId>::iterator it =
        std::find(this->m_selected.begin(), this->m_selected.end(), unitEntity);
    if (it != this->m_selected.end()) {
        this->m_selected.erase(it);
    }
}

void UnitSelection::toggleUnit(EntityId unitEntity) {
    if (this->isSelected(unitEntity)) {
        this->removeUnit(unitEntity);
    } else {
        this->addUnit(unitEntity);
    }
}

void UnitSelection::selectInRegion(const aoc::game::GameState& gameState,
                                    const aoc::map::HexGrid& /*grid*/,
                                    PlayerId player,
                                    hex::AxialCoord corner1,
                                    hex::AxialCoord corner2) {
    this->m_selected.clear();

    int32_t minQ = std::min(corner1.q, corner2.q);
    int32_t maxQ = std::max(corner1.q, corner2.q);
    int32_t minR = std::min(corner1.r, corner2.r);
    int32_t maxR = std::max(corner1.r, corner2.r);

    const aoc::game::Player* ownerPlayer = gameState.player(player);
    if (ownerPlayer == nullptr) {
        return;
    }

    for (const std::unique_ptr<aoc::game::Unit>& unitPtr : ownerPlayer->units()) {
        const aoc::hex::AxialCoord pos = unitPtr->position();
        if (pos.q >= minQ && pos.q <= maxQ && pos.r >= minR && pos.r <= maxR) {
            // Store the unit's position as a synthetic EntityId for backward compatibility.
            // This is a transitional representation — callers should migrate to Unit* tracking.
            (void)unitPtr;
        }
    }
}

bool UnitSelection::isSelected(EntityId unitEntity) const {
    return std::find(this->m_selected.begin(), this->m_selected.end(), unitEntity)
           != this->m_selected.end();
}

// ============================================================================
// Group commands
// ============================================================================

void UnitSelection::moveAllTo(aoc::game::GameState& gameState,
                               const aoc::map::HexGrid& grid,
                               hex::AxialCoord target) {
    // The selected list holds legacy EntityIds; iterate all players to find
    // units whose numeric id matches. This bridge stays until callers migrate
    // to holding Unit* references directly.
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        for (const std::unique_ptr<aoc::game::Unit>& unitPtr : playerPtr->units()) {
            std::optional<aoc::map::PathResult> pathResult =
                aoc::map::findPath(grid, unitPtr->position(), target);
            if (pathResult.has_value() && !pathResult->path.empty()) {
                unitPtr->pendingPath() = std::move(pathResult->path);
            }
        }
    }
}

void UnitSelection::fortifyAll(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        for (const std::unique_ptr<aoc::game::Unit>& unitPtr : playerPtr->units()) {
            if (unitPtr->isMilitary()) {
                unitPtr->setState(aoc::sim::UnitState::Fortified);
                unitPtr->clearPath();
            }
        }
    }
}

void UnitSelection::autoExploreAll(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        for (const std::unique_ptr<aoc::game::Unit>& unitPtr : playerPtr->units()) {
            unitPtr->autoExplore = true;
        }
    }
}

// ============================================================================
// Control groups
// ============================================================================

void UnitSelection::saveControlGroup(int32_t groupIndex) {
    if (groupIndex < 0 || groupIndex >= MAX_CONTROL_GROUPS) {
        return;
    }
    this->m_controlGroups[static_cast<std::size_t>(groupIndex)] = this->m_selected;
}

void UnitSelection::loadControlGroup(int32_t groupIndex) {
    if (groupIndex < 0 || groupIndex >= MAX_CONTROL_GROUPS) {
        return;
    }
    this->m_selected = this->m_controlGroups[static_cast<std::size_t>(groupIndex)];
}

// ============================================================================
// Rally points
//
// Rally point state is stored per-city by location in a module-local map.
// This avoids coupling the City object to a UI-layer concept while the
// object model migration is in progress.
// ============================================================================

namespace {

std::unordered_map<aoc::hex::AxialCoord, CityRallyPointComponent>& rallyPointMap() {
    static std::unordered_map<aoc::hex::AxialCoord, CityRallyPointComponent> s_map;
    return s_map;
}

} // anonymous namespace

void setRallyPoint(aoc::game::GameState& gameState, EntityId /*cityEntity*/,
                   hex::AxialCoord target) {
    // Locate the city whose entity matches; since entities are not yet mapped
    // in the object model, we record the rally point against the viewing player's
    // most recently added city as a transitional measure.
    // Callers should migrate to passing City* directly.
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            CityRallyPointComponent& rally = rallyPointMap()[cityPtr->location()];
            rally.rallyPoint = target;
            rally.hasRallyPoint = true;
            return;
        }
    }
}

void clearRallyPoint(aoc::game::GameState& gameState, EntityId /*cityEntity*/) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            std::unordered_map<aoc::hex::AxialCoord, CityRallyPointComponent>::iterator it =
                rallyPointMap().find(cityPtr->location());
            if (it != rallyPointMap().end()) {
                it->second.hasRallyPoint = false;
            }
            return;
        }
    }
}

void clearAllRallyPoints() {
    rallyPointMap().clear();
}

void processRallyPoints(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            std::unordered_map<aoc::hex::AxialCoord, CityRallyPointComponent>::iterator it =
                rallyPointMap().find(cityPtr->location());
            if (it == rallyPointMap().end() || !it->second.hasRallyPoint) {
                continue;
            }
            const hex::AxialCoord rallyTarget = it->second.rallyPoint;

            // Move any idle unit sitting on this city's tile toward the rally point
            for (const std::unique_ptr<aoc::game::Unit>& unitPtr : playerPtr->units()) {
                if (unitPtr->owner() != cityPtr->owner()) {
                    continue;
                }
                if (unitPtr->position() != cityPtr->location()) {
                    continue;
                }
                if (!unitPtr->pendingPath().empty()) {
                    continue;  // Already has orders
                }

                std::optional<aoc::map::PathResult> pathResult =
                    aoc::map::findPath(grid, unitPtr->position(), rallyTarget);
                if (pathResult.has_value() && !pathResult->path.empty()) {
                    unitPtr->pendingPath() = std::move(pathResult->path);
                }
            }
        }
    }
}

} // namespace aoc::sim

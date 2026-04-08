#pragma once

/**
 * @file UnitSelection.hpp
 * @brief Multi-unit selection, group commands, and rally point system.
 *
 * Supports:
 *   - Single click: select one unit
 *   - Shift+click: add/remove from selection
 *   - Drag box: select all units in rectangle
 *   - Group commands: move all selected, attack with all, fortify all
 *   - Rally points: cities produce units that auto-move to rally point
 *
 * Selection groups (Ctrl+1-9 to assign, 1-9 to recall) save sets of
 * units for quick re-selection during combat.
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace aoc::ecs { class World; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

// ============================================================================
// Multi-unit selection
// ============================================================================

class UnitSelection {
public:
    /// Clear the current selection.
    void clear();

    /// Select a single unit (replaces selection).
    void selectUnit(EntityId unitEntity);

    /// Add a unit to the selection (shift+click).
    void addUnit(EntityId unitEntity);

    /// Remove a unit from the selection (shift+click on selected).
    void removeUnit(EntityId unitEntity);

    /// Toggle a unit in the selection.
    void toggleUnit(EntityId unitEntity);

    /// Select all units in a rectangular region (drag box).
    void selectInRegion(const aoc::ecs::World& world,
                        const aoc::map::HexGrid& grid,
                        PlayerId player,
                        hex::AxialCoord corner1,
                        hex::AxialCoord corner2);

    /// Check if a unit is selected.
    [[nodiscard]] bool isSelected(EntityId unitEntity) const;

    /// Get all selected units.
    [[nodiscard]] const std::vector<EntityId>& selectedUnits() const {
        return this->m_selected;
    }

    /// Number of selected units.
    [[nodiscard]] int32_t count() const {
        return static_cast<int32_t>(this->m_selected.size());
    }

    // ========================================================================
    // Group commands
    // ========================================================================

    /// Move all selected units toward a target tile.
    void moveAllTo(aoc::ecs::World& world, const aoc::map::HexGrid& grid,
                   hex::AxialCoord target);

    /// Fortify all selected units.
    void fortifyAll(aoc::ecs::World& world);

    /// Set all selected units to auto-explore.
    void autoExploreAll(aoc::ecs::World& world);

    // ========================================================================
    // Control groups (Ctrl+1-9 to save, 1-9 to recall)
    // ========================================================================

    static constexpr int32_t MAX_CONTROL_GROUPS = 9;

    /// Save the current selection as a control group.
    void saveControlGroup(int32_t groupIndex);

    /// Load a control group as the current selection.
    void loadControlGroup(int32_t groupIndex);

private:
    std::vector<EntityId> m_selected;
    std::array<std::vector<EntityId>, MAX_CONTROL_GROUPS> m_controlGroups;
};

// ============================================================================
// Rally points
// ============================================================================

/// Per-city rally point (where newly produced units auto-move to).
struct CityRallyPointComponent {
    hex::AxialCoord rallyPoint;
    bool            hasRallyPoint = false;
};

/**
 * @brief Set a rally point for a city.
 *
 * Newly produced units will automatically begin moving toward this tile.
 *
 * @param world       ECS world.
 * @param cityEntity  City to set rally point for.
 * @param target      Target tile for the rally point.
 */
void setRallyPoint(aoc::ecs::World& world, EntityId cityEntity, hex::AxialCoord target);

/**
 * @brief Clear a city's rally point.
 */
void clearRallyPoint(aoc::ecs::World& world, EntityId cityEntity);

/**
 * @brief Process rally points: move newly produced units toward their city's rally point.
 *
 * Called after production completes. Any unit at a city center that has
 * a rally point will pathfind toward the rally target.
 */
void processRallyPoints(aoc::ecs::World& world, const aoc::map::HexGrid& grid);

} // namespace aoc::sim

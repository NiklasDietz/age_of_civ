#pragma once

/**
 * @file TechGating.hpp
 * @brief Helper functions for checking tech prerequisites on buildable items.
 *
 * Provides utility functions to determine whether a player can build specific
 * units, buildings, wonders, or districts based on their researched technologies
 * and city state.
 */

#include "aoc/core/Types.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace aoc::game {
class GameState;
class City;
}
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

/// A single item that a city can produce.
struct BuildableItem {
    ProductionItemType type;
    uint16_t           id;
    std::string_view   name;
    float              cost;
    bool               locked = false;        ///< true = greyed out (prereq unmet)
    uint8_t            lockReason = 0;        ///< WonderLockReason cast as uint8 (0 = None)
};

/// Check if a player can build a specific unit type.
[[nodiscard]] bool canBuildUnit(const aoc::game::GameState& gameState, PlayerId player, UnitTypeId unitType);

/// Check if a player can build a specific building in a city.
/// When `grid` is provided, spatial prerequisites (e.g. adjacent GeothermalVent
/// tile for the GeothermalPlant building) are enforced. With `grid == nullptr`
/// only tech + district gates are checked (UI listing fallback).
[[nodiscard]] bool canBuildBuilding(const aoc::game::GameState& gameState, PlayerId player,
                                    const aoc::game::City& city, BuildingId buildingId,
                                    const aoc::map::HexGrid* grid = nullptr);

/// Check if a player can build a specific wonder.
[[nodiscard]] bool canBuildWonder(const aoc::game::GameState& gameState, PlayerId player, uint8_t wonderId);

/// Detailed wonder availability check returning the specific lock reason.
/// Considers tech, civic, adjacency requirements + already-built status.
[[nodiscard]] uint8_t wonderLockReason(const aoc::game::GameState& gameState,
                                        PlayerId player,
                                        const aoc::game::City& city,
                                        uint8_t wonderId,
                                        const aoc::map::HexGrid* grid = nullptr);

/// Detailed building availability: checks tech, civic, district, spatial,
/// resource. Returns BuildLockReason cast as uint8_t.
[[nodiscard]] uint8_t buildingLockReason(const aoc::game::GameState& gameState,
                                          PlayerId player,
                                          const aoc::game::City& city,
                                          BuildingId buildingId,
                                          const aoc::map::HexGrid* grid = nullptr);

/// Detailed district availability: stage, presence, adjacency.
[[nodiscard]] uint8_t districtLockReason(const aoc::game::GameState& gameState,
                                          PlayerId player,
                                          const aoc::game::City& city,
                                          uint8_t districtTypeIdx,
                                          const aoc::map::HexGrid* grid = nullptr);

/// Get all buildable items for a city (units + buildings + wonders + districts).
[[nodiscard]] std::vector<BuildableItem> getBuildableItems(const aoc::game::GameState& gameState,
                                                           PlayerId player,
                                                           const aoc::game::City& city);

// Legacy EntityId overloads for code not yet migrated from ECS iteration
[[nodiscard]] bool canBuildBuilding(const aoc::game::GameState& gameState, PlayerId player,
                                    EntityId cityEntity, BuildingId buildingId);
[[nodiscard]] std::vector<BuildableItem> getBuildableItems(const aoc::game::GameState& gameState,
                                                           PlayerId player, EntityId cityEntity);

} // namespace aoc::sim

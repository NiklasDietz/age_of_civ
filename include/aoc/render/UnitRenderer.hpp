#pragma once

/**
 * @file UnitRenderer.hpp
 * @brief Renders units and cities as simple shapes on the hex map.
 */

#include "aoc/map/HexCoord.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>
#include <vector>

namespace vulkan_app::renderer {
class Renderer2D;
}

namespace aoc::game {
class GameState;
}

namespace aoc::map {
class FogOfWar;
class HexGrid;
}

namespace aoc::render {

class CameraController;

class UnitRenderer {
public:
    UnitRenderer() = default;

    /// Draw all units visible in the viewport.
    void drawUnits(vulkan_app::renderer::Renderer2D& renderer2d,
                   const aoc::game::GameState& gameState,
                   const aoc::map::FogOfWar& fog,
                   const aoc::map::HexGrid& grid,
                   PlayerId viewingPlayer,
                   const CameraController& camera,
                   float hexSize,
                   uint32_t screenWidth, uint32_t screenHeight) const;

    /// Draw all cities visible in the viewport.
    void drawCities(vulkan_app::renderer::Renderer2D& renderer2d,
                    const aoc::game::GameState& gameState,
                    const aoc::map::FogOfWar& fog,
                    const aoc::map::HexGrid& grid,
                    PlayerId viewingPlayer,
                    const CameraController& camera,
                    float hexSize,
                    uint32_t screenWidth, uint32_t screenHeight) const;

    /// Draw movement path for the selected unit.
    void drawPath(vulkan_app::renderer::Renderer2D& renderer2d,
                  const std::vector<hex::AxialCoord>& path,
                  float hexSize) const;

    /// Draw reachable tile highlights.
    void drawReachable(vulkan_app::renderer::Renderer2D& renderer2d,
                       const std::vector<hex::AxialCoord>& tiles,
                       float hexSize) const;

    /// Draw a translucent range indicator for ranged units with enemy highlights.
    void drawRangedRange(vulkan_app::renderer::Renderer2D& renderer2d,
                         const aoc::game::GameState& gameState,
                         hex::AxialCoord center, int32_t range,
                         PlayerId unitOwner, float hexSize) const;

    /// Selected entity (NULL_ENTITY if nothing selected).
    EntityId selectedEntity = NULL_ENTITY;
};

} // namespace aoc::render

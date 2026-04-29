#pragma once

/**
 * @file Minimap.hpp
 * @brief Small overview map rendered in screen space showing terrain, ownership,
 *        and camera viewport.
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <vector>

namespace vulkan_app::renderer {
class Renderer2D;
}

namespace aoc::map {
class HexGrid;
class FogOfWar;
}

namespace aoc::render {

class CameraController;

class Minimap {
public:
    Minimap() = default;

    /// Screen-space rectangle for the minimap. Computed identically by
    /// the draw path and the click handler so big-map dynamic sizing
    /// can't drift between the two.
    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    /// Compute the bottom-left minimap rectangle given the grid + the
    /// current framebuffer height. Aspect ratio matches the grid; height
    /// scales with map area (130 px floor, 240 px ceiling).
    [[nodiscard]] static Rect computeRect(const aoc::map::HexGrid& grid,
                                          uint32_t screenHeight);

    /**
     * @brief Draw the minimap overlay in screen space.
     *
     * Renders a small rectangle showing terrain colors, player ownership tints,
     * and a white viewport rectangle indicating the current camera view.
     *
     * @param renderer2d  The 2D renderer (must be in screen-space mode).
     * @param grid        The hex grid with terrain data.
     * @param fog         Fog-of-war state for the viewing player.
     * @param player      The viewing player ID.
     * @param camera      Current camera state (for viewport rectangle).
     * @param mapX        Minimap screen-space X position.
     * @param mapY        Minimap screen-space Y position.
     * @param mapW        Minimap width in pixels.
     * @param mapH        Minimap height in pixels.
     * @param screenWidth  Current framebuffer width.
     * @param screenHeight Current framebuffer height.
     */
    void draw(vulkan_app::renderer::Renderer2D& renderer2d,
              const aoc::map::HexGrid& grid,
              const aoc::map::FogOfWar& fog,
              PlayerId player,
              const CameraController& camera,
              float mapX, float mapY, float mapW, float mapH,
              uint32_t screenWidth, uint32_t screenHeight) const;

    /**
     * @brief Test if a screen-space click is inside the minimap.
     * @return true if the click is within the minimap bounds.
     */
    [[nodiscard]] bool containsPoint(float screenX, float screenY,
                                     float mapX, float mapY,
                                     float mapW, float mapH) const;

    /**
     * @brief Convert a minimap click to world coordinates.
     *
     * @param screenX     Click screen X.
     * @param screenY     Click screen Y.
     * @param mapX        Minimap position X.
     * @param mapY        Minimap position Y.
     * @param mapW        Minimap width.
     * @param mapH        Minimap height.
     * @param grid        The hex grid (for world dimensions).
     * @param hexSize     Hex outer radius.
     * @param outWorldX   Output world X coordinate.
     * @param outWorldY   Output world Y coordinate.
     */
    void screenToWorld(float screenX, float screenY,
                       float mapX, float mapY, float mapW, float mapH,
                       const aoc::map::HexGrid& grid, float hexSize,
                       float& outWorldX, float& outWorldY) const;

    /// Pip data for overlay rendering (units and cities on the minimap).
    struct MinimapPip {
        hex::AxialCoord position;
        PlayerId owner = 0;
        bool isCity = false;  ///< true = city marker, false = unit pip
    };

    /**
     * @brief Draw unit pips and city markers on the minimap.
     *
     * Call after draw() to overlay unit/city positions.
     * The caller builds the pip list from ECS data.
     */
    void drawOverlays(vulkan_app::renderer::Renderer2D& renderer2d,
                      const aoc::map::HexGrid& grid,
                      const std::vector<MinimapPip>& pips,
                      float mapX, float mapY, float mapW, float mapH) const;
};

} // namespace aoc::render

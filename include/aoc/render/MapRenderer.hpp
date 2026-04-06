#pragma once

/**
 * @file MapRenderer.hpp
 * @brief Renders the hex tile map using Renderer2D with real terrain data.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace vulkan_app::renderer {
class Renderer2D;
}

namespace aoc::map {
class HexGrid;
class FogOfWar;
}

namespace aoc::render {

class CameraController;
class DrawCommandBuffer;

class MapRenderer {
public:
    MapRenderer() = default;

    /**
     * @brief Draw the hex grid terrain, features, and rivers.
     *
     * Only draws tiles visible within the current camera viewport for
     * performance. Uses terrain colors from Terrain.hpp.
     */
    void draw(vulkan_app::renderer::Renderer2D& renderer2d,
              const aoc::map::HexGrid& grid,
              const aoc::map::FogOfWar& fog,
              PlayerId viewingPlayer,
              const CameraController& camera,
              uint32_t screenWidth, uint32_t screenHeight) const;

    /**
     * @brief Draw colored border lines around territory edges.
     *
     * For each owned tile, checks its 6 neighbors. If a neighbor is unowned
     * or owned by a different player, draws a colored line along that hex edge
     * using the owner's player color.
     */
    void drawTerritoryBorders(vulkan_app::renderer::Renderer2D& renderer2d,
                               const aoc::map::HexGrid& grid,
                               const CameraController& camera,
                               uint32_t screenWidth, uint32_t screenHeight) const;

    /**
     * @brief Draw map tiles to a DrawCommandBuffer for deferred rendering.
     *
     * Same as draw() but pushes commands to the buffer instead of calling
     * Renderer2D directly. The caller can then sort and flush.
     */
    void drawToBuffer(DrawCommandBuffer& buffer,
                      const aoc::map::HexGrid& grid,
                      const aoc::map::FogOfWar& fog,
                      PlayerId viewingPlayer,
                      const CameraController& camera,
                      uint32_t screenWidth, uint32_t screenHeight) const;

    /// Set hex outer radius (pixels at zoom 1.0).
    void setHexSize(float size) { this->m_hexSize = size; }
    [[nodiscard]] float hexSize() const { return this->m_hexSize; }

private:
    void drawTile(vulkan_app::renderer::Renderer2D& renderer2d,
                  const aoc::map::HexGrid& grid,
                  int32_t tileIndex, float cx, float cy,
                  bool dimmed = false) const;

    void drawRiverEdges(vulkan_app::renderer::Renderer2D& renderer2d,
                        uint8_t riverMask, float cx, float cy) const;

    float m_hexSize = 30.0f;
};

} // namespace aoc::render

#pragma once

/**
 * @file Tooltip.hpp
 * @brief Hover tooltip showing tile, unit, and city information.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <string>

namespace vulkan_app::renderer {
class Renderer2D;
}

namespace aoc::ecs {
class World;
}

namespace aoc::map {
class HexGrid;
class FogOfWar;
}

namespace aoc::render {
class CameraController;
}

namespace aoc::ui {

class TooltipManager {
public:
    /**
     * @brief Update tooltip based on mouse position and game state.
     *
     * Converts the mouse screen position to a world hex coordinate,
     * then builds a multi-line text description of the tile contents.
     *
     * @param mouseX    Mouse screen X.
     * @param mouseY    Mouse screen Y.
     * @param world     ECS world for unit/city queries.
     * @param grid      Hex grid for terrain/yield data.
     * @param camera    Camera controller for screen-to-world conversion.
     * @param fog       Fog-of-war for visibility checks.
     * @param player    Viewing player ID.
     * @param screenW   Framebuffer width.
     * @param screenH   Framebuffer height.
     */
    void update(float mouseX, float mouseY,
                const aoc::ecs::World& world,
                const aoc::map::HexGrid& grid,
                const aoc::render::CameraController& camera,
                const aoc::map::FogOfWar& fog,
                PlayerId player,
                uint32_t screenW, uint32_t screenH,
                EntityId selectedEntity = NULL_ENTITY);

    /**
     * @brief Render the tooltip if visible.
     *
     * Draws a dark background panel with multi-line text. Must be called
     * in screen-space rendering mode (after resetCamera).
     *
     * @param renderer2d The 2D renderer in screen-space mode.
     */
    void render(vulkan_app::renderer::Renderer2D& renderer2d) const;

    [[nodiscard]] bool isVisible() const { return this->m_visible; }
    [[nodiscard]] float getX() const { return this->m_x; }
    [[nodiscard]] float getY() const { return this->m_y; }
    void setPosition(float x, float y) { this->m_x = x; this->m_y = y; }
    void setRenderScale(float scale) { this->m_renderScale = scale; }

private:
    bool        m_visible = false;
    float       m_x = 0.0f;
    float       m_y = 0.0f;
    float       m_renderScale = 1.0f;  ///< Scale for world-space rendering (invZoom).
    std::string m_text;
    float       m_showDelay = 0.0f;  ///< Accumulated hover frames before showing.

    static constexpr float SHOW_DELAY_FRAMES = 5.0f;  ///< Frames to hover before showing (reduced for responsiveness)
    static constexpr float TOOLTIP_OFFSET_X  = 15.0f;
    static constexpr float TOOLTIP_OFFSET_Y  = 15.0f;
    static constexpr float FONT_SIZE         = 11.0f;
    static constexpr float LINE_HEIGHT       = 14.0f;
    static constexpr float PADDING           = 6.0f;
};

} // namespace aoc::ui

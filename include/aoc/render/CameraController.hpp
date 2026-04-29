#pragma once

/**
 * @file CameraController.hpp
 * @brief 2D camera with pan (WASD / middle-mouse drag) and zoom (scroll wheel).
 *
 * Drives Renderer2D::setCamera() and setZoom(). Provides screen-to-world
 * and world-to-screen coordinate conversions for hex picking.
 */

#include <cstdint>

namespace aoc::app {
class InputManager;
}

namespace aoc::render {

class CameraController {
public:
    struct Config {
        float panSpeed    = 500.0f;   ///< World units per second at zoom 1.0
        float zoomSpeed   = 0.15f;    ///< Zoom multiplier per scroll notch
        float minZoom     = 0.35f;  // hex stays ~7 px at hexSize=20
        float maxZoom     = 5.0f;
        float dragSpeed   = 1.0f;     ///< Middle-mouse drag multiplier
    };

    CameraController();
    explicit CameraController(const Config& config);

    /**
     * @brief Update camera based on input. Call once per frame.
     * @param input  The input manager (for keys, mouse, scroll).
     * @param deltaTime  Seconds since last frame.
     * @param screenWidth  Current framebuffer width.
     * @param screenHeight Current framebuffer height.
     */
    void update(const aoc::app::InputManager& input, float deltaTime,
                uint32_t screenWidth, uint32_t screenHeight,
                bool suppressEdgeScroll = false);

    [[nodiscard]] float cameraX() const { return this->m_cameraX; }
    [[nodiscard]] float cameraY() const { return this->m_cameraY; }
    [[nodiscard]] float zoom() const    { return this->m_zoom; }

    /// Convert screen pixel coordinates to world coordinates.
    void screenToWorld(double screenX, double screenY,
                       float& worldX, float& worldY,
                       uint32_t screenWidth, uint32_t screenHeight) const;

    /// Convert world coordinates to screen pixel coordinates.
    void worldToScreen(float worldX, float worldY,
                       double& screenX, double& screenY,
                       uint32_t screenWidth, uint32_t screenHeight) const;

    void setPosition(float x, float y) { this->m_cameraX = x; this->m_cameraY = y; }
    void setZoom(float z) { this->m_zoom = z; }
    void setMinZoom(float z) { this->m_config.minZoom = z; }
    void setMaxZoom(float z) { this->m_config.maxZoom = z; }
    [[nodiscard]] float minZoom() const { return this->m_config.minZoom; }

    /// Set world width for cylindrical wrapping (0 = no wrapping).
    void setWorldWidth(float w) { this->m_worldWidth = w; }
    [[nodiscard]] float worldWidth() const { return this->m_worldWidth; }

private:
    Config m_config;
    float  m_cameraX    = 0.0f;
    float  m_cameraY    = 0.0f;
    float  m_zoom       = 1.0f;
    float  m_worldWidth = 0.0f;  ///< >0 when cylindrical wrapping is active
};

} // namespace aoc::render

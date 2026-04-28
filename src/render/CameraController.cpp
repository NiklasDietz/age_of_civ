/**
 * @file CameraController.cpp
 * @brief Camera pan/zoom logic driven by InputManager.
 */

#include "aoc/render/CameraController.hpp"
#include "aoc/app/InputManager.hpp"
#include "aoc/app/InputActions.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace aoc::render {

CameraController::CameraController()
    : m_config{}
{
}

CameraController::CameraController(const Config& config)
    : m_config(config)
{
}

void CameraController::update(const aoc::app::InputManager& input, float deltaTime,
                               uint32_t screenWidth, uint32_t screenHeight) {
    // --- Keyboard pan (WASD) ---
    const float panAmount = this->m_config.panSpeed * deltaTime / this->m_zoom;

    if (input.isActionHeld(aoc::app::InputAction::PanLeft)) {
        this->m_cameraX -= panAmount;
    }
    if (input.isActionHeld(aoc::app::InputAction::PanRight)) {
        this->m_cameraX += panAmount;
    }
    if (input.isActionHeld(aoc::app::InputAction::PanUp)) {
        this->m_cameraY -= panAmount;
    }
    if (input.isActionHeld(aoc::app::InputAction::PanDown)) {
        this->m_cameraY += panAmount;
    }

    // --- Mouse edge scrolling ---
    // Move camera when cursor enters edge zone (Civ-style). Zone width
    // 16 pixels. Speed ramps to full panSpeed at 0 px from edge.
    {
        constexpr float EDGE_ZONE = 16.0f;
        const float mx = static_cast<float>(input.mouseX());
        const float my = static_cast<float>(input.mouseY());
        const float w = static_cast<float>(screenWidth);
        const float h = static_cast<float>(screenHeight);
        // Only scroll while cursor is inside the window.
        if (mx >= 0.0f && mx <= w && my >= 0.0f && my <= h) {
            float dx = 0.0f, dy = 0.0f;
            if (mx < EDGE_ZONE) {
                dx = -(EDGE_ZONE - mx) / EDGE_ZONE;
            } else if (mx > w - EDGE_ZONE) {
                dx = (mx - (w - EDGE_ZONE)) / EDGE_ZONE;
            }
            if (my < EDGE_ZONE) {
                dy = -(EDGE_ZONE - my) / EDGE_ZONE;
            } else if (my > h - EDGE_ZONE) {
                dy = (my - (h - EDGE_ZONE)) / EDGE_ZONE;
            }
            this->m_cameraX += dx * panAmount;
            this->m_cameraY += dy * panAmount;
        }
    }

    // --- Right-mouse or middle-mouse drag pan ---
    if (input.isMouseButtonHeld(GLFW_MOUSE_BUTTON_RIGHT) ||
        input.isMouseButtonHeld(GLFW_MOUSE_BUTTON_MIDDLE)) {
        const float dragScale = this->m_config.dragSpeed / this->m_zoom;
        this->m_cameraX -= static_cast<float>(input.mouseDeltaX()) * dragScale;
        this->m_cameraY -= static_cast<float>(input.mouseDeltaY()) * dragScale;
    }

    // --- Scroll wheel zoom ---
    const double scroll = input.scrollDelta();
    if (std::abs(scroll) > 0.01) {
        const float factor = 1.0f + this->m_config.zoomSpeed * static_cast<float>(scroll);
        this->m_zoom *= factor;
        this->m_zoom = std::clamp(this->m_zoom, this->m_config.minZoom, this->m_config.maxZoom);
    }

    // --- Keyboard zoom (+/-) ---
    if (input.isActionHeld(aoc::app::InputAction::ZoomIn)) {
        this->m_zoom *= 1.0f + this->m_config.zoomSpeed * 2.0f * deltaTime;
        this->m_zoom = std::min(this->m_zoom, this->m_config.maxZoom);
    }
    if (input.isActionHeld(aoc::app::InputAction::ZoomOut)) {
        this->m_zoom *= 1.0f - this->m_config.zoomSpeed * 2.0f * deltaTime;
        this->m_zoom = std::max(this->m_zoom, this->m_config.minZoom);
    }

    // --- Cylindrical wrapping: wrap camera X within [0, worldWidth) ---
    if (this->m_worldWidth > 0.0f) {
        this->m_cameraX = std::fmod(this->m_cameraX, this->m_worldWidth);
        if (this->m_cameraX < 0.0f) {
            this->m_cameraX += this->m_worldWidth;
        }
    }
}

void CameraController::screenToWorld(double screenX, double screenY,
                                      float& worldX, float& worldY,
                                      uint32_t screenWidth, uint32_t screenHeight) const {
    // The Renderer2D shader maps world to screen as:
    //   viewPos = (worldPos - cameraTopLeft) * zoom
    //   ndc = viewPos / screenSize * 2.0 - 1.0
    // Where cameraTopLeft = m_camera - screenSize / (2 * zoom)
    //
    // Inverting: worldPos = screenPos / zoom + cameraTopLeft
    //          = screenPos / zoom + m_camera - screenSize / (2 * zoom)
    //          = (screenPos - screenSize/2) / zoom + m_camera
    const float halfW = static_cast<float>(screenWidth) * 0.5f;
    const float halfH = static_cast<float>(screenHeight) * 0.5f;

    worldX = (static_cast<float>(screenX) - halfW) / this->m_zoom + this->m_cameraX;
    worldY = (static_cast<float>(screenY) - halfH) / this->m_zoom + this->m_cameraY;
}

void CameraController::worldToScreen(float worldX, float worldY,
                                      double& screenX, double& screenY,
                                      uint32_t screenWidth, uint32_t screenHeight) const {
    const float halfW = static_cast<float>(screenWidth) * 0.5f;
    const float halfH = static_cast<float>(screenHeight) * 0.5f;

    screenX = static_cast<double>((worldX - this->m_cameraX) * this->m_zoom + halfW);
    screenY = static_cast<double>((worldY - this->m_cameraY) * this->m_zoom + halfH);
}

} // namespace aoc::render

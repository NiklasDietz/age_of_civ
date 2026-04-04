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
                               uint32_t /*screenWidth*/, uint32_t /*screenHeight*/) {
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

    // --- Middle-mouse drag pan ---
    if (input.isMouseButtonHeld(GLFW_MOUSE_BUTTON_MIDDLE)) {
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
}

void CameraController::screenToWorld(double screenX, double screenY,
                                      float& worldX, float& worldY,
                                      uint32_t screenWidth, uint32_t screenHeight) const {
    // Screen center is the camera position in world space.
    // Renderer2D uses: screenPos = (worldPos - camera) * zoom + screenCenter
    // So: worldPos = (screenPos - screenCenter) / zoom + camera
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

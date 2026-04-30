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
                               uint32_t screenWidth, uint32_t screenHeight,
                               bool suppressEdgeScroll) {
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
    // Tight 4 px trigger zone right at the window edge. Earlier wider
    // zones (48/16 px) fired when the cursor merely approached the
    // bottom of the HUD or a button, sliding the map under the user
    // mid-action. Only the outermost rim should scroll. Speed boost
    // keeps the scroll responsive once committed.
    if (!suppressEdgeScroll) {
        constexpr float EDGE_ZONE  = 4.0f;
        constexpr float RAMP_BAND  = 4.0f;
        constexpr float EDGE_BOOST = 1.5f;
        const float mx = static_cast<float>(input.mouseX());
        const float my = static_cast<float>(input.mouseY());
        const float w = static_cast<float>(screenWidth);
        const float h = static_cast<float>(screenHeight);
        const auto edgeCoeff = [](float dist) -> float {
            // dist = pixels into zone (0 at outer boundary, EDGE_ZONE at edge).
            if (dist <= 0.0f)        { return 0.0f; }
            if (dist >= RAMP_BAND)   { return 1.0f; }
            return dist / RAMP_BAND;
        };
        if (mx >= 0.0f && mx <= w && my >= 0.0f && my <= h) {
            float dx = 0.0f, dy = 0.0f;
            if (mx < EDGE_ZONE) {
                dx = -edgeCoeff(EDGE_ZONE - mx);
            } else if (mx > w - EDGE_ZONE) {
                dx = edgeCoeff(mx - (w - EDGE_ZONE));
            }
            if (my < EDGE_ZONE) {
                dy = -edgeCoeff(EDGE_ZONE - my);
            } else if (my > h - EDGE_ZONE) {
                dy = edgeCoeff(my - (h - EDGE_ZONE));
            }
            this->m_cameraX += dx * panAmount * EDGE_BOOST;
            this->m_cameraY += dy * panAmount * EDGE_BOOST;
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

    // Per-frame zoom clamp: even when the user isn't actively scrolling
    // (e.g. game started before minZoom was raised, or saved-state loaded
    // a sub-floor zoom), pin zoom into [minZoom, maxZoom] so the map
    // never renders at the broken sub-pixel scale.
    this->m_zoom = std::clamp(this->m_zoom,
                               this->m_config.minZoom, this->m_config.maxZoom);

    // --- Cylindrical wrapping: wrap camera X within [0, worldWidth) ---
    if (this->m_worldWidth > 0.0f) {
        this->m_cameraX = std::fmod(this->m_cameraX, this->m_worldWidth);
        if (this->m_cameraX < 0.0f) {
            this->m_cameraX += this->m_worldWidth;
        }
    } else if (this->m_worldHeight > 0.0f) {
        // Non-cylindrical X clamp. Allow camera to pan up to half a screen
        // past the world edge — beyond that, less than half the screen
        // would show tiles.
        const float halfWView = static_cast<float>(screenWidth)
            / (2.0f * this->m_zoom);
        this->m_cameraX = std::clamp(this->m_cameraX,
            -halfWView * 0.5f,
            this->m_worldWidth + halfWView * 0.5f);
    }

    // --- Vertical clamp: keep at least half the screen on tiles ---
    // halfHView is the world-space half-height visible at current zoom.
    // Clamp m_cameraY so the world overlaps the screen by at least half:
    //   cameraY >= -halfHView/2  (top of map sits at most 1/4 from top)
    //   cameraY <= worldH + halfHView/2
    if (this->m_worldHeight > 0.0f) {
        const float halfHView = static_cast<float>(screenHeight)
            / (2.0f * this->m_zoom);
        this->m_cameraY = std::clamp(this->m_cameraY,
            -halfHView * 0.5f,
            this->m_worldHeight + halfHView * 0.5f);
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

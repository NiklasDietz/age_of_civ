#pragma once

/**
 * @file InputManager.hpp
 * @brief Collects raw GLFW input and translates it to game actions.
 *
 * Tracks per-frame pressed/released/held state for both raw keys and
 * mapped game actions. Also tracks mouse position and scroll delta.
 */

#include "aoc/app/InputActions.hpp"

#include <array>
#include <cstdint>

namespace aoc::app {

class Window;

class InputManager {
public:
    InputManager();

    /// Bind GLFW callbacks to the given window. Call once after window creation.
    void bindToWindow(Window& window);

    /// Call at the start of each frame to update pressed/released state.
    void processFrame();

    // ========================================================================
    // Action queries (mapped from keys via default bindings)
    // ========================================================================

    [[nodiscard]] bool isActionPressed(InputAction action) const;
    [[nodiscard]] bool isActionHeld(InputAction action) const;
    [[nodiscard]] bool isActionReleased(InputAction action) const;

    // ========================================================================
    // Raw key queries (GLFW key codes)
    // ========================================================================

    [[nodiscard]] bool isKeyPressed(int glfwKey) const;
    [[nodiscard]] bool isKeyHeld(int glfwKey) const;
    [[nodiscard]] bool isKeyReleased(int glfwKey) const;

    // ========================================================================
    // Mouse
    // ========================================================================

    [[nodiscard]] double mouseX() const { return this->m_mouseX; }
    [[nodiscard]] double mouseY() const { return this->m_mouseY; }
    [[nodiscard]] double mouseDeltaX() const { return this->m_mouseDeltaX; }
    [[nodiscard]] double mouseDeltaY() const { return this->m_mouseDeltaY; }
    [[nodiscard]] double scrollDelta() const { return this->m_scrollDelta; }

    [[nodiscard]] bool isMouseButtonPressed(int button) const;
    [[nodiscard]] bool isMouseButtonHeld(int button) const;
    [[nodiscard]] bool isMouseButtonReleased(int button) const;

private:
    void onKey(int key, int scancode, int action, int mods);
    void onMouseButton(int button, int action, int mods);
    void onCursorPos(double x, double y);
    void onScroll(double xOffset, double yOffset);

    void setupDefaultBindings();

    // Key state: current frame and previous frame
    static constexpr int MAX_KEYS = 512;
    std::array<bool, MAX_KEYS> m_keyCurrent{};
    std::array<bool, MAX_KEYS> m_keyPrevious{};

    // Mouse button state
    static constexpr int MAX_MOUSE_BUTTONS = 8;
    std::array<bool, MAX_MOUSE_BUTTONS> m_mouseButtonCurrent{};
    std::array<bool, MAX_MOUSE_BUTTONS> m_mouseButtonPrevious{};

    // Mouse position
    double m_mouseX      = 0.0;
    double m_mouseY      = 0.0;
    double m_prevMouseX  = 0.0;
    double m_prevMouseY  = 0.0;
    double m_mouseDeltaX = 0.0;
    double m_mouseDeltaY = 0.0;

    // Scroll
    double m_scrollDelta     = 0.0;
    double m_scrollAccum     = 0.0;  ///< Accumulated between frames

    // Action -> key mapping (simple: one key per action)
    static constexpr uint8_t ACTION_COUNT = static_cast<uint8_t>(InputAction::Count);
    std::array<int, ACTION_COUNT> m_actionToKey{};
};

} // namespace aoc::app

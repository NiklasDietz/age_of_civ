/**
 * @file InputManager.cpp
 * @brief Input collection and action mapping implementation.
 */

#include "aoc/app/InputManager.hpp"
#include "aoc/app/Window.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace aoc::app {

InputManager::InputManager() {
    this->m_keyCurrent.fill(false);
    this->m_keyPrevious.fill(false);
    this->m_mouseButtonCurrent.fill(false);
    this->m_mouseButtonPrevious.fill(false);
    this->m_actionToKey.fill(-1);
    this->setupDefaultBindings();
}

void InputManager::bindToWindow(Window& window) {
    window.setKeyCallback([this](int key, int scancode, int action, int mods) {
        this->onKey(key, scancode, action, mods);
    });
    window.setMouseButtonCallback([this](int button, int action, int mods) {
        this->onMouseButton(button, action, mods);
    });
    window.setCursorPosCallback([this](double x, double y) {
        this->onCursorPos(x, y);
    });
    window.setScrollCallback([this](double xOffset, double yOffset) {
        this->onScroll(xOffset, yOffset);
    });
}

void InputManager::processFrame() {
    this->m_keyPrevious = this->m_keyCurrent;
    this->m_mouseButtonPrevious = this->m_mouseButtonCurrent;

    this->m_mouseDeltaX = this->m_mouseX - this->m_prevMouseX;
    this->m_mouseDeltaY = this->m_mouseY - this->m_prevMouseY;
    this->m_prevMouseX  = this->m_mouseX;
    this->m_prevMouseY  = this->m_mouseY;

    this->m_scrollDelta = this->m_scrollAccum;
    this->m_scrollAccum = 0.0;
}

// ============================================================================
// Action queries
// ============================================================================

bool InputManager::isActionPressed(InputAction action) const {
    int key = this->m_actionToKey[static_cast<uint8_t>(action)];
    if (key < 0 || key >= MAX_KEYS) {
        return false;
    }
    std::size_t idx = static_cast<std::size_t>(key);
    return this->m_keyCurrent[idx] && !this->m_keyPrevious[idx];
}

bool InputManager::isActionHeld(InputAction action) const {
    int key = this->m_actionToKey[static_cast<uint8_t>(action)];
    if (key < 0 || key >= MAX_KEYS) {
        return false;
    }
    return this->m_keyCurrent[static_cast<std::size_t>(key)];
}

bool InputManager::isActionReleased(InputAction action) const {
    int key = this->m_actionToKey[static_cast<uint8_t>(action)];
    if (key < 0 || key >= MAX_KEYS) {
        return false;
    }
    std::size_t idx = static_cast<std::size_t>(key);
    return !this->m_keyCurrent[idx] && this->m_keyPrevious[idx];
}

// ============================================================================
// Raw key queries
// ============================================================================

bool InputManager::isKeyPressed(int glfwKey) const {
    if (glfwKey < 0 || glfwKey >= MAX_KEYS) {
        return false;
    }
    std::size_t idx = static_cast<std::size_t>(glfwKey);
    return this->m_keyCurrent[idx] && !this->m_keyPrevious[idx];
}

bool InputManager::isKeyHeld(int glfwKey) const {
    if (glfwKey < 0 || glfwKey >= MAX_KEYS) {
        return false;
    }
    return this->m_keyCurrent[static_cast<std::size_t>(glfwKey)];
}

bool InputManager::isKeyReleased(int glfwKey) const {
    if (glfwKey < 0 || glfwKey >= MAX_KEYS) {
        return false;
    }
    std::size_t idx = static_cast<std::size_t>(glfwKey);
    return !this->m_keyCurrent[idx] && this->m_keyPrevious[idx];
}

// ============================================================================
// Mouse button queries
// ============================================================================

bool InputManager::isMouseButtonPressed(int button) const {
    if (button < 0 || button >= MAX_MOUSE_BUTTONS) {
        return false;
    }
    std::size_t idx = static_cast<std::size_t>(button);
    return this->m_mouseButtonCurrent[idx] && !this->m_mouseButtonPrevious[idx];
}

bool InputManager::isMouseButtonHeld(int button) const {
    if (button < 0 || button >= MAX_MOUSE_BUTTONS) {
        return false;
    }
    return this->m_mouseButtonCurrent[static_cast<std::size_t>(button)];
}

bool InputManager::isMouseButtonReleased(int button) const {
    if (button < 0 || button >= MAX_MOUSE_BUTTONS) {
        return false;
    }
    std::size_t idx = static_cast<std::size_t>(button);
    return !this->m_mouseButtonCurrent[idx] && this->m_mouseButtonPrevious[idx];
}

// ============================================================================
// GLFW event handlers
// ============================================================================

void InputManager::onKey(int key, int /*scancode*/, int action, int /*mods*/) {
    if (key < 0 || key >= MAX_KEYS) {
        return;
    }
    std::size_t idx = static_cast<std::size_t>(key);
    if (action == GLFW_PRESS) {
        this->m_keyCurrent[idx] = true;
    } else if (action == GLFW_RELEASE) {
        this->m_keyCurrent[idx] = false;
    }
    // GLFW_REPEAT is ignored; held state is tracked via current/previous
}

void InputManager::onMouseButton(int button, int action, int /*mods*/) {
    if (button < 0 || button >= MAX_MOUSE_BUTTONS) {
        return;
    }
    std::size_t idx = static_cast<std::size_t>(button);
    if (action == GLFW_PRESS) {
        this->m_mouseButtonCurrent[idx] = true;
    } else if (action == GLFW_RELEASE) {
        this->m_mouseButtonCurrent[idx] = false;
    }
}

void InputManager::onCursorPos(double x, double y) {
    this->m_mouseX = x;
    this->m_mouseY = y;
}

void InputManager::onScroll(double /*xOffset*/, double yOffset) {
    this->m_scrollAccum += yOffset;
}

// ============================================================================
// Default key bindings
// ============================================================================

void InputManager::setupDefaultBindings() {
    this->m_actionToKey[static_cast<uint8_t>(InputAction::PanLeft)]      = GLFW_KEY_A;
    this->m_actionToKey[static_cast<uint8_t>(InputAction::PanRight)]     = GLFW_KEY_D;
    this->m_actionToKey[static_cast<uint8_t>(InputAction::PanUp)]        = GLFW_KEY_W;
    this->m_actionToKey[static_cast<uint8_t>(InputAction::PanDown)]      = GLFW_KEY_S;
    this->m_actionToKey[static_cast<uint8_t>(InputAction::ZoomIn)]       = GLFW_KEY_EQUAL;     // +/=
    this->m_actionToKey[static_cast<uint8_t>(InputAction::ZoomOut)]      = GLFW_KEY_MINUS;
    this->m_actionToKey[static_cast<uint8_t>(InputAction::EndTurn)]      = GLFW_KEY_ENTER;
    this->m_actionToKey[static_cast<uint8_t>(InputAction::Cancel)]       = GLFW_KEY_ESCAPE;
    this->m_actionToKey[static_cast<uint8_t>(InputAction::ToggleGrid)]   = GLFW_KEY_H;
    this->m_actionToKey[static_cast<uint8_t>(InputAction::OpenTechTree)] = GLFW_KEY_T;
    this->m_actionToKey[static_cast<uint8_t>(InputAction::OpenEconomy)]  = GLFW_KEY_E;
    this->m_actionToKey[static_cast<uint8_t>(InputAction::OpenProductionPicker)] = GLFW_KEY_P;
    this->m_actionToKey[static_cast<uint8_t>(InputAction::OpenGovernment)]     = GLFW_KEY_G;
    this->m_actionToKey[static_cast<uint8_t>(InputAction::OpenReligion)]      = GLFW_KEY_R;
    this->m_actionToKey[static_cast<uint8_t>(InputAction::QuickSave)]    = GLFW_KEY_F5;
    this->m_actionToKey[static_cast<uint8_t>(InputAction::QuickLoad)]    = GLFW_KEY_F9;
    this->m_actionToKey[static_cast<uint8_t>(InputAction::UpgradeUnit)]  = GLFW_KEY_U;
    this->m_actionToKey[static_cast<uint8_t>(InputAction::ShowHelp)]     = GLFW_KEY_F1;
    this->m_actionToKey[static_cast<uint8_t>(InputAction::UndoAction)]  = GLFW_KEY_Z;
    this->m_actionToKey[static_cast<uint8_t>(InputAction::CycleNextUnit)] = GLFW_KEY_TAB;
}

} // namespace aoc::app

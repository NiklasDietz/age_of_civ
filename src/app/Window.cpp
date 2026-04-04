/**
 * @file Window.cpp
 * @brief GLFW window implementation.
 */

#include "aoc/app/Window.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cassert>

namespace aoc::app {

Window::~Window() {
    this->destroy();
}

Window::Window(Window&& other) noexcept
    : m_window(other.m_window)
    , m_resizeCallback(std::move(other.m_resizeCallback))
    , m_keyCallback(std::move(other.m_keyCallback))
    , m_mouseButtonCallback(std::move(other.m_mouseButtonCallback))
    , m_cursorPosCallback(std::move(other.m_cursorPosCallback))
    , m_scrollCallback(std::move(other.m_scrollCallback))
{
    other.m_window = nullptr;
    if (this->m_window != nullptr) {
        glfwSetWindowUserPointer(this->m_window, this);
    }
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        this->destroy();
        this->m_window              = other.m_window;
        this->m_resizeCallback      = std::move(other.m_resizeCallback);
        this->m_keyCallback         = std::move(other.m_keyCallback);
        this->m_mouseButtonCallback = std::move(other.m_mouseButtonCallback);
        this->m_cursorPosCallback   = std::move(other.m_cursorPosCallback);
        this->m_scrollCallback      = std::move(other.m_scrollCallback);
        other.m_window = nullptr;
        if (this->m_window != nullptr) {
            glfwSetWindowUserPointer(this->m_window, this);
        }
    }
    return *this;
}

ErrorCode Window::create(const Config& config) {
    if (this->m_window != nullptr) {
        return ErrorCode::WindowAlreadyInitialized;
    }

    if (glfwInit() == GLFW_FALSE) {
        return ErrorCode::WindowCreationFailed;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // Vulkan, no OpenGL
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    this->m_window = glfwCreateWindow(
        static_cast<int>(config.width),
        static_cast<int>(config.height),
        config.title.c_str(),
        nullptr,  // No fullscreen monitor
        nullptr   // No shared context
    );

    if (this->m_window == nullptr) {
        glfwTerminate();
        return ErrorCode::WindowCreationFailed;
    }

    glfwSetWindowUserPointer(this->m_window, this);
    glfwSetFramebufferSizeCallback(this->m_window, glfwFramebufferSizeCallback);
    glfwSetKeyCallback(this->m_window, glfwKeyCallback);
    glfwSetMouseButtonCallback(this->m_window, glfwMouseButtonCallback);
    glfwSetCursorPosCallback(this->m_window, glfwCursorPosCallback);
    glfwSetScrollCallback(this->m_window, glfwScrollCallback);

    return ErrorCode::Ok;
}

void Window::destroy() {
    if (this->m_window != nullptr) {
        glfwDestroyWindow(this->m_window);
        this->m_window = nullptr;
        glfwTerminate();
    }
}

void Window::pollEvents() const {
    glfwPollEvents();
}

bool Window::shouldClose() const {
    if (this->m_window == nullptr) {
        return true;
    }
    return glfwWindowShouldClose(this->m_window) != 0;
}

std::pair<uint32_t, uint32_t> Window::framebufferSize() const {
    int width = 0;
    int height = 0;
    if (this->m_window != nullptr) {
        glfwGetFramebufferSize(this->m_window, &width, &height);
    }
    return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
}

std::pair<uint32_t, uint32_t> Window::windowSize() const {
    int width = 0;
    int height = 0;
    if (this->m_window != nullptr) {
        glfwGetWindowSize(this->m_window, &width, &height);
    }
    return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
}

void Window::setResizeCallback(ResizeCallback callback) {
    this->m_resizeCallback = std::move(callback);
}

void Window::setKeyCallback(KeyCallback callback) {
    this->m_keyCallback = std::move(callback);
}

void Window::setMouseButtonCallback(MouseButtonCallback callback) {
    this->m_mouseButtonCallback = std::move(callback);
}

void Window::setCursorPosCallback(CursorPosCallback callback) {
    this->m_cursorPosCallback = std::move(callback);
}

void Window::setScrollCallback(ScrollCallback callback) {
    this->m_scrollCallback = std::move(callback);
}

// ============================================================================
// GLFW static callbacks -> dispatch to Window instance
// ============================================================================

void Window::glfwFramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    Window* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr && self->m_resizeCallback) {
        self->m_resizeCallback(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    }
}

void Window::glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    Window* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr && self->m_keyCallback) {
        self->m_keyCallback(key, scancode, action, mods);
    }
}

void Window::glfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    Window* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr && self->m_mouseButtonCallback) {
        self->m_mouseButtonCallback(button, action, mods);
    }
}

void Window::glfwCursorPosCallback(GLFWwindow* window, double x, double y) {
    Window* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr && self->m_cursorPosCallback) {
        self->m_cursorPosCallback(x, y);
    }
}

void Window::glfwScrollCallback(GLFWwindow* window, double xOffset, double yOffset) {
    Window* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr && self->m_scrollCallback) {
        self->m_scrollCallback(xOffset, yOffset);
    }
}

} // namespace aoc::app

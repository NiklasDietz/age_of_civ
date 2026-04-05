#pragma once

/**
 * @file Window.hpp
 * @brief GLFW window wrapper providing platform abstraction for windowing and input.
 */

#include "aoc/core/ErrorCodes.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

struct GLFWwindow;

namespace aoc::app {

class Window {
public:
    struct Config {
        uint32_t    width  = 1280;
        uint32_t    height = 720;
        std::string title  = "Age of Civilization";
        bool        vsync  = true;
    };

    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) noexcept;
    Window& operator=(Window&&) noexcept;

    /**
     * @brief Create and show the GLFW window.
     * @return Ok on success, WindowCreationFailed on error.
     */
    [[nodiscard]] ErrorCode create(const Config& config);

    /// Destroy the window and terminate GLFW.
    void destroy();

    /// Poll OS events (keyboard, mouse, resize, close).
    void pollEvents() const;

    [[nodiscard]] bool shouldClose() const;

    [[nodiscard]] std::pair<uint32_t, uint32_t> framebufferSize() const;
    [[nodiscard]] std::pair<uint32_t, uint32_t> windowSize() const;

    /// Raw GLFW handle for Vulkan surface creation.
    [[nodiscard]] GLFWwindow* handle() const { return this->m_window; }

    /// Toggle fullscreen mode. Stores/restores windowed position and size.
    void setFullscreen(bool fullscreen);
    [[nodiscard]] bool isFullscreen() const { return this->m_isFullscreen; }

    /// Callback fired on framebuffer resize.
    using ResizeCallback = std::function<void(uint32_t, uint32_t)>;
    void setResizeCallback(ResizeCallback callback);

    /// Callback fired on key events.
    using KeyCallback = std::function<void(int key, int scancode, int action, int mods)>;
    void setKeyCallback(KeyCallback callback);

    /// Callback fired on mouse button events.
    using MouseButtonCallback = std::function<void(int button, int action, int mods)>;
    void setMouseButtonCallback(MouseButtonCallback callback);

    /// Callback fired on cursor position change.
    using CursorPosCallback = std::function<void(double x, double y)>;
    void setCursorPosCallback(CursorPosCallback callback);

    /// Callback fired on scroll events.
    using ScrollCallback = std::function<void(double xOffset, double yOffset)>;
    void setScrollCallback(ScrollCallback callback);

private:
    static void glfwFramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void glfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void glfwCursorPosCallback(GLFWwindow* window, double x, double y);
    static void glfwScrollCallback(GLFWwindow* window, double xOffset, double yOffset);

    GLFWwindow* m_window = nullptr;
    bool m_isFullscreen = false;
    int  m_windowedX = 0, m_windowedY = 0;
    int  m_windowedW = 1280, m_windowedH = 720;

    ResizeCallback      m_resizeCallback;
    KeyCallback         m_keyCallback;
    MouseButtonCallback m_mouseButtonCallback;
    CursorPosCallback   m_cursorPosCallback;
    ScrollCallback      m_scrollCallback;
};

} // namespace aoc::app

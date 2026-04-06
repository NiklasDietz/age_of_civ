#pragma once

/// @file DrawCommandBuffer.hpp
/// @brief Deferred draw command buffer for render optimization.
///
/// Game systems push draw commands during the update phase. The renderer
/// consumes them during the render phase, allowing for sorting, batching,
/// and culling optimizations before actual GPU submission.

#include <cstdint>
#include <vector>

namespace vulkan_app::renderer {
class Renderer2D;
}

namespace aoc::render {

/// A single draw command representing one primitive to render.
struct DrawCommand {
    enum class Type : uint8_t {
        Hexagon,        ///< Filled hexagon (terrain, city, overlay)
        FilledRect,     ///< Filled rectangle (buildings, improvements)
        FilledCircle,   ///< Filled circle (units, badges)
        FilledTriangle, ///< Filled triangle (trees, mountains)
        Line,           ///< Line segment (rivers, borders)
    };

    Type type;
    float x, y;           ///< Position (center for hex/circle, top-left for rect)
    float w, h;           ///< Size (radiusX/Y for hex, width/height for rect, radius for circle)
    float r, g, b, a;     ///< Color
    float extra1 = 0.0f;  ///< Type-specific (triangle vertices, line thickness, etc.)
    float extra2 = 0.0f;
    float extra3 = 0.0f;
    float extra4 = 0.0f;
    float extra5 = 0.0f;
    float extra6 = 0.0f;
    int32_t layer = 0;    ///< Render layer for sorting (0=terrain, 1=overlay, 2=units, 3=UI)
};

/// Collects draw commands from game systems for deferred rendering.
class DrawCommandBuffer {
public:
    void clear() { this->m_commands.clear(); }
    void reserve(std::size_t count) { this->m_commands.reserve(count); }

    void pushHexagon(float cx, float cy, float radiusX, float radiusY,
                     float r, float g, float b, float a, int32_t layer = 0);
    void pushFilledRect(float x, float y, float w, float h,
                        float r, float g, float b, float a, int32_t layer = 0);
    void pushFilledCircle(float cx, float cy, float radius,
                          float r, float g, float b, float a, int32_t layer = 0);
    void pushFilledTriangle(float x1, float y1, float x2, float y2,
                            float x3, float y3,
                            float r, float g, float b, float a, int32_t layer = 0);
    void pushLine(float x1, float y1, float x2, float y2, float thickness,
                  float r, float g, float b, float a, int32_t layer = 0);

    /// Sort commands by layer for correct draw ordering.
    void sortByLayer();

    /// Submit all commands to the Renderer2D.
    void flush(vulkan_app::renderer::Renderer2D& renderer2d) const;

    [[nodiscard]] std::size_t size() const { return this->m_commands.size(); }
    [[nodiscard]] bool empty() const { return this->m_commands.empty(); }
    [[nodiscard]] const std::vector<DrawCommand>& commands() const { return this->m_commands; }

private:
    std::vector<DrawCommand> m_commands;
};

} // namespace aoc::render

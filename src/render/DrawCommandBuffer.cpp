/**
 * @file DrawCommandBuffer.cpp
 * @brief Deferred draw command buffer implementation.
 */

#include "aoc/render/DrawCommandBuffer.hpp"

#include <renderer/Renderer2D.hpp>

#include <algorithm>

namespace aoc::render {

void DrawCommandBuffer::pushHexagon(float cx, float cy, float radiusX, float radiusY,
                                    float r, float g, float b, float a, int32_t layer) {
    DrawCommand cmd{};
    cmd.type = DrawCommand::Type::Hexagon;
    cmd.x = cx;
    cmd.y = cy;
    cmd.w = radiusX;
    cmd.h = radiusY;
    cmd.r = r;
    cmd.g = g;
    cmd.b = b;
    cmd.a = a;
    cmd.layer = layer;
    this->m_commands.push_back(cmd);
}

void DrawCommandBuffer::pushFilledRect(float x, float y, float w, float h,
                                       float r, float g, float b, float a, int32_t layer) {
    DrawCommand cmd{};
    cmd.type = DrawCommand::Type::FilledRect;
    cmd.x = x;
    cmd.y = y;
    cmd.w = w;
    cmd.h = h;
    cmd.r = r;
    cmd.g = g;
    cmd.b = b;
    cmd.a = a;
    cmd.layer = layer;
    this->m_commands.push_back(cmd);
}

void DrawCommandBuffer::pushFilledCircle(float cx, float cy, float radius,
                                         float r, float g, float b, float a, int32_t layer) {
    DrawCommand cmd{};
    cmd.type = DrawCommand::Type::FilledCircle;
    cmd.x = cx;
    cmd.y = cy;
    cmd.w = radius;
    cmd.h = 0.0f;
    cmd.r = r;
    cmd.g = g;
    cmd.b = b;
    cmd.a = a;
    cmd.layer = layer;
    this->m_commands.push_back(cmd);
}

void DrawCommandBuffer::pushFilledTriangle(float x1, float y1, float x2, float y2,
                                           float x3, float y3,
                                           float r, float g, float b, float a, int32_t layer) {
    DrawCommand cmd{};
    cmd.type = DrawCommand::Type::FilledTriangle;
    cmd.x = x1;
    cmd.y = y1;
    cmd.w = 0.0f;
    cmd.h = 0.0f;
    cmd.r = r;
    cmd.g = g;
    cmd.b = b;
    cmd.a = a;
    cmd.extra1 = x2;
    cmd.extra2 = y2;
    cmd.extra3 = x3;
    cmd.extra4 = y3;
    cmd.layer = layer;
    this->m_commands.push_back(cmd);
}

void DrawCommandBuffer::pushLine(float x1, float y1, float x2, float y2, float thickness,
                                 float r, float g, float b, float a, int32_t layer) {
    DrawCommand cmd{};
    cmd.type = DrawCommand::Type::Line;
    cmd.x = x1;
    cmd.y = y1;
    cmd.w = 0.0f;
    cmd.h = 0.0f;
    cmd.r = r;
    cmd.g = g;
    cmd.b = b;
    cmd.a = a;
    cmd.extra1 = x2;
    cmd.extra2 = y2;
    cmd.extra3 = thickness;
    cmd.layer = layer;
    this->m_commands.push_back(cmd);
}

void DrawCommandBuffer::sortByLayer() {
    std::sort(this->m_commands.begin(), this->m_commands.end(),
              [](const DrawCommand& lhs, const DrawCommand& rhs) {
                  return lhs.layer < rhs.layer;
              });
}

void DrawCommandBuffer::flush(vulkan_app::renderer::Renderer2D& renderer2d) const {
    for (const DrawCommand& cmd : this->m_commands) {
        switch (cmd.type) {
            case DrawCommand::Type::Hexagon:
                renderer2d.drawFilledHexagon(cmd.x, cmd.y, cmd.w, cmd.h,
                                             cmd.r, cmd.g, cmd.b, cmd.a);
                break;
            case DrawCommand::Type::FilledRect:
                renderer2d.drawFilledRect(cmd.x, cmd.y, cmd.w, cmd.h,
                                          cmd.r, cmd.g, cmd.b, cmd.a);
                break;
            case DrawCommand::Type::FilledCircle:
                renderer2d.drawFilledCircle(cmd.x, cmd.y, cmd.w,
                                            cmd.r, cmd.g, cmd.b, cmd.a);
                break;
            case DrawCommand::Type::FilledTriangle:
                renderer2d.drawFilledTriangle(cmd.x, cmd.y,
                                              cmd.extra1, cmd.extra2,
                                              cmd.extra3, cmd.extra4,
                                              cmd.r, cmd.g, cmd.b, cmd.a);
                break;
            case DrawCommand::Type::Line:
                renderer2d.drawLine(cmd.x, cmd.y, cmd.extra1, cmd.extra2,
                                    cmd.extra3, cmd.r, cmd.g, cmd.b, cmd.a);
                break;
        }
    }
}

} // namespace aoc::render

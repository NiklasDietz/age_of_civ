#pragma once

/**
 * @file Selectable.hpp
 * @brief Interface for selectable game objects (units, cities, tiles).
 *
 * Provides a common selection visualization pattern: selected objects
 * get a hex outline in a highlight color. This can be used by the
 * renderer to draw selection indicators consistently.
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>

namespace vulkan_app::renderer {
class Renderer2D;
}

namespace aoc {

/// Selection state for any game object that can be highlighted.
struct SelectionInfo {
    EntityId entity = NULL_ENTITY;
    hex::AxialCoord position;
    float highlightR = 1.0f;
    float highlightG = 1.0f;
    float highlightB = 1.0f;
    float highlightA = 0.85f;
    float borderWidth = 3.0f;

    [[nodiscard]] bool isValid() const { return this->entity.isValid(); }
};

/// Draws a hex outline as a selection indicator at the given world position.
void drawSelectionHex(vulkan_app::renderer::Renderer2D& renderer2d,
                       float cx, float cy, float hexSize,
                       float r, float g, float b, float a = 0.85f,
                       float borderWidth = 3.0f);

} // namespace aoc

#pragma once

/**
 * @file IconPainter.hpp
 * @brief Procedural vector icons drawn from Renderer2D primitives.
 *
 * Icons were flat coloured squares -- IconData's render branch just filled
 * the widget bounds. Rather than ship a bitmap atlas or an icon font (no
 * OFL icon font is available on the target systems, and it would add a
 * licensing surface for ~12 glyphs), each icon is composed from two or three
 * SDF primitives the renderer already has. They stay crisp at any size and
 * recolour for free from the caller's tint.
 *
 * Add a shape by extending IconShape and its switch in IconPainter.cpp, then
 * map the sprite name to it in IconAtlas::seedBuiltIns.
 */

#include "aoc/ui/Color.hpp"

#include <cstdint>

namespace vulkan_app::renderer {
class Renderer2D;
}

namespace aoc::ui {

/// Which vector recipe to paint. `None` keeps the old filled-square
/// behaviour, so unregistered or ad-hoc icons still render something.
enum class IconShape : uint8_t {
    None = 0,
    Coin,    ///< Gold: disc with an inset ring
    Flask,   ///< Science: tapered body with a neck
    Note,    ///< Culture: note head with a stem
    Flame,   ///< Faith: teardrop over a rounded base
    Leaf,    ///< Food: ellipse with a midrib
    Gear,    ///< Production: hexagon with a bore
    Bolt,    ///< Power: lightning zigzag
    Compass, ///< Tourism: ring with a needle
    Shield,  ///< Defence / military
    Capital, ///< City / capital marker: star-ish burst
};

/// Paint `shape` inside the square box (x, y, size) in `color`.
/// Sub-pixel sizes are skipped rather than drawn as degenerate primitives.
void drawIcon(vulkan_app::renderer::Renderer2D& renderer2d, IconShape shape, float x, float y,
              float size, Color color);

} // namespace aoc::ui

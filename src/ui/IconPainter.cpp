/**
 * @file IconPainter.cpp
 * @brief Vector icon recipes. See IconPainter.hpp for why these are drawn
 *        rather than sampled from an atlas.
 *
 * Every recipe works in a normalized 0..1 box and is scaled to the requested
 * size at the end, so icons stay proportional at any dimension. Shapes are
 * kept to two or three primitives each -- these read at 14-20 px, where extra
 * detail turns to mush.
 */

#include "aoc/ui/IconPainter.hpp"

#include <renderer/Renderer2D.hpp>

namespace aoc::ui {

namespace {

/// Below this the primitives degenerate and just look like noise.
constexpr float MIN_DRAWABLE_SIZE = 4.0f;

/// Darker variant for inset detail (bore holes, rings, midribs). Multiplying
/// keeps the detail in the same hue as the caller's tint instead of punching a
/// grey hole in a coloured icon.
Color shade(Color c, float factor) {
    return Color{c.r * factor, c.g * factor, c.b * factor, c.a};
}

} // namespace

void drawIcon(vulkan_app::renderer::Renderer2D& renderer2d, IconShape shape, float x, float y,
              float size, Color color) {
    if (shape == IconShape::None || size < MIN_DRAWABLE_SIZE) {
        return;
    }

    // Normalized helpers: u/v map 0..1 onto the icon box, s scales a length.
    const auto u = [x, size](float t) { return x + t * size; };
    const auto v = [y, size](float t) { return y + t * size; };
    const auto s = [size](float t) { return t * size; };

    const Color dark = shade(color, 0.55f);

    switch (shape) {
    case IconShape::Coin: {
        renderer2d.drawFilledCircle(u(0.5f), v(0.5f), s(0.44f), color.r, color.g, color.b, color.a);
        renderer2d.drawCircle(u(0.5f), v(0.5f), s(0.26f), dark.r, dark.g, dark.b, dark.a, s(0.07f));
        break;
    }
    case IconShape::Flask: {
        // Neck, then a tapered body: a triangle reads as a conical flask at
        // small sizes far better than an outlined vessel does.
        renderer2d.drawFilledRect(u(0.42f), v(0.08f), s(0.16f), s(0.24f), color.r, color.g, color.b,
                                  color.a);
        renderer2d.drawFilledTriangle(u(0.5f), v(0.26f), u(0.14f), v(0.9f), u(0.86f), v(0.9f),
                                      color.r, color.g, color.b, color.a);
        break;
    }
    case IconShape::Note: {
        renderer2d.drawFilledEllipse(u(0.36f), v(0.74f), s(0.24f), s(0.18f), color.r, color.g,
                                     color.b, color.a);
        renderer2d.drawFilledRect(u(0.56f), v(0.12f), s(0.10f), s(0.62f), color.r, color.g, color.b,
                                  color.a);
        break;
    }
    case IconShape::Flame: {
        renderer2d.drawFilledCircle(u(0.5f), v(0.66f), s(0.30f), color.r, color.g, color.b,
                                    color.a);
        renderer2d.drawFilledTriangle(u(0.5f), v(0.06f), u(0.22f), v(0.66f), u(0.78f), v(0.66f),
                                      color.r, color.g, color.b, color.a);
        break;
    }
    case IconShape::Leaf: {
        renderer2d.drawFilledEllipse(u(0.5f), v(0.5f), s(0.28f), s(0.44f), color.r, color.g,
                                     color.b, color.a);
        renderer2d.drawLine(u(0.5f), v(0.12f), u(0.5f), v(0.88f), s(0.07f), dark.r, dark.g, dark.b,
                            dark.a);
        break;
    }
    case IconShape::Gear: {
        // Hexagon stands in for a toothed gear; teeth vanish at 14 px anyway.
        renderer2d.drawFilledHexagon(u(0.5f), v(0.5f), s(0.46f), s(0.48f), color.r, color.g,
                                     color.b, color.a);
        renderer2d.drawFilledCircle(u(0.5f), v(0.5f), s(0.17f), dark.r, dark.g, dark.b, dark.a);
        break;
    }
    case IconShape::Bolt: {
        renderer2d.drawFilledTriangle(u(0.58f), v(0.04f), u(0.20f), v(0.56f), u(0.52f), v(0.56f),
                                      color.r, color.g, color.b, color.a);
        renderer2d.drawFilledTriangle(u(0.42f), v(0.96f), u(0.80f), v(0.44f), u(0.48f), v(0.44f),
                                      color.r, color.g, color.b, color.a);
        break;
    }
    case IconShape::Compass: {
        renderer2d.drawCircle(u(0.5f), v(0.5f), s(0.44f), color.r, color.g, color.b, color.a,
                              s(0.09f));
        renderer2d.drawFilledTriangle(u(0.68f), v(0.32f), u(0.5f), v(0.5f), u(0.34f), v(0.66f),
                                      color.r, color.g, color.b, color.a);
        break;
    }
    case IconShape::Shield: {
        renderer2d.drawFilledRect(u(0.16f), v(0.10f), s(0.68f), s(0.42f), color.r, color.g, color.b,
                                  color.a);
        renderer2d.drawFilledTriangle(u(0.16f), v(0.50f), u(0.84f), v(0.50f), u(0.5f), v(0.94f),
                                      color.r, color.g, color.b, color.a);
        break;
    }
    case IconShape::Capital: {
        // Two overlapping triangles read as a star burst at icon sizes.
        renderer2d.drawFilledTriangle(u(0.5f), v(0.04f), u(0.10f), v(0.68f), u(0.90f), v(0.68f),
                                      color.r, color.g, color.b, color.a);
        renderer2d.drawFilledTriangle(u(0.5f), v(0.96f), u(0.10f), v(0.32f), u(0.90f), v(0.32f),
                                      color.r, color.g, color.b, color.a);
        break;
    }
    case IconShape::Bow: {
        renderer2d.drawArc(u(0.34f), v(0.5f), s(0.40f), -1.2f, 1.2f, color.r, color.g, color.b,
                           color.a, s(0.09f));
        renderer2d.drawLine(u(0.68f), v(0.14f), u(0.68f), v(0.86f), s(0.05f), color.r, color.g,
                            color.b, color.a);
        break;
    }
    case IconShape::Anchor: {
        renderer2d.drawFilledCircle(u(0.5f), v(0.16f), s(0.11f), color.r, color.g, color.b,
                                    color.a);
        renderer2d.drawLine(u(0.5f), v(0.24f), u(0.5f), v(0.88f), s(0.10f), color.r, color.g,
                            color.b, color.a);
        renderer2d.drawArc(u(0.5f), v(0.62f), s(0.34f), 0.25f, 2.9f, color.r, color.g, color.b,
                           color.a, s(0.09f));
        break;
    }
    case IconShape::Plane: {
        renderer2d.drawFilledTriangle(u(0.5f), v(0.06f), u(0.06f), v(0.66f), u(0.94f), v(0.66f),
                                      color.r, color.g, color.b, color.a);
        renderer2d.drawFilledRect(u(0.42f), v(0.60f), s(0.16f), s(0.34f), color.r, color.g, color.b,
                                  color.a);
        break;
    }
    case IconShape::Tent: {
        renderer2d.drawFilledTriangle(u(0.5f), v(0.10f), u(0.06f), v(0.88f), u(0.94f), v(0.88f),
                                      color.r, color.g, color.b, color.a);
        renderer2d.drawFilledTriangle(u(0.5f), v(0.44f), u(0.34f), v(0.88f), u(0.66f), v(0.88f),
                                      dark.r, dark.g, dark.b, dark.a);
        break;
    }
    case IconShape::Hammer: {
        renderer2d.drawFilledRect(u(0.16f), v(0.14f), s(0.68f), s(0.24f), color.r, color.g, color.b,
                                  color.a);
        renderer2d.drawFilledRect(u(0.42f), v(0.34f), s(0.16f), s(0.54f), dark.r, dark.g, dark.b,
                                  dark.a);
        break;
    }
    case IconShape::Eye: {
        renderer2d.drawFilledEllipse(u(0.5f), v(0.5f), s(0.46f), s(0.28f), color.r, color.g,
                                     color.b, color.a);
        renderer2d.drawFilledCircle(u(0.5f), v(0.5f), s(0.16f), dark.r, dark.g, dark.b, dark.a);
        break;
    }
    case IconShape::House: {
        renderer2d.drawFilledTriangle(u(0.5f), v(0.08f), u(0.06f), v(0.46f), u(0.94f), v(0.46f),
                                      color.r, color.g, color.b, color.a);
        renderer2d.drawFilledRect(u(0.18f), v(0.46f), s(0.64f), s(0.46f), color.r, color.g, color.b,
                                  color.a);
        renderer2d.drawFilledRect(u(0.42f), v(0.64f), s(0.16f), s(0.28f), dark.r, dark.g, dark.b,
                                  dark.a);
        break;
    }
    case IconShape::Banner: {
        renderer2d.drawFilledRect(u(0.10f), v(0.10f), s(0.68f), s(0.52f), color.r, color.g, color.b,
                                  color.a);
        renderer2d.drawLine(u(0.14f), v(0.10f), u(0.14f), v(0.92f), s(0.08f), dark.r, dark.g,
                            dark.b, dark.a);
        break;
    }
    case IconShape::Scroll: {
        renderer2d.drawFilledRect(u(0.20f), v(0.12f), s(0.60f), s(0.76f), color.r, color.g, color.b,
                                  color.a);
        renderer2d.drawLine(u(0.32f), v(0.34f), u(0.68f), v(0.34f), s(0.06f), dark.r, dark.g,
                            dark.b, dark.a);
        renderer2d.drawLine(u(0.32f), v(0.54f), u(0.68f), v(0.54f), s(0.06f), dark.r, dark.g,
                            dark.b, dark.a);
        break;
    }
    case IconShape::Ore: {
        renderer2d.drawFilledHexagon(u(0.5f), v(0.56f), s(0.38f), s(0.36f), color.r, color.g,
                                     color.b, color.a);
        renderer2d.drawFilledCircle(u(0.30f), v(0.26f), s(0.15f), color.r, color.g, color.b,
                                    color.a);
        break;
    }
    case IconShape::Droplet: {
        renderer2d.drawFilledCircle(u(0.5f), v(0.64f), s(0.30f), color.r, color.g, color.b,
                                    color.a);
        renderer2d.drawFilledTriangle(u(0.5f), v(0.08f), u(0.26f), v(0.64f), u(0.74f), v(0.64f),
                                      color.r, color.g, color.b, color.a);
        break;
    }
    case IconShape::Check: {
        renderer2d.drawLine(u(0.14f), v(0.52f), u(0.42f), v(0.82f), s(0.14f), color.r, color.g,
                            color.b, color.a);
        renderer2d.drawLine(u(0.42f), v(0.82f), u(0.88f), v(0.20f), s(0.14f), color.r, color.g,
                            color.b, color.a);
        break;
    }
    case IconShape::Warning: {
        renderer2d.drawFilledTriangle(u(0.5f), v(0.08f), u(0.04f), v(0.90f), u(0.96f), v(0.90f),
                                      color.r, color.g, color.b, color.a);
        renderer2d.drawFilledRect(u(0.44f), v(0.38f), s(0.12f), s(0.30f), dark.r, dark.g, dark.b,
                                  dark.a);
        break;
    }
    case IconShape::Arrow: {
        renderer2d.drawLine(u(0.12f), v(0.5f), u(0.66f), v(0.5f), s(0.12f), color.r, color.g,
                            color.b, color.a);
        renderer2d.drawFilledTriangle(u(0.94f), v(0.5f), u(0.56f), v(0.20f), u(0.56f), v(0.80f),
                                      color.r, color.g, color.b, color.a);
        break;
    }
    case IconShape::None:
    default:
        break;
    }
}

} // namespace aoc::ui

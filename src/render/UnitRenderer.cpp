/**
 * @file UnitRenderer.cpp
 * @brief Draws units as colored shapes and cities as bordered circles.
 */

#include "aoc/render/UnitRenderer.hpp"
#include "aoc/render/CameraController.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/map/HexCoord.hpp"

#include <renderer/Renderer2D.hpp>

#include <array>

namespace aoc::render {

namespace {

/// Player colors (up to 16 players).
constexpr std::array<std::array<float, 3>, 8> PLAYER_COLORS = {{
    {0.20f, 0.40f, 0.90f},  // Player 0: blue
    {0.90f, 0.20f, 0.20f},  // Player 1: red
    {0.20f, 0.80f, 0.20f},  // Player 2: green
    {0.90f, 0.80f, 0.10f},  // Player 3: yellow
    {0.70f, 0.30f, 0.80f},  // Player 4: purple
    {0.90f, 0.50f, 0.10f},  // Player 5: orange
    {0.10f, 0.80f, 0.80f},  // Player 6: cyan
    {0.80f, 0.40f, 0.60f},  // Player 7: pink
}};

void playerColor(PlayerId player, float& r, float& g, float& b) {
    std::size_t idx = static_cast<std::size_t>(player) % PLAYER_COLORS.size();
    r = PLAYER_COLORS[idx][0];
    g = PLAYER_COLORS[idx][1];
    b = PLAYER_COLORS[idx][2];
}

} // anonymous namespace

void UnitRenderer::drawUnits(vulkan_app::renderer::Renderer2D& renderer2d,
                              const aoc::ecs::World& world,
                              const CameraController& camera,
                              float hexSize,
                              uint32_t screenWidth, uint32_t screenHeight) const {
    const aoc::ecs::ComponentPool<aoc::sim::UnitComponent>* pool =
        world.getPool<aoc::sim::UnitComponent>();
    if (pool == nullptr) {
        return;
    }

    // Compute visible bounds
    float topLeftX = 0.0f, topLeftY = 0.0f, botRightX = 0.0f, botRightY = 0.0f;
    camera.screenToWorld(0.0, 0.0, topLeftX, topLeftY, screenWidth, screenHeight);
    camera.screenToWorld(static_cast<double>(screenWidth), static_cast<double>(screenHeight),
                         botRightX, botRightY, screenWidth, screenHeight);
    float margin = hexSize * 2.0f;
    topLeftX -= margin; topLeftY -= margin;
    botRightX += margin; botRightY += margin;

    float unitRadius = hexSize * 0.30f;

    for (uint32_t i = 0; i < pool->size(); ++i) {
        const aoc::sim::UnitComponent& unit = pool->data()[i];
        EntityId entity = pool->entities()[i];

        float cx = 0.0f, cy = 0.0f;
        hex::axialToPixel(unit.position, hexSize, cx, cy);

        // Frustum cull
        if (cx < topLeftX || cx > botRightX || cy < topLeftY || cy > botRightY) {
            continue;
        }

        float r = 0.0f, g = 0.0f, b = 0.0f;
        playerColor(unit.owner, r, g, b);

        // Draw unit as a filled circle with a colored border
        renderer2d.drawFilledCircle(cx, cy, unitRadius, r, g, b, 0.9f);
        renderer2d.drawCircle(cx, cy, unitRadius, 0.0f, 0.0f, 0.0f, 0.8f, 2.0f);

        // Draw unit class indicator
        const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(unit.typeId);
        switch (def.unitClass) {
            case aoc::sim::UnitClass::Melee: {
                // Sword shape: small cross
                float s = unitRadius * 0.5f;
                renderer2d.drawLine(cx - s, cy, cx + s, cy, 2.0f, 1.0f, 1.0f, 1.0f, 0.9f);
                renderer2d.drawLine(cx, cy - s, cx, cy + s, 2.0f, 1.0f, 1.0f, 1.0f, 0.9f);
                break;
            }
            case aoc::sim::UnitClass::Ranged: {
                // Dot in center
                renderer2d.drawFilledCircle(cx, cy, unitRadius * 0.25f, 1.0f, 1.0f, 1.0f, 0.9f);
                break;
            }
            case aoc::sim::UnitClass::Scout: {
                // Small triangle pointing up
                float s = unitRadius * 0.4f;
                renderer2d.drawFilledTriangle(cx, cy - s, cx - s, cy + s * 0.5f,
                                              cx + s, cy + s * 0.5f,
                                              1.0f, 1.0f, 1.0f, 0.9f);
                break;
            }
            case aoc::sim::UnitClass::Settler: {
                // Small square
                float s = unitRadius * 0.35f;
                renderer2d.drawFilledRect(cx - s, cy - s, s * 2.0f, s * 2.0f,
                                           1.0f, 1.0f, 1.0f, 0.9f);
                break;
            }
            case aoc::sim::UnitClass::Cavalry: {
                // Arrow shape pointing right
                float s = unitRadius * 0.4f;
                renderer2d.drawFilledTriangle(cx + s, cy, cx - s, cy - s * 0.6f,
                                              cx - s, cy + s * 0.6f,
                                              1.0f, 1.0f, 1.0f, 0.9f);
                break;
            }
            default:
                break;
        }

        // Selection highlight
        if (entity == this->selectedEntity) {
            renderer2d.drawCircle(cx, cy, unitRadius + 3.0f, 1.0f, 1.0f, 1.0f, 0.8f, 2.0f);
        }
    }
}

void UnitRenderer::drawCities(vulkan_app::renderer::Renderer2D& renderer2d,
                               const aoc::ecs::World& world,
                               const CameraController& camera,
                               float hexSize,
                               uint32_t screenWidth, uint32_t screenHeight) const {
    const aoc::ecs::ComponentPool<aoc::sim::CityComponent>* pool =
        world.getPool<aoc::sim::CityComponent>();
    if (pool == nullptr) {
        return;
    }

    float topLeftX = 0.0f, topLeftY = 0.0f, botRightX = 0.0f, botRightY = 0.0f;
    camera.screenToWorld(0.0, 0.0, topLeftX, topLeftY, screenWidth, screenHeight);
    camera.screenToWorld(static_cast<double>(screenWidth), static_cast<double>(screenHeight),
                         botRightX, botRightY, screenWidth, screenHeight);
    float margin = hexSize * 2.0f;
    topLeftX -= margin; topLeftY -= margin;
    botRightX += margin; botRightY += margin;

    float cityRadius = hexSize * 0.45f;

    for (uint32_t i = 0; i < pool->size(); ++i) {
        const aoc::sim::CityComponent& city = pool->data()[i];

        float cx = 0.0f, cy = 0.0f;
        hex::axialToPixel(city.location, hexSize, cx, cy);

        if (cx < topLeftX || cx > botRightX || cy < topLeftY || cy > botRightY) {
            continue;
        }

        float r = 0.0f, g = 0.0f, b = 0.0f;
        playerColor(city.owner, r, g, b);

        // City is a larger filled circle with a thick border
        renderer2d.drawFilledCircle(cx, cy, cityRadius, r * 0.6f, g * 0.6f, b * 0.6f, 0.9f);
        renderer2d.drawCircle(cx, cy, cityRadius, r, g, b, 1.0f, 3.0f);

        // Population number as centered dot pattern (simple: just a white filled rect)
        float textSize = cityRadius * 0.5f;
        renderer2d.drawFilledRect(cx - textSize * 0.3f, cy - textSize * 0.4f,
                                   textSize * 0.6f, textSize * 0.8f,
                                   1.0f, 1.0f, 1.0f, 0.9f);
    }
}

void UnitRenderer::drawPath(vulkan_app::renderer::Renderer2D& renderer2d,
                             const std::vector<hex::AxialCoord>& path,
                             float hexSize) const {
    if (path.size() < 2) {
        return;
    }

    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
        hex::axialToPixel(path[i], hexSize, x1, y1);
        hex::axialToPixel(path[i + 1], hexSize, x2, y2);

        renderer2d.drawLine(x1, y1, x2, y2, 2.5f, 1.0f, 1.0f, 1.0f, 0.6f);
    }

    // Draw dots at each path node
    for (std::size_t i = 1; i < path.size(); ++i) {
        float cx = 0.0f, cy = 0.0f;
        hex::axialToPixel(path[i], hexSize, cx, cy);
        renderer2d.drawFilledCircle(cx, cy, 3.0f, 1.0f, 1.0f, 1.0f, 0.5f);
    }
}

void UnitRenderer::drawReachable(vulkan_app::renderer::Renderer2D& renderer2d,
                                  const std::vector<hex::AxialCoord>& tiles,
                                  float hexSize) const {
    for (const hex::AxialCoord& tile : tiles) {
        float cx = 0.0f, cy = 0.0f;
        hex::axialToPixel(tile, hexSize, cx, cy);

        float points[12];
        hex::hexVertices(cx, cy, hexSize, points);
        renderer2d.drawFilledPolygon(points, 6, 1.0f, 1.0f, 1.0f, 0.15f);
    }
}

} // namespace aoc::render

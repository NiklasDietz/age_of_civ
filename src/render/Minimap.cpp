/**
 * @file Minimap.cpp
 * @brief Minimap rendering: terrain overview, ownership tint, and viewport rectangle.
 */

#include "aoc/render/Minimap.hpp"
#include "aoc/render/CameraController.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

#include <renderer/Renderer2D.hpp>

#include <algorithm>
#include <cmath>

namespace aoc::render {

/// Player colors for ownership tinting on the minimap.
static constexpr float PLAYER_COLORS[][3] = {
    {0.2f, 0.4f, 1.0f},  // Player 0: blue
    {1.0f, 0.2f, 0.2f},  // Player 1: red
    {0.2f, 0.8f, 0.2f},  // Player 2: green
    {0.8f, 0.8f, 0.2f},  // Player 3: yellow
};

static constexpr uint8_t PLAYER_COLOR_COUNT = 4;

void Minimap::draw(vulkan_app::renderer::Renderer2D& renderer2d,
                   const aoc::map::HexGrid& grid,
                   const aoc::map::FogOfWar& fog,
                   PlayerId player,
                   const CameraController& camera,
                   float mapX, float mapY, float mapW, float mapH,
                   uint32_t screenWidth, uint32_t screenHeight) const {
    // Background
    renderer2d.drawFilledRect(mapX - 2.0f, mapY - 2.0f,
                              mapW + 4.0f, mapH + 4.0f,
                              0.02f, 0.02f, 0.05f, 0.9f);

    const int32_t gridWidth = grid.width();
    const int32_t gridHeight = grid.height();

    if (gridWidth <= 0 || gridHeight <= 0) {
        return;
    }

    const float tileW = mapW / static_cast<float>(gridWidth);
    const float tileH = mapH / static_cast<float>(gridHeight);
    const float dotSize = std::max(1.0f, std::min(tileW, tileH));

    // Draw each tile as a colored dot
    for (int32_t row = 0; row < gridHeight; ++row) {
        for (int32_t col = 0; col < gridWidth; ++col) {
            const int32_t index = row * gridWidth + col;

            const aoc::map::TileVisibility vis = fog.visibility(player, index);
            if (vis == aoc::map::TileVisibility::Unseen) {
                // Draw unseen tiles as black instead of skipping
                const float px = mapX + static_cast<float>(col) * tileW;
                const float py = mapY + static_cast<float>(row) * tileH;
                renderer2d.drawFilledRect(px, py, dotSize, dotSize, 0.02f, 0.02f, 0.04f, 1.0f);
                continue;
            }

            const aoc::map::TerrainType terrain = grid.terrain(index);
            aoc::map::TerrainColor tc = aoc::map::terrainColor(terrain);

            // Dim revealed (non-visible) tiles
            if (vis == aoc::map::TileVisibility::Revealed) {
                tc.r *= 0.5f;
                tc.g *= 0.5f;
                tc.b *= 0.5f;
            }

            // Tint with player ownership color
            const PlayerId tileOwner = grid.owner(index);
            if (tileOwner != INVALID_PLAYER && tileOwner < PLAYER_COLOR_COUNT) {
                const float blend = 0.4f;
                tc.r = tc.r * (1.0f - blend) + PLAYER_COLORS[tileOwner][0] * blend;
                tc.g = tc.g * (1.0f - blend) + PLAYER_COLORS[tileOwner][1] * blend;
                tc.b = tc.b * (1.0f - blend) + PLAYER_COLORS[tileOwner][2] * blend;
            }

            const float px = mapX + static_cast<float>(col) * tileW;
            const float py = mapY + static_cast<float>(row) * tileH;

            renderer2d.drawFilledRect(px, py, dotSize, dotSize, tc.r, tc.g, tc.b, 1.0f);
        }
    }

    // Draw camera viewport rectangle
    // Compute the world-space bounds visible through the camera
    const float sqrt3 = 1.7320508075688772f;
    const float hexSize = 30.0f;  // Default hex size (same as MapRenderer)
    const float worldWidth = sqrt3 * hexSize * static_cast<float>(gridWidth);
    const float worldHeight = 1.5f * hexSize * static_cast<float>(gridHeight);

    // Camera viewport in world space
    const float viewW = static_cast<float>(screenWidth) / camera.zoom();
    const float viewH = static_cast<float>(screenHeight) / camera.zoom();
    const float viewLeft = camera.cameraX() - viewW * 0.5f;
    const float viewTop = camera.cameraY() - viewH * 0.5f;

    // Convert to minimap space
    const float vpX = mapX + (viewLeft / worldWidth) * mapW;
    const float vpY = mapY + (viewTop / worldHeight) * mapH;
    const float vpW = (viewW / worldWidth) * mapW;
    const float vpH = (viewH / worldHeight) * mapH;

    // Clamp viewport rect to minimap bounds
    const float clampedX = std::max(mapX, std::min(vpX, mapX + mapW));
    const float clampedY = std::max(mapY, std::min(vpY, mapY + mapH));
    const float clampedW = std::min(vpW, mapX + mapW - clampedX);
    const float clampedH = std::min(vpH, mapY + mapH - clampedY);

    if (clampedW > 1.0f && clampedH > 1.0f) {
        renderer2d.drawRect(clampedX, clampedY, clampedW, clampedH,
                           1.0f, 1.0f, 1.0f, 0.8f, 1.5f);
    }
}

bool Minimap::containsPoint(float screenX, float screenY,
                            float mapX, float mapY,
                            float mapW, float mapH) const {
    return screenX >= mapX && screenX <= mapX + mapW
        && screenY >= mapY && screenY <= mapY + mapH;
}

void Minimap::screenToWorld(float screenX, float screenY,
                            float mapX, float mapY, float mapW, float mapH,
                            const aoc::map::HexGrid& grid, float hexSize,
                            float& outWorldX, float& outWorldY) const {
    const float sqrt3 = 1.7320508075688772f;
    const float worldWidth = sqrt3 * hexSize * static_cast<float>(grid.width());
    const float worldHeight = 1.5f * hexSize * static_cast<float>(grid.height());

    const float normalizedX = (screenX - mapX) / mapW;
    const float normalizedY = (screenY - mapY) / mapH;

    outWorldX = normalizedX * worldWidth;
    outWorldY = normalizedY * worldHeight;
}

void Minimap::drawOverlays(vulkan_app::renderer::Renderer2D& renderer2d,
                            const aoc::map::HexGrid& grid,
                            const std::vector<MinimapPip>& pips,
                            float mapX, float mapY,
                            float mapW, float mapH) const {
    const int32_t gridWidth  = grid.width();
    const int32_t gridHeight = grid.height();
    if (gridWidth <= 0 || gridHeight <= 0) {
        return;
    }

    const float tileW = mapW / static_cast<float>(gridWidth);
    const float tileH = mapH / static_cast<float>(gridHeight);

    for (const MinimapPip& pip : pips) {
        const hex::OffsetCoord offset = hex::axialToOffset(pip.position);
        if (offset.col < 0 || offset.col >= gridWidth ||
            offset.row < 0 || offset.row >= gridHeight) {
            continue;
        }

        const float px = mapX + static_cast<float>(offset.col) * tileW + tileW * 0.5f;
        const float py = mapY + static_cast<float>(offset.row) * tileH + tileH * 0.5f;

        // Player color
        const std::size_t ci = static_cast<std::size_t>(pip.owner) % PLAYER_COLOR_COUNT;
        const float cr = PLAYER_COLORS[ci][0];
        const float cg = PLAYER_COLORS[ci][1];
        const float cb = PLAYER_COLORS[ci][2];

        if (pip.isCity) {
            // City markers: slightly larger filled circle
            const float cityRadius = std::max(2.0f, std::min(tileW, tileH) * 1.2f);
            renderer2d.drawFilledCircle(px, py, cityRadius, cr, cg, cb, 1.0f);
        } else {
            // Unit pips: small dot
            const float unitRadius = std::max(1.0f, std::min(tileW, tileH) * 0.6f);
            renderer2d.drawFilledCircle(px, py, unitRadius, cr, cg, cb, 0.9f);
        }
    }
}

} // namespace aoc::render

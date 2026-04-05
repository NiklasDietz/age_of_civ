/**
 * @file MapRenderer.cpp
 * @brief Hex map rendering with terrain colors, features, and rivers.
 */

#include "aoc/render/MapRenderer.hpp"
#include "aoc/render/CameraController.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <renderer/Renderer2D.hpp>

#include <algorithm>

namespace aoc::render {

void MapRenderer::draw(vulkan_app::renderer::Renderer2D& renderer2d,
                        const aoc::map::HexGrid& grid,
                        const aoc::map::FogOfWar& fog,
                        PlayerId viewingPlayer,
                        const CameraController& camera,
                        uint32_t screenWidth, uint32_t screenHeight) const {
    // Compute visible world-space bounds from camera
    float topLeftX = 0.0f, topLeftY = 0.0f;
    float botRightX = 0.0f, botRightY = 0.0f;
    camera.screenToWorld(0.0, 0.0, topLeftX, topLeftY, screenWidth, screenHeight);
    camera.screenToWorld(static_cast<double>(screenWidth), static_cast<double>(screenHeight),
                         botRightX, botRightY, screenWidth, screenHeight);

    // Add margin to avoid popping at edges
    const float margin = this->m_hexSize * 3.0f;
    topLeftX  -= margin;
    topLeftY  -= margin;
    botRightX += margin;
    botRightY += margin;

    // Iterate all tiles and draw those within the visible region.
    // For large maps this should use spatial indexing; for now a bounds check is sufficient.
    const int32_t width  = grid.width();
    const int32_t height = grid.height();

    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            int32_t index = row * width + col;

            aoc::map::TileVisibility vis = fog.visibility(viewingPlayer, index);
            if (vis == aoc::map::TileVisibility::Unseen) {
                continue;
            }

            hex::AxialCoord axial = hex::offsetToAxial({col, row});
            float cx = 0.0f, cy = 0.0f;
            hex::axialToPixel(axial, this->m_hexSize, cx, cy);

            if (cx < topLeftX || cx > botRightX || cy < topLeftY || cy > botRightY) {
                continue;
            }

            this->drawTile(renderer2d, grid, index, cx, cy,
                           vis == aoc::map::TileVisibility::Revealed);
        }
    }
}

void MapRenderer::drawTile(vulkan_app::renderer::Renderer2D& renderer2d,
                            const aoc::map::HexGrid& grid,
                            int32_t tileIndex, float cx, float cy,
                            bool dimmed) const {
    // Compute hex vertices
    float points[12];
    hex::hexVertices(cx, cy, this->m_hexSize, points);

    // Base terrain color
    map::TerrainType terrain = grid.terrain(tileIndex);
    map::TerrainColor baseColor = map::terrainColor(terrain);

    // Apply feature tint
    map::FeatureType feature = grid.feature(tileIndex);
    map::TerrainColor tint = map::featureColorTint(feature);
    float r = std::clamp(baseColor.r + tint.r, 0.0f, 1.0f);
    float g = std::clamp(baseColor.g + tint.g, 0.0f, 1.0f);
    float b = std::clamp(baseColor.b + tint.b, 0.0f, 1.0f);

    // Dim revealed-but-not-visible tiles
    if (dimmed) {
        constexpr float DIM_FACTOR = 0.4f;
        r *= DIM_FACTOR;
        g *= DIM_FACTOR;
        b *= DIM_FACTOR;
    }

    // Draw filled hex
    renderer2d.drawFilledPolygon(points, 6, r, g, b, 1.0f);

    // Draw hex border (subtle)
    renderer2d.drawPolygon(points, 6, 0.0f, 0.0f, 0.0f, 0.15f, 1.0f);

    // Draw feature indicators
    if (feature == map::FeatureType::Forest || feature == map::FeatureType::Jungle) {
        // Small triangle to indicate tree/vegetation
        float treeSize = this->m_hexSize * 0.25f;
        float treeR = (feature == map::FeatureType::Forest) ? 0.15f : 0.10f;
        float treeG = (feature == map::FeatureType::Forest) ? 0.40f : 0.35f;
        float treeB = (feature == map::FeatureType::Forest) ? 0.10f : 0.05f;
        renderer2d.drawFilledTriangle(
            cx, cy - treeSize,
            cx - treeSize * 0.7f, cy + treeSize * 0.5f,
            cx + treeSize * 0.7f, cy + treeSize * 0.5f,
            treeR, treeG, treeB, 0.8f
        );
    } else if (feature == map::FeatureType::Hills) {
        // Small bump shape
        float hillSize = this->m_hexSize * 0.20f;
        renderer2d.drawFilledCircle(cx, cy + hillSize * 0.5f, hillSize,
                                     0.50f, 0.45f, 0.35f, 0.5f);
    } else if (feature == map::FeatureType::Oasis) {
        // Small blue-green circle
        float oasisSize = this->m_hexSize * 0.18f;
        renderer2d.drawFilledCircle(cx, cy, oasisSize, 0.15f, 0.55f, 0.40f, 0.8f);
    }

    // Draw mountain peak indicator
    if (terrain == map::TerrainType::Mountain) {
        float peakSize = this->m_hexSize * 0.35f;
        renderer2d.drawFilledTriangle(
            cx, cy - peakSize,
            cx - peakSize * 0.6f, cy + peakSize * 0.3f,
            cx + peakSize * 0.6f, cy + peakSize * 0.3f,
            0.55f, 0.50f, 0.45f, 0.9f
        );
        // Snow cap
        renderer2d.drawFilledTriangle(
            cx, cy - peakSize,
            cx - peakSize * 0.25f, cy - peakSize * 0.4f,
            cx + peakSize * 0.25f, cy - peakSize * 0.4f,
            0.90f, 0.92f, 0.95f, 0.9f
        );
    }

    // Draw natural wonder indicator (golden diamond shape)
    map::NaturalWonderType wonder = grid.naturalWonder(tileIndex);
    if (wonder != map::NaturalWonderType::None) {
        const float wonderSize = this->m_hexSize * 0.30f;
        // Golden diamond shape
        renderer2d.drawFilledTriangle(
            cx, cy - wonderSize,
            cx - wonderSize * 0.7f, cy,
            cx + wonderSize * 0.7f, cy,
            0.95f, 0.80f, 0.20f, 0.9f
        );
        renderer2d.drawFilledTriangle(
            cx, cy + wonderSize,
            cx - wonderSize * 0.7f, cy,
            cx + wonderSize * 0.7f, cy,
            0.85f, 0.70f, 0.15f, 0.9f
        );
        // Golden border glow
        renderer2d.drawCircle(cx, cy, wonderSize * 1.2f,
                              1.0f, 0.85f, 0.30f, 0.7f, 2.0f);
    }

    // Draw resource icon (color-coded dot at bottom of hex)
    const ResourceId tileResource = grid.resource(tileIndex);
    if (tileResource.isValid() && tileResource.value < aoc::sim::goodCount()) {
        const aoc::sim::GoodDef& gdef = aoc::sim::goodDef(tileResource.value);
        const float dotRadius = this->m_hexSize * 0.12f;
        const float dotY = cy + this->m_hexSize * 0.45f;

        float dotR = 0.5f;
        float dotG = 0.5f;
        float dotB = 0.5f;

        switch (gdef.category) {
            case aoc::sim::GoodCategory::RawStrategic:
                dotR = 0.9f; dotG = 0.35f; dotB = 0.15f;  // Orange-red
                break;
            case aoc::sim::GoodCategory::RawLuxury:
                dotR = 0.7f; dotG = 0.3f; dotB = 0.85f;   // Purple
                break;
            case aoc::sim::GoodCategory::RawBonus:
                dotR = 0.3f; dotG = 0.8f; dotB = 0.3f;    // Green
                break;
            default:
                break;
        }

        if (dimmed) {
            constexpr float DIM = 0.4f;
            dotR *= DIM;
            dotG *= DIM;
            dotB *= DIM;
        }

        renderer2d.drawFilledCircle(cx, dotY, dotRadius, dotR, dotG, dotB, 0.9f);
    }

    // Draw rivers
    uint8_t riverMask = grid.riverEdges(tileIndex);
    if (riverMask != 0) {
        this->drawRiverEdges(renderer2d, riverMask, cx, cy);
    }
}

void MapRenderer::drawRiverEdges(vulkan_app::renderer::Renderer2D& renderer2d,
                                  uint8_t riverMask, float cx, float cy) const {
    // Rivers are drawn as blue lines along hex edges.
    // For each edge with a river, draw a line from the midpoint of that edge
    // toward the hex center (shortened to look like a river segment).
    float vertices[12];
    hex::hexVertices(cx, cy, this->m_hexSize, vertices);

    for (int dir = 0; dir < 6; ++dir) {
        if ((riverMask & (1u << dir)) == 0) {
            continue;
        }

        // Edge midpoint between vertex[dir] and vertex[(dir+1)%6]
        int next = (dir + 1) % 6;
        float mx = (vertices[dir * 2] + vertices[next * 2]) * 0.5f;
        float my = (vertices[dir * 2 + 1] + vertices[next * 2 + 1]) * 0.5f;

        // Draw from edge midpoint toward center (but stop partway)
        float riverEndX = cx + (mx - cx) * 0.3f;
        float riverEndY = cy + (my - cy) * 0.3f;

        renderer2d.drawLine(mx, my, riverEndX, riverEndY,
                            2.0f, 0.15f, 0.35f, 0.75f, 0.9f);
    }
}

} // namespace aoc::render

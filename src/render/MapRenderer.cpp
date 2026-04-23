/**
 * @file MapRenderer.cpp
 * @brief Hex map rendering with terrain colors, features, and rivers.
 */

#include "aoc/core/Log.hpp"
#include "aoc/render/MapRenderer.hpp"
#include "aoc/render/DrawCommandBuffer.hpp"
#include "aoc/render/CameraController.hpp"
#include "aoc/ui/BitmapFont.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <cstdio>

#include <renderer/Renderer2D.hpp>

#include <algorithm>

namespace {

/// Player colors for territory overlay (matches UnitRenderer).
constexpr std::array<std::array<float, 3>, 8> TERRITORY_COLORS = {{
    {0.20f, 0.40f, 0.90f},  // Player 0: blue
    {0.90f, 0.20f, 0.20f},  // Player 1: red
    {0.20f, 0.80f, 0.20f},  // Player 2: green
    {0.90f, 0.80f, 0.10f},  // Player 3: yellow
    {0.70f, 0.30f, 0.80f},  // Player 4: purple
    {0.90f, 0.50f, 0.10f},  // Player 5: orange
    {0.10f, 0.80f, 0.80f},  // Player 6: cyan
    {0.80f, 0.40f, 0.60f},  // Player 7: pink
}};

} // anonymous namespace

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

    // Convert world-space bounds to approximate grid row/col ranges so we
    // only iterate tiles that could possibly be visible, instead of the full
    // grid (width * height).  The hex layout is pointy-top with:
    //   x-spacing = sqrt(3) * hexSize,  y-spacing = 1.5 * hexSize.
    const int32_t width  = grid.width();
    const int32_t height = grid.height();

    constexpr float SQRT3 = 1.7320508075688772f;
    const float xSpacing = SQRT3 * this->m_hexSize;
    const float ySpacing = 1.5f  * this->m_hexSize;

    const bool cylindrical = (grid.topology() == aoc::map::MapTopology::Cylindrical);

    // For cylindrical topology: don't clamp columns — let them extend beyond
    // [0, width) so tiles wrap seamlessly. Use wrapped col for data lookup,
    // raw col for pixel position.
    const int32_t minCol = cylindrical
        ? (static_cast<int32_t>(topLeftX / xSpacing) - 1)
        : std::max(0, static_cast<int32_t>(std::max(0.0f, topLeftX) / xSpacing) - 1);
    const int32_t maxCol = cylindrical
        ? (static_cast<int32_t>(botRightX / xSpacing) + 1)
        : std::min(width - 1, static_cast<int32_t>(std::max(0.0f, botRightX) / xSpacing) + 1);
    const int32_t minRow = std::max(0,          static_cast<int32_t>(std::max(0.0f, topLeftY)  / ySpacing) - 1);
    const int32_t maxRow = std::min(height - 1, static_cast<int32_t>(std::max(0.0f, botRightY) / ySpacing) + 1);

    for (int32_t row = minRow; row <= maxRow; ++row) {
        for (int32_t col = minCol; col <= maxCol; ++col) {
            // Wrap column for data lookup (cylindrical), or skip if OOB (flat)
            int32_t dataCol = col;
            if (cylindrical) {
                dataCol = ((col % width) + width) % width;
            } else {
                if (col < 0 || col >= width) { continue; }
            }
            const int32_t index = row * width + dataCol;

            const aoc::map::TileVisibility vis = fog.visibility(viewingPlayer, index);
            if (vis == aoc::map::TileVisibility::Unseen) {
                continue;
            }

            // Use the RAW (unwrapped) col for pixel position so tiles render
            // at the correct screen location across the seam.
            const hex::AxialCoord axial = hex::offsetToAxial({col, row});
            float cx = 0.0f, cy = 0.0f;
            hex::axialToPixel(axial, this->m_hexSize, cx, cy);

            // Fine-grained per-tile check (accounts for odd-row offset shift)
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
    const float hs = this->m_hexSize;
    float points[12];
    hex::hexVertices(cx, cy, hs, points);

    map::TerrainType terrain = grid.terrain(tileIndex);
    map::TerrainColor baseColor = map::terrainColor(terrain);
    map::FeatureType feature = grid.feature(tileIndex);
    map::TerrainColor tint = map::featureColorTint(feature);

    float r = std::clamp(baseColor.r + tint.r, 0.0f, 1.0f);
    float g = std::clamp(baseColor.g + tint.g, 0.0f, 1.0f);
    float b = std::clamp(baseColor.b + tint.b, 0.0f, 1.0f);

    // Per-tile color variation: deterministic noise based on tile index.
    // Makes terrain look less uniform without affecting gameplay.
    {
        uint32_t noiseHash = static_cast<uint32_t>(tileIndex) * 2654435761u;
        float noise = (static_cast<float>(noiseHash % 1000u) / 1000.0f - 0.5f) * 0.08f;
        r = std::clamp(r + noise, 0.0f, 1.0f);
        g = std::clamp(g + noise * 0.8f, 0.0f, 1.0f);
        b = std::clamp(b + noise * 0.6f, 0.0f, 1.0f);
    }

    if (dimmed) {
        constexpr float DIM = 0.4f;
        r *= DIM; g *= DIM; b *= DIM;
    }

    // ---- Base hex fill: SDF hexagon (single primitive, no seams) ----
    const float hexW = hs * 0.866f * 1.02f;
    const float hexH = hs * 1.02f;
    renderer2d.drawFilledHexagon(cx, cy, hexW, hexH, r, g, b, 1.0f);

    // ---- Hex grid outline (subtle border between tiles) ----
    if (!dimmed) {
        float borderAlpha = 0.15f;
        renderer2d.drawHexagonOutline(cx, cy, hexW * 0.98f, hexH * 0.98f,
                                      0.0f, 0.0f, 0.0f, borderAlpha, 1.0f);
    }

    // Territory borders are drawn as oversized halos in drawTerritoryBorders()
    // (rendered before terrain). No per-tile tint overlay needed.

    // ---- Mountains: layered triangles forming a mountain range ----
    if (terrain == map::TerrainType::Mountain) {
        const float s = hs * 0.38f;
        // Left peak
        renderer2d.drawFilledTriangle(
            cx - s * 0.5f, cy - s * 0.7f,
            cx - s * 1.1f, cy + s * 0.4f,
            cx + s * 0.1f, cy + s * 0.4f,
            0.50f, 0.46f, 0.40f, dimmed ? 0.4f : 0.95f);
        // Right peak (taller)
        renderer2d.drawFilledTriangle(
            cx + s * 0.3f, cy - s * 1.0f,
            cx - s * 0.3f, cy + s * 0.4f,
            cx + s * 0.9f, cy + s * 0.4f,
            0.55f, 0.50f, 0.44f, dimmed ? 0.4f : 0.95f);
        // Snow caps
        renderer2d.drawFilledTriangle(
            cx + s * 0.3f, cy - s * 1.0f,
            cx + s * 0.05f, cy - s * 0.5f,
            cx + s * 0.55f, cy - s * 0.5f,
            0.92f, 0.94f, 0.96f, dimmed ? 0.3f : 0.9f);
        renderer2d.drawFilledTriangle(
            cx - s * 0.5f, cy - s * 0.7f,
            cx - s * 0.7f, cy - s * 0.25f,
            cx - s * 0.3f, cy - s * 0.25f,
            0.88f, 0.90f, 0.93f, dimmed ? 0.3f : 0.85f);
    }

    // ---- Hills: overlapping rounded bumps ----
    if (feature == map::FeatureType::Hills) {
        const float s = hs * 0.22f;
        float ha = dimmed ? 0.3f : 0.55f;
        // Two overlapping hill bumps
        renderer2d.drawFilledCircle(cx - s * 0.5f, cy + s * 0.3f, s * 1.1f,
                                     0.52f, 0.48f, 0.38f, ha);
        renderer2d.drawFilledCircle(cx + s * 0.4f, cy + s * 0.1f, s * 1.0f,
                                     0.56f, 0.52f, 0.42f, ha);
        // Highlight on top
        renderer2d.drawFilledCircle(cx + s * 0.3f, cy - s * 0.2f, s * 0.5f,
                                     0.62f, 0.58f, 0.48f, ha * 0.6f);
    }

    // ---- Forest: three small tree shapes ----
    if (feature == map::FeatureType::Forest || feature == map::FeatureType::Jungle) {
        const float s = hs * 0.18f;
        const bool jungle = (feature == map::FeatureType::Jungle);
        const float tR = jungle ? 0.08f : 0.12f;
        const float tG = jungle ? 0.32f : 0.38f;
        const float tB = jungle ? 0.05f : 0.08f;
        const float fa = dimmed ? 0.35f : 0.85f;

        // Three trees at different positions
        float treeX[3] = {cx - s * 1.2f, cx + s * 0.2f, cx + s * 1.4f};
        float treeY[3] = {cy + s * 0.3f, cy - s * 0.5f, cy + s * 0.2f};
        float treeScale[3] = {0.9f, 1.1f, 0.85f};

        for (int t = 0; t < 3; ++t) {
            float tx = treeX[t];
            float ty = treeY[t];
            float ts = s * treeScale[t];
            // Tree crown (triangle)
            renderer2d.drawFilledTriangle(
                tx, ty - ts * 1.4f,
                tx - ts * 0.8f, ty,
                tx + ts * 0.8f, ty,
                tR, tG, tB, fa);
            // Second layer (wider, overlapping)
            renderer2d.drawFilledTriangle(
                tx, ty - ts * 0.8f,
                tx - ts * 1.0f, ty + ts * 0.5f,
                tx + ts * 1.0f, ty + ts * 0.5f,
                tR * 0.85f, tG * 0.9f, tB * 0.85f, fa);
            // Trunk (small rect)
            renderer2d.drawFilledRect(
                tx - ts * 0.15f, ty + ts * 0.3f,
                ts * 0.3f, ts * 0.5f,
                0.35f, 0.22f, 0.10f, fa);
        }
    }

    // ---- Marsh: wavy water lines ----
    if (feature == map::FeatureType::Marsh) {
        const float s = hs * 0.15f;
        const float ma = dimmed ? 0.2f : 0.5f;
        for (int i = -1; i <= 1; ++i) {
            float ly = cy + static_cast<float>(i) * s * 1.5f;
            renderer2d.drawLine(cx - s * 2.0f, ly, cx - s * 0.5f, ly - s * 0.4f,
                                1.5f, 0.2f, 0.4f, 0.6f, ma);
            renderer2d.drawLine(cx - s * 0.5f, ly - s * 0.4f, cx + s * 1.0f, ly + s * 0.3f,
                                1.5f, 0.2f, 0.4f, 0.6f, ma);
            renderer2d.drawLine(cx + s * 1.0f, ly + s * 0.3f, cx + s * 2.0f, ly,
                                1.5f, 0.2f, 0.4f, 0.6f, ma);
        }
    }

    // ---- Oasis: water pool with palm tree ----
    if (feature == map::FeatureType::Oasis) {
        const float s = hs * 0.20f;
        const float oa = dimmed ? 0.3f : 0.8f;
        // Water pool
        renderer2d.drawFilledCircle(cx, cy + s * 0.3f, s * 1.2f, 0.15f, 0.45f, 0.65f, oa);
        // Palm trunk
        renderer2d.drawLine(cx - s * 0.8f, cy + s * 0.5f, cx - s * 0.3f, cy - s * 0.8f,
                            2.0f, 0.40f, 0.28f, 0.12f, oa);
        // Palm fronds
        renderer2d.drawFilledTriangle(
            cx - s * 0.3f, cy - s * 0.8f,
            cx - s * 1.3f, cy - s * 0.3f,
            cx + s * 0.2f, cy - s * 0.4f,
            0.15f, 0.45f, 0.10f, oa);
    }

    // ---- Natural wonders: golden star shape ----
    map::NaturalWonderType wonder = grid.naturalWonder(tileIndex);
    if (wonder != map::NaturalWonderType::None) {
        const float s = hs * 0.30f;
        const float wa = dimmed ? 0.4f : 0.95f;
        // Diamond shape
        renderer2d.drawFilledTriangle(cx, cy - s, cx - s * 0.7f, cy, cx + s * 0.7f, cy,
                                       0.95f, 0.82f, 0.22f, wa);
        renderer2d.drawFilledTriangle(cx, cy + s, cx - s * 0.7f, cy, cx + s * 0.7f, cy,
                                       0.88f, 0.72f, 0.18f, wa);
        // Glow ring
        renderer2d.drawCircle(cx, cy, s * 1.3f, 1.0f, 0.88f, 0.35f, wa * 0.6f, 2.0f);
    }

    // ---- Canal: blue waterway stripe through the hex ----
    map::ImprovementType improvement = grid.improvement(tileIndex);
    if (improvement == map::ImprovementType::Canal) {
        const float ca = dimmed ? 0.25f : 0.65f;
        const float canalW = hs * 0.35f;
        // Central water channel (vertical stripe)
        renderer2d.drawFilledRect(cx - canalW * 0.5f, cy - hs * 0.6f,
                                  canalW, hs * 1.2f,
                                  0.18f, 0.42f, 0.68f, ca);
        // Stone banks on each side
        const float bankW = hs * 0.06f;
        const float bankA = dimmed ? 0.2f : 0.55f;
        renderer2d.drawFilledRect(cx - canalW * 0.5f - bankW, cy - hs * 0.6f,
                                  bankW, hs * 1.2f,
                                  0.50f, 0.48f, 0.42f, bankA);
        renderer2d.drawFilledRect(cx + canalW * 0.5f, cy - hs * 0.6f,
                                  bankW, hs * 1.2f,
                                  0.50f, 0.48f, 0.42f, bankA);
    }

    // ---- Improvement indicator (small icon at top of hex) ----
    if (improvement != map::ImprovementType::None && improvement != map::ImprovementType::Road
        && improvement != map::ImprovementType::Canal) {
        const float s = hs * 0.12f;
        const float iy = cy - hs * 0.35f;
        const float ia = dimmed ? 0.3f : 0.7f;

        switch (improvement) {
            case map::ImprovementType::Farm:
                // Wheat icon: small filled rect
                renderer2d.drawFilledRect(cx - s, iy - s * 0.5f, s * 2.0f, s, 0.75f, 0.65f, 0.20f, ia);
                break;
            case map::ImprovementType::Mine:
                // Pickaxe: small triangle
                renderer2d.drawFilledTriangle(cx, iy - s, cx - s, iy + s * 0.5f, cx + s, iy + s * 0.5f,
                                               0.55f, 0.50f, 0.45f, ia);
                break;
            case map::ImprovementType::Plantation:
                renderer2d.drawFilledCircle(cx, iy, s * 0.8f, 0.40f, 0.65f, 0.20f, ia);
                break;
            case map::ImprovementType::LumberMill:
                renderer2d.drawFilledRect(cx - s * 0.8f, iy - s * 0.3f, s * 1.6f, s * 0.6f, 0.50f, 0.35f, 0.15f, ia);
                break;
            case map::ImprovementType::Fort:
                // Small square with cross
                renderer2d.drawFilledRect(cx - s, iy - s, s * 2.0f, s * 2.0f, 0.45f, 0.45f, 0.50f, ia);
                renderer2d.drawLine(cx - s * 0.7f, iy, cx + s * 0.7f, iy, 1.5f, 0.7f, 0.7f, 0.7f, ia);
                renderer2d.drawLine(cx, iy - s * 0.7f, cx, iy + s * 0.7f, 1.5f, 0.7f, 0.7f, 0.7f, ia);
                break;
            default:
                // Generic small diamond for other improvements
                renderer2d.drawFilledTriangle(cx, iy - s, cx - s * 0.6f, iy, cx + s * 0.6f, iy, 0.6f, 0.55f, 0.4f, ia);
                renderer2d.drawFilledTriangle(cx, iy + s, cx - s * 0.6f, iy, cx + s * 0.6f, iy, 0.55f, 0.50f, 0.35f, ia);
                break;
        }
    }

    // ---- Road indicator (dashed center line) ----
    if (grid.hasRoad(tileIndex) && !dimmed) {
        renderer2d.drawFilledCircle(cx, cy, hs * 0.06f, 0.45f, 0.40f, 0.30f, 0.5f);
    }

    // ---- Resource icon (only if revealed by tech) ----
    const ResourceId tileResource = grid.resource(tileIndex);
    if (tileResource.isValid() && tileResource.value < aoc::sim::goodCount()
        && this->isResourceRevealed(tileResource.value)) {
        const aoc::sim::GoodDef& gdef = aoc::sim::goodDef(tileResource.value);
        const float s = hs * 0.13f;
        const float ry = cy + hs * 0.38f;
        const float ra = dimmed ? 0.35f : 0.9f;

        float rr = 0.5f, rg = 0.5f, rb = 0.5f;
        switch (gdef.category) {
            case aoc::sim::GoodCategory::RawStrategic:
                rr = 0.9f; rg = 0.40f; rb = 0.15f;
                // Strategic: hexagonal shape (small filled circle with border)
                renderer2d.drawFilledCircle(cx, ry, s * 1.1f, rr, rg, rb, ra);
                renderer2d.drawCircle(cx, ry, s * 1.1f, 1.0f, 1.0f, 1.0f, ra * 0.6f, 1.5f);
                break;
            case aoc::sim::GoodCategory::RawLuxury:
                rr = 0.75f; rg = 0.35f; rb = 0.85f;
                // Luxury: diamond shape
                renderer2d.drawFilledTriangle(cx, ry - s, cx - s * 0.7f, ry, cx + s * 0.7f, ry, rr, rg, rb, ra);
                renderer2d.drawFilledTriangle(cx, ry + s, cx - s * 0.7f, ry, cx + s * 0.7f, ry, rr * 0.8f, rg * 0.8f, rb * 0.8f, ra);
                break;
            case aoc::sim::GoodCategory::RawBonus:
                rr = 0.35f; rg = 0.75f; rb = 0.30f;
                // Bonus: small square
                renderer2d.drawFilledRect(cx - s * 0.8f, ry - s * 0.8f, s * 1.6f, s * 1.6f, rr, rg, rb, ra);
                break;
            default:
                break;
        }
    }

    // ---- Rivers ----
    uint8_t riverMask = grid.riverEdges(tileIndex);
    if (riverMask != 0) {
        this->drawRiverEdges(renderer2d, riverMask, cx, cy);
    }
}

void MapRenderer::drawTerritoryBorders(vulkan_app::renderer::Renderer2D& renderer2d,
                                        const aoc::map::HexGrid& grid,
                                        const aoc::map::FogOfWar& fog,
                                        PlayerId viewingPlayer,
                                        const CameraController& camera,
                                        uint32_t screenWidth,
                                        uint32_t screenHeight) const {
    // Compute visible world-space bounds from camera
    float topLeftX = 0.0f, topLeftY = 0.0f;
    float botRightX = 0.0f, botRightY = 0.0f;
    camera.screenToWorld(0.0, 0.0, topLeftX, topLeftY, screenWidth, screenHeight);
    camera.screenToWorld(static_cast<double>(screenWidth), static_cast<double>(screenHeight),
                         botRightX, botRightY, screenWidth, screenHeight);

    const float margin = this->m_hexSize * 3.0f;
    topLeftX  -= margin;
    topLeftY  -= margin;
    botRightX += margin;
    botRightY += margin;

    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    constexpr float SQRT3_B = 1.7320508075688772f;
    const float xSpacing = SQRT3_B * this->m_hexSize;
    const float ySpacing = 1.5f   * this->m_hexSize;

    const bool cylindricalB = (grid.topology() == aoc::map::MapTopology::Cylindrical);
    const int32_t minCol = cylindricalB
        ? (static_cast<int32_t>(topLeftX / xSpacing) - 1)
        : std::max(0, static_cast<int32_t>(std::max(0.0f, topLeftX) / xSpacing) - 1);
    const int32_t maxCol = cylindricalB
        ? (static_cast<int32_t>(botRightX / xSpacing) + 1)
        : std::min(width - 1, static_cast<int32_t>(std::max(0.0f, botRightX) / xSpacing) + 1);
    const int32_t minRow = std::max(0,          static_cast<int32_t>(std::max(0.0f, topLeftY)  / ySpacing) - 1);
    const int32_t maxRow = std::min(height - 1, static_cast<int32_t>(std::max(0.0f, botRightY) / ySpacing) + 1);

    for (int32_t row = minRow; row <= maxRow; ++row) {
        for (int32_t col = minCol; col <= maxCol; ++col) {
            int32_t dataCol = col;
            if (cylindricalB) {
                dataCol = ((col % width) + width) % width;
            } else {
                if (col < 0 || col >= width) { continue; }
            }
            const int32_t index = row * width + dataCol;
            const PlayerId tileOwner = grid.owner(index);
            if (tileOwner == INVALID_PLAYER) {
                continue;
            }

            // Fog check: own territory on Revealed+Visible, foreign only on Visible
            const aoc::map::TileVisibility tileVis = fog.visibility(viewingPlayer, index);
            if (tileVis == aoc::map::TileVisibility::Unseen) { continue; }
            if (tileOwner != viewingPlayer && tileVis != aoc::map::TileVisibility::Visible) {
                continue;
            }

            hex::AxialCoord axial = hex::offsetToAxial({col, row});
            float cx = 0.0f, cy = 0.0f;
            hex::axialToPixel(axial, this->m_hexSize, cx, cy);

            if (cx < topLeftX || cx > botRightX || cy < topLeftY || cy > botRightY) {
                continue;
            }

            // Get player color
            const std::size_t ci = static_cast<std::size_t>(tileOwner) % TERRITORY_COLORS.size();
            const float cr = TERRITORY_COLORS[ci][0];
            const float cg = TERRITORY_COLORS[ci][1];
            const float cb = TERRITORY_COLORS[ci][2];

            // Civ 6-style single outline per territory: draw each of the 6
            // hex edges only when the neighbor across that edge belongs to a
            // different owner.  Interior edges (same-owner neighbours) stay
            // invisible, so the rendered border traces the perimeter of the
            // territory instead of framing every tile individually.
            //
            // Screen Y is inverted (down-positive), so hexVertices() with
            // angles {30°,90°,150°,210°,270°,330°} places:
            //   v0 lower-right, v1 bottom,      v2 lower-left
            //   v3 upper-left,  v4 top,         v5 upper-right
            // Edge e connects v[e]→v[(e+1)%6], giving:
            //   e0 lower-right edge  ↔ dir 5 (lower-right neighbour: (0,+1))
            //   e1 lower-left edge   ↔ dir 4 (lower-left:  (-1,+1))
            //   e2 left edge         ↔ dir 3 (left:        (-1, 0))
            //   e3 upper-left edge   ↔ dir 2 (upper-left:  ( 0,-1))
            //   e4 upper-right edge  ↔ dir 1 (upper-right: (+1,-1))
            //   e5 right edge        ↔ dir 0 (right:       (+1, 0))
            // => dir = (5 - edge + 6) % 6.
            const std::array<hex::AxialCoord, 6> neighCoords = hex::neighbors(axial);
            float edgeVerts[12];
            hex::hexVertices(cx, cy, this->m_hexSize, edgeVerts);

            for (int edge = 0; edge < 6; ++edge) {
                const int dir = (5 - edge + 6) % 6;
                const hex::AxialCoord& neighbor = neighCoords[static_cast<std::size_t>(dir)];
                PlayerId neighborOwner = INVALID_PLAYER;
                if (grid.isValid(neighbor)) {
                    neighborOwner = grid.owner(grid.toIndex(neighbor));
                }
                // Off-map neighbours stay INVALID_PLAYER, which != tileOwner,
                // so border closes at the map edge. HexGrid::isValid handles
                // cylindrical wrap internally.
                if (neighborOwner == tileOwner) { continue; }

                const int v0 = edge;
                const int v1 = (edge + 1) % 6;
                const float x1 = edgeVerts[v0 * 2];
                const float y1 = edgeVerts[v0 * 2 + 1];
                const float x2 = edgeVerts[v1 * 2];
                const float y2 = edgeVerts[v1 * 2 + 1];
                renderer2d.drawLine(x1, y1, x2, y2, 3.0f, cr, cg, cb, 0.95f);
            }
        }
    }
}

void MapRenderer::drawRiverEdges(vulkan_app::renderer::Renderer2D& renderer2d,
                                  uint8_t riverMask, float cx, float cy) const {
    // Rivers run along shared tile boundaries.  Each set bit of riverMask is
    // drawn as a capsule along the corresponding edge (dir → edge mapping
    // via (5 - dir + 6) % 6 in the hexVertices layout).  Generator enforces
    // adjacent-edge transitions so consecutive river segments share a vertex
    // — rounded capsule caps then overlap cleanly at each junction.
    float vertices[12];
    hex::hexVertices(cx, cy, this->m_hexSize, vertices);

    for (int dir = 0; dir < 6; ++dir) {
        if ((riverMask & (1u << dir)) == 0) { continue; }
        const int edge = (5 - dir + 6) % 6;
        const int v0 = edge;
        const int v1 = (edge + 1) % 6;
        const float x1 = vertices[v0 * 2];
        const float y1 = vertices[v0 * 2 + 1];
        const float x2 = vertices[v1 * 2];
        const float y2 = vertices[v1 * 2 + 1];
        renderer2d.drawCapsule(x1, y1, x2, y2,
                               2.5f, 0.10f, 0.35f, 0.80f, 1.0f, 0.0f);
    }
}

void MapRenderer::drawToBuffer(DrawCommandBuffer& buffer,
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

    const float margin = this->m_hexSize * 3.0f;
    topLeftX  -= margin;
    topLeftY  -= margin;
    botRightX += margin;
    botRightY += margin;

    const int32_t width  = grid.width();
    const int32_t height = grid.height();

    constexpr float SQRT3 = 1.7320508075688772f;
    const float xSpacing = SQRT3 * this->m_hexSize;
    const float ySpacing = 1.5f  * this->m_hexSize;

    const int32_t minCol = std::max(0,          static_cast<int32_t>(std::max(0.0f, topLeftX)  / xSpacing) - 1);
    const int32_t maxCol = std::min(width  - 1, static_cast<int32_t>(std::max(0.0f, botRightX) / xSpacing) + 1);
    const int32_t minRow = std::max(0,          static_cast<int32_t>(std::max(0.0f, topLeftY)  / ySpacing) - 1);
    const int32_t maxRow = std::min(height - 1, static_cast<int32_t>(std::max(0.0f, botRightY) / ySpacing) + 1);

    for (int32_t row = minRow; row <= maxRow; ++row) {
        for (int32_t col = minCol; col <= maxCol; ++col) {
            const int32_t index = row * width + col;

            const aoc::map::TileVisibility vis = fog.visibility(viewingPlayer, index);
            if (vis == aoc::map::TileVisibility::Unseen) {
                continue;
            }

            const hex::AxialCoord axial = hex::offsetToAxial({col, row});
            float cx = 0.0f, cy = 0.0f;
            hex::axialToPixel(axial, this->m_hexSize, cx, cy);

            if (cx < topLeftX || cx > botRightX || cy < topLeftY || cy > botRightY) {
                continue;
            }

            const bool dimmed = (vis == aoc::map::TileVisibility::Revealed);
            const float hs = this->m_hexSize;

            map::TerrainType terrain = grid.terrain(index);
            map::TerrainColor baseColor = map::terrainColor(terrain);
            map::FeatureType feature = grid.feature(index);
            map::TerrainColor tint = map::featureColorTint(feature);

            float r = std::clamp(baseColor.r + tint.r, 0.0f, 1.0f);
            float g = std::clamp(baseColor.g + tint.g, 0.0f, 1.0f);
            float b = std::clamp(baseColor.b + tint.b, 0.0f, 1.0f);

            if (dimmed) {
                constexpr float DIM = 0.4f;
                r *= DIM; g *= DIM; b *= DIM;
            }

            const float hexW = hs * 0.866f * 1.02f;
            const float hexH = hs * 1.02f;
            buffer.pushHexagon(cx, cy, hexW, hexH, r, g, b, 1.0f, 0);
        }
    }
}

void MapRenderer::drawYieldLabels(vulkan_app::renderer::Renderer2D& renderer2d,
                                   const aoc::map::HexGrid& grid,
                                   const aoc::map::FogOfWar& fog,
                                   PlayerId viewingPlayer,
                                   const CameraController& camera,
                                   uint32_t screenWidth, uint32_t screenHeight) const {
    float topLeftX = 0.0f, topLeftY = 0.0f, botRightX = 0.0f, botRightY = 0.0f;
    camera.screenToWorld(0.0, 0.0, topLeftX, topLeftY, screenWidth, screenHeight);
    camera.screenToWorld(static_cast<double>(screenWidth), static_cast<double>(screenHeight),
                         botRightX, botRightY, screenWidth, screenHeight);
    float margin = this->m_hexSize * 2.0f;
    topLeftX -= margin; topLeftY -= margin;
    botRightX += margin; botRightY += margin;

    const int32_t width = grid.width();
    const int32_t height = grid.height();
    constexpr float SQRT3 = 1.7320508075688772f;
    const float xSpacing = SQRT3 * this->m_hexSize;
    const float ySpacing = 1.5f * this->m_hexSize;
    const bool cylindricalY = (grid.topology() == aoc::map::MapTopology::Cylindrical);
    const int32_t minCol = cylindricalY
        ? (static_cast<int32_t>(topLeftX / xSpacing) - 1)
        : std::max(0, static_cast<int32_t>(topLeftX / xSpacing) - 1);
    const int32_t maxCol = cylindricalY
        ? (static_cast<int32_t>(botRightX / xSpacing) + 1)
        : std::min(width - 1, static_cast<int32_t>(botRightX / xSpacing) + 1);
    const int32_t minRow = std::max(0, static_cast<int32_t>(topLeftY / ySpacing) - 1);
    const int32_t maxRow = std::min(height - 1, static_cast<int32_t>(botRightY / ySpacing) + 1);

    (void)camera;  // Camera used for bounds calculation above

    for (int32_t row = minRow; row <= maxRow; ++row) {
        for (int32_t col = minCol; col <= maxCol; ++col) {
            int32_t dataCol = col;
            if (cylindricalY) {
                dataCol = ((col % width) + width) % width;
            } else {
                if (col < 0 || col >= width) { continue; }
            }
            const int32_t index = row * width + dataCol;
            const aoc::map::TileVisibility vis = fog.visibility(viewingPlayer, index);
            if (vis != aoc::map::TileVisibility::Visible) { continue; }

            const aoc::hex::AxialCoord axial = aoc::hex::offsetToAxial({col, row});
            float cx = 0.0f, cy = 0.0f;
            aoc::hex::axialToPixel(axial, this->m_hexSize, cx, cy);
            if (cx < topLeftX || cx > botRightX || cy < topLeftY || cy > botRightY) { continue; }

            const aoc::map::TileYield yields = grid.tileYield(index);
            if (yields.food == 0 && yields.production == 0 && yields.gold == 0
                && yields.science == 0 && yields.culture == 0) {
                continue;
            }

            // Civ 6 style: colored circles for each yield type with count
            // Layout: small icons in a row above the resource icon area
            const float iconR = this->m_hexSize * 0.07f;  // Icon radius
            const float iconSpacing = iconR * 2.6f;
            const float baseY = cy + this->m_hexSize * 0.28f;  // Between center and resource icon

            // Count how many yield types to display for centering
            int32_t totalIcons = 0;
            if (yields.food > 0)       { totalIcons += yields.food; }
            if (yields.production > 0) { totalIcons += yields.production; }
            if (yields.gold > 0)       { totalIcons += yields.gold; }
            if (yields.science > 0)    { totalIcons += yields.science; }
            if (yields.culture > 0)    { totalIcons += yields.culture; }

            // Center the icons horizontally
            float totalWidth = static_cast<float>(totalIcons) * iconSpacing;
            float startX = cx - totalWidth * 0.5f + iconR;

            // Draw each yield as repeated colored circles (like Civ 6)
            float curX = startX;
            // Food: green circles
            for (int32_t fi = 0; fi < yields.food; ++fi) {
                renderer2d.drawFilledCircle(curX, baseY, iconR, 0.2f, 0.7f, 0.2f, 0.9f);
                renderer2d.drawCircle(curX, baseY, iconR, 0.1f, 0.4f, 0.1f, 0.6f, 0.8f);
                curX += iconSpacing;
            }
            // Production: orange circles
            for (int32_t pi = 0; pi < yields.production; ++pi) {
                renderer2d.drawFilledCircle(curX, baseY, iconR, 0.85f, 0.5f, 0.15f, 0.9f);
                renderer2d.drawCircle(curX, baseY, iconR, 0.5f, 0.3f, 0.1f, 0.6f, 0.8f);
                curX += iconSpacing;
            }
            // Gold: yellow circles
            for (int32_t gi = 0; gi < yields.gold; ++gi) {
                renderer2d.drawFilledCircle(curX, baseY, iconR, 0.9f, 0.8f, 0.15f, 0.9f);
                renderer2d.drawCircle(curX, baseY, iconR, 0.6f, 0.5f, 0.1f, 0.6f, 0.8f);
                curX += iconSpacing;
            }
            // Science: blue circles
            for (int32_t si = 0; si < yields.science; ++si) {
                renderer2d.drawFilledCircle(curX, baseY, iconR, 0.2f, 0.45f, 0.85f, 0.9f);
                renderer2d.drawCircle(curX, baseY, iconR, 0.1f, 0.25f, 0.5f, 0.6f, 0.8f);
                curX += iconSpacing;
            }
            // Culture: purple circles
            for (int32_t ci2 = 0; ci2 < yields.culture; ++ci2) {
                renderer2d.drawFilledCircle(curX, baseY, iconR, 0.6f, 0.2f, 0.7f, 0.9f);
                renderer2d.drawCircle(curX, baseY, iconR, 0.35f, 0.1f, 0.4f, 0.6f, 0.8f);
                curX += iconSpacing;
            }
        }
    }
}

} // namespace aoc::render

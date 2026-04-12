/**
 * @file UnitRenderer.cpp
 * @brief Draws units as colored shapes and cities as bordered circles.
 */

#include "aoc/render/UnitRenderer.hpp"
#include "aoc/render/CameraController.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/core/Selectable.hpp"

#include <renderer/Renderer2D.hpp>

#include <array>
#include <cmath>
#include <cstdlib>

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
                              const aoc::game::GameState& gameState,
                              const aoc::map::FogOfWar& fog,
                              const aoc::map::HexGrid& grid,
                              PlayerId viewingPlayer,
                              const CameraController& camera,
                              float hexSize,
                              uint32_t screenWidth, uint32_t screenHeight) const {
    // Compute visible bounds
    float topLeftX = 0.0f, topLeftY = 0.0f, botRightX = 0.0f, botRightY = 0.0f;
    camera.screenToWorld(0.0, 0.0, topLeftX, topLeftY, screenWidth, screenHeight);
    camera.screenToWorld(static_cast<double>(screenWidth), static_cast<double>(screenHeight),
                         botRightX, botRightY, screenWidth, screenHeight);
    float margin = hexSize * 2.0f;
    topLeftX -= margin; topLeftY -= margin;
    botRightX += margin; botRightY += margin;

    float unitRadius = hexSize * 0.30f;

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        for (const std::unique_ptr<aoc::game::Unit>& unitPtr : playerPtr->units()) {
            const aoc::game::Unit& unit = *unitPtr;

            // Fog of war: only show units on tiles that are currently Visible to the viewer
            if (!grid.isValid(unit.position())) {
                continue;
            }
            int32_t tileIndex = grid.toIndex(unit.position());
            aoc::map::TileVisibility vis = fog.visibility(viewingPlayer, tileIndex);
            if (vis != aoc::map::TileVisibility::Visible) {
                continue;
            }

            float cx = 0.0f, cy = 0.0f;
            if (unit.isAnimating) {
                float fromX = 0.0f, fromY = 0.0f;
                float toX = 0.0f, toY = 0.0f;
                hex::axialToPixel(unit.animFrom, hexSize, fromX, fromY);
                hex::axialToPixel(unit.animTo, hexSize, toX, toY);
                const float t = unit.animProgress;
                cx = fromX + (toX - fromX) * t;
                cy = fromY + (toY - fromY) * t;
            } else {
                hex::axialToPixel(unit.position(), hexSize, cx, cy);
            }

            // Frustum cull
            if (cx < topLeftX || cx > botRightX || cy < topLeftY || cy > botRightY) {
                continue;
            }

            float r = 0.0f, g = 0.0f, b = 0.0f;
            playerColor(unit.owner(), r, g, b);

            const float s = unitRadius;
            const aoc::sim::UnitTypeDef& def = unit.typeDef();

            // ---- Unit background: filled circle with player color ----
            renderer2d.drawFilledCircle(cx, cy, s, r * 0.35f, g * 0.35f, b * 0.35f, 0.88f);

            // ---- Unit icon (white on player-colored background) ----
            const float iconA = 0.95f;
            switch (def.unitClass) {
                case aoc::sim::UnitClass::Melee: {
                    float sw = s * 0.65f;
                    renderer2d.drawLine(cx - sw * 0.6f, cy + sw * 0.6f,
                                         cx + sw * 0.6f, cy - sw * 0.6f,
                                         2.5f, 1.0f, 1.0f, 1.0f, iconA);
                    renderer2d.drawLine(cx - sw * 0.3f, cy - sw * 0.2f,
                                         cx + sw * 0.3f, cy + sw * 0.2f,
                                         2.0f, 0.8f, 0.8f, 0.8f, iconA);
                    renderer2d.drawFilledCircle(cx - sw * 0.55f, cy + sw * 0.55f, sw * 0.15f,
                                                 0.9f, 0.85f, 0.6f, iconA);
                    break;
                }
                case aoc::sim::UnitClass::Ranged: {
                    float bw = s * 0.55f;
                    renderer2d.drawArc(cx - bw * 0.2f, cy, bw, -1.2f, 1.2f, 2.0f,
                                        1.0f, 1.0f, 1.0f, iconA);
                    renderer2d.drawLine(cx - bw * 0.3f, cy, cx + bw * 0.7f, cy,
                                         2.0f, 1.0f, 1.0f, 1.0f, iconA);
                    renderer2d.drawFilledTriangle(cx + bw * 0.7f, cy,
                                                  cx + bw * 0.4f, cy - bw * 0.2f,
                                                  cx + bw * 0.4f, cy + bw * 0.2f,
                                                  1.0f, 1.0f, 1.0f, iconA);
                    break;
                }
                case aoc::sim::UnitClass::Scout: {
                    float ew = s * 0.5f;
                    renderer2d.drawFilledCircle(cx, cy, ew * 0.5f, 1.0f, 1.0f, 1.0f, iconA);
                    renderer2d.drawFilledCircle(cx, cy, ew * 0.25f, 0.2f, 0.6f, 0.9f, iconA);
                    renderer2d.drawCircle(cx, cy, ew * 0.5f, 1.0f, 1.0f, 1.0f, iconA, 1.5f);
                    break;
                }
                case aoc::sim::UnitClass::Settler: {
                    float ww = s * 0.5f;
                    renderer2d.drawFilledRect(cx - ww, cy, ww * 2.0f, ww * 0.7f,
                                               0.6f, 0.45f, 0.25f, iconA);
                    renderer2d.drawFilledCircle(cx, cy - ww * 0.1f, ww * 0.7f,
                                                 0.9f, 0.85f, 0.75f, iconA * 0.8f);
                    renderer2d.drawFilledCircle(cx - ww * 0.6f, cy + ww * 0.7f, ww * 0.25f,
                                                 0.3f, 0.25f, 0.2f, iconA);
                    renderer2d.drawFilledCircle(cx + ww * 0.6f, cy + ww * 0.7f, ww * 0.25f,
                                                 0.3f, 0.25f, 0.2f, iconA);
                    break;
                }
                case aoc::sim::UnitClass::Cavalry: {
                    float hw = s * 0.55f;
                    renderer2d.drawFilledTriangle(cx - hw * 0.8f, cy + hw * 0.3f,
                                                  cx + hw * 0.5f, cy + hw * 0.3f,
                                                  cx + hw * 0.2f, cy - hw * 0.2f,
                                                  1.0f, 1.0f, 1.0f, iconA);
                    renderer2d.drawFilledTriangle(cx + hw * 0.2f, cy - hw * 0.2f,
                                                  cx + hw * 0.8f, cy - hw * 0.7f,
                                                  cx + hw * 0.6f, cy + hw * 0.1f,
                                                  1.0f, 1.0f, 1.0f, iconA);
                    renderer2d.drawFilledTriangle(cx + hw * 0.7f, cy - hw * 0.7f,
                                                  cx + hw * 0.55f, cy - hw * 0.9f,
                                                  cx + hw * 0.85f, cy - hw * 0.8f,
                                                  0.9f, 0.9f, 0.9f, iconA);
                    break;
                }
                case aoc::sim::UnitClass::Civilian: {
                    float bw = s * 0.45f;
                    renderer2d.drawLine(cx, cy + bw * 0.6f, cx, cy - bw * 0.3f,
                                         2.5f, 0.6f, 0.4f, 0.2f, iconA);
                    renderer2d.drawFilledRect(cx - bw * 0.5f, cy - bw * 0.6f, bw * 1.0f, bw * 0.35f,
                                               0.7f, 0.7f, 0.75f, iconA);
                    break;
                }
                case aoc::sim::UnitClass::Naval: {
                    float nw = s * 0.55f;
                    renderer2d.drawFilledTriangle(cx - nw, cy + nw * 0.2f,
                                                  cx + nw, cy + nw * 0.2f,
                                                  cx, cy + nw * 0.7f,
                                                  0.5f, 0.35f, 0.2f, iconA);
                    renderer2d.drawLine(cx, cy + nw * 0.2f, cx, cy - nw * 0.7f,
                                         2.0f, 0.6f, 0.4f, 0.25f, iconA);
                    renderer2d.drawFilledTriangle(cx, cy - nw * 0.6f,
                                                  cx + nw * 0.6f, cy,
                                                  cx, cy + nw * 0.1f,
                                                  1.0f, 1.0f, 1.0f, iconA);
                    break;
                }
                case aoc::sim::UnitClass::Religious: {
                    float rw = s * 0.4f;
                    renderer2d.drawCircle(cx, cy, rw, 1.0f, 0.9f, 0.5f, iconA, 2.0f);
                    renderer2d.drawLine(cx, cy - rw * 0.6f, cx, cy + rw * 0.6f,
                                         2.0f, 1.0f, 0.9f, 0.5f, iconA);
                    renderer2d.drawLine(cx - rw * 0.4f, cy - rw * 0.1f,
                                         cx + rw * 0.4f, cy - rw * 0.1f,
                                         2.0f, 1.0f, 0.9f, 0.5f, iconA);
                    break;
                }
                default:
                    break;
            }

            // ---- Selection: hex outline + HP bar ----
            // Entity identity is now the unit pointer itself; compare by address via selectedEntity
            // which stores the legacy EntityId. Units are identified by their position for now.
            // The selectedEntity field is retained for backward compatibility with callers.
            if (this->selectedEntity.isValid()) {
                // We can only draw selection state if this unit's entity matches selectedEntity.
                // For the object model we rely on callers updating selectedEntity when they select.
                // This block intentionally left as a no-op for units not matching selected entity.
            }

            // ---- HP bar (always shown above unit for quick status read) ----
            {
                float barW = s * 2.0f;
                float barH = s * 0.22f;
                float barX = cx - barW * 0.5f;
                float barY = cy - s - barH - 4.0f;
                float hpFrac = static_cast<float>(unit.hitPoints()) /
                               static_cast<float>(def.maxHitPoints);
                hpFrac = std::clamp(hpFrac, 0.0f, 1.0f);
                renderer2d.drawFilledRect(barX, barY, barW, barH, 0.15f, 0.15f, 0.15f, 0.8f);
                float hpR = (hpFrac < 0.5f) ? 1.0f : (1.0f - hpFrac) * 2.0f;
                float hpG = (hpFrac > 0.5f) ? 1.0f : hpFrac * 2.0f;
                renderer2d.drawFilledRect(barX, barY, barW * hpFrac, barH, hpR, hpG, 0.1f, 0.9f);
            }

            // ---- Pending path (dashed line) ----
            if (!unit.pendingPath().empty()) {
                float prevX = cx;
                float prevY = cy;
                for (const aoc::hex::AxialCoord& waypoint : unit.pendingPath()) {
                    float nextX = 0.0f;
                    float nextY = 0.0f;
                    aoc::hex::axialToPixel(waypoint, hexSize, nextX, nextY);

                    renderer2d.drawDashedLine(prevX, prevY, nextX, nextY,
                                               2.0f, 6.0f, 4.0f,
                                               1.0f, 1.0f, 1.0f, 0.6f);
                    renderer2d.drawFilledCircle(nextX, nextY, hexSize * 0.08f,
                                                 1.0f, 1.0f, 1.0f, 0.5f);
                    prevX = nextX;
                    prevY = nextY;
                }

                float destX = 0.0f;
                float destY = 0.0f;
                aoc::hex::axialToPixel(unit.pendingPath().back(), hexSize, destX, destY);
                renderer2d.drawCircle(destX, destY, hexSize * 0.25f,
                                       1.0f, 1.0f, 1.0f, 0.7f, 2.0f);
            }
        }
    }
}

void UnitRenderer::drawCities(vulkan_app::renderer::Renderer2D& renderer2d,
                               const aoc::game::GameState& gameState,
                               const aoc::map::FogOfWar& fog,
                               const aoc::map::HexGrid& grid,
                               PlayerId viewingPlayer,
                               const CameraController& camera,
                               float hexSize,
                               uint32_t screenWidth, uint32_t screenHeight) const {
    float topLeftX = 0.0f, topLeftY = 0.0f, botRightX = 0.0f, botRightY = 0.0f;
    camera.screenToWorld(0.0, 0.0, topLeftX, topLeftY, screenWidth, screenHeight);
    camera.screenToWorld(static_cast<double>(screenWidth), static_cast<double>(screenHeight),
                         botRightX, botRightY, screenWidth, screenHeight);
    float margin = hexSize * 2.0f;
    topLeftX -= margin; topLeftY -= margin;
    botRightX += margin; botRightY += margin;

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            const aoc::game::City& city = *cityPtr;

            // Fog of war: own cities on Revealed+Visible, foreign cities ONLY on Visible
            if (grid.isValid(city.location())) {
                int32_t tileIndex = grid.toIndex(city.location());
                aoc::map::TileVisibility vis = fog.visibility(viewingPlayer, tileIndex);
                if (vis == aoc::map::TileVisibility::Unseen) {
                    continue;
                }
                if (city.owner() != viewingPlayer && vis != aoc::map::TileVisibility::Visible) {
                    continue;
                }
            }

            float cx = 0.0f, cy = 0.0f;
            hex::axialToPixel(city.location(), hexSize, cx, cy);

            if (cx < topLeftX || cx > botRightX || cy < topLeftY || cy > botRightY) {
                continue;
            }

            float r = 0.0f, g = 0.0f, b = 0.0f;
            playerColor(city.owner(), r, g, b);

            const bool isCityState = city.owner() >= aoc::sim::CITY_STATE_PLAYER_BASE;

        // ---- City covers the full hex ----
        float hexPoints[12];
        hex::hexVertices(cx, cy, hexSize, hexPoints);

        // City fill: SDF hexagon (single primitive, no seams).
        {
            float fillR = isCityState ? 0.40f : r * 0.25f;
            float fillG = isCityState ? 0.40f : g * 0.25f;
            float fillB = isCityState ? 0.42f : b * 0.25f;
            renderer2d.drawFilledHexagon(cx, cy, hexSize * 0.866f, hexSize,
                                          fillR, fillG, fillB, 0.90f);
        }

        // ---- City buildings silhouette (small rectangles like a skyline) ----
        const float bw = hexSize * 0.12f;
        const float baseY = cy + hexSize * 0.15f;

        // Central tall building
        renderer2d.drawFilledRect(cx - bw * 0.6f, baseY - hexSize * 0.45f,
                                   bw * 1.2f, hexSize * 0.45f,
                                   0.55f, 0.50f, 0.45f, 0.9f);
        // Left building
        renderer2d.drawFilledRect(cx - bw * 2.5f, baseY - hexSize * 0.28f,
                                   bw * 1.5f, hexSize * 0.28f,
                                   0.50f, 0.46f, 0.40f, 0.85f);
        // Right building
        renderer2d.drawFilledRect(cx + bw * 1.0f, baseY - hexSize * 0.32f,
                                   bw * 1.4f, hexSize * 0.32f,
                                   0.52f, 0.48f, 0.42f, 0.88f);
        // Far left small building
        renderer2d.drawFilledRect(cx - bw * 3.8f, baseY - hexSize * 0.18f,
                                   bw * 1.0f, hexSize * 0.18f,
                                   0.48f, 0.44f, 0.38f, 0.8f);
        // Far right small building
        renderer2d.drawFilledRect(cx + bw * 2.8f, baseY - hexSize * 0.22f,
                                   bw * 1.0f, hexSize * 0.22f,
                                   0.48f, 0.44f, 0.38f, 0.8f);

        // ---- Ground line ----
        renderer2d.drawLine(cx - hexSize * 0.55f, baseY,
                             cx + hexSize * 0.55f, baseY,
                             1.5f, 0.35f, 0.30f, 0.25f, 0.7f);

        // ---- Population badge (bottom of hex) ----
        const float badgeR = hexSize * 0.18f;
        const float badgeY = cy + hexSize * 0.55f;
        renderer2d.drawFilledCircle(cx, badgeY, badgeR, 0.15f, 0.15f, 0.20f, 0.9f);

        // ---- City center marker (small flag at top) ----
        const float flagX = cx + hexSize * 0.05f;
        const float flagY = cy - hexSize * 0.55f;
        // Flagpole
        renderer2d.drawLine(flagX, flagY + hexSize * 0.25f, flagX, flagY,
                             1.5f, 0.6f, 0.55f, 0.5f, 0.9f);
        // Flag (small triangle in player color)
        if (!isCityState) {
            renderer2d.drawFilledTriangle(flagX, flagY,
                                          flagX + hexSize * 0.15f, flagY + hexSize * 0.06f,
                                          flagX, flagY + hexSize * 0.12f,
                                          r, g, b, 0.9f);
        } else {
            renderer2d.drawFilledTriangle(flagX, flagY,
                                          flagX + hexSize * 0.15f, flagY + hexSize * 0.06f,
                                          flagX, flagY + hexSize * 0.12f,
                                          0.85f, 0.85f, 0.85f, 0.9f);
        }
        } // end city loop
    } // end player loop
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

        renderer2d.drawFilledHexagon(cx, cy, hexSize * 0.866f, hexSize,
                                       1.0f, 1.0f, 1.0f, 0.15f);
    }
}

void UnitRenderer::drawRangedRange(vulkan_app::renderer::Renderer2D& renderer2d,
                                    const aoc::game::GameState& gameState,
                                    hex::AxialCoord center, int32_t range,
                                    PlayerId unitOwner, float hexSize) const {
    // Draw all tiles within range as a translucent overlay, highlighting enemy-occupied tiles.
    for (int32_t dq = -range; dq <= range; ++dq) {
        for (int32_t dr = -range; dr <= range; ++dr) {
            const int32_t ds = -dq - dr;
            if (std::abs(dq) + std::abs(dr) + std::abs(ds) > range * 2) {
                continue;
            }
            if (dq == 0 && dr == 0) {
                continue;  // Skip the center tile
            }

            hex::AxialCoord tile{center.q + dq, center.r + dr};
            float cx = 0.0f;
            float cy = 0.0f;
            hex::axialToPixel(tile, hexSize, cx, cy);

            // Check if an enemy unit occupies this tile
            bool hasEnemy = false;
            for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
                if (playerPtr->id() == unitOwner) {
                    continue;
                }
                if (playerPtr->unitAt(tile) != nullptr) {
                    hasEnemy = true;
                    break;
                }
            }

            if (hasEnemy) {
                renderer2d.drawFilledHexagon(cx, cy, hexSize * 0.866f, hexSize,
                                              1.0f, 0.2f, 0.2f, 0.25f);
            } else {
                renderer2d.drawFilledHexagon(cx, cy, hexSize * 0.866f, hexSize,
                                              0.8f, 0.8f, 0.2f, 0.10f);
            }
        }
    }

    // Draw range ring
    constexpr int32_t RING_SEGMENTS = 48;
    constexpr float PI = 3.14159265f;
    float centerX = 0.0f;
    float centerY = 0.0f;
    hex::axialToPixel(center, hexSize, centerX, centerY);
    const float ringRadius = static_cast<float>(range) * hexSize * 1.5f;

    for (int32_t i = 0; i < RING_SEGMENTS; ++i) {
        const float angle1 = static_cast<float>(i) * 2.0f * PI / static_cast<float>(RING_SEGMENTS);
        const float angle2 = static_cast<float>(i + 1) * 2.0f * PI / static_cast<float>(RING_SEGMENTS);
        const float x1 = centerX + ringRadius * std::cos(angle1);
        const float y1 = centerY + ringRadius * std::sin(angle1);
        const float x2 = centerX + ringRadius * std::cos(angle2);
        const float y2 = centerY + ringRadius * std::sin(angle2);
        renderer2d.drawLine(x1, y1, x2, y2, 1.5f, 1.0f, 0.8f, 0.2f, 0.4f);
    }
}

} // namespace aoc::render

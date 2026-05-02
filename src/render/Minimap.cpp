/**
 * @file Minimap.cpp
 * @brief Minimap rendering: terrain overview, ownership tint, and viewport rectangle.
 */

#include "aoc/render/Minimap.hpp"
#include "aoc/render/CameraController.hpp"
#include "aoc/render/GameRenderer.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Log.hpp"

#include <renderer/Renderer2D.hpp>

#include <algorithm>
#include <cmath>

namespace aoc::render {

/// Player colors for ownership tinting on the minimap. Mirrors the 8-slot
/// palette in UnitRenderer.cpp so minimap and unit colors stay consistent.
/// The game supports up to 16 players (Types.hpp MAX_PLAYERS); PlayerIds
/// beyond slot 7 wrap via modulo rather than being skipped, which matches
/// UnitRenderer's `% PLAYER_COLORS.size()` fallback and avoids leaving
/// territory for players 4-15 untinted on the minimap.
static constexpr float PLAYER_COLORS[][3] = {
    {0.20f, 0.40f, 0.90f},  // Player 0: blue
    {0.90f, 0.20f, 0.20f},  // Player 1: red
    {0.20f, 0.80f, 0.20f},  // Player 2: green
    {0.90f, 0.80f, 0.10f},  // Player 3: yellow
    {0.70f, 0.30f, 0.80f},  // Player 4: purple
    {0.90f, 0.50f, 0.10f},  // Player 5: orange
    {0.10f, 0.80f, 0.80f},  // Player 6: cyan
    {0.80f, 0.40f, 0.60f},  // Player 7: pink
};

static constexpr uint8_t PLAYER_COLOR_COUNT = 8;

Minimap::Rect Minimap::computeRect(const aoc::map::HexGrid& grid,
                                    uint32_t screenHeight,
                                    float bottomReservedPx) {
    constexpr float MARGIN = 10.0f;
    const float aspect = (grid.height() > 0)
        ? static_cast<float>(grid.width()) / static_cast<float>(grid.height())
        : 1.5f;
    const float tilesArea = static_cast<float>(grid.width() * grid.height());
    const float scaleArea = std::clamp(std::sqrt(tilesArea / 4160.0f), 1.0f, 1.8f);
    const float h = std::clamp(130.0f * scaleArea, 130.0f, 240.0f);
    const float w = h * aspect;
    Rect r;
    r.x = MARGIN;
    // bottomReservedPx pushes minimap UP so it doesn't overlap a tall
    // bottom-anchored panel (creator mode has two stacked panels).
    r.y = static_cast<float>(screenHeight) - h - bottomReservedPx;
    r.w = w;
    r.h = h;
    return r;
}

void Minimap::draw(vulkan_app::renderer::Renderer2D& renderer2d,
                   const aoc::map::HexGrid& grid,
                   const aoc::map::FogOfWar& fog,
                   PlayerId player,
                   const CameraController& camera,
                   float mapX, float mapY, float mapW, float mapH,
                   uint32_t screenWidth, uint32_t screenHeight,
                   bool platesOverlay,
                   int32_t overlayModeRaw) const {
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

            // Plate overlay: replace per-tile colour with a deterministic
            // hue derived from plate id. Same hash as the main map's
            // plate overlay so colours match between the two views.
            if (platesOverlay) {
                const uint8_t cat = grid.plateId(index);
                if (cat != 0xFFu) {
                    const uint32_t h = static_cast<uint32_t>(cat) * 2654435761u;
                    tc.r = std::min(1.0f,
                        static_cast<float>((h >> 0)  & 0xFFu) / 255.0f * 0.7f + 0.3f);
                    tc.g = std::min(1.0f,
                        static_cast<float>((h >> 8)  & 0xFFu) / 255.0f * 0.7f + 0.3f);
                    tc.b = std::min(1.0f,
                        static_cast<float>((h >> 16) & 0xFFu) / 255.0f * 0.7f + 0.3f);
                }
            }
            // Mirror main-map overlays on the minimap. Each overlay
            // fills tiles with a flat color (matching the colors used
            // by GameRenderer's overlay pass) so the same view is
            // available from both surfaces.
            using MO = aoc::render::GameRenderer::MapOverlay;
            const MO mode = static_cast<MO>(overlayModeRaw);
            const std::size_t si = static_cast<std::size_t>(index);
            auto setRGB = [&](float r, float g, float b) {
                tc.r = r; tc.g = g; tc.b = b;
            };
            switch (mode) {
                case MO::Resources: {
                    const aoc::ResourceId res = grid.resource(index);
                    if (res.isValid()) {
                        const uint16_t v = res.value;
                        if (v == 0 || v == 1 || v == 7 || v == 10
                            || v == 11 || v == 20 || v == 151 || v == 152
                            || v == 154 || v == 162 || v == 164 || v == 165) {
                            setRGB(0.95f, 0.45f, 0.10f);
                        } else if (v == 2 || v == 3 || v == 5 || v == 6
                            || v == 12 || v == 153) {
                            setRGB(0.95f, 0.85f, 0.10f);
                        } else if (v == 21 || v == 22 || v == 23 || v == 24
                            || v == 25 || v == 26 || v == 27 || v == 28
                            || v == 32 || v == 33 || v == 34 || v == 160
                            || v == 161) {
                            setRGB(0.85f, 0.20f, 0.85f);
                        } else if ((v >= 40 && v <= 47) || v == 4
                            || v == 8 || v == 9) {
                            setRGB(0.20f, 0.85f, 0.30f);
                        } else if (v == 29 || v == 30 || v == 47
                            || v == 156 || v == 158 || v == 159 || v == 155) {
                            setRGB(0.65f, 0.65f, 0.65f);
                        } else if (v == 145 || v == 146 || v == 150
                            || v == 157 || v == 163 || v == 166) {
                            setRGB(0.10f, 0.85f, 0.95f);
                        }
                    }
                    break;
                }
                case MO::CrustAge: {
                    const auto& a = grid.crustAgeTile();
                    if (!a.empty() && si < a.size()) {
                        const float t = std::clamp(a[si] / 200.0f, 0.0f, 1.0f);
                        setRGB(1.0f - t,
                               (t < 0.5f) ? (t * 2.0f) : (1.0f - (t - 0.5f) * 2.0f),
                               t);
                    }
                    break;
                }
                case MO::Sediment: {
                    const auto& s = grid.sedimentDepth();
                    if (!s.empty() && si < s.size() && s[si] > 0.005f) {
                        const float t = std::clamp(s[si] / 0.20f, 0.0f, 1.0f);
                        setRGB(1.0f, 0.85f - 0.55f * t, 0.10f * (1.0f - t));
                    }
                    break;
                }
                case MO::RockType: {
                    const auto& r = grid.rockType();
                    if (!r.empty() && si < r.size()) {
                        switch (r[si]) {
                            case 0: setRGB(0.95f, 0.85f, 0.55f); break;
                            case 1: setRGB(0.20f, 0.30f, 0.55f); break;
                            case 2: setRGB(0.55f, 0.25f, 0.65f); break;
                            case 3: setRGB(0.10f, 0.85f, 0.25f); break;
                            default: break;
                        }
                    }
                    break;
                }
                case MO::Margins: {
                    const auto& m = grid.marginType();
                    if (!m.empty() && si < m.size()) {
                        if (m[si] == 1) { setRGB(0.95f, 0.20f, 0.20f); }
                        else if (m[si] == 2) { setRGB(0.20f, 0.45f, 0.95f); }
                    }
                    break;
                }
                case MO::Volcanism: {
                    const auto& v = grid.volcanism();
                    if (!v.empty() && si < v.size() && v[si] != 0) {
                        switch (v[si]) {
                            case 1: setRGB(1.0f, 0.30f, 0.10f); break;
                            case 2: setRGB(1.0f, 0.55f, 0.0f);  break;
                            case 3: setRGB(0.55f, 0.05f, 0.35f); break;
                            case 4: setRGB(0.10f, 0.85f, 0.55f); break;
                            case 5: setRGB(1.0f, 1.0f, 0.20f);  break;
                            case 6: setRGB(0.85f, 0.85f, 0.85f); break;
                            case 7: setRGB(0.95f, 0.90f, 0.60f); break;
                            default: break;
                        }
                    }
                    break;
                }
                case MO::Hazard: {
                    const auto& h = grid.seismicHazard();
                    if (!h.empty() && si < h.size()) {
                        const uint8_t lvl = h[si] & 0x07;
                        const bool tsu = (h[si] & 0x08) != 0;
                        if (lvl > 0 || tsu) {
                            setRGB(0.50f + 0.15f * lvl,
                                   (lvl >= 3) ? 0.10f : 0.40f,
                                   tsu ? 0.85f : 0.10f);
                        }
                    }
                    break;
                }
                case MO::Soil: {
                    const auto& s = grid.soilFertility();
                    if (!s.empty() && si < s.size()) {
                        const float v = s[si];
                        setRGB(0.45f + 0.15f * (1.0f - v),
                               0.30f + 0.55f * v,
                               0.20f * (1.0f - v));
                    }
                    break;
                }
                case MO::Insolation: {
                    const auto& iv = grid.solarInsolation();
                    if (!iv.empty() && si < iv.size()) {
                        const float v = static_cast<float>(iv[si]) / 255.0f;
                        setRGB(v, 0.85f * v, 0.10f * (1.0f - v));
                    }
                    break;
                }
                case MO::PelagicProd: {
                    const auto& p = grid.pelagicProductivity();
                    if (!p.empty() && si < p.size() && p[si] > 0) {
                        const float v = static_cast<float>(p[si]) / 255.0f;
                        setRGB(0.20f * (1.0f - v),
                               0.30f + 0.65f * v,
                               0.10f * (1.0f - v));
                    }
                    break;
                }
                case MO::Habit: {
                    const auto& h = grid.habitability();
                    if (!h.empty() && si < h.size()) {
                        const float v = static_cast<float>(h[si]) / 255.0f;
                        setRGB(0.20f * (1.0f - v),
                               0.30f + 0.65f * v, 0.20f);
                    }
                    break;
                }
                case MO::Trade: {
                    const auto& tp = grid.tradeRoutePotential();
                    if (!tp.empty() && si < tp.size()) {
                        const float v = static_cast<float>(tp[si]) / 255.0f;
                        setRGB(0.10f + 0.85f * v, 0.65f * v, 0.20f);
                    }
                    break;
                }
                case MO::Realms: {
                    const auto& iso = grid.isolatedRealm();
                    const auto& bri = grid.landBridge();
                    const auto& ref = grid.refugium();
                    if (!iso.empty() && si < iso.size() && iso[si] != 0) {
                        setRGB(0.85f, 0.20f, 0.85f);
                    } else if (!bri.empty() && si < bri.size() && bri[si] != 0) {
                        setRGB(0.20f, 0.85f, 0.95f);
                    } else if (!ref.empty() && si < ref.size() && ref[si] != 0) {
                        setRGB(0.65f, 0.95f, 0.20f);
                    }
                    break;
                }
                case MO::Cliff: {
                    const auto& c = grid.cliffCoastAll();
                    if (!c.empty() && si < c.size() && c[si] != 0) {
                        switch (c[si]) {
                            case 1: setRGB(0.85f, 0.20f, 0.20f); break;
                            case 2: setRGB(0.20f, 0.30f, 0.85f); break;
                            case 3: setRGB(0.85f, 0.55f, 0.20f); break;
                            case 4: setRGB(0.95f, 0.95f, 1.0f);  break;
                            default: break;
                        }
                    }
                    break;
                }
                default: break; // Other overlays use main-map only
            }

            // Dim revealed (non-visible) tiles
            if (vis == aoc::map::TileVisibility::Revealed) {
                tc.r *= 0.5f;
                tc.g *= 0.5f;
                tc.b *= 0.5f;
            }

            // Tint with player ownership color. Wrap via modulo so owners
            // beyond slot 7 still receive a visible tint (shared palette
            // slot) instead of being rendered as unowned territory.
            const PlayerId tileOwner = grid.owner(index);
            if (tileOwner != INVALID_PLAYER) {
                const std::size_t ci = static_cast<std::size_t>(tileOwner) % PLAYER_COLOR_COUNT;
                const float blend = 0.4f;
                tc.r = tc.r * (1.0f - blend) + PLAYER_COLORS[ci][0] * blend;
                tc.g = tc.g * (1.0f - blend) + PLAYER_COLORS[ci][1] * blend;
                tc.b = tc.b * (1.0f - blend) + PLAYER_COLORS[ci][2] * blend;
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

    // For cylindrical wrapping: if viewport straddles the seam, draw two rects
    if (camera.worldWidth() > 0.0f) {
        const float clampedY2 = std::max(mapY, std::min(vpY, mapY + mapH));
        const float clampedH2 = std::min(vpH, mapY + mapH - clampedY2);

        if (vpX < mapX) {
            // Left part wraps to right side
            float rightX = mapX + mapW + vpX - mapX;  // wrap portion
            float rightW = mapX - vpX;
            float leftW  = vpW - rightW;
            if (leftW > 1.0f && clampedH2 > 1.0f) {
                renderer2d.drawRect(mapX, clampedY2, leftW, clampedH2,
                                   1.0f, 1.0f, 1.0f, 0.8f, 1.5f);
            }
            if (rightW > 1.0f && clampedH2 > 1.0f) {
                renderer2d.drawRect(rightX, clampedY2, rightW, clampedH2,
                                   1.0f, 1.0f, 1.0f, 0.8f, 1.5f);
            }
        } else if (vpX + vpW > mapX + mapW) {
            // Right part wraps to left side
            float leftW  = vpX + vpW - (mapX + mapW);
            float rightW = vpW - leftW;
            if (rightW > 1.0f && clampedH2 > 1.0f) {
                renderer2d.drawRect(vpX, clampedY2, rightW, clampedH2,
                                   1.0f, 1.0f, 1.0f, 0.8f, 1.5f);
            }
            if (leftW > 1.0f && clampedH2 > 1.0f) {
                renderer2d.drawRect(mapX, clampedY2, leftW, clampedH2,
                                   1.0f, 1.0f, 1.0f, 0.8f, 1.5f);
            }
        } else if (vpW > 1.0f && clampedH2 > 1.0f) {
            renderer2d.drawRect(vpX, clampedY2, vpW, clampedH2,
                               1.0f, 1.0f, 1.0f, 0.8f, 1.5f);
        }
    } else {
        // Flat topology: clamp viewport rect to minimap bounds
        const float clampedX = std::max(mapX, std::min(vpX, mapX + mapW));
        const float clampedY = std::max(mapY, std::min(vpY, mapY + mapH));
        const float clampedW = std::min(vpW, mapX + mapW - clampedX);
        const float clampedH = std::min(vpH, mapY + mapH - clampedY);

        if (clampedW > 1.0f && clampedH > 1.0f) {
            renderer2d.drawRect(clampedX, clampedY, clampedW, clampedH,
                               1.0f, 1.0f, 1.0f, 0.8f, 1.5f);
        }
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

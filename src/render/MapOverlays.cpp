/**
 * @file MapOverlays.cpp
 * @brief Map overlay rendering for infrastructure, trade routes, and pollution.
 */

#include "aoc/render/MapOverlays.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"

#include <renderer/Renderer2D.hpp>

#include <cmath>

namespace aoc::render {

namespace {

/// Convert hex axial coordinate to screen pixel position.
void hexToScreen(hex::AxialCoord coord, float cameraX, float cameraY, float zoom,
                 float& outX, float& outY) {
    // Pointy-top hex layout
    constexpr float HEX_SIZE = 32.0f;
    float q = static_cast<float>(coord.q);
    float r = static_cast<float>(coord.r);
    float x = HEX_SIZE * (std::sqrt(3.0f) * q + std::sqrt(3.0f) / 2.0f * r);
    float y = HEX_SIZE * (3.0f / 2.0f * r);
    outX = (x - cameraX) * zoom;
    outY = (y - cameraY) * zoom;
}

} // anonymous namespace

void renderMapOverlays(vulkan_app::renderer::Renderer2D& renderer,
                       const aoc::game::GameState& gameState,
                       const aoc::map::HexGrid& grid,
                       const OverlayState& state,
                       float cameraX, float cameraY, float zoom,
                       PlayerId player) {
    if (state.enabled[static_cast<int32_t>(OverlayType::Infrastructure)]) {
        renderInfrastructureOverlay(renderer, grid, cameraX, cameraY, zoom, player);
    }
    if (state.enabled[static_cast<int32_t>(OverlayType::Pollution)]) {
        renderPollutionOverlay(renderer, gameState, grid, cameraX, cameraY, zoom);
    }
    if (state.enabled[static_cast<int32_t>(OverlayType::TradeRoutes)]) {
        renderTradeRouteOverlay(renderer, gameState, grid, cameraX, cameraY, zoom, player);
    }
}

void renderInfrastructureOverlay(vulkan_app::renderer::Renderer2D& renderer,
                                 const aoc::map::HexGrid& grid,
                                 float cameraX, float cameraY, float zoom,
                                 PlayerId player) {
    int32_t tileCount = grid.tileCount();
    for (int32_t i = 0; i < tileCount; ++i) {
        if (grid.owner(i) != player) { continue; }

        hex::AxialCoord center = grid.toAxial(i);
        float cx = 0.0f;
        float cy = 0.0f;
        hexToScreen(center, cameraX, cameraY, zoom, cx, cy);

        const int32_t tier = grid.infrastructureTier(i);
        if (tier >= 2) {
            float r = (tier == 3) ? 0.3f : 0.5f;  // Highway light grey, Railway brown
            float g = (tier == 3) ? 0.3f : 0.3f;
            float b = (tier == 3) ? 0.4f : 0.2f;
            float size = 4.0f * zoom;
            renderer.drawFilledRect(cx - size / 2.0f, cy - size / 2.0f,
                              size, size, r, g, b, 0.7f);
        }

        // WP-C3: PowerPole marker (yellow dot, offset top-left so it
        // doesn't overlap the road tier dot).
        if (grid.hasPowerPole(i)) {
            const float size = 3.0f * zoom;
            const float ox = cx - 4.0f * zoom - size / 2.0f;
            const float oy = cy - 4.0f * zoom - size / 2.0f;
            renderer.drawFilledRect(ox, oy, size, size,
                              0.95f, 0.85f, 0.15f, 0.85f);
        }
        // WP-C3: Pipeline marker (orange dot, offset top-right).
        if (grid.hasPipeline(i)) {
            const float size = 3.0f * zoom;
            const float ox = cx + 4.0f * zoom - size / 2.0f;
            const float oy = cy - 4.0f * zoom - size / 2.0f;
            renderer.drawFilledRect(ox, oy, size, size,
                              0.95f, 0.55f, 0.10f, 0.85f);
        }
    }
}

void renderPollutionOverlay(vulkan_app::renderer::Renderer2D& renderer,
                            const aoc::game::GameState& gameState,
                            const aoc::map::HexGrid& grid,
                            float cameraX, float cameraY, float zoom) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            const aoc::sim::CityPollutionComponent& pollution = cityPtr->pollution();
            if (pollution.wasteAccumulated < 10) { continue; }

            float cx = 0.0f;
            float cy = 0.0f;
            hexToScreen(cityPtr->location(), cameraX, cameraY, zoom, cx, cy);

            // Brown haze overlay, intensity based on pollution level
            float alpha = std::min(0.5f, static_cast<float>(pollution.wasteAccumulated) * 0.005f);
            float size = 24.0f * zoom;

            renderer.drawFilledRect(cx - size / 2.0f, cy - size / 2.0f,
                              size, size, 0.4f, 0.3f, 0.1f, alpha);
        }
    }
}

void renderTradeRouteOverlay(vulkan_app::renderer::Renderer2D& renderer,
                             const aoc::game::GameState& gameState,
                             const aoc::map::HexGrid& grid,
                             float cameraX, float cameraY, float zoom,
                             PlayerId player) {
    for (const aoc::sim::TradeRouteComponent& route : gameState.tradeRoutes()) {
        if (route.sourcePlayer != player && route.destPlayer != player) {
            continue;
        }

        // Draw gold lines between path waypoints
        for (std::size_t p = 0; p + 1 < route.path.size(); ++p) {
            float x1 = 0.0f, y1 = 0.0f;
            float x2 = 0.0f, y2 = 0.0f;
            hexToScreen(route.path[p], cameraX, cameraY, zoom, x1, y1);
            hexToScreen(route.path[p + 1], cameraX, cameraY, zoom, x2, y2);

            renderer.drawLine(x1, y1, x2, y2, 0.9f, 0.8f, 0.2f, 0.6f);
        }
    }
}

void renderAdjacencyArrowOverlay(vulkan_app::renderer::Renderer2D& renderer,
                                 const aoc::map::HexGrid& grid,
                                 aoc::hex::AxialCoord hovered,
                                 float /*cameraX*/, float /*cameraY*/, float /*zoom*/) {
    if (!grid.isValid(hovered)) { return; }
    const int32_t idx = grid.toIndex(hovered);
    const aoc::map::ImprovementType type = grid.improvement(idx);

    // Color by improvement kind. Only tiles with known adjacency mechanics
    // produce arrows.
    float rC = 0.0f, gC = 0.0f, bC = 0.0f;
    bool drawArrows = false;
    switch (type) {
        case aoc::map::ImprovementType::Farm:
            rC = 0.95f; gC = 0.85f; bC = 0.25f; drawArrows = true; break;
        case aoc::map::ImprovementType::BiogasPlant:
            rC = 0.30f; gC = 0.85f; bC = 0.30f; drawArrows = true; break;
        case aoc::map::ImprovementType::SolarFarm:
            rC = 0.98f; gC = 0.75f; bC = 0.10f; drawArrows = true; break;
        case aoc::map::ImprovementType::WindFarm:
            rC = 0.25f; gC = 0.80f; bC = 0.95f; drawArrows = true; break;
        default: break;
    }
    if (!drawArrows) { return; }

    // World-space coordinates — renderer shader handles the camera transform
    // when called inside the world-space draw pass.
    float cx = 0.0f, cy = 0.0f;
    aoc::hex::axialToPixel(hovered, 30.0f, cx, cy);
    const std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(hovered);
    for (const aoc::hex::AxialCoord& n : nbrs) {
        if (!grid.isValid(n)) { continue; }
        if (grid.improvement(grid.toIndex(n)) != type) { continue; }

        float nx = 0.0f, ny = 0.0f;
        aoc::hex::axialToPixel(n, 30.0f, nx, ny);

        const float dx = nx - cx;
        const float dy = ny - cy;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.0f) { continue; }
        const float inv = 1.0f / len;
        const float ux = dx * inv;
        const float uy = dy * inv;
        const float inset = 6.0f;
        const float sx = cx + ux * inset;
        const float sy = cy + uy * inset;
        const float ex = nx - ux * inset;
        const float ey = ny - uy * inset;

        renderer.drawLine(sx, sy, ex, ey, rC, gC, bC, 0.9f);
        const float head = 5.0f;
        const float perpX = -uy;
        const float perpY =  ux;
        renderer.drawLine(ex, ey,
                          ex - ux * head + perpX * head * 0.5f,
                          ey - uy * head + perpY * head * 0.5f,
                          rC, gC, bC, 0.9f);
        renderer.drawLine(ex, ey,
                          ex - ux * head - perpX * head * 0.5f,
                          ey - uy * head - perpY * head * 0.5f,
                          rC, gC, bC, 0.9f);
    }
}

} // namespace aoc::render

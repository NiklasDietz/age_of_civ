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

        int32_t tier = grid.infrastructureTier(i);
        if (tier < 2) { continue; }  // Only render railway (2) and highway (3)

        hex::AxialCoord center = grid.toAxial(i);
        float cx = 0.0f;
        float cy = 0.0f;
        hexToScreen(center, cameraX, cameraY, zoom, cx, cy);

        // Draw a colored dot at the tile center
        float r = (tier == 3) ? 0.3f : 0.5f;  // Highway = light grey, Railway = brown
        float g = (tier == 3) ? 0.3f : 0.3f;
        float b = (tier == 3) ? 0.4f : 0.2f;
        float size = 4.0f * zoom;

        renderer.drawFilledRect(cx - size / 2.0f, cy - size / 2.0f,
                          size, size, r, g, b, 0.7f);
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

} // namespace aoc::render

/**
 * @file GameRenderer.cpp
 * @brief Top-level render orchestrator implementation.
 *
 * The Renderer2D uses a single GPU instance buffer per frame. Multiple
 * begin/end batches overwrite each other. We render everything in one batch
 * using the world-space camera, and transform UI positions to world space
 * so the camera shader maps them back to the correct screen positions.
 */

#include "aoc/render/GameRenderer.hpp"
#include "aoc/render/CameraController.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/ecs/World.hpp"

#include <renderer/Renderer2D.hpp>
#include <renderer/RenderPipeline.hpp>

namespace aoc::render {

void GameRenderer::initialize(vulkan_app::RenderPipeline& /*pipeline*/,
                               vulkan_app::renderer::Renderer2D& /*renderer2d*/) {
}

void GameRenderer::render(vulkan_app::renderer::Renderer2D& renderer2d,
                           VkCommandBuffer commandBuffer,
                           uint32_t frameIndex,
                           const CameraController& camera,
                           const aoc::map::HexGrid& grid,
                           const aoc::ecs::World& world,
                           const aoc::map::FogOfWar& fog,
                           PlayerId viewingPlayer,
                           aoc::ui::UIManager& uiManager,
                           uint32_t screenWidth, uint32_t screenHeight) {
    float hexSize = this->m_mapRenderer.hexSize();

    // The shader's cameraPos is the TOP-LEFT corner of the viewport in world space.
    // Our CameraController stores the CENTER of the viewport.
    float topLeftX = camera.cameraX() - static_cast<float>(screenWidth)  / (2.0f * camera.zoom());
    float topLeftY = camera.cameraY() - static_cast<float>(screenHeight) / (2.0f * camera.zoom());

    renderer2d.setCamera(topLeftX, topLeftY);
    renderer2d.setZoom(camera.zoom());

    renderer2d.beginFrame(frameIndex);
    renderer2d.begin();

    // Layer 1: Map tiles (world coordinates -- shader transforms via camera)
    this->m_mapRenderer.draw(renderer2d, grid, fog, viewingPlayer, camera,
                              screenWidth, screenHeight);

    // Layer 2: Cities (world coordinates)
    this->m_unitRenderer.drawCities(renderer2d, world, fog, grid, viewingPlayer,
                                     camera, hexSize, screenWidth, screenHeight);

    // Layer 3: Units (world coordinates)
    this->m_unitRenderer.drawUnits(renderer2d, world, fog, grid, viewingPlayer,
                                    camera, hexSize, screenWidth, screenHeight);

    // Layer 4: UI overlay
    // UI widgets are in screen-pixel space. Transform to world space so the
    // camera shader maps them back: worldPos = topLeft + screenPos / zoom
    uiManager.layout();
    float invZoom = 1.0f / camera.zoom();
    uiManager.transformBounds(topLeftX, topLeftY, invZoom);
    uiManager.render(renderer2d);
    uiManager.untransformBounds(topLeftX, topLeftY, invZoom);

    // Minimap: convert screen-space position to world-space for the camera batch.
    // Screen (sx, sy) -> world (topLeftX + sx * invZoom, topLeftY + sy * invZoom)
    constexpr float MINIMAP_W = 200.0f;
    constexpr float MINIMAP_H = 130.0f;
    constexpr float MINIMAP_MARGIN = 10.0f;
    float mmScreenX = MINIMAP_MARGIN;
    float mmScreenY = static_cast<float>(screenHeight) - MINIMAP_H - MINIMAP_MARGIN;
    float mmWorldX = topLeftX + mmScreenX * invZoom;
    float mmWorldY = topLeftY + mmScreenY * invZoom;
    this->m_minimap.draw(renderer2d, grid, fog, viewingPlayer, camera,
                         mmWorldX, mmWorldY, MINIMAP_W * invZoom, MINIMAP_H * invZoom,
                         screenWidth, screenHeight);

    renderer2d.end(commandBuffer);
}

} // namespace aoc::render

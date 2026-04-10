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
#include "aoc/ui/Notifications.hpp"
#include "aoc/ui/Tutorial.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/ui/BitmapFont.hpp"

#include <renderer/Renderer2D.hpp>
#include <renderer/RenderPipeline.hpp>

namespace {

/// Player colors for city name labels (matches UnitRenderer).
constexpr std::array<std::array<float, 3>, 8> LABEL_PLAYER_COLORS = {{
    {0.20f, 0.40f, 0.90f},
    {0.90f, 0.20f, 0.20f},
    {0.20f, 0.80f, 0.20f},
    {0.90f, 0.80f, 0.10f},
    {0.70f, 0.30f, 0.80f},
    {0.90f, 0.50f, 0.10f},
    {0.10f, 0.80f, 0.80f},
    {0.80f, 0.40f, 0.60f},
}};

} // anonymous namespace

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
                           uint32_t screenWidth, uint32_t screenHeight,
                           const aoc::ui::EventLog* eventLog,
                           const aoc::ui::NotificationManager* notifications,
                           const aoc::ui::TutorialManager* tutorial) {
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

    // Layer 1.5: Territory borders (hex outlines drawn ON TOP of terrain)
    this->m_mapRenderer.drawTerritoryBorders(renderer2d, grid, camera,
                                              screenWidth, screenHeight);

    // Layer 2: Cities (world coordinates)
    this->m_unitRenderer.drawCities(renderer2d, world, fog, grid, viewingPlayer,
                                     camera, hexSize, screenWidth, screenHeight);

    // Layer 3: Units (world coordinates)
    this->m_unitRenderer.drawUnits(renderer2d, world, fog, grid, viewingPlayer,
                                    camera, hexSize, screenWidth, screenHeight);

    // Layer 3.5: City name labels (world-space text above each city hex)
    {
        const aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cityPool =
            world.getPool<aoc::sim::CityComponent>();
        if (cityPool != nullptr) {
            const float invZoomLabel = 1.0f / camera.zoom();
            constexpr float LABEL_FONT_SIZE = 10.0f;
            const float labelOffsetY = hexSize * 0.60f;

            for (uint32_t ci = 0; ci < cityPool->size(); ++ci) {
                const aoc::sim::CityComponent& city = cityPool->data()[ci];

                // Skip cities on unseen tiles
                if (grid.isValid(city.location)) {
                    int32_t tileIdx = grid.toIndex(city.location);
                    aoc::map::TileVisibility vis = fog.visibility(viewingPlayer, tileIdx);
                    if (vis == aoc::map::TileVisibility::Unseen) {
                        continue;
                    }
                }

                float cityCx = 0.0f, cityCy = 0.0f;
                hex::axialToPixel(city.location, hexSize, cityCx, cityCy);

                // Measure text at screen size, then convert to world width.
                // drawText with pixelScale rasterizes at fontSize/pixelScale screen pixels,
                // and each pixel occupies pixelScale world units.
                // So total world width = measureText(fontSize).w * pixelScale
                // (measureText returns screen-pixel width at the given fontSize)
                const aoc::ui::Rect textBounds =
                    aoc::ui::BitmapFont::measureText(city.name, LABEL_FONT_SIZE);
                const float textWorldW = textBounds.w * invZoomLabel;
                const float textWorldH = textBounds.h * invZoomLabel;
                const float textX = cityCx - textWorldW * 0.5f;
                const float textY = cityCy - labelOffsetY - textWorldH * 0.5f;

                // Player color
                const std::size_t cIdx =
                    static_cast<std::size_t>(city.owner) % LABEL_PLAYER_COLORS.size();
                const aoc::ui::Color labelColor{
                    LABEL_PLAYER_COLORS[cIdx][0],
                    LABEL_PLAYER_COLORS[cIdx][1],
                    LABEL_PLAYER_COLORS[cIdx][2],
                    1.0f};

                aoc::ui::BitmapFont::drawText(renderer2d, city.name,
                                               textX, textY,
                                               LABEL_FONT_SIZE, labelColor,
                                               invZoomLabel);
            }
        }
    }

    // Layer 3.5b: Combat animations (between units layer and UI)
    this->m_combatAnimator.render(renderer2d, hexSize);

    // Layer 3.5c: Particle effects
    this->m_particleSystem.render(renderer2d);

    // Layer 3.6: Ranged attack range overlay for selected ranged unit
    if (this->m_unitRenderer.selectedEntity.isValid() &&
        world.hasComponent<aoc::sim::UnitComponent>(this->m_unitRenderer.selectedEntity)) {
        const aoc::sim::UnitComponent& selUnit =
            world.getComponent<aoc::sim::UnitComponent>(this->m_unitRenderer.selectedEntity);
        const aoc::sim::UnitTypeDef& selDef = aoc::sim::unitTypeDef(selUnit.typeId);
        if (selDef.rangedStrength > 0 && selDef.range > 0) {
            this->m_unitRenderer.drawRangedRange(renderer2d, world,
                                                  selUnit.position, selDef.range,
                                                  selUnit.owner, hexSize);
        }
    }

    // Layer 4: UI overlay (single batch - transform UI to world-space so the
    // camera shader maps it back to screen-space correctly).
    float invZoom = 1.0f / camera.zoom();
    uiManager.layout();
    uiManager.transformBounds(topLeftX, topLeftY, invZoom);
    uiManager.render(renderer2d);
    uiManager.untransformBounds(topLeftX, topLeftY, invZoom);

    // Event log (transformed to world-space)
    if (eventLog != nullptr && !eventLog->events().empty()) {
        constexpr float EVENT_LOG_W = 320.0f;
        constexpr float EVENT_LOG_H = 160.0f;
        constexpr float EVENT_LOG_MARGIN = 10.0f;
        float elScreenX = static_cast<float>(screenWidth) - EVENT_LOG_W - EVENT_LOG_MARGIN;
        float elScreenY = static_cast<float>(screenHeight) - EVENT_LOG_H - 70.0f;
        float elWorldX = topLeftX + elScreenX * invZoom;
        float elWorldY = topLeftY + elScreenY * invZoom;
        eventLog->render(renderer2d, elWorldX, elWorldY,
                          EVENT_LOG_W * invZoom, EVENT_LOG_H * invZoom, invZoom);
    }

    // Minimap (transformed to world-space)
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

    // Notifications (transformed to world-space)
    if (notifications != nullptr) {
        notifications->render(renderer2d,
                               static_cast<float>(screenWidth),
                               static_cast<float>(screenHeight),
                               invZoom);
    }

    // Tutorial overlay (transformed to world-space)
    if (tutorial != nullptr && tutorial->isActive()) {
        tutorial->render(renderer2d,
                          static_cast<float>(screenWidth),
                          static_cast<float>(screenHeight),
                          invZoom);
    }

    renderer2d.end(commandBuffer);
}

} // namespace aoc::render

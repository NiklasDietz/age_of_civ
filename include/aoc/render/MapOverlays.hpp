#pragma once

/**
 * @file MapOverlays.hpp
 * @brief Visual overlay rendering for economic/infrastructure systems on the map.
 *
 * Draws visual indicators for systems that would otherwise be invisible:
 *   - Railways: thick dashed lines along railway tiles
 *   - Highways: solid double lines along highway tiles
 *   - Trade routes: colored curved lines between source and destination
 *   - Pollution: brown/grey haze overlay on polluted city tiles
 *   - Fallout: radiation symbol on nuked tiles
 *   - District adjacency: glow highlights showing bonus sources
 *   - Power grid: lightning bolt on cities with brownouts
 *   - River navigation: anchor icon on navigable river tiles
 *   - Barbarian clans: colored banner over encampments by clan type
 *   - Economic zones: striped overlay on colonized cities
 *
 * All overlays are optional and can be toggled on/off.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace vulkan_app::renderer { class Renderer2D; }
namespace aoc::ecs { class World; }
namespace aoc::map { class HexGrid; }

namespace aoc::render {

enum class OverlayType : uint8_t {
    Infrastructure,    ///< Railways, highways
    TradeRoutes,       ///< Active trade route lines
    Pollution,         ///< Pollution haze on tiles
    DistrictAdjacency, ///< Adjacency bonus highlights
    PowerGrid,         ///< Brownout indicators
    EconomicZones,     ///< Colonial zone striping

    Count
};

inline constexpr int32_t OVERLAY_COUNT = static_cast<int32_t>(OverlayType::Count);

/// Overlay visibility state.
struct OverlayState {
    bool enabled[OVERLAY_COUNT] = {true, true, true, false, false, false};

    void toggle(OverlayType type) {
        this->enabled[static_cast<int32_t>(type)] =
            !this->enabled[static_cast<int32_t>(type)];
    }
};

/**
 * @brief Render all enabled map overlays.
 *
 * Called after the base map is rendered but before UI.
 *
 * @param renderer   2D renderer for drawing lines, quads, icons.
 * @param world      ECS world with all game components.
 * @param grid       Hex grid.
 * @param state      Which overlays are enabled.
 * @param cameraX    Camera position for coordinate transform.
 * @param cameraY    Camera position Y.
 * @param zoom       Camera zoom level.
 * @param player     Active player (for filtering).
 */
void renderMapOverlays(vulkan_app::renderer::Renderer2D& renderer,
                       const aoc::ecs::World& world,
                       const aoc::map::HexGrid& grid,
                       const OverlayState& state,
                       float cameraX, float cameraY, float zoom,
                       PlayerId player);

/**
 * @brief Render infrastructure overlay (railways as thick lines, highways as double lines).
 */
void renderInfrastructureOverlay(vulkan_app::renderer::Renderer2D& renderer,
                                 const aoc::map::HexGrid& grid,
                                 float cameraX, float cameraY, float zoom,
                                 PlayerId player);

/**
 * @brief Render pollution overlay (brown tint on polluted tiles).
 */
void renderPollutionOverlay(vulkan_app::renderer::Renderer2D& renderer,
                            const aoc::ecs::World& world,
                            const aoc::map::HexGrid& grid,
                            float cameraX, float cameraY, float zoom);

/**
 * @brief Render active trade routes as colored lines on the map.
 */
void renderTradeRouteOverlay(vulkan_app::renderer::Renderer2D& renderer,
                             const aoc::ecs::World& world,
                             const aoc::map::HexGrid& grid,
                             float cameraX, float cameraY, float zoom,
                             PlayerId player);

} // namespace aoc::render

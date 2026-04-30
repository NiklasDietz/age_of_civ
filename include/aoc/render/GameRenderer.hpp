#pragma once

/**
 * @file GameRenderer.hpp
 * @brief Top-level render orchestrator. Drives MapRenderer, UnitRenderer, UIRenderer.
 */

#include "aoc/render/MapRenderer.hpp"
#include "aoc/render/UnitRenderer.hpp"
#include "aoc/render/Minimap.hpp"
#include "aoc/render/CombatAnimation.hpp"
#include "aoc/render/Particles.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Tooltip.hpp"
#include "aoc/ui/EventLog.hpp"
#include "aoc/ui/Notifications.hpp"
#include "aoc/ui/Tutorial.hpp"
#include "aoc/core/Types.hpp"

#include <cstdint>
#include <vulkan/vulkan.h>

namespace vulkan_app {
class RenderPipeline;
namespace renderer {
class Renderer2D;
}
}

namespace aoc::game {
class GameState;
class City;
}

namespace aoc::map {
class HexGrid;
class FogOfWar;
}


namespace aoc::render {

class CameraController;

class GameRenderer {
public:
    GameRenderer() = default;

    void initialize(vulkan_app::RenderPipeline& pipeline,
                    vulkan_app::renderer::Renderer2D& renderer2d);

    void render(vulkan_app::renderer::Renderer2D& renderer2d,
                VkCommandBuffer commandBuffer,
                uint32_t frameIndex,
                const CameraController& camera,
                const aoc::map::HexGrid& grid,
                const aoc::game::GameState& gameState,
                const aoc::map::FogOfWar& fog,
                PlayerId viewingPlayer,
                aoc::ui::UIManager& uiManager,
                uint32_t screenWidth, uint32_t screenHeight,
                const aoc::ui::EventLog* eventLog = nullptr,
                const aoc::ui::NotificationManager* notifications = nullptr,
                const aoc::ui::TutorialManager* tutorial = nullptr);

    MapRenderer&      mapRenderer()      { return this->m_mapRenderer; }
    UnitRenderer&     unitRenderer()     { return this->m_unitRenderer; }
    Minimap&          minimap()          { return this->m_minimap; }
    CombatAnimator&   combatAnimator()   { return this->m_combatAnimator; }
    ParticleSystem&   particleSystem()   { return this->m_particleSystem; }
    aoc::ui::TooltipManager& tooltipManager() { return this->m_tooltipManager; }

    /// Whether to show yield labels on all visible tiles.
    bool showTileYields = true;

    /// When true, the world minimap overlay is hidden. Set by
    /// Application while a modal screen (tech tree, diplomacy, etc.)
    /// is open so the world overview doesn't peek through.
    bool  m_minimapSuppressed   = false;
    float m_minimapBottomOffset = 0.0f; ///< Extra upward shift in screen px (clears bottom HUD panels).

    /// World map overlay mode. Controls the per-tile colour tint that
    /// the renderer applies on top of terrain. None = no overlay (
    /// normal terrain). TectonicPlates = colour each tile by its
    /// generator-assigned plate id. Future modes (Government / Religion
    /// / Continents) wire in here too.
    enum class MapOverlay : uint8_t {
        // Modes:
        //   None             — normal terrain, no tint
        //   TectonicPlates   — per-plate hue + boundary borders
        //   Winds            — arrows showing prevailing wind direction
        //                       (trade easterlies / westerlies / polar)
        //   OceanCurrents    — arrows along coast water tiles showing
        //                       gyre direction with warm/cold colour
        None,
        TectonicPlates,
        Winds,
        OceanCurrents,
        Hotspots,
    };
    MapOverlay overlayMode = MapOverlay::None;

    /// Civ-6-style worker placement overlay. When non-null, the
    /// renderer draws a highlight ring on every tile that the city
    /// can work (3-hex radius around its centre, owned + walkable),
    /// with a filled marker on currently-worked tiles. Click handling
    /// lives in Application; the renderer only draws.
    const aoc::game::City* workerOverlayCity = nullptr;

    /// Selection highlight: axial coord of the currently-selected unit or
    /// city.  Set by Application each frame.  Draws a glowing hex outline at
    /// this location so the player sees which entity they're controlling.
    /// Cleared by setting to INVALID_SELECTION.
    hex::AxialCoord selectionHighlight = {0x7FFFFFFF, 0x7FFFFFFF};
    static inline constexpr hex::AxialCoord INVALID_SELECTION = {0x7FFFFFFF, 0x7FFFFFFF};
    [[nodiscard]] bool hasSelection() const {
        return this->selectionHighlight.q != INVALID_SELECTION.q
            || this->selectionHighlight.r != INVALID_SELECTION.r;
    }

private:
    MapRenderer              m_mapRenderer;
    UnitRenderer             m_unitRenderer;
    Minimap                  m_minimap;
    CombatAnimator           m_combatAnimator;
    ParticleSystem           m_particleSystem;
    aoc::ui::TooltipManager  m_tooltipManager;
};

} // namespace aoc::render

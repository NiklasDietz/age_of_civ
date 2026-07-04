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
#include <string>
#include <unordered_map>
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

    /// When true, the 2D hex map and its overlays are skipped --
    /// caller is rendering the world via an alternative path (e.g.
    /// the 3D globe). UI / minimap / overlays-that-belong-to-screen-
    /// space still render. Used by the Continent Creator's globe
    /// toggle so the flat map doesn't paint over the 3D sphere.
    bool skipFlatMap = false;

    /// Globe-view minimap indicator. When `globeViewActive` is true,
    /// the minimap draws an ellipse marking the lat/lon footprint of
    /// the visible hemisphere on the 3D sphere instead of the flat
    /// camera viewport rect. (yawDeg, pitchDeg) = orbit camera
    /// orientation; zoom = camera radius in unit-sphere multiples.
    bool  globeViewActive = false;
    float globeYawDeg     = 0.0f;
    float globePitchDeg   = 0.0f;
    float globeZoom       = 2.5f;

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
        PlateMotion,    ///< Arrow at each plate centre showing its velocity vector
        PlateBoundaries,///< Boundary lines only (no plate-id fill) — collision-type colours
        CrustAge,       ///< Per-tile crust age — red young / blue ancient
        Sediment,       ///< Per-tile sediment depth — yellow basins
        RockType,       ///< Per-tile rock-type tag — Sed / Igneous / Metamorphic / Ophiolite
        Margins,        ///< Active (red) vs passive (blue) continental margins
        Volcanism,      ///< Volcano/hot-spring/dune/inselberg markers
        Hazard,         ///< Seismic + tsunami hazard intensity
        Soil,           ///< Soil fertility 0..1
        Realms,         ///< Biogeographic isolation + land bridges + refugia
        Storms,         ///< Hurricane / tornado / storm track / jet stream
        Glacial,        ///< Moraines, U-valleys, drumlins, eskers, caves
        Ocean,          ///< Tidal range + salinity bins
        Clouds,         ///< Cloud cover proxy
        Flow,           ///< Drainage flow direction (downhill)
        Hazards,        ///< Natural hazards (wildfire/flood/drought/etc)
        BiomeSub,       ///< Biome subtypes (Mediterranean / taiga / atoll / etc)
        MarineDepth,    ///< Shelf / slope / rise / abyssal / trench
        Wildlife,       ///< Big game / fur / marine / salmon / birds
        Disease,        ///< Malaria / yellow fever / sleeping sickness / etc
        EnergyWind,
        EnergySolar,
        EnergyHydro,
        EnergyGeothermal,
        EnergyTidal,
        EnergyWave,
        AtmExtras,      ///< Föhn / katabatic / pressure cells / vortex
        HydroExtras,    ///< Aquifers / springs / lake types
        Events,         ///< Eruption sites / impact craters / supervolcano
        Pass,           ///< Mountain pass (saddle between mountain massifs)
        Defense,        ///< Defensibility 0-255
        Domestic,       ///< Domesticable species bitfield
        Trade,          ///< Trade route potential
        Habit,          ///< Habitability composite score
        Wetland,        ///< Wetland subtypes (peat/swamp/fen/floodplain)
        Reef,           ///< Coral reef tier (fringing/barrier/atoll/patch)
        Cliff,          ///< Cliff coast (hard rock / fjord / headland / ice)
        CoastalLF,      ///< Coastal landforms (stack/spit/bar/tombolo/lagoon/flat/foreland)
        RiverRegime,    ///< Perennial / intermittent / ephemeral / glacier-fed / snow-fed
        AridLF,         ///< Mesa / butte / plateau / yardang / hoodoo / pediment / canyon
        TransformFault, ///< Pull-apart / restraining bend / plain transform
        LakeFX,         ///< Lake-effect snow zones
        Drumlin,        ///< Drumlin field paleo-ice-flow direction
        SutureReact,    ///< Reactivated suture (Atlas-style fold belts)
        Resources,      ///< Color-tint per resource group for placement audit
        Insolation,     ///< Annual mean solar insolation per tile
        Aspect,         ///< Topographic aspect (slope direction)
        Slope,          ///< Slope angle 0-255
        Ecotone,        ///< Biome boundary tiles
        PelagicProd,    ///< Pelagic primary productivity
        ShelfSed,       ///< Continental shelf sediment thickness
        Rebound,        ///< Glacial isostatic rebound rate
        SedDir,         ///< Sediment transport direction
        CoastChg,       ///< Coastal accretion / erosion
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

    /// Cache of measured city-name label rects, keyed by the name string.
    /// measureText() rasterizes/looks up every glyph, so memoizing avoids
    /// that work per city per frame. Keying on the name auto-invalidates on
    /// rename (a renamed city becomes a different key). Font size for labels
    /// is constant, so the name alone is a sufficient key.
    std::unordered_map<std::string, aoc::ui::Rect> m_cityLabelSizeCache;
};

} // namespace aoc::render

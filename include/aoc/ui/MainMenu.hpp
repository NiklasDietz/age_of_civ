#pragma once

/**
 * @file MainMenu.hpp
 * @brief Main menu, game setup screen, and settings screen.
 */

#include "aoc/ui/Widget.hpp"
#include "aoc/ui/IScreen.hpp"
#include "aoc/map/MapGenerator.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"

#include <array>
#include <functional>
#include <string>

namespace aoc::ui {
class UIManager;
}

namespace aoc::ui {

// ============================================================================
// Game Setup types
// ============================================================================

/// Game configuration for one player slot.
struct PlayerSlotConfig {
    bool     isActive = false;
    bool     isHuman  = false;
    uint8_t  civId    = 0;
};

/// AI difficulty level affecting AI bonuses/penalties.
enum class AIDifficulty : uint8_t {
    Easy,
    Normal,
    Hard,
};

/// Victory mode selection. Default = CSI score system; Classic = traditional Civ win conditions.
enum class VictoryMode : uint8_t {
    Default,   ///< CSI score + Era VP + Global Integration (the game's unique system)
    Classic,   ///< Traditional win conditions: Domination, Science, Culture, Religion, Diplomatic, Score
};

/// Configuration from the setup screen passed to startGame.
struct GameSetupConfig {
    aoc::map::MapType mapType = aoc::map::MapType::Continents;
    aoc::map::MapSize mapSize = aoc::map::MapSize::Standard;
    /// Custom map dimensions. When >0, override the preset.
    /// Updated by setup-screen W/H +/- spinners; preset buttons sync these
    /// to the preset's tile counts so all paths use the same fields.
    int32_t customWidth  = 200;
    int32_t customHeight = 120;
    aoc::map::ResourcePlacementMode placement = aoc::map::ResourcePlacementMode::Realistic;
    uint8_t           playerCount = 2;
    std::array<PlayerSlotConfig, 20> players;  ///< max 20 players
    bool sequentialTurnsInWar = false;        ///< Use sequential turns when at war
    AIDifficulty aiDifficulty = AIDifficulty::Normal; ///< AI difficulty level
    VictoryMode victoryMode = VictoryMode::Default;   ///< Victory condition mode
    int32_t maxTurns = 1000;                          ///< User-selectable turn limit
    /// Continents-only generation knobs. tectonicEpochs controls how
    /// long the plate sim runs (more = older, more eroded world,
    /// Pangaea cycles get more chances to play out). landPlateCount
    /// caps how many continent seeds are placed before ocean fills
    /// the rest. mapSeed is the deterministic RNG seed; same seed +
    /// same parameters → same world. mapSeed = 0 means "auto" (the
    /// app picks a fresh random seed at game start).
    int32_t tectonicEpochs = 40;
    int32_t landPlateCount = 7;    ///< Earth has ~7 major plates
    uint32_t mapSeed = 0;
};

class MainMenu {
public:
    /**
     * @brief Build the main menu widgets.
     *
     * All callbacks except onStartGame and onQuit are optional.
     * onSpectate is invoked when the user clicks "Spectate".
     */
    void build(UIManager& ui, float screenW, float screenH,
               std::function<void()> onStartGame,
               std::function<void()> onQuit,
               std::function<void()> onSettings  = {},
               std::function<void()> onTutorial  = {},
               std::function<void()> onSpectate  = {},
               std::function<void()> onContinentCreator = {},
               std::function<void()> onMapEditor = {});

    /// Rebuild positions after resize.
    void updateLayout(UIManager& ui, float screenW, float screenH);

    /// Remove all menu widgets.
    void destroy(UIManager& ui);

    [[nodiscard]] bool isBuilt() const { return this->m_isBuilt; }

private:
    bool m_isBuilt = false;
    WidgetId m_rootPanel = INVALID_WIDGET;

    // Stored callbacks
    std::function<void()> m_onStartGame;
    std::function<void()> m_onQuit;
    std::function<void()> m_onSettings;
    std::function<void()> m_onTutorial;
    std::function<void()> m_onSpectate;
    std::function<void()> m_onContinentCreator;
    std::function<void()> m_onMapEditor;
};

// ============================================================================
// GameSetupScreen
// ============================================================================

class GameSetupScreen {
public:
    void build(UIManager& ui, float screenW, float screenH,
               std::function<void(const GameSetupConfig&)> onStart,
               std::function<void()> onBack);
    void destroy(UIManager& ui);
    void refresh(UIManager& ui);
    [[nodiscard]] bool isBuilt() const { return this->m_isBuilt; }

    /// Pre-fill the screen's working config before build(). Used by
    /// the Continent Creator's "Use This Map" handoff so the chosen
    /// seed + tectonic params survive into the actual game launch.
    /// Caller passes a partial config; only the continent-gen knobs
    /// (mapType / seed / tectonicEpochs / landPlateCount) are
    /// adopted — player slots and the rest stay at defaults.
    void setContinentPreset(aoc::map::MapType mapType,
                             uint32_t seed,
                             int32_t tectonicEpochs,
                             int32_t landPlateCount) {
        this->m_config.mapType = mapType;
        this->m_config.mapSeed = seed;
        this->m_config.tectonicEpochs = tectonicEpochs;
        this->m_config.landPlateCount = landPlateCount;
    }

private:
    bool m_isBuilt = false;
    WidgetId m_rootPanel = INVALID_WIDGET;
    GameSetupConfig m_config;

    // Widget IDs for dynamic content
    WidgetId m_playerCountLabel = INVALID_WIDGET;
    std::array<WidgetId, 20> m_playerRows{};
    std::array<WidgetId, 20> m_civLabels{};
    std::array<WidgetId, 20> m_typeLabels{};

    // Sequential turns toggle
    WidgetId m_btnSequential  = INVALID_WIDGET;

    // AI difficulty toggle
    WidgetId m_btnDifficulty  = INVALID_WIDGET;

    // Map selection buttons
    WidgetId m_btnContinents             = INVALID_WIDGET;
    WidgetId m_btnIslands                = INVALID_WIDGET;
    WidgetId m_btnContinentsPlusIslands  = INVALID_WIDGET;
    WidgetId m_btnLandOnly               = INVALID_WIDGET;
    WidgetId m_btnLandWithSeas           = INVALID_WIDGET;
    WidgetId m_btnFractal                = INVALID_WIDGET;
    WidgetId m_btnSmall       = INVALID_WIDGET;
    WidgetId m_btnStandard    = INVALID_WIDGET;
    WidgetId m_btnLarge       = INVALID_WIDGET;
    WidgetId m_btnHuge        = INVALID_WIDGET;
    /// Custom map dimension labels — updated when W/H spinners fire.
    WidgetId m_widthLabel     = INVALID_WIDGET;
    WidgetId m_heightLabel    = INVALID_WIDGET;

    // Turn count selection buttons (300, 1000, 2000, 5000)
    WidgetId m_btnTurns300    = INVALID_WIDGET;
    WidgetId m_btnTurns1000   = INVALID_WIDGET;
    WidgetId m_btnTurns2000   = INVALID_WIDGET;
    WidgetId m_btnTurns5000   = INVALID_WIDGET;

    // Resource placement mode buttons
    WidgetId m_btnPlaceRealistic = INVALID_WIDGET;
    WidgetId m_btnPlaceFair      = INVALID_WIDGET;
    WidgetId m_btnPlaceRandom    = INVALID_WIDGET;

    // Continent generation knobs (Continents map type only).
    WidgetId m_epochsLabel       = INVALID_WIDGET;
    WidgetId m_landCountLabel    = INVALID_WIDGET;
    WidgetId m_seedLabel         = INVALID_WIDGET;

    /// Re-color map type selection buttons to reflect current selection.
    void updateMapTypeButtons(UIManager& ui);

    /// Re-color map size selection buttons to reflect current selection.
    void updateMapSizeButtons(UIManager& ui);

    /// Re-color turn-count selection buttons to reflect current selection.
    void updateTurnButtons(UIManager& ui);

    /// Re-color placement-mode buttons to reflect current selection.
    void updatePlacementButtons(UIManager& ui);
};

} // namespace aoc::ui

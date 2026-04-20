#pragma once

/**
 * @file MainMenu.hpp
 * @brief Main menu, game setup screen, and settings screen.
 */

#include "aoc/ui/Widget.hpp"
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
    uint8_t           playerCount = 2;
    std::array<PlayerSlotConfig, 8> players;  ///< max 8 players
    bool sequentialTurnsInWar = false;        ///< Use sequential turns when at war
    AIDifficulty aiDifficulty = AIDifficulty::Normal; ///< AI difficulty level
    VictoryMode victoryMode = VictoryMode::Default;   ///< Victory condition mode
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
               std::function<void()> onSpectate  = {});

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

private:
    bool m_isBuilt = false;
    WidgetId m_rootPanel = INVALID_WIDGET;
    GameSetupConfig m_config;

    // Widget IDs for dynamic content
    WidgetId m_playerCountLabel = INVALID_WIDGET;
    std::array<WidgetId, 8> m_playerRows{};
    std::array<WidgetId, 8> m_civLabels{};
    std::array<WidgetId, 8> m_typeLabels{};

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

    /// Re-color map type selection buttons to reflect current selection.
    void updateMapTypeButtons(UIManager& ui);

    /// Re-color map size selection buttons to reflect current selection.
    void updateMapSizeButtons(UIManager& ui);
};

/// Persistent settings that can be applied to the game.
struct GameSettings {
    int32_t masterVolume = 100;   ///< 0-100
    int32_t sfxVolume    = 100;   ///< 0-100
    int32_t musicVolume  = 70;    ///< 0-100
    bool    vsync        = true;
    bool    fullscreen   = false;
    bool    showFPS      = false;
    bool    showTileYields = true; ///< Display F/P/G/S yields directly on all map tiles
};

class SettingsMenu {
public:
    void build(UIManager& ui, float screenW, float screenH,
               std::function<void()> onBack);
    void destroy(UIManager& ui);
    void refresh(UIManager& ui);
    [[nodiscard]] bool isBuilt() const { return this->m_isBuilt; }

    [[nodiscard]] const GameSettings& settings() const { return this->m_settings; }
    [[nodiscard]] GameSettings& settings() { return this->m_settings; }

private:
    bool m_isBuilt = false;
    WidgetId m_rootPanel = INVALID_WIDGET;
    WidgetId m_masterVolLabel = INVALID_WIDGET;
    WidgetId m_sfxVolLabel    = INVALID_WIDGET;
    WidgetId m_musicVolLabel  = INVALID_WIDGET;
    WidgetId m_vsyncLabel     = INVALID_WIDGET;
    WidgetId m_fullscreenLabel = INVALID_WIDGET;
    WidgetId m_fpsLabel       = INVALID_WIDGET;
    WidgetId m_yieldLabel     = INVALID_WIDGET;
    GameSettings m_settings;
};

/// Save settings to a simple key=value text file.
void saveSettings(const GameSettings& settings, const std::string& filepath);

/// Load settings from file. Returns defaults if file doesn't exist.
[[nodiscard]] GameSettings loadSettings(const std::string& filepath);

} // namespace aoc::ui

#pragma once

/**
 * @file MainMenu.hpp
 * @brief Main menu and settings screen.
 */

#include "aoc/ui/Widget.hpp"
#include "aoc/map/MapGenerator.hpp"

#include <functional>
#include <string>

namespace aoc::ui {
class UIManager;
}

namespace aoc::ui {

/// Callback types for menu actions.
using StartGameCallback = std::function<void(aoc::map::MapType, aoc::map::MapSize)>;
using QuitCallback = std::function<void()>;

class MainMenu {
public:
    /// Build the main menu widgets. Callbacks fire when buttons are clicked.
    void build(UIManager& ui, float screenW, float screenH,
               StartGameCallback onStartGame, QuitCallback onQuit,
               std::function<void()> onSettings = {});

    /// Rebuild positions after resize.
    void updateLayout(UIManager& ui, float screenW, float screenH);

    /// Remove all menu widgets.
    void destroy(UIManager& ui);

    [[nodiscard]] bool isBuilt() const { return this->m_isBuilt; }

private:
    /// Re-color map type selection buttons to reflect current selection.
    void updateMapTypeButtons(UIManager& ui);

    /// Re-color map size selection buttons to reflect current selection.
    void updateMapSizeButtons(UIManager& ui);

    bool m_isBuilt = false;
    WidgetId m_rootPanel = INVALID_WIDGET;

    // Current selection state for game start options
    aoc::map::MapType m_selectedMapType = aoc::map::MapType::Continents;
    aoc::map::MapSize m_selectedMapSize = aoc::map::MapSize::Standard;

    // Map type buttons (for re-coloring on selection change)
    WidgetId m_btnContinents  = INVALID_WIDGET;
    WidgetId m_btnPangaea     = INVALID_WIDGET;
    WidgetId m_btnArchipelago = INVALID_WIDGET;
    WidgetId m_btnFractal     = INVALID_WIDGET;

    // Map size buttons
    WidgetId m_btnSmall    = INVALID_WIDGET;
    WidgetId m_btnStandard = INVALID_WIDGET;
    WidgetId m_btnLarge    = INVALID_WIDGET;

    // Stored callbacks
    StartGameCallback m_onStartGame;
    QuitCallback m_onQuit;
    std::function<void()> m_onSettings;
};

/// Persistent settings that can be applied to the game.
struct GameSettings {
    int32_t masterVolume = 100;   ///< 0-100
    int32_t sfxVolume    = 100;   ///< 0-100
    int32_t musicVolume  = 70;    ///< 0-100
    bool    vsync        = true;
    bool    fullscreen   = false;
    bool    showFPS      = false;
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
    GameSettings m_settings;
};

/// Save settings to a simple key=value text file.
void saveSettings(const GameSettings& settings, const std::string& filepath);

/// Load settings from file. Returns defaults if file doesn't exist.
[[nodiscard]] GameSettings loadSettings(const std::string& filepath);

} // namespace aoc::ui

#pragma once

/**
 * @file SettingsMenu.hpp
 * @brief Full-screen settings overlay + the persistent GameSettings record.
 *
 * Extracted from MainMenu so the settings surface can evolve without
 * touching the menu layer. `SettingsMenu` implements `IScreen` and thus
 * participates in `ScreenRegistry` (Esc-close, resize broadcast, input-
 * gate detection).
 */

#include "aoc/ui/Widget.hpp"
#include "aoc/ui/IScreen.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace aoc::ui {

class UIManager;

/// Persistent settings applied to the game at launch and at runtime.
struct GameSettings {
    int32_t masterVolume = 100;   ///< 0-100
    int32_t sfxVolume    = 100;   ///< 0-100
    int32_t musicVolume  = 70;    ///< 0-100
    bool    vsync        = true;
    bool    fullscreen   = false;
    bool    showFPS      = false;
    bool    showTileYields = true; ///< Display F/P/G/S yields directly on all map tiles
};

class SettingsMenu : public IScreen {
public:
    void build(UIManager& ui, float screenW, float screenH,
               std::function<void()> onBack);
    void destroy(UIManager& ui);
    void refresh(UIManager& ui);
    [[nodiscard]] bool isBuilt() const { return this->m_isBuilt; }

    // IScreen — adapter over build/destroy lifecycle. `isOpen` aliases
    // `isBuilt` so the screen participates in the registry (closing on
    // Esc, reflowing on resize) without reshaping the callsite API.
    [[nodiscard]] bool isOpen() const override { return this->m_isBuilt; }
    void close(UIManager& ui) override { this->destroy(ui); }
    void onResize(UIManager& ui, float width, float height) override;

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
    /// Retained `onBack` so `onResize` can rebuild without losing it.
    std::function<void()> m_onBack;
};

/// Save settings to a simple key=value text file.
void saveSettings(const GameSettings& settings, const std::string& filepath);

/// Load settings from file. Returns defaults if file doesn't exist.
[[nodiscard]] GameSettings loadSettings(const std::string& filepath);

} // namespace aoc::ui

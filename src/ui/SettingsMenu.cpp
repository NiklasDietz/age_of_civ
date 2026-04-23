/**
 * @file SettingsMenu.cpp
 */

#include "aoc/ui/SettingsMenu.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/MainMenuTheme.hpp"
#include "aoc/ui/Theme.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <string>

namespace aoc::ui {

namespace {

/// Helper: create a volume row with label, -/+ buttons, and a value label.
WidgetId createVolumeRow(UIManager& ui, WidgetId parent, float rowW,
                          const std::string& name, int32_t value,
                          WidgetId& valueLabelOut,
                          std::function<void()> onMinus,
                          std::function<void()> onPlus) {
    constexpr float ROW_H   = 28.0f;
    constexpr float LABEL_W = 160.0f;
    constexpr float BTN_W   = 30.0f;
    constexpr float VALUE_W = 60.0f;

    WidgetId row = ui.createPanel(
        parent, {0.0f, 0.0f, rowW, ROW_H},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
    {
        Widget* r = ui.getWidget(row);
        assert(r != nullptr);
        r->layoutDirection = LayoutDirection::Horizontal;
        r->childSpacing = 4.0f;
    }

    (void)ui.createLabel(
        row, {0.0f, 0.0f, LABEL_W, ROW_H},
        LabelData{name, GREY_TEXT, 13.0f});

    {
        ButtonData btn;
        btn.label        = "-";
        btn.fontSize     = 13.0f;
        btn.normalColor  = BTN_GREY;
        btn.hoverColor   = BTN_GREY_HOVER;
        btn.pressedColor = BTN_GREY_PRESS;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 3.0f;
        btn.onClick      = std::move(onMinus);
        (void)ui.createButton(row, {0.0f, 0.0f, BTN_W, ROW_H}, std::move(btn));
    }

    valueLabelOut = ui.createLabel(
        row, {0.0f, 0.0f, VALUE_W, ROW_H},
        LabelData{std::to_string(value) + "%", WHITE_TEXT, 13.0f});

    {
        ButtonData btn;
        btn.label        = "+";
        btn.fontSize     = 13.0f;
        btn.normalColor  = BTN_GREY;
        btn.hoverColor   = BTN_GREY_HOVER;
        btn.pressedColor = BTN_GREY_PRESS;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 3.0f;
        btn.onClick      = std::move(onPlus);
        (void)ui.createButton(row, {0.0f, 0.0f, BTN_W, ROW_H}, std::move(btn));
    }

    return row;
}

/// Helper: create a toggle row with label and an On/Off button.
WidgetId createToggleRow(UIManager& ui, WidgetId parent, float rowW,
                          const std::string& name, bool value,
                          WidgetId& toggleBtnOut,
                          std::function<void()> onToggle) {
    constexpr float ROW_H   = 28.0f;
    constexpr float LABEL_W = 160.0f;
    constexpr float BTN_W   = 80.0f;

    WidgetId row = ui.createPanel(
        parent, {0.0f, 0.0f, rowW, ROW_H},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
    {
        Widget* r = ui.getWidget(row);
        assert(r != nullptr);
        r->layoutDirection = LayoutDirection::Horizontal;
        r->childSpacing = 4.0f;
    }

    (void)ui.createLabel(
        row, {0.0f, 0.0f, LABEL_W, ROW_H},
        LabelData{name, GREY_TEXT, 13.0f});

    {
        ButtonData btn;
        btn.label        = value ? "On" : "Off";
        btn.fontSize     = 13.0f;
        btn.normalColor  = BTN_GREY;
        btn.hoverColor   = BTN_GREY_HOVER;
        btn.pressedColor = BTN_GREY_PRESS;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 3.0f;
        btn.onClick      = std::move(onToggle);
        toggleBtnOut = ui.createButton(
            row, {0.0f, 0.0f, BTN_W, ROW_H}, std::move(btn));
    }

    return row;
}

} // namespace

void SettingsMenu::build(UIManager& ui, float screenW, float screenH,
                         std::function<void()> onBack) {
    assert(!this->m_isBuilt);

    // Retain the back handler so onResize can rebuild after a window
    // change without the caller having to re-thread it through.
    this->m_onBack = onBack;

    this->m_rootPanel = ui.createPanel(
        {0.0f, 0.0f, screenW, screenH},
        PanelData{{0.0f, 0.0f, 0.0f, 0.6f}, 0.0f});

    constexpr float PANEL_W = 420.0f;
    constexpr float PANEL_H = 450.0f;
    const float panelX = (screenW - PANEL_W) * 0.5f;
    const float panelY = (screenH - PANEL_H) * 0.5f;

    WidgetId contentPanel = ui.createPanel(
        this->m_rootPanel,
        {panelX, panelY, PANEL_W, PANEL_H},
        PanelData{PANEL_BG, 8.0f});
    {
        Widget* cp = ui.getWidget(contentPanel);
        assert(cp != nullptr);
        cp->padding = {20.0f, 20.0f, 20.0f, 20.0f};
        cp->childSpacing = 8.0f;
    }

    const float innerW = PANEL_W - 40.0f;

    (void)ui.createLabel(contentPanel, {0.0f, 0.0f, innerW, 30.0f},
                         LabelData{"Settings", GOLDEN_TEXT, 22.0f});

    (void)ui.createLabel(contentPanel, {0.0f, 0.0f, innerW, 20.0f},
                         LabelData{"Audio", SECTION_TEXT, 16.0f});

    (void)createVolumeRow(
        ui, contentPanel, innerW, "Master Volume", this->m_settings.masterVolume,
        this->m_masterVolLabel,
        [this, &ui]() {
            this->m_settings.masterVolume = std::max(0, this->m_settings.masterVolume - 10);
            this->refresh(ui);
        },
        [this, &ui]() {
            this->m_settings.masterVolume = std::min(100, this->m_settings.masterVolume + 10);
            this->refresh(ui);
        });

    (void)createVolumeRow(
        ui, contentPanel, innerW, "SFX Volume", this->m_settings.sfxVolume,
        this->m_sfxVolLabel,
        [this, &ui]() {
            this->m_settings.sfxVolume = std::max(0, this->m_settings.sfxVolume - 10);
            this->refresh(ui);
        },
        [this, &ui]() {
            this->m_settings.sfxVolume = std::min(100, this->m_settings.sfxVolume + 10);
            this->refresh(ui);
        });

    (void)createVolumeRow(
        ui, contentPanel, innerW, "Music Volume", this->m_settings.musicVolume,
        this->m_musicVolLabel,
        [this, &ui]() {
            this->m_settings.musicVolume = std::max(0, this->m_settings.musicVolume - 10);
            this->refresh(ui);
        },
        [this, &ui]() {
            this->m_settings.musicVolume = std::min(100, this->m_settings.musicVolume + 10);
            this->refresh(ui);
        });

    (void)ui.createLabel(contentPanel, {0.0f, 0.0f, innerW, 20.0f},
                         LabelData{"Graphics", SECTION_TEXT, 16.0f});

    (void)createToggleRow(
        ui, contentPanel, innerW, "VSync", this->m_settings.vsync,
        this->m_vsyncLabel,
        [this, &ui]() {
            this->m_settings.vsync = !this->m_settings.vsync;
            this->refresh(ui);
        });

    (void)createToggleRow(
        ui, contentPanel, innerW, "Fullscreen", this->m_settings.fullscreen,
        this->m_fullscreenLabel,
        [this, &ui]() {
            this->m_settings.fullscreen = !this->m_settings.fullscreen;
            this->refresh(ui);
        });

    (void)createToggleRow(
        ui, contentPanel, innerW, "Show FPS", this->m_settings.showFPS,
        this->m_fpsLabel,
        [this, &ui]() {
            this->m_settings.showFPS = !this->m_settings.showFPS;
            this->refresh(ui);
        });

    (void)ui.createLabel(contentPanel, {0.0f, 0.0f, innerW, 20.0f},
                         LabelData{"Gameplay", SECTION_TEXT, 16.0f});

    (void)createToggleRow(
        ui, contentPanel, innerW, "Show Tile Yields", this->m_settings.showTileYields,
        this->m_yieldLabel,
        [this, &ui]() {
            this->m_settings.showTileYields = !this->m_settings.showTileYields;
            this->refresh(ui);
        });

    // UI scale slider (0.75 .. 1.5). Writes directly to Theme.userScale
    // so widgets `scaled()` calls pick it up immediately.
    {
        WidgetId scaleRow = ui.createPanel(
            contentPanel, {0.0f, 0.0f, innerW, 28.0f},
            PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
        Widget* r = ui.getWidget(scaleRow);
        if (r != nullptr) {
            r->layoutDirection = LayoutDirection::Horizontal;
            r->childSpacing = 6.0f;
        }
        (void)ui.createLabel(scaleRow, {0.0f, 0.0f, 160.0f, 24.0f},
                             LabelData{"UI Scale", GREY_TEXT, 13.0f});
        SliderData s;
        s.minValue = 0.75f;
        s.maxValue = 1.50f;
        s.value    = theme().userScale;
        s.step     = 0.05f;
        s.onValueChanged = [](float v) { theme().userScale = v; theme().bumpRevision(); };
        (void)ui.createSlider(scaleRow, {0.0f, 0.0f, innerW - 180.0f, 18.0f},
                               std::move(s));
    }

    // Colour scheme cycler: Default → Deuteranopia → HighContrast → …
    {
        WidgetId row = ui.createPanel(
            contentPanel, {0.0f, 0.0f, innerW, 28.0f},
            PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
        Widget* r = ui.getWidget(row);
        if (r != nullptr) {
            r->layoutDirection = LayoutDirection::Horizontal;
            r->childSpacing = 6.0f;
        }
        (void)ui.createLabel(row, {0.0f, 0.0f, 160.0f, 24.0f},
                             LabelData{"Colour Scheme", GREY_TEXT, 13.0f});
        ButtonData btn;
        const auto schemeName = []() -> const char* {
            switch (theme().colorScheme) {
                case ColorScheme::Default:      return "Default";
                case ColorScheme::Deuteranopia: return "Deuteranopia";
                case ColorScheme::Protanopia:   return "Protanopia";
                case ColorScheme::Tritanopia:   return "Tritanopia";
                case ColorScheme::HighContrast: return "High Contrast";
            }
            return "Default";
        };
        btn.label = schemeName();
        btn.fontSize = 13.0f;
        btn.normalColor  = BTN_GREY;
        btn.hoverColor   = BTN_GREY_HOVER;
        btn.pressedColor = BTN_GREY_PRESS;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 3.0f;
        btn.onClick = [&ui, schemeName]() {
            Theme& t = theme();
            t.colorScheme = static_cast<ColorScheme>(
                (static_cast<uint8_t>(t.colorScheme) + 1) % 5);
            t.bumpRevision();
            (void)schemeName;  // Can't retarget the button label from here
                               // without the widget id — refresh on next
                               // menu open surfaces the new choice.
            (void)ui;
        };
        (void)ui.createButton(row, {0.0f, 0.0f, innerW - 180.0f, 22.0f},
                               std::move(btn));
    }

    (void)ui.createPanel(
        contentPanel, {0.0f, 0.0f, innerW, 10.0f},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});

    {
        ButtonData btn;
        btn.label        = "Back";
        btn.fontSize     = 14.0f;
        btn.normalColor  = BTN_GREY;
        btn.hoverColor   = BTN_GREY_HOVER;
        btn.pressedColor = BTN_GREY_PRESS;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick      = std::move(onBack);
        (void)ui.createButton(contentPanel, {0.0f, 0.0f, innerW, 34.0f},
                              std::move(btn));
    }

    this->m_isBuilt = true;
    LOG_INFO("Settings menu built");
}

void SettingsMenu::destroy(UIManager& ui) {
    if (!this->m_isBuilt) {
        return;
    }
    ui.removeWidget(this->m_rootPanel);
    this->m_rootPanel       = INVALID_WIDGET;
    this->m_masterVolLabel  = INVALID_WIDGET;
    this->m_sfxVolLabel     = INVALID_WIDGET;
    this->m_musicVolLabel   = INVALID_WIDGET;
    this->m_vsyncLabel      = INVALID_WIDGET;
    this->m_fullscreenLabel = INVALID_WIDGET;
    this->m_fpsLabel        = INVALID_WIDGET;
    this->m_yieldLabel      = INVALID_WIDGET;
    this->m_isBuilt         = false;
    LOG_INFO("Settings menu destroyed");
}

void SettingsMenu::onResize(UIManager& ui, float width, float height) {
    if (!this->m_isBuilt) { return; }
    std::function<void()> onBack = this->m_onBack;
    this->destroy(ui);
    this->build(ui, width, height, std::move(onBack));
}

void SettingsMenu::refresh(UIManager& ui) {
    ui.setLabelText(this->m_masterVolLabel,
                    std::to_string(this->m_settings.masterVolume) + "%");
    ui.setLabelText(this->m_sfxVolLabel,
                    std::to_string(this->m_settings.sfxVolume) + "%");
    ui.setLabelText(this->m_musicVolLabel,
                    std::to_string(this->m_settings.musicVolume) + "%");
    ui.setButtonLabel(this->m_vsyncLabel,
                      this->m_settings.vsync ? "On" : "Off");
    ui.setButtonLabel(this->m_fullscreenLabel,
                      this->m_settings.fullscreen ? "On" : "Off");
    ui.setButtonLabel(this->m_fpsLabel,
                      this->m_settings.showFPS ? "On" : "Off");
}

void saveSettings(const GameSettings& settings, const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open settings file for writing: %s", filepath.c_str());
        return;
    }
    file << "masterVolume=" << settings.masterVolume << "\n";
    file << "sfxVolume=" << settings.sfxVolume << "\n";
    file << "musicVolume=" << settings.musicVolume << "\n";
    file << "vsync=" << (settings.vsync ? 1 : 0) << "\n";
    file << "fullscreen=" << (settings.fullscreen ? 1 : 0) << "\n";
    file << "showFPS=" << (settings.showFPS ? 1 : 0) << "\n";
    file << "showTileYields=" << (settings.showTileYields ? 1 : 0) << "\n";
    LOG_INFO("Settings saved to %s", filepath.c_str());
}

GameSettings loadSettings(const std::string& filepath) {
    GameSettings settings;
    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_INFO("No settings file found at %s, using defaults", filepath.c_str());
        return settings;
    }
    std::string line;
    while (std::getline(file, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key   = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "masterVolume") {
            settings.masterVolume = std::stoi(value);
        } else if (key == "sfxVolume") {
            settings.sfxVolume = std::stoi(value);
        } else if (key == "musicVolume") {
            settings.musicVolume = std::stoi(value);
        } else if (key == "vsync") {
            settings.vsync = (value == "1");
        } else if (key == "fullscreen") {
            settings.fullscreen = (value == "1");
        } else if (key == "showFPS") {
            settings.showFPS = (value == "1");
        } else if (key == "showTileYields") {
            settings.showTileYields = (value == "1");
        }
    }
    LOG_INFO("Settings loaded from %s", filepath.c_str());
    return settings;
}

} // namespace aoc::ui

/**
 * @file MainMenu.cpp
 * @brief Main menu and settings screen implementation.
 */

#include "aoc/ui/MainMenu.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/core/Log.hpp"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <string>
#include <utility>

namespace aoc::ui {

// ============================================================================
// Color constants
// ============================================================================

static constexpr Color GOLDEN_TEXT      = {1.0f, 0.85f, 0.3f, 1.0f};
static constexpr Color WHITE_TEXT       = {1.0f, 1.0f, 1.0f, 1.0f};
static constexpr Color GREY_TEXT        = {0.7f, 0.7f, 0.7f, 1.0f};
static constexpr Color SECTION_TEXT     = {0.8f, 0.8f, 0.6f, 1.0f};

static constexpr Color BG_DARK         = {0.05f, 0.05f, 0.08f, 0.95f};
static constexpr Color PANEL_BG        = {0.10f, 0.10f, 0.15f, 0.92f};

static constexpr Color BTN_NORMAL      = {0.20f, 0.20f, 0.28f, 0.9f};
static constexpr Color BTN_HOVER       = {0.30f, 0.30f, 0.38f, 0.9f};
static constexpr Color BTN_PRESSED     = {0.12f, 0.12f, 0.18f, 0.9f};

static constexpr Color BTN_SELECTED    = {0.35f, 0.55f, 0.75f, 0.95f};
static constexpr Color BTN_SEL_HOVER   = {0.40f, 0.60f, 0.80f, 0.95f};
static constexpr Color BTN_SEL_PRESSED = {0.25f, 0.45f, 0.65f, 0.95f};

static constexpr Color BTN_GREEN       = {0.15f, 0.45f, 0.15f, 0.9f};
static constexpr Color BTN_GREEN_HOVER = {0.20f, 0.55f, 0.20f, 0.9f};
static constexpr Color BTN_GREEN_PRESS = {0.10f, 0.35f, 0.10f, 0.9f};

static constexpr Color BTN_RED         = {0.50f, 0.15f, 0.15f, 0.9f};
static constexpr Color BTN_RED_HOVER   = {0.60f, 0.20f, 0.20f, 0.9f};
static constexpr Color BTN_RED_PRESS   = {0.40f, 0.10f, 0.10f, 0.9f};

static constexpr Color BTN_GREY        = {0.25f, 0.25f, 0.30f, 0.9f};
static constexpr Color BTN_GREY_HOVER  = {0.35f, 0.35f, 0.40f, 0.9f};
static constexpr Color BTN_GREY_PRESS  = {0.15f, 0.15f, 0.20f, 0.9f};

// ============================================================================
// Helper to set button colors for selected/unselected state
// ============================================================================

static void setButtonSelected(UIManager& ui, WidgetId id, bool selected) {
    Widget* widget = ui.getWidget(id);
    if (widget == nullptr) {
        return;
    }
    ButtonData* btn = std::get_if<ButtonData>(&widget->data);
    if (btn == nullptr) {
        return;
    }
    if (selected) {
        btn->normalColor  = BTN_SELECTED;
        btn->hoverColor   = BTN_SEL_HOVER;
        btn->pressedColor = BTN_SEL_PRESSED;
    } else {
        btn->normalColor  = BTN_NORMAL;
        btn->hoverColor   = BTN_HOVER;
        btn->pressedColor = BTN_PRESSED;
    }
}

// ============================================================================
// MainMenu
// ============================================================================

void MainMenu::build(UIManager& ui, float screenW, float screenH,
                     StartGameCallback onStartGame, QuitCallback onQuit,
                     std::function<void()> onSettings) {
    assert(!this->m_isBuilt);

    this->m_onStartGame = std::move(onStartGame);
    this->m_onQuit      = std::move(onQuit);
    this->m_onSettings  = std::move(onSettings);

    // Full-screen dark background
    this->m_rootPanel = ui.createPanel(
        {0.0f, 0.0f, screenW, screenH},
        PanelData{BG_DARK, 0.0f});

    // Centered content panel
    constexpr float PANEL_W = 420.0f;
    constexpr float PANEL_H = 520.0f;
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

    const float innerW = PANEL_W - 40.0f;  // 20px padding each side

    // Title
    [[maybe_unused]] WidgetId titleLabel = ui.createLabel(
        contentPanel,
        {0.0f, 0.0f, innerW, 30.0f},
        LabelData{"Age of Civilization", GOLDEN_TEXT, 22.0f});

    // Spacer
    [[maybe_unused]] WidgetId spacer1 = ui.createPanel(
        contentPanel,
        {0.0f, 0.0f, innerW, 10.0f},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});

    // --- Map Type section ---
    [[maybe_unused]] WidgetId mapTypeLabel = ui.createLabel(
        contentPanel,
        {0.0f, 0.0f, innerW, 18.0f},
        LabelData{"Map Type:", SECTION_TEXT, 14.0f});

    // Row of map type buttons
    WidgetId mapTypeRow = ui.createPanel(
        contentPanel,
        {0.0f, 0.0f, innerW, 32.0f},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
    {
        Widget* row = ui.getWidget(mapTypeRow);
        assert(row != nullptr);
        row->layoutDirection = LayoutDirection::Horizontal;
        row->childSpacing = 6.0f;
    }

    constexpr float MAP_TYPE_BTN_W = 90.0f;
    constexpr float MAP_TYPE_BTN_H = 28.0f;

    // Continents
    {
        ButtonData btn;
        btn.label       = "Continents";
        btn.fontSize    = 12.0f;
        btn.normalColor = BTN_SELECTED;
        btn.hoverColor  = BTN_SEL_HOVER;
        btn.pressedColor = BTN_SEL_PRESSED;
        btn.labelColor  = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this, &ui]() {
            this->m_selectedMapType = aoc::map::MapType::Continents;
            this->updateMapTypeButtons(ui);
        };
        this->m_btnContinents = ui.createButton(
            mapTypeRow, {0.0f, 0.0f, MAP_TYPE_BTN_W, MAP_TYPE_BTN_H}, std::move(btn));
    }

    // Pangaea
    {
        ButtonData btn;
        btn.label       = "Pangaea";
        btn.fontSize    = 12.0f;
        btn.normalColor = BTN_NORMAL;
        btn.hoverColor  = BTN_HOVER;
        btn.pressedColor = BTN_PRESSED;
        btn.labelColor  = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this, &ui]() {
            this->m_selectedMapType = aoc::map::MapType::Pangaea;
            this->updateMapTypeButtons(ui);
        };
        this->m_btnPangaea = ui.createButton(
            mapTypeRow, {0.0f, 0.0f, MAP_TYPE_BTN_W, MAP_TYPE_BTN_H}, std::move(btn));
    }

    // Archipelago
    {
        ButtonData btn;
        btn.label       = "Archipelago";
        btn.fontSize    = 12.0f;
        btn.normalColor = BTN_NORMAL;
        btn.hoverColor  = BTN_HOVER;
        btn.pressedColor = BTN_PRESSED;
        btn.labelColor  = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this, &ui]() {
            this->m_selectedMapType = aoc::map::MapType::Archipelago;
            this->updateMapTypeButtons(ui);
        };
        this->m_btnArchipelago = ui.createButton(
            mapTypeRow, {0.0f, 0.0f, MAP_TYPE_BTN_W, MAP_TYPE_BTN_H}, std::move(btn));
    }

    // Fractal
    {
        ButtonData btn;
        btn.label       = "Fractal";
        btn.fontSize    = 12.0f;
        btn.normalColor = BTN_NORMAL;
        btn.hoverColor  = BTN_HOVER;
        btn.pressedColor = BTN_PRESSED;
        btn.labelColor  = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this, &ui]() {
            this->m_selectedMapType = aoc::map::MapType::Fractal;
            this->updateMapTypeButtons(ui);
        };
        this->m_btnFractal = ui.createButton(
            mapTypeRow, {0.0f, 0.0f, MAP_TYPE_BTN_W, MAP_TYPE_BTN_H}, std::move(btn));
    }

    // --- Map Size section ---
    [[maybe_unused]] WidgetId mapSizeLabel = ui.createLabel(
        contentPanel,
        {0.0f, 0.0f, innerW, 18.0f},
        LabelData{"Map Size:", SECTION_TEXT, 14.0f});

    WidgetId mapSizeRow = ui.createPanel(
        contentPanel,
        {0.0f, 0.0f, innerW, 32.0f},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
    {
        Widget* row = ui.getWidget(mapSizeRow);
        assert(row != nullptr);
        row->layoutDirection = LayoutDirection::Horizontal;
        row->childSpacing = 6.0f;
    }

    constexpr float MAP_SIZE_BTN_W = 100.0f;
    constexpr float MAP_SIZE_BTN_H = 28.0f;

    // Small
    {
        ButtonData btn;
        btn.label       = "Small";
        btn.fontSize    = 12.0f;
        btn.normalColor = BTN_NORMAL;
        btn.hoverColor  = BTN_HOVER;
        btn.pressedColor = BTN_PRESSED;
        btn.labelColor  = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this, &ui]() {
            this->m_selectedMapSize = aoc::map::MapSize::Small;
            this->updateMapSizeButtons(ui);
        };
        this->m_btnSmall = ui.createButton(
            mapSizeRow, {0.0f, 0.0f, MAP_SIZE_BTN_W, MAP_SIZE_BTN_H}, std::move(btn));
    }

    // Standard (selected by default)
    {
        ButtonData btn;
        btn.label       = "Standard";
        btn.fontSize    = 12.0f;
        btn.normalColor = BTN_SELECTED;
        btn.hoverColor  = BTN_SEL_HOVER;
        btn.pressedColor = BTN_SEL_PRESSED;
        btn.labelColor  = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this, &ui]() {
            this->m_selectedMapSize = aoc::map::MapSize::Standard;
            this->updateMapSizeButtons(ui);
        };
        this->m_btnStandard = ui.createButton(
            mapSizeRow, {0.0f, 0.0f, MAP_SIZE_BTN_W, MAP_SIZE_BTN_H}, std::move(btn));
    }

    // Large
    {
        ButtonData btn;
        btn.label       = "Large";
        btn.fontSize    = 12.0f;
        btn.normalColor = BTN_NORMAL;
        btn.hoverColor  = BTN_HOVER;
        btn.pressedColor = BTN_PRESSED;
        btn.labelColor  = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this, &ui]() {
            this->m_selectedMapSize = aoc::map::MapSize::Large;
            this->updateMapSizeButtons(ui);
        };
        this->m_btnLarge = ui.createButton(
            mapSizeRow, {0.0f, 0.0f, MAP_SIZE_BTN_W, MAP_SIZE_BTN_H}, std::move(btn));
    }

    // Spacer
    [[maybe_unused]] WidgetId spacer2 = ui.createPanel(
        contentPanel,
        {0.0f, 0.0f, innerW, 20.0f},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});

    // --- Start Game button ---
    {
        ButtonData btn;
        btn.label       = "Start Game";
        btn.fontSize    = 16.0f;
        btn.normalColor = BTN_GREEN;
        btn.hoverColor  = BTN_GREEN_HOVER;
        btn.pressedColor = BTN_GREEN_PRESS;
        btn.labelColor  = WHITE_TEXT;
        btn.cornerRadius = 5.0f;
        btn.onClick = [this]() {
            if (this->m_onStartGame) {
                this->m_onStartGame(this->m_selectedMapType, this->m_selectedMapSize);
            }
        };
        [[maybe_unused]] WidgetId startBtn = ui.createButton(
            contentPanel, {0.0f, 0.0f, innerW, 40.0f}, std::move(btn));
    }

    // --- Settings button ---
    {
        ButtonData btn;
        btn.label       = "Settings";
        btn.fontSize    = 14.0f;
        btn.normalColor = BTN_GREY;
        btn.hoverColor  = BTN_GREY_HOVER;
        btn.pressedColor = BTN_GREY_PRESS;
        btn.labelColor  = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = this->m_onSettings;
        [[maybe_unused]] WidgetId settingsBtn = ui.createButton(
            contentPanel, {0.0f, 0.0f, innerW, 34.0f}, std::move(btn));
    }

    // --- Quit button ---
    {
        ButtonData btn;
        btn.label       = "Quit";
        btn.fontSize    = 14.0f;
        btn.normalColor = BTN_RED;
        btn.hoverColor  = BTN_RED_HOVER;
        btn.pressedColor = BTN_RED_PRESS;
        btn.labelColor  = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this]() {
            if (this->m_onQuit) {
                this->m_onQuit();
            }
        };
        [[maybe_unused]] WidgetId quitBtn = ui.createButton(
            contentPanel, {0.0f, 0.0f, innerW, 34.0f}, std::move(btn));
    }

    this->m_isBuilt = true;
    LOG_INFO("Main menu built (%.0fx%.0f)",
             static_cast<double>(screenW), static_cast<double>(screenH));
}

void MainMenu::updateLayout(UIManager& ui, float screenW, float screenH) {
    if (!this->m_isBuilt) {
        return;
    }
    Widget* root = ui.getWidget(this->m_rootPanel);
    if (root == nullptr) {
        return;
    }
    root->requestedBounds.w = screenW;
    root->requestedBounds.h = screenH;

    // Re-center the content panel (first child of root)
    if (!root->children.empty()) {
        constexpr float PANEL_W = 420.0f;
        constexpr float PANEL_H = 520.0f;
        Widget* content = ui.getWidget(root->children[0]);
        if (content != nullptr) {
            content->requestedBounds.x = (screenW - PANEL_W) * 0.5f;
            content->requestedBounds.y = (screenH - PANEL_H) * 0.5f;
        }
    }
}

void MainMenu::destroy(UIManager& ui) {
    if (!this->m_isBuilt) {
        return;
    }
    ui.removeWidget(this->m_rootPanel);
    this->m_rootPanel      = INVALID_WIDGET;
    this->m_btnContinents  = INVALID_WIDGET;
    this->m_btnPangaea     = INVALID_WIDGET;
    this->m_btnArchipelago = INVALID_WIDGET;
    this->m_btnFractal     = INVALID_WIDGET;
    this->m_btnSmall       = INVALID_WIDGET;
    this->m_btnStandard    = INVALID_WIDGET;
    this->m_btnLarge       = INVALID_WIDGET;
    this->m_onStartGame    = nullptr;
    this->m_onQuit         = nullptr;
    this->m_isBuilt        = false;
    LOG_INFO("Main menu destroyed");
}

void MainMenu::updateMapTypeButtons(UIManager& ui) {
    setButtonSelected(ui, this->m_btnContinents,
                      this->m_selectedMapType == aoc::map::MapType::Continents);
    setButtonSelected(ui, this->m_btnPangaea,
                      this->m_selectedMapType == aoc::map::MapType::Pangaea);
    setButtonSelected(ui, this->m_btnArchipelago,
                      this->m_selectedMapType == aoc::map::MapType::Archipelago);
    setButtonSelected(ui, this->m_btnFractal,
                      this->m_selectedMapType == aoc::map::MapType::Fractal);
}

void MainMenu::updateMapSizeButtons(UIManager& ui) {
    setButtonSelected(ui, this->m_btnSmall,
                      this->m_selectedMapSize == aoc::map::MapSize::Small);
    setButtonSelected(ui, this->m_btnStandard,
                      this->m_selectedMapSize == aoc::map::MapSize::Standard);
    setButtonSelected(ui, this->m_btnLarge,
                      this->m_selectedMapSize == aoc::map::MapSize::Large);
}

// ============================================================================
// SettingsMenu
// ============================================================================

/// Helper: create a volume row with label, -/+ buttons, and a value label.
static WidgetId createVolumeRow(UIManager& ui, WidgetId parent, float rowW,
                                 const std::string& name, int32_t value,
                                 WidgetId& valueLabelOut,
                                 std::function<void()> onMinus,
                                 std::function<void()> onPlus) {
    constexpr float ROW_H      = 28.0f;
    constexpr float LABEL_W    = 160.0f;
    constexpr float BTN_W      = 30.0f;
    constexpr float VALUE_W    = 60.0f;

    WidgetId row = ui.createPanel(
        parent, {0.0f, 0.0f, rowW, ROW_H},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
    {
        Widget* r = ui.getWidget(row);
        assert(r != nullptr);
        r->layoutDirection = LayoutDirection::Horizontal;
        r->childSpacing = 4.0f;
    }

    // Setting name label
    [[maybe_unused]] WidgetId lbl = ui.createLabel(
        row, {0.0f, 0.0f, LABEL_W, ROW_H},
        LabelData{name, GREY_TEXT, 13.0f});

    // Minus button
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
        [[maybe_unused]] WidgetId minusBtn = ui.createButton(
            row, {0.0f, 0.0f, BTN_W, ROW_H}, std::move(btn));
    }

    // Value label
    valueLabelOut = ui.createLabel(
        row, {0.0f, 0.0f, VALUE_W, ROW_H},
        LabelData{std::to_string(value) + "%", WHITE_TEXT, 13.0f});

    // Plus button
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
        [[maybe_unused]] WidgetId plusBtn = ui.createButton(
            row, {0.0f, 0.0f, BTN_W, ROW_H}, std::move(btn));
    }

    return row;
}

/// Helper: create a toggle row with label and an On/Off button.
static WidgetId createToggleRow(UIManager& ui, WidgetId parent, float rowW,
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

    // Setting name label
    [[maybe_unused]] WidgetId lbl = ui.createLabel(
        row, {0.0f, 0.0f, LABEL_W, ROW_H},
        LabelData{name, GREY_TEXT, 13.0f});

    // Toggle button
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

void SettingsMenu::build(UIManager& ui, float screenW, float screenH,
                         std::function<void()> onBack) {
    assert(!this->m_isBuilt);

    // Overlay background (semi-transparent)
    this->m_rootPanel = ui.createPanel(
        {0.0f, 0.0f, screenW, screenH},
        PanelData{{0.0f, 0.0f, 0.0f, 0.6f}, 0.0f});

    // Centered settings panel
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

    // Title
    [[maybe_unused]] WidgetId titleLabel = ui.createLabel(
        contentPanel,
        {0.0f, 0.0f, innerW, 30.0f},
        LabelData{"Settings", GOLDEN_TEXT, 22.0f});

    // --- Audio section ---
    [[maybe_unused]] WidgetId audioSection = ui.createLabel(
        contentPanel,
        {0.0f, 0.0f, innerW, 20.0f},
        LabelData{"Audio", SECTION_TEXT, 16.0f});

    // Master Volume
    [[maybe_unused]] WidgetId masterRow = createVolumeRow(
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

    // SFX Volume
    [[maybe_unused]] WidgetId sfxRow = createVolumeRow(
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

    // Music Volume
    [[maybe_unused]] WidgetId musicRow = createVolumeRow(
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

    // --- Graphics section ---
    [[maybe_unused]] WidgetId graphicsSection = ui.createLabel(
        contentPanel,
        {0.0f, 0.0f, innerW, 20.0f},
        LabelData{"Graphics", SECTION_TEXT, 16.0f});

    // VSync
    [[maybe_unused]] WidgetId vsyncRow = createToggleRow(
        ui, contentPanel, innerW, "VSync", this->m_settings.vsync,
        this->m_vsyncLabel,
        [this, &ui]() {
            this->m_settings.vsync = !this->m_settings.vsync;
            this->refresh(ui);
        });

    // Fullscreen
    [[maybe_unused]] WidgetId fullscreenRow = createToggleRow(
        ui, contentPanel, innerW, "Fullscreen", this->m_settings.fullscreen,
        this->m_fullscreenLabel,
        [this, &ui]() {
            this->m_settings.fullscreen = !this->m_settings.fullscreen;
            this->refresh(ui);
        });

    // Show FPS
    [[maybe_unused]] WidgetId fpsRow = createToggleRow(
        ui, contentPanel, innerW, "Show FPS", this->m_settings.showFPS,
        this->m_fpsLabel,
        [this, &ui]() {
            this->m_settings.showFPS = !this->m_settings.showFPS;
            this->refresh(ui);
        });

    // Spacer
    [[maybe_unused]] WidgetId spacer = ui.createPanel(
        contentPanel,
        {0.0f, 0.0f, innerW, 10.0f},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});

    // Back button
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
        [[maybe_unused]] WidgetId backBtn = ui.createButton(
            contentPanel, {0.0f, 0.0f, innerW, 34.0f}, std::move(btn));
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
    this->m_isBuilt         = false;
    LOG_INFO("Settings menu destroyed");
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

// ============================================================================
// Settings persistence
// ============================================================================

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
        }
    }
    LOG_INFO("Settings loaded from %s", filepath.c_str());
    return settings;
}

} // namespace aoc::ui

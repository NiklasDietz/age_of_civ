/**
 * @file MainMenu.cpp
 * @brief Main menu and settings screen implementation.
 */

#include "aoc/ui/MainMenu.hpp"
#include "aoc/ui/MainMenuTheme.hpp"
#include "aoc/ui/Theme.hpp"
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

// Palette now lives in MainMenuTheme.hpp so SettingsMenu (split into its
// own translation unit) can share it without duplication.

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
                     std::function<void()> onStartGame,
                     std::function<void()> onQuit,
                     std::function<void()> onSettings,
                     std::function<void()> onTutorial,
                     std::function<void()> onSpectate) {
    assert(!this->m_isBuilt);

    this->m_onStartGame = std::move(onStartGame);
    this->m_onQuit      = std::move(onQuit);
    this->m_onSettings  = std::move(onSettings);
    this->m_onTutorial  = std::move(onTutorial);
    this->m_onSpectate  = std::move(onSpectate);

    // Full-screen dark background
    this->m_rootPanel = ui.createPanel(
        {0.0f, 0.0f, screenW, screenH},
        PanelData{BG_DARK, 0.0f});

    // Centered content panel — extra 42px for the Spectate button row.
    constexpr float PANEL_W = 420.0f;
    constexpr float PANEL_H = 362.0f;
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
        {0.0f, 0.0f, innerW, 20.0f},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});

    // --- Start Game button (opens Game Setup screen) ---
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
                this->m_onStartGame();
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

    // --- Tutorial button ---
    if (this->m_onTutorial) {
        ButtonData btn;
        btn.label       = "Tutorial";
        btn.fontSize    = 14.0f;
        btn.normalColor = BTN_NORMAL;
        btn.hoverColor  = BTN_HOVER;
        btn.pressedColor = BTN_PRESSED;
        btn.labelColor  = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this]() {
            if (this->m_onTutorial) {
                this->m_onTutorial();
            }
        };
        [[maybe_unused]] WidgetId tutorialBtn = ui.createButton(
            contentPanel, {0.0f, 0.0f, innerW, 34.0f}, std::move(btn));
    }

    // --- Spectate button ---
    if (this->m_onSpectate) {
        ButtonData btn;
        btn.label        = "Spectate AI Game";
        btn.fontSize     = 14.0f;
        btn.normalColor  = {0.15f, 0.25f, 0.45f, 0.90f};
        btn.hoverColor   = {0.20f, 0.35f, 0.60f, 0.90f};
        btn.pressedColor = {0.10f, 0.18f, 0.32f, 0.90f};
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this]() {
            if (this->m_onSpectate) {
                this->m_onSpectate();
            }
        };
        [[maybe_unused]] WidgetId spectateBtn = ui.createButton(
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
        constexpr float PANEL_H = 362.0f;
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
    this->m_rootPanel   = INVALID_WIDGET;
    this->m_onStartGame = nullptr;
    this->m_onQuit      = nullptr;
    this->m_onSettings  = nullptr;
    this->m_onTutorial  = nullptr;
    this->m_onSpectate  = nullptr;
    this->m_isBuilt     = false;
    LOG_INFO("Main menu destroyed");
}

// ============================================================================
// GameSetupScreen
// ============================================================================

/// Civilization names (indexed by CivId).
static constexpr std::array<std::string_view, aoc::sim::CIV_COUNT> CIV_NAMES = {{
    "Rome", "Egypt", "China", "Germany", "Greece", "England", "Japan", "Persia",
    "Aztec", "India", "Russia", "Brazil"
}};

void GameSetupScreen::build(UIManager& ui, float screenW, float screenH,
                            std::function<void(const GameSetupConfig&)> onStart,
                            std::function<void()> onBack) {
    assert(!this->m_isBuilt);

    // Initialize default config
    this->m_config = GameSetupConfig{};
    this->m_config.mapType     = aoc::map::MapType::Continents;
    this->m_config.mapSize     = aoc::map::MapSize::Standard;
    this->m_config.playerCount = 2;
    for (uint8_t i = 0; i < 8; ++i) {
        this->m_config.players[i].isActive = (i < 2);
        this->m_config.players[i].isHuman  = (i == 0);
        this->m_config.players[i].civId    = i;  // Each slot defaults to a unique civ
    }

    // Full-screen dark background
    this->m_rootPanel = ui.createPanel(
        {0.0f, 0.0f, screenW, screenH},
        PanelData{BG_DARK, 0.0f});

    // Centered content panel.  Height clamped to 90% of screen so on small
    // displays the panel doesn't spill offscreen; a ScrollList inside takes
    // over when the content exceeds the visible window (e.g. 8 player rows).
    constexpr float PANEL_W = 550.0f;
    const float PANEL_H = std::min(620.0f, screenH * 0.92f);
    const float panelX = (screenW - PANEL_W) * 0.5f;
    const float panelY = (screenH - PANEL_H) * 0.5f;

    WidgetId outerPanel = ui.createPanel(
        this->m_rootPanel,
        {panelX, panelY, PANEL_W, PANEL_H},
        PanelData{PANEL_BG, 8.0f});
    {
        Widget* cp = ui.getWidget(outerPanel);
        assert(cp != nullptr);
        cp->padding = {20.0f, 20.0f, 20.0f, 20.0f};
        cp->childSpacing = 6.0f;
    }

    // ScrollList wraps the actual content so many-player configs stay
    // reachable by scroll-wheel when they exceed the panel's visible height.
    WidgetId contentPanel = ui.createScrollList(
        outerPanel,
        {0.0f, 0.0f, PANEL_W - 40.0f, PANEL_H - 40.0f},
        ScrollListData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f, 0.0f});
    {
        Widget* cp = ui.getWidget(contentPanel);
        assert(cp != nullptr);
        cp->padding = {0.0f, 0.0f, 0.0f, 0.0f};
        cp->childSpacing = 6.0f;
    }

    const float innerW = PANEL_W - 40.0f;

    // Title
    [[maybe_unused]] WidgetId titleLabel = ui.createLabel(
        contentPanel,
        {0.0f, 0.0f, innerW, 30.0f},
        LabelData{"Game Setup", GOLDEN_TEXT, 22.0f});

    // ---- Map Type section ----
    [[maybe_unused]] WidgetId mapTypeLabel = ui.createLabel(
        contentPanel,
        {0.0f, 0.0f, innerW, 18.0f},
        LabelData{"Map Type:", SECTION_TEXT, 14.0f});

    // HorizontalWrap container — 6 map-type buttons flow onto a
    // second row when the panel is narrower than 6 × button width.
    // Height is 68 (two 32px rows + spacing) so both rows fit inside
    // the clamp even on the tightest panel size.
    WidgetId mapTypeRow = ui.createPanel(
        contentPanel,
        {0.0f, 0.0f, innerW, 72.0f},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
    {
        Widget* row = ui.getWidget(mapTypeRow);
        assert(row != nullptr);
        row->layoutDirection = LayoutDirection::HorizontalWrap;
        row->childSpacing = 6.0f;
    }

    constexpr float MAP_TYPE_BTN_W = 90.0f;
    constexpr float MAP_TYPE_BTN_H = 28.0f;

    // Continents
    {
        ButtonData btn;
        btn.label        = "Continents";
        btn.fontSize     = 12.0f;
        btn.normalColor  = BTN_SELECTED;
        btn.hoverColor   = BTN_SEL_HOVER;
        btn.pressedColor = BTN_SEL_PRESSED;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this, &ui]() {
            this->m_config.mapType = aoc::map::MapType::Continents;
            this->updateMapTypeButtons(ui);
        };
        this->m_btnContinents = ui.createButton(
            mapTypeRow, {0.0f, 0.0f, MAP_TYPE_BTN_W, MAP_TYPE_BTN_H}, std::move(btn));
    }

    // Islands
    {
        ButtonData btn;
        btn.label        = "Islands";
        btn.fontSize     = 12.0f;
        btn.normalColor  = BTN_NORMAL;
        btn.hoverColor   = BTN_HOVER;
        btn.pressedColor = BTN_PRESSED;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this, &ui]() {
            this->m_config.mapType = aoc::map::MapType::Islands;
            this->updateMapTypeButtons(ui);
        };
        this->m_btnIslands = ui.createButton(
            mapTypeRow, {0.0f, 0.0f, MAP_TYPE_BTN_W, MAP_TYPE_BTN_H}, std::move(btn));
    }

    // Continents + Islands
    {
        ButtonData btn;
        btn.label        = "Cont+Isl";
        btn.fontSize     = 12.0f;
        btn.normalColor  = BTN_NORMAL;
        btn.hoverColor   = BTN_HOVER;
        btn.pressedColor = BTN_PRESSED;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this, &ui]() {
            this->m_config.mapType = aoc::map::MapType::ContinentsPlusIslands;
            this->updateMapTypeButtons(ui);
        };
        this->m_btnContinentsPlusIslands = ui.createButton(
            mapTypeRow, {0.0f, 0.0f, MAP_TYPE_BTN_W, MAP_TYPE_BTN_H}, std::move(btn));
    }

    // Land Only
    {
        ButtonData btn;
        btn.label        = "Land Only";
        btn.fontSize     = 12.0f;
        btn.normalColor  = BTN_NORMAL;
        btn.hoverColor   = BTN_HOVER;
        btn.pressedColor = BTN_PRESSED;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this, &ui]() {
            this->m_config.mapType = aoc::map::MapType::LandOnly;
            this->updateMapTypeButtons(ui);
        };
        this->m_btnLandOnly = ui.createButton(
            mapTypeRow, {0.0f, 0.0f, MAP_TYPE_BTN_W, MAP_TYPE_BTN_H}, std::move(btn));
    }

    // Land With Seas
    {
        ButtonData btn;
        btn.label        = "LandWSeas";
        btn.fontSize     = 12.0f;
        btn.normalColor  = BTN_NORMAL;
        btn.hoverColor   = BTN_HOVER;
        btn.pressedColor = BTN_PRESSED;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this, &ui]() {
            this->m_config.mapType = aoc::map::MapType::LandWithSeas;
            this->updateMapTypeButtons(ui);
        };
        this->m_btnLandWithSeas = ui.createButton(
            mapTypeRow, {0.0f, 0.0f, MAP_TYPE_BTN_W, MAP_TYPE_BTN_H}, std::move(btn));
    }

    // Fractal
    {
        ButtonData btn;
        btn.label        = "Fractal";
        btn.fontSize     = 12.0f;
        btn.normalColor  = BTN_NORMAL;
        btn.hoverColor   = BTN_HOVER;
        btn.pressedColor = BTN_PRESSED;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this, &ui]() {
            this->m_config.mapType = aoc::map::MapType::Fractal;
            this->updateMapTypeButtons(ui);
        };
        this->m_btnFractal = ui.createButton(
            mapTypeRow, {0.0f, 0.0f, MAP_TYPE_BTN_W, MAP_TYPE_BTN_H}, std::move(btn));
    }

    // ---- Map Size section ----
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
        btn.label        = "Small";
        btn.fontSize     = 12.0f;
        btn.normalColor  = BTN_NORMAL;
        btn.hoverColor   = BTN_HOVER;
        btn.pressedColor = BTN_PRESSED;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this, &ui]() {
            this->m_config.mapSize = aoc::map::MapSize::Small;
            this->updateMapSizeButtons(ui);
        };
        this->m_btnSmall = ui.createButton(
            mapSizeRow, {0.0f, 0.0f, MAP_SIZE_BTN_W, MAP_SIZE_BTN_H}, std::move(btn));
    }

    // Standard (selected by default)
    {
        ButtonData btn;
        btn.label        = "Standard";
        btn.fontSize     = 12.0f;
        btn.normalColor  = BTN_SELECTED;
        btn.hoverColor   = BTN_SEL_HOVER;
        btn.pressedColor = BTN_SEL_PRESSED;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this, &ui]() {
            this->m_config.mapSize = aoc::map::MapSize::Standard;
            this->updateMapSizeButtons(ui);
        };
        this->m_btnStandard = ui.createButton(
            mapSizeRow, {0.0f, 0.0f, MAP_SIZE_BTN_W, MAP_SIZE_BTN_H}, std::move(btn));
    }

    // Large
    {
        ButtonData btn;
        btn.label        = "Large";
        btn.fontSize     = 12.0f;
        btn.normalColor  = BTN_NORMAL;
        btn.hoverColor   = BTN_HOVER;
        btn.pressedColor = BTN_PRESSED;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this, &ui]() {
            this->m_config.mapSize = aoc::map::MapSize::Large;
            this->updateMapSizeButtons(ui);
        };
        this->m_btnLarge = ui.createButton(
            mapSizeRow, {0.0f, 0.0f, MAP_SIZE_BTN_W, MAP_SIZE_BTN_H}, std::move(btn));
    }

    // ---- Resource placement section ----
    [[maybe_unused]] WidgetId placementLabel = ui.createLabel(
        contentPanel,
        {0.0f, 0.0f, innerW, 18.0f},
        LabelData{"Resource Placement:", SECTION_TEXT, 14.0f});

    WidgetId placementRow = ui.createPanel(
        contentPanel,
        {0.0f, 0.0f, innerW, 32.0f},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
    {
        Widget* row = ui.getWidget(placementRow);
        assert(row != nullptr);
        row->layoutDirection = LayoutDirection::Horizontal;
        row->childSpacing = 6.0f;
    }

    constexpr float PLACE_BTN_W = 110.0f;
    constexpr float PLACE_BTN_H = 28.0f;

    auto buildPlaceBtn = [&](const std::string& label,
                             aoc::map::ResourcePlacementMode mode,
                             const std::string& tip,
                             bool selected) -> WidgetId {
        ButtonData btn;
        btn.label        = label;
        btn.fontSize     = 12.0f;
        btn.normalColor  = selected ? BTN_SELECTED : BTN_NORMAL;
        btn.hoverColor   = selected ? BTN_SEL_HOVER : BTN_HOVER;
        btn.pressedColor = selected ? BTN_SEL_PRESSED : BTN_PRESSED;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this, &ui, mode]() {
            this->m_config.placement = mode;
            this->updatePlacementButtons(ui);
        };
        const WidgetId id = ui.createButton(
            placementRow, {0.0f, 0.0f, PLACE_BTN_W, PLACE_BTN_H}, std::move(btn));
        ui.setWidgetTooltip(id, tip);
        return id;
    };

    this->m_btnPlaceRealistic = buildPlaceBtn(
        "Realistic",
        aoc::map::ResourcePlacementMode::Realistic,
        "Geology-driven: coal in sedimentary basins, iron on continental shield, "
        "oil near subduction boundaries.",
        this->m_config.placement == aoc::map::ResourcePlacementMode::Realistic);
    this->m_btnPlaceFair = buildPlaceBtn(
        "Fair",
        aoc::map::ResourcePlacementMode::Fair,
        "Guarantees each quadrant gets comparable strategic-resource access. "
        "Starts realistic then rebalances surplus.",
        this->m_config.placement == aoc::map::ResourcePlacementMode::Fair);
    this->m_btnPlaceRandom = buildPlaceBtn(
        "Random",
        aoc::map::ResourcePlacementMode::Random,
        "Uniform per-tile chance, ignores geology. Wider swings between "
        "resource-rich and resource-starved starts.",
        this->m_config.placement == aoc::map::ResourcePlacementMode::Random);

    // ---- Players section ----
    [[maybe_unused]] WidgetId playersSectionLabel = ui.createLabel(
        contentPanel,
        {0.0f, 0.0f, innerW, 18.0f},
        LabelData{"Players:", SECTION_TEXT, 14.0f});

    // Player count row: "Players: [N]  [-] [+]"
    WidgetId playerCountRow = ui.createPanel(
        contentPanel,
        {0.0f, 0.0f, innerW, 28.0f},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
    {
        Widget* row = ui.getWidget(playerCountRow);
        assert(row != nullptr);
        row->layoutDirection = LayoutDirection::Horizontal;
        row->childSpacing = 6.0f;
    }

    [[maybe_unused]] WidgetId countTextLabel = ui.createLabel(
        playerCountRow,
        {0.0f, 0.0f, 80.0f, 28.0f},
        LabelData{"Number:", GREY_TEXT, 13.0f});

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
        btn.onClick = [this, &ui]() {
            if (this->m_config.playerCount > 2) {
                --this->m_config.playerCount;
                for (uint8_t i = 0; i < 8; ++i) {
                    this->m_config.players[i].isActive = (i < this->m_config.playerCount);
                }
                this->refresh(ui);
            }
        };
        [[maybe_unused]] WidgetId minusBtn = ui.createButton(
            playerCountRow, {0.0f, 0.0f, 30.0f, 28.0f}, std::move(btn));
    }

    this->m_playerCountLabel = ui.createLabel(
        playerCountRow,
        {0.0f, 0.0f, 30.0f, 28.0f},
        LabelData{std::to_string(this->m_config.playerCount), WHITE_TEXT, 13.0f});

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
        btn.onClick = [this, &ui]() {
            if (this->m_config.playerCount < 8) {
                ++this->m_config.playerCount;
                for (uint8_t i = 0; i < 8; ++i) {
                    this->m_config.players[i].isActive = (i < this->m_config.playerCount);
                }
                this->refresh(ui);
            }
        };
        [[maybe_unused]] WidgetId plusBtn = ui.createButton(
            playerCountRow, {0.0f, 0.0f, 30.0f, 28.0f}, std::move(btn));
    }

    // ---- Player slot rows ----
    constexpr float SLOT_ROW_H  = 26.0f;
    constexpr float LABEL_W     = 70.0f;
    constexpr float CIV_BTN_W   = 100.0f;
    constexpr float TYPE_BTN_W  = 70.0f;

    for (uint8_t slot = 0; slot < 8; ++slot) {
        WidgetId slotRow = ui.createPanel(
            contentPanel,
            {0.0f, 0.0f, innerW, SLOT_ROW_H},
            PanelData{{0.08f, 0.08f, 0.12f, 0.75f}, 4.0f});
        {
            Widget* row = ui.getWidget(slotRow);
            assert(row != nullptr);
            row->layoutDirection = LayoutDirection::Horizontal;
            row->childSpacing = 6.0f;
            row->padding = {4.0f, 2.0f, 4.0f, 2.0f};
        }
        this->m_playerRows[slot] = slotRow;

        // Player colour swatch — 6px accent on the leading edge.
        IconData swatch;
        swatch.tint          = theme().playerColor(slot);
        swatch.fallbackColor = swatch.tint;
        (void)ui.createIcon(slotRow, {0.0f, 0.0f, 6.0f, SLOT_ROW_H - 4.0f},
                             std::move(swatch));

        // "Player N:" label
        const std::string slotLabel = "Player " + std::to_string(slot + 1) + ":";
        [[maybe_unused]] WidgetId nameLabel = ui.createLabel(
            slotRow,
            {0.0f, 0.0f, LABEL_W, SLOT_ROW_H},
            LabelData{slotLabel, GREY_TEXT, 12.0f});

        // Civ cycle button
        {
            const std::string civName(CIV_NAMES[this->m_config.players[slot].civId]);
            ButtonData btn;
            btn.label        = civName;
            btn.fontSize     = 12.0f;
            btn.normalColor  = BTN_NORMAL;
            btn.hoverColor   = BTN_HOVER;
            btn.pressedColor = BTN_PRESSED;
            btn.labelColor   = WHITE_TEXT;
            btn.cornerRadius = 3.0f;
            btn.onClick = [this, slot, &ui]() {
                this->m_config.players[slot].civId =
                    static_cast<uint8_t>((this->m_config.players[slot].civId + 1) % aoc::sim::CIV_COUNT);
                this->refresh(ui);
            };
            this->m_civLabels[slot] = ui.createButton(
                slotRow, {0.0f, 0.0f, CIV_BTN_W, SLOT_ROW_H}, std::move(btn));
        }

        // Type toggle button (Human/AI)
        {
            const bool isHuman = this->m_config.players[slot].isHuman;
            ButtonData btn;
            btn.label        = isHuman ? "Human" : "AI";
            btn.fontSize     = 12.0f;
            btn.normalColor  = BTN_NORMAL;
            btn.hoverColor   = BTN_HOVER;
            btn.pressedColor = BTN_PRESSED;
            btn.labelColor   = WHITE_TEXT;
            btn.cornerRadius = 3.0f;
            if (slot == 0) {
                // Player 1 is always Human -- no toggle
                btn.normalColor = BTN_SELECTED;
                btn.hoverColor  = BTN_SEL_HOVER;
                btn.pressedColor = BTN_SEL_PRESSED;
            } else {
                btn.onClick = [this, slot, &ui]() {
                    this->m_config.players[slot].isHuman = !this->m_config.players[slot].isHuman;
                    this->refresh(ui);
                };
            }
            this->m_typeLabels[slot] = ui.createButton(
                slotRow, {0.0f, 0.0f, TYPE_BTN_W, SLOT_ROW_H}, std::move(btn));
        }

        // Hide inactive slots
        if (slot >= this->m_config.playerCount) {
            ui.setVisible(slotRow, false);
        }
    }

    // ---- Sequential Turns in War toggle ----
    {
        const bool seqOn = this->m_config.sequentialTurnsInWar;
        ButtonData btn;
        btn.label        = seqOn ? "Sequential in War: ON" : "Sequential in War: OFF";
        btn.fontSize     = 12.0f;
        btn.normalColor  = seqOn ? BTN_SELECTED : BTN_NORMAL;
        btn.hoverColor   = seqOn ? BTN_SEL_HOVER : BTN_HOVER;
        btn.pressedColor = seqOn ? BTN_SEL_PRESSED : BTN_PRESSED;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this, &ui]() {
            this->m_config.sequentialTurnsInWar = !this->m_config.sequentialTurnsInWar;
            this->refresh(ui);
        };
        this->m_btnSequential = ui.createButton(
            contentPanel, {0.0f, 0.0f, innerW, 28.0f}, std::move(btn));
    }

    // ---- AI Difficulty toggle ----
    {
        const char* diffLabel = "AI Difficulty: Normal";
        if (this->m_config.aiDifficulty == AIDifficulty::Easy) {
            diffLabel = "AI Difficulty: Easy";
        } else if (this->m_config.aiDifficulty == AIDifficulty::Hard) {
            diffLabel = "AI Difficulty: Hard";
        }
        ButtonData btn;
        btn.label        = diffLabel;
        btn.fontSize     = 12.0f;
        btn.normalColor  = BTN_NORMAL;
        btn.hoverColor   = BTN_HOVER;
        btn.pressedColor = BTN_PRESSED;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick = [this, &ui]() {
            // Cycle: Easy -> Normal -> Hard -> Easy
            switch (this->m_config.aiDifficulty) {
                case AIDifficulty::Easy:
                    this->m_config.aiDifficulty = AIDifficulty::Normal;
                    break;
                case AIDifficulty::Normal:
                    this->m_config.aiDifficulty = AIDifficulty::Hard;
                    break;
                case AIDifficulty::Hard:
                    this->m_config.aiDifficulty = AIDifficulty::Easy;
                    break;
            }
            this->refresh(ui);
        };
        this->m_btnDifficulty = ui.createButton(
            contentPanel, {0.0f, 0.0f, innerW, 28.0f}, std::move(btn));
    }

    // Spacer
    [[maybe_unused]] WidgetId spacer = ui.createPanel(
        contentPanel,
        {0.0f, 0.0f, innerW, 8.0f},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});

    // ---- Start Game button ----
    {
        ButtonData btn;
        btn.label        = "Start Game";
        btn.fontSize     = 16.0f;
        btn.normalColor  = BTN_GREEN;
        btn.hoverColor   = BTN_GREEN_HOVER;
        btn.pressedColor = BTN_GREEN_PRESS;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 5.0f;
        std::function<void(const GameSetupConfig&)> startCb = std::move(onStart);
        btn.onClick = [this, startCb]() {
            if (startCb) {
                startCb(this->m_config);
            }
        };
        [[maybe_unused]] WidgetId startBtn = ui.createButton(
            contentPanel, {0.0f, 0.0f, innerW, 40.0f}, std::move(btn));
    }

    // ---- Back button ----
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
    LOG_INFO("Game setup screen built (%.0fx%.0f)",
             static_cast<double>(screenW), static_cast<double>(screenH));
}

void GameSetupScreen::destroy(UIManager& ui) {
    if (!this->m_isBuilt) {
        return;
    }
    ui.removeWidget(this->m_rootPanel);
    this->m_rootPanel        = INVALID_WIDGET;
    this->m_playerCountLabel = INVALID_WIDGET;
    this->m_btnContinents            = INVALID_WIDGET;
    this->m_btnIslands               = INVALID_WIDGET;
    this->m_btnContinentsPlusIslands = INVALID_WIDGET;
    this->m_btnLandOnly              = INVALID_WIDGET;
    this->m_btnLandWithSeas          = INVALID_WIDGET;
    this->m_btnFractal               = INVALID_WIDGET;
    this->m_btnSmall         = INVALID_WIDGET;
    this->m_btnStandard      = INVALID_WIDGET;
    this->m_btnLarge         = INVALID_WIDGET;
    this->m_btnPlaceRealistic= INVALID_WIDGET;
    this->m_btnPlaceFair     = INVALID_WIDGET;
    this->m_btnPlaceRandom   = INVALID_WIDGET;
    this->m_btnSequential    = INVALID_WIDGET;
    this->m_btnDifficulty    = INVALID_WIDGET;
    for (uint8_t i = 0; i < 8; ++i) {
        this->m_playerRows[i] = INVALID_WIDGET;
        this->m_civLabels[i]  = INVALID_WIDGET;
        this->m_typeLabels[i] = INVALID_WIDGET;
    }
    this->m_isBuilt = false;
    LOG_INFO("Game setup screen destroyed");
}

void GameSetupScreen::refresh(UIManager& ui) {
    // Update player count label
    ui.setLabelText(this->m_playerCountLabel,
                    std::to_string(this->m_config.playerCount));

    // Show/hide player rows and update labels
    for (uint8_t i = 0; i < 8; ++i) {
        const bool active = (i < this->m_config.playerCount);
        ui.setVisible(this->m_playerRows[i], active);
        if (active) {
            ui.setButtonLabel(this->m_civLabels[i],
                              std::string(CIV_NAMES[this->m_config.players[i].civId]));
            if (i == 0) {
                ui.setButtonLabel(this->m_typeLabels[i], "Human");
            } else {
                ui.setButtonLabel(this->m_typeLabels[i],
                                  this->m_config.players[i].isHuman ? "Human" : "AI");
            }
        }
    }

    // Update sequential turns toggle
    const bool seqOn = this->m_config.sequentialTurnsInWar;
    ui.setButtonLabel(this->m_btnSequential,
                      seqOn ? "Sequential in War: ON" : "Sequential in War: OFF");
    setButtonSelected(ui, this->m_btnSequential, seqOn);

    // Update AI difficulty toggle
    const char* diffLabel = "AI Difficulty: Normal";
    if (this->m_config.aiDifficulty == AIDifficulty::Easy) {
        diffLabel = "AI Difficulty: Easy";
    } else if (this->m_config.aiDifficulty == AIDifficulty::Hard) {
        diffLabel = "AI Difficulty: Hard";
    }
    ui.setButtonLabel(this->m_btnDifficulty, diffLabel);
}

void GameSetupScreen::updateMapTypeButtons(UIManager& ui) {
    setButtonSelected(ui, this->m_btnContinents,
                      this->m_config.mapType == aoc::map::MapType::Continents);
    setButtonSelected(ui, this->m_btnIslands,
                      this->m_config.mapType == aoc::map::MapType::Islands);
    setButtonSelected(ui, this->m_btnContinentsPlusIslands,
                      this->m_config.mapType == aoc::map::MapType::ContinentsPlusIslands);
    setButtonSelected(ui, this->m_btnLandOnly,
                      this->m_config.mapType == aoc::map::MapType::LandOnly);
    setButtonSelected(ui, this->m_btnLandWithSeas,
                      this->m_config.mapType == aoc::map::MapType::LandWithSeas);
    setButtonSelected(ui, this->m_btnFractal,
                      this->m_config.mapType == aoc::map::MapType::Fractal);
}

void GameSetupScreen::updateMapSizeButtons(UIManager& ui) {
    setButtonSelected(ui, this->m_btnSmall,
                      this->m_config.mapSize == aoc::map::MapSize::Small);
    setButtonSelected(ui, this->m_btnStandard,
                      this->m_config.mapSize == aoc::map::MapSize::Standard);
    setButtonSelected(ui, this->m_btnLarge,
                      this->m_config.mapSize == aoc::map::MapSize::Large);
}

void GameSetupScreen::updatePlacementButtons(UIManager& ui) {
    setButtonSelected(ui, this->m_btnPlaceRealistic,
                      this->m_config.placement == aoc::map::ResourcePlacementMode::Realistic);
    setButtonSelected(ui, this->m_btnPlaceFair,
                      this->m_config.placement == aoc::map::ResourcePlacementMode::Fair);
    setButtonSelected(ui, this->m_btnPlaceRandom,
                      this->m_config.placement == aoc::map::ResourcePlacementMode::Random);
}


} // namespace aoc::ui

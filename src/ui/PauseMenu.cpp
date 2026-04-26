/**
 * @file PauseMenu.cpp
 * @brief In-game pause menu implementation.
 */

#include "aoc/ui/PauseMenu.hpp"

#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/MainMenuTheme.hpp"
#include "aoc/core/Log.hpp"

#include <cassert>

namespace aoc::ui {

void PauseMenu::build(UIManager& ui, float screenW, float screenH,
                       std::function<void()> onResume,
                       std::function<void()> onSave,
                       std::function<void()> onLoad,
                       std::function<void()> onMainMenu,
                       std::function<void()> onQuit) {
    assert(!this->m_isBuilt);
    this->m_onResume   = std::move(onResume);
    this->m_onSave     = std::move(onSave);
    this->m_onLoad     = std::move(onLoad);
    this->m_onMainMenu = std::move(onMainMenu);
    this->m_onQuit     = std::move(onQuit);
    this->m_lastW      = screenW;
    this->m_lastH      = screenH;

    // Full-screen dim background.
    this->m_rootPanel = ui.createPanel(
        {0.0f, 0.0f, screenW, screenH},
        PanelData{{0.0f, 0.0f, 0.0f, 0.55f}, 0.0f});

    constexpr float PANEL_W = 340.0f;
    constexpr float PANEL_H = 320.0f;
    const float panelX = (screenW - PANEL_W) * 0.5f;
    const float panelY = (screenH - PANEL_H) * 0.5f;

    WidgetId panel = ui.createPanel(
        this->m_rootPanel,
        {panelX, panelY, PANEL_W, PANEL_H},
        PanelData{PANEL_BG, 8.0f});
    {
        Widget* p = ui.getWidget(panel);
        assert(p != nullptr);
        p->padding = {20.0f, 20.0f, 20.0f, 20.0f};
        p->childSpacing = 8.0f;
    }
    const float innerW = PANEL_W - 40.0f;

    // Title
    [[maybe_unused]] WidgetId title = ui.createLabel(
        panel, {0.0f, 0.0f, innerW, 30.0f},
        LabelData{"Paused", GOLDEN_TEXT, 22.0f});

    [[maybe_unused]] WidgetId spacer = ui.createPanel(
        panel, {0.0f, 0.0f, innerW, 12.0f},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});

    auto makeButton = [&](const char* label, Color color, Color hover, Color pressed,
                          std::function<void()> cb) {
        ButtonData btn;
        btn.label        = label;
        btn.fontSize     = 14.0f;
        btn.normalColor  = color;
        btn.hoverColor   = hover;
        btn.pressedColor = pressed;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick      = std::move(cb);
        ui.createButton(panel, {0.0f, 0.0f, innerW, 36.0f}, std::move(btn));
    };

    makeButton("Resume",     BTN_GREEN,  BTN_GREEN_HOVER, BTN_GREEN_PRESS,
               [this]() { if (this->m_onResume) { this->m_onResume(); } });
    makeButton("Save Game",  BTN_NORMAL, BTN_HOVER,       BTN_PRESSED,
               [this]() { if (this->m_onSave) { this->m_onSave(); } });
    makeButton("Load Game",  BTN_NORMAL, BTN_HOVER,       BTN_PRESSED,
               [this]() { if (this->m_onLoad) { this->m_onLoad(); } });
    makeButton("Main Menu",  BTN_GREY,   BTN_GREY_HOVER,  BTN_GREY_PRESS,
               [this]() { if (this->m_onMainMenu) { this->m_onMainMenu(); } });
    makeButton("Quit Game",  BTN_RED,    BTN_RED_HOVER,   BTN_RED_PRESS,
               [this]() { if (this->m_onQuit) { this->m_onQuit(); } });

    this->m_isBuilt = true;
    LOG_INFO("Pause menu built");
}

void PauseMenu::destroy(UIManager& ui) {
    if (!this->m_isBuilt) { return; }
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
    this->m_isBuilt = false;
}

void PauseMenu::onResize(UIManager& ui, float width, float height) {
    if (!this->m_isBuilt) { return; }
    // Cheapest path: rebuild on resize.
    auto resume   = this->m_onResume;
    auto save     = this->m_onSave;
    auto load     = this->m_onLoad;
    auto mainMenu = this->m_onMainMenu;
    auto quit     = this->m_onQuit;
    this->destroy(ui);
    this->build(ui, width, height, resume, save, load, mainMenu, quit);
}

} // namespace aoc::ui

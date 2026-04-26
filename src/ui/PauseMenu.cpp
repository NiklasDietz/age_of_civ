/**
 * @file PauseMenu.cpp
 * @brief In-game pause menu implementation. 5 save slots + Resume/Main/Quit.
 */

#include "aoc/ui/PauseMenu.hpp"

#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/MainMenuTheme.hpp"
#include "aoc/core/Log.hpp"

#include <cassert>
#include <string>

namespace aoc::ui {

void PauseMenu::build(UIManager& ui, float screenW, float screenH,
                       std::function<void()> onResume,
                       std::function<void(int)> onSaveSlot,
                       std::function<void(int)> onLoadSlot,
                       std::function<void()> onMainMenu,
                       std::function<void()> onQuit) {
    assert(!this->m_isBuilt);
    this->m_onResume    = std::move(onResume);
    this->m_onSaveSlot  = std::move(onSaveSlot);
    this->m_onLoadSlot  = std::move(onLoadSlot);
    this->m_onMainMenu  = std::move(onMainMenu);
    this->m_onQuit      = std::move(onQuit);
    this->m_lastW       = screenW;
    this->m_lastH       = screenH;

    // Full-screen dim background.
    this->m_rootPanel = ui.createPanel(
        {0.0f, 0.0f, screenW, screenH},
        PanelData{{0.0f, 0.0f, 0.0f, 0.55f}, 0.0f});

    constexpr float PANEL_W = 420.0f;
    constexpr float PANEL_H = 480.0f;
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
        p->childSpacing = 6.0f;
    }
    const float innerW = PANEL_W - 40.0f;

    [[maybe_unused]] WidgetId title = ui.createLabel(
        panel, {0.0f, 0.0f, innerW, 30.0f},
        LabelData{"Paused", GOLDEN_TEXT, 22.0f});

    [[maybe_unused]] WidgetId spacer1 = ui.createPanel(
        panel, {0.0f, 0.0f, innerW, 8.0f},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});

    auto makeBtn = [&](WidgetId parent, const char* label, float w,
                       Color color, Color hover, Color pressed,
                       std::function<void()> cb) {
        ButtonData btn;
        btn.label        = label;
        btn.fontSize     = 13.0f;
        btn.normalColor  = color;
        btn.hoverColor   = hover;
        btn.pressedColor = pressed;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = 4.0f;
        btn.onClick      = std::move(cb);
        [[maybe_unused]] WidgetId id = ui.createButton(parent,
            {0.0f, 0.0f, w, 30.0f}, std::move(btn));
    };

    // Resume (full-width, prominent)
    makeBtn(panel, "Resume", innerW, BTN_GREEN, BTN_GREEN_HOVER, BTN_GREEN_PRESS,
            [this]() { if (this->m_onResume) { this->m_onResume(); } });

    [[maybe_unused]] WidgetId spacer2 = ui.createPanel(
        panel, {0.0f, 0.0f, innerW, 6.0f},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});

    // 5 save slots: Save N + Load N side-by-side per row
    const float slotBtnW = (innerW - 6.0f) * 0.5f;
    for (int slot = 0; slot < 5; ++slot) {
        WidgetId row = ui.createPanel(
            panel, {0.0f, 0.0f, innerW, 32.0f},
            PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
        {
            Widget* w = ui.getWidget(row);
            assert(w != nullptr);
            w->layoutDirection = LayoutDirection::Horizontal;
            w->childSpacing = 6.0f;
        }

        std::string saveLabel = "Save Slot " + std::to_string(slot + 1);
        std::string loadLabel = "Load Slot " + std::to_string(slot + 1);

        ButtonData saveBtn;
        saveBtn.label        = saveLabel;
        saveBtn.fontSize     = 13.0f;
        saveBtn.normalColor  = BTN_NORMAL;
        saveBtn.hoverColor   = BTN_HOVER;
        saveBtn.pressedColor = BTN_PRESSED;
        saveBtn.labelColor   = WHITE_TEXT;
        saveBtn.cornerRadius = 4.0f;
        saveBtn.onClick      = [this, slot]() {
            if (this->m_onSaveSlot) { this->m_onSaveSlot(slot); }
        };
        [[maybe_unused]] WidgetId saveBtnId = ui.createButton(row,
            {0.0f, 0.0f, slotBtnW, 30.0f}, std::move(saveBtn));

        ButtonData loadBtn;
        loadBtn.label        = loadLabel;
        loadBtn.fontSize     = 13.0f;
        loadBtn.normalColor  = BTN_NORMAL;
        loadBtn.hoverColor   = BTN_HOVER;
        loadBtn.pressedColor = BTN_PRESSED;
        loadBtn.labelColor   = WHITE_TEXT;
        loadBtn.cornerRadius = 4.0f;
        loadBtn.onClick      = [this, slot]() {
            if (this->m_onLoadSlot) { this->m_onLoadSlot(slot); }
        };
        [[maybe_unused]] WidgetId loadBtnId = ui.createButton(row,
            {0.0f, 0.0f, slotBtnW, 30.0f}, std::move(loadBtn));
    }

    [[maybe_unused]] WidgetId spacer3 = ui.createPanel(
        panel, {0.0f, 0.0f, innerW, 8.0f},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});

    makeBtn(panel, "Main Menu", innerW, BTN_GREY, BTN_GREY_HOVER, BTN_GREY_PRESS,
            [this]() { if (this->m_onMainMenu) { this->m_onMainMenu(); } });
    makeBtn(panel, "Quit Game", innerW, BTN_RED, BTN_RED_HOVER, BTN_RED_PRESS,
            [this]() { if (this->m_onQuit) { this->m_onQuit(); } });

    this->m_isBuilt = true;
    LOG_INFO("Pause menu built (5 save slots)");
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
    auto resume   = this->m_onResume;
    auto save     = this->m_onSaveSlot;
    auto load     = this->m_onLoadSlot;
    auto mainMenu = this->m_onMainMenu;
    auto quit     = this->m_onQuit;
    this->destroy(ui);
    this->build(ui, width, height, resume, save, load, mainMenu, quit);
}

} // namespace aoc::ui

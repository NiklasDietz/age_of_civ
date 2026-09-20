/**
 * @file PauseMenu.cpp
 * @brief In-game pause menu implementation. 5 save slots + Resume/Main/Quit.
 *
 * Reskinned 2026-04-28 to the parchment/bronze design language. See
 * docs/ui/style_guide.md for tokens and rationale.
 */

#include "aoc/ui/PauseMenu.hpp"

#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/StyleTokens.hpp"
#include "aoc/core/Log.hpp"

#include <cassert>
#include <string>

namespace aoc::ui {

void PauseMenu::build(UIManager& ui, float screenW, float screenH, std::function<void()> onResume,
                      std::function<void(int)> onSaveSlot, std::function<void(int)> onLoadSlot,
                      std::function<void()> onMainMenu, std::function<void()> onQuit) {
    assert(!this->m_isBuilt);
    this->m_onResume   = std::move(onResume);
    this->m_onSaveSlot = std::move(onSaveSlot);
    this->m_onLoadSlot = std::move(onLoadSlot);
    this->m_onMainMenu = std::move(onMainMenu);
    this->m_onQuit     = std::move(onQuit);
    this->m_lastW      = screenW;
    this->m_lastH      = screenH;

    // Full-screen frost dim -- map shows through faintly.
    this->m_rootPanel =
        ui.createPanel({0.0f, 0.0f, screenW, screenH}, PanelData{tokens::SURFACE_FROST_DIM, 0.0f});
    {
        // Frame, rail and panel below are overlapping layers at absolute
        // positions; the default vertical stack laid them out one below the
        // other, pushing Main Menu and Quit past the bottom of the screen.
        Widget* root = ui.getWidget(this->m_rootPanel);
        assert(root != nullptr);
        root->layoutDirection = LayoutDirection::None;
    }

    constexpr float PANEL_W = 440.0f;
    constexpr float PANEL_H = 520.0f;
    const float panelX      = (screenW - PANEL_W) * 0.5f;
    const float panelY      = (screenH - PANEL_H) * 0.5f;

    // --- Outer dark frame (mahogany shadow) -- sits 4 px outside the panel.
    [[maybe_unused]] WidgetId outerFrame = ui.createPanel(
        this->m_rootPanel, {panelX - 4.0f, panelY - 4.0f, PANEL_W + 8.0f, PANEL_H + 8.0f},
        PanelData{tokens::SURFACE_MAHOGANY, tokens::CORNER_PANEL + 2.0f});

    // --- Bronze rail (top edge) ---
    [[maybe_unused]] WidgetId rail =
        ui.createPanel(this->m_rootPanel, {panelX, panelY, PANEL_W, tokens::BORDER_RAIL},
                       PanelData{tokens::BRONZE_BASE, tokens::CORNER_PANEL});

    // --- Main parchment panel ---
    WidgetId panel = ui.createPanel(this->m_rootPanel,
                                    {panelX, panelY + tokens::BORDER_RAIL, PANEL_W, PANEL_H - tokens::BORDER_RAIL},
                                    PanelData{tokens::SURFACE_PARCHMENT, tokens::CORNER_PANEL});
    {
        Widget* p = ui.getWidget(panel);
        assert(p != nullptr);
        p->padding      = {tokens::S5, tokens::S5, tokens::S5, tokens::S5};
        p->childSpacing = tokens::S2;
    }
    const float innerW = PANEL_W - 2 * tokens::S5;

    // --- Title ribbon: deep header text ---
    [[maybe_unused]] WidgetId title =
        ui.createLabel(panel, {0.0f, 0.0f, innerW, 36.0f}, LabelData{"PAUSED", tokens::TEXT_HEADER, tokens::FS_H2});

    // Bronze divider rule under title.
    [[maybe_unused]] WidgetId rule =
        ui.createPanel(panel, {0.0f, 0.0f, innerW, 2.0f}, PanelData{tokens::BRONZE_DARK, 1.0f});

    [[maybe_unused]] WidgetId spacer1 =
        ui.createPanel(panel, {0.0f, 0.0f, innerW, tokens::S2}, PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});

    // --- Button factory: ornate (primary) and standard (secondary) variants.
    auto makeBtn = [&](WidgetId parent, const char* label, float w, float h, Color color,
                       Color hover, Color pressed, Color labelColor, std::function<void()> cb) {
        ButtonData btn;
        btn.label                    = label;
        btn.fontSize                 = tokens::FS_BODY;
        btn.normalColor              = color;
        btn.hoverColor               = hover;
        btn.pressedColor             = pressed;
        btn.labelColor               = labelColor;
        btn.cornerRadius             = tokens::CORNER_BUTTON;
        btn.onClick                  = std::move(cb);
        [[maybe_unused]] WidgetId id = ui.createButton(parent, {0.0f, 0.0f, w, h}, std::move(btn));
    };

    // --- Resume (primary action, bronze + gilt label) ---
    makeBtn(panel, "Resume", innerW, 36.0f, tokens::BRONZE_BASE, tokens::BRONZE_LIGHT, tokens::STATE_PRESSED, tokens::TEXT_GILT,
            [this]() {
                if (this->m_onResume) {
                    this->m_onResume();
                }
            });

    [[maybe_unused]] WidgetId spacer2 =
        ui.createPanel(panel, {0.0f, 0.0f, innerW, tokens::S2}, PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});

    // --- 5 save slots: Save N + Load N per row ---
    const float slotBtnW = (innerW - tokens::S2) * 0.5f;
    for (int slot = 0; slot < 5; ++slot) {
        WidgetId row = ui.createPanel(panel, {0.0f, 0.0f, innerW, 32.0f},
                                      PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
        {
            Widget* w = ui.getWidget(row);
            assert(w != nullptr);
            w->layoutDirection = LayoutDirection::Horizontal;
            w->childSpacing    = tokens::S2;
        }

        std::string saveLabel = "Save " + std::to_string(slot + 1);
        std::string loadLabel = "Load " + std::to_string(slot + 1);

        ButtonData saveBtn;
        saveBtn.label        = saveLabel;
        saveBtn.fontSize     = tokens::FS_SMALL;
        saveBtn.normalColor  = tokens::SURFACE_PARCHMENT_DIM;
        saveBtn.hoverColor   = tokens::BRONZE_LIGHT;
        saveBtn.pressedColor = tokens::BRONZE_DARK;
        saveBtn.labelColor   = tokens::TEXT_INK;
        saveBtn.cornerRadius = tokens::CORNER_BUTTON;
        saveBtn.onClick      = [this, slot]() {
            if (this->m_onSaveSlot) {
                this->m_onSaveSlot(slot);
            }
        };
        [[maybe_unused]] WidgetId saveBtnId =
            ui.createButton(row, {0.0f, 0.0f, slotBtnW, 28.0f}, std::move(saveBtn));

        ButtonData loadBtn;
        loadBtn.label        = loadLabel;
        loadBtn.fontSize     = tokens::FS_SMALL;
        loadBtn.normalColor  = tokens::SURFACE_PARCHMENT_DIM;
        loadBtn.hoverColor   = tokens::BRONZE_LIGHT;
        loadBtn.pressedColor = tokens::BRONZE_DARK;
        loadBtn.labelColor   = tokens::TEXT_INK;
        loadBtn.cornerRadius = tokens::CORNER_BUTTON;
        loadBtn.onClick      = [this, slot]() {
            if (this->m_onLoadSlot) {
                this->m_onLoadSlot(slot);
            }
        };
        [[maybe_unused]] WidgetId loadBtnId =
            ui.createButton(row, {0.0f, 0.0f, slotBtnW, 28.0f}, std::move(loadBtn));
    }

    [[maybe_unused]] WidgetId spacer3 =
        ui.createPanel(panel, {0.0f, 0.0f, innerW, tokens::S3}, PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});

    // --- Main Menu (secondary) ---
    makeBtn(panel, "Main Menu", innerW, 32.0f, tokens::SURFACE_PARCHMENT_DIM, tokens::BRONZE_LIGHT, tokens::BRONZE_DARK,
            tokens::TEXT_INK, [this]() {
                if (this->m_onMainMenu) {
                    this->m_onMainMenu();
                }
            });

    // --- Quit (danger) ---
    makeBtn(panel, "Quit Game", innerW, 32.0f, tokens::STATE_DANGER, {0.767f, 0.272f, 0.197f, 1.0f},
            {0.511f, 0.182f, 0.131f, 1.0f}, tokens::TEXT_PARCHMENT, [this]() {
                if (this->m_onQuit) {
                    this->m_onQuit();
                }
            });

    this->m_isBuilt = true;
    LOG_INFO("Pause menu built (parchment skin, 5 save slots)");
}

void PauseMenu::destroy(UIManager& ui) {
    if (!this->m_isBuilt) {
        return;
    }
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
    this->m_isBuilt = false;
}

void PauseMenu::onResize(UIManager& ui, float width, float height) {
    if (!this->m_isBuilt) {
        return;
    }
    // Tear down + rebuild on resize so the centered panel re-centers.
    auto onResume   = this->m_onResume;
    auto onSaveSlot = this->m_onSaveSlot;
    auto onLoadSlot = this->m_onLoadSlot;
    auto onMainMenu = this->m_onMainMenu;
    auto onQuit     = this->m_onQuit;
    this->destroy(ui);
    this->build(ui, width, height, std::move(onResume), std::move(onSaveSlot),
                std::move(onLoadSlot), std::move(onMainMenu), std::move(onQuit));
}

} // namespace aoc::ui

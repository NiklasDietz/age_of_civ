/**
 * @file LoadGameMenu.cpp
 * @brief Main-menu save-slot picker, parchment/bronze skin matching PauseMenu.
 */

#include "aoc/ui/LoadGameMenu.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/ui/StyleTokens.hpp"
#include "aoc/ui/UIManager.hpp"

#include <cassert>
#include <string>
#include <utility>

namespace aoc::ui {

namespace {

constexpr Color NO_FILL = {0.0f, 0.0f, 0.0f, 0.0f};

ButtonData makeParchmentButton(std::string label, bool enabled, std::function<void()> onClick) {
    ButtonData btn;
    btn.label        = std::move(label);
    btn.fontSize     = tokens::FS_BODY;
    btn.normalColor  = tokens::SURFACE_PARCHMENT_DIM;
    btn.hoverColor   = tokens::BRONZE_LIGHT;
    btn.pressedColor = tokens::BRONZE_DARK;
    btn.labelColor   = enabled ? tokens::TEXT_INK : tokens::TEXT_DISABLED;
    btn.cornerRadius = tokens::CORNER_BUTTON;
    btn.disabled     = !enabled;
    btn.onClick      = std::move(onClick);
    return btn;
}

} // namespace

void LoadGameMenu::build(UIManager& ui, float screenW, float screenH, const SlotFlags& occupied,
                         std::function<void(int)> onLoadSlot, std::function<void()> onBack) {
    assert(!this->m_isBuilt);
    this->m_occupied   = occupied;
    this->m_onLoadSlot = std::move(onLoadSlot);
    this->m_onBack     = std::move(onBack);

    this->m_rootPanel =
        ui.createPanel({0.0f, 0.0f, screenW, screenH}, PanelData{tokens::SURFACE_FROST_DIM, 0.0f});
    {
        // Frame, rail and panel are overlapping layers at absolute positions;
        // the default vertical stack would lay them out one below the other.
        Widget* root = ui.getWidget(this->m_rootPanel);
        assert(root != nullptr);
        root->layoutDirection = LayoutDirection::None;
    }

    // Children in creation order; the panel height is derived from them so a
    // slot-count change cannot leave the Back button hanging off the bottom.
    constexpr float TITLE_H  = 36.0f;
    constexpr float RULE_H   = 2.0f;
    constexpr float SLOT_H   = 36.0f;
    constexpr float STATUS_H = 24.0f;
    constexpr float BACK_H   = 32.0f;
    constexpr int CHILDREN = 6 + aoc::save::SAVE_SLOT_COUNT; // title, rule, 2 spacers, status, back
    constexpr float PANEL_W = 440.0f;
    constexpr float BODY_H  = TITLE_H + RULE_H + tokens::S2 + SLOT_H * aoc::save::SAVE_SLOT_COUNT +
                              tokens::S2 + STATUS_H + BACK_H + tokens::S2 * (CHILDREN - 1) +
                              2.0f * tokens::S5;
    constexpr float PANEL_H = BODY_H + tokens::BORDER_RAIL;
    const float panelX      = (screenW - PANEL_W) * 0.5f;
    const float panelY      = (screenH - PANEL_H) * 0.5f;

    [[maybe_unused]] WidgetId outerFrame = ui.createPanel(
        this->m_rootPanel, {panelX - 4.0f, panelY - 4.0f, PANEL_W + 8.0f, PANEL_H + 8.0f},
        PanelData{tokens::SURFACE_MAHOGANY, tokens::CORNER_PANEL + 2.0f});
    [[maybe_unused]] WidgetId rail =
        ui.createPanel(this->m_rootPanel, {panelX, panelY, PANEL_W, tokens::BORDER_RAIL},
                       PanelData{tokens::BRONZE_BASE, tokens::CORNER_PANEL});
    WidgetId panel = ui.createPanel(
        this->m_rootPanel,
        {panelX, panelY + tokens::BORDER_RAIL, PANEL_W, PANEL_H - tokens::BORDER_RAIL},
        PanelData{tokens::SURFACE_PARCHMENT, tokens::CORNER_PANEL});
    {
        Widget* p = ui.getWidget(panel);
        assert(p != nullptr);
        p->padding      = {tokens::S5, tokens::S5, tokens::S5, tokens::S5};
        p->childSpacing = tokens::S2;
    }
    const float innerW = PANEL_W - 2 * tokens::S5;

    [[maybe_unused]] WidgetId title =
        ui.createLabel(panel, {0.0f, 0.0f, innerW, TITLE_H},
                       LabelData{"LOAD GAME", tokens::TEXT_HEADER, tokens::FS_H2});
    [[maybe_unused]] WidgetId rule =
        ui.createPanel(panel, {0.0f, 0.0f, innerW, RULE_H}, PanelData{tokens::BRONZE_DARK, 1.0f});
    [[maybe_unused]] WidgetId spacer1 =
        ui.createPanel(panel, {0.0f, 0.0f, innerW, tokens::S2}, PanelData{NO_FILL, 0.0f});

    for (int slot = 0; slot < aoc::save::SAVE_SLOT_COUNT; ++slot) {
        const bool present = this->m_occupied[static_cast<std::size_t>(slot)];
        std::string label  = "Slot " + std::to_string(slot + 1) + (present ? "" : "  (empty)");
        [[maybe_unused]] WidgetId id =
            ui.createButton(panel, {0.0f, 0.0f, innerW, SLOT_H},
                            makeParchmentButton(std::move(label), present, [this, slot]() {
                                if (this->m_onLoadSlot) {
                                    this->m_onLoadSlot(slot);
                                }
                            }));
    }

    [[maybe_unused]] WidgetId spacer2 =
        ui.createPanel(panel, {0.0f, 0.0f, innerW, tokens::S2}, PanelData{NO_FILL, 0.0f});
    this->m_statusLabel =
        ui.createLabel(panel, {0.0f, 0.0f, innerW, STATUS_H},
                       LabelData{this->m_status, tokens::STATE_DANGER, tokens::FS_SMALL});

    [[maybe_unused]] WidgetId backId = ui.createButton(panel, {0.0f, 0.0f, innerW, BACK_H},
                                                       makeParchmentButton("Back", true, [this]() {
                                                           if (this->m_onBack) {
                                                               this->m_onBack();
                                                           }
                                                       }));

    this->m_isBuilt = true;
    LOG_INFO("Load game menu built (%d slots)", aoc::save::SAVE_SLOT_COUNT);
}

void LoadGameMenu::destroy(UIManager& ui) {
    if (!this->m_isBuilt) {
        return;
    }
    ui.removeWidget(this->m_rootPanel);
    this->m_rootPanel   = INVALID_WIDGET;
    this->m_statusLabel = INVALID_WIDGET;
    this->m_status.clear();
    this->m_onLoadSlot = nullptr;
    this->m_onBack     = nullptr;
    this->m_isBuilt    = false;
}

void LoadGameMenu::setStatus(UIManager& ui, const std::string& text) {
    this->m_status = text;
    if (this->m_statusLabel != INVALID_WIDGET) {
        ui.setLabelText(this->m_statusLabel, text);
    }
}

void LoadGameMenu::onResize(UIManager& ui, float width, float height) {
    if (!this->m_isBuilt) {
        return;
    }
    // Tear down and rebuild so the centred panel re-centres; keep the status text.
    std::function<void(int)> onLoadSlot = this->m_onLoadSlot;
    std::function<void()> onBack        = this->m_onBack;
    const SlotFlags occupied            = this->m_occupied;
    const std::string status            = this->m_status;
    this->destroy(ui);
    this->m_status = status;
    this->build(ui, width, height, occupied, std::move(onLoadSlot), std::move(onBack));
}

} // namespace aoc::ui

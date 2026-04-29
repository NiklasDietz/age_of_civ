/**
 * @file LoadingScreen.cpp
 */

#include "aoc/ui/LoadingScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Theme.hpp"
#include "aoc/ui/StyleTokens.hpp"

#include <utility>

namespace aoc::ui {

void LoadingScreen::open(UIManager& ui, const std::string& title) {
    if (this->m_isOpen) { return; }
    this->m_title = title;

    Theme& t = theme();
    const float sw = t.viewportW;
    const float sh = t.viewportH;

    this->m_rootPanel = ui.createPanel(
        {0.0f, 0.0f, sw, sh},
        PanelData{tokens::SURFACE_FROST_DIM, 0.0f});

    const float panelW = t.scaled(480.0f);
    const float panelH = t.scaled(140.0f);

    WidgetId panel = ui.createPanel(
        this->m_rootPanel,
        {(sw - panelW) * 0.5f, (sh - panelH) * 0.5f, panelW, panelH},
        PanelData{PANEL_BG, t.cornerRadius()});
    {
        Widget* p = ui.getWidget(panel);
        if (p != nullptr) {
            p->padding = {t.panelPadding(), t.panelPadding(),
                          t.panelPadding(), t.panelPadding()};
            p->childSpacing = t.scaled(10.0f);
        }
    }

    this->m_titleLabel = ui.createLabel(
        panel, {0.0f, 0.0f, panelW - t.scaled(30.0f), t.scaled(24.0f)},
        LabelData{this->m_title, GOLDEN_TEXT, t.fontLarge()});

    ProgressBarData pb;
    pb.fillFraction = 0.0f;
    pb.cornerRadius = t.scaled(3.0f);
    this->m_progressBar = ui.createProgressBar(
        panel, {0.0f, 0.0f, panelW - t.scaled(30.0f), t.scaled(14.0f)},
        std::move(pb));

    this->m_statusLabel = ui.createLabel(
        panel, {0.0f, 0.0f, panelW - t.scaled(30.0f), t.scaled(16.0f)},
        LabelData{"Starting...", WHITE_TEXT, t.fontSmall()});

    ui.layout();
    this->m_isOpen = true;
}

void LoadingScreen::close(UIManager& ui) {
    if (!this->m_isOpen) { return; }
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
    }
    this->m_rootPanel    = INVALID_WIDGET;
    this->m_progressBar  = INVALID_WIDGET;
    this->m_statusLabel  = INVALID_WIDGET;
    this->m_titleLabel   = INVALID_WIDGET;
    this->m_isOpen = false;
}

void LoadingScreen::onResize(UIManager& ui, float /*width*/, float /*height*/) {
    if (!this->m_isOpen) { return; }
    const std::string title = this->m_title;
    this->close(ui);
    this->open(ui, title);
}

void LoadingScreen::setStatus(const std::string& status) {
    this->m_status = status;
}

void LoadingScreen::tick(UIManager& ui) {
    if (!this->m_isOpen) { return; }
    Widget* bar = ui.getWidget(this->m_progressBar);
    if (bar != nullptr) {
        if (ProgressBarData* pb = std::get_if<ProgressBarData>(&bar->data)) {
            pb->fillFraction = this->m_progress.load(std::memory_order_relaxed);
        }
    }
    ui.setLabelText(this->m_statusLabel, this->m_status);
}

} // namespace aoc::ui

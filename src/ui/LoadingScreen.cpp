/**
 * @file LoadingScreen.cpp
 */

#include "aoc/ui/LoadingScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Theme.hpp"
#include "aoc/ui/StyleTokens.hpp"

#include <chrono>
#include <utility>

namespace aoc::ui {

// Kept deliberately concrete — a tip that restates the UI ("click the button
// to build") teaches nothing. Each of these names a real mechanic the sim
// actually models.
const std::array<const char*, 14> LoadingScreen::TIPS = {
    "Cities founded on a river or coast gain extra trade and fresh water.",
    "Keep loyalty above 30 — a city that stays disloyal will revolt and can cost you the game.",
    "Citizens must be assigned to tiles. An unworked tile yields nothing.",
    "Eureka bonuses cut a technology's cost. Meeting one early pays off all game.",
    "Trade routes need a road or river path — scout the terrain before committing a merchant.",
    "Hills give defenders a real combat edge. Fight uphill only when you outnumber them.",
    "Buildings have capacity tiers. Upgrading an existing one often beats building a new one.",
    "Ranged units strike without retaliation. Screen them with melee to keep them alive.",
    "Specialists in a building out-produce a worked tile once the building is upgraded.",
    "Watch your treasury: negative gold forces automatic unit disbandment.",
    "Diplomatic favour is spendable. Bank it before you need a vote.",
    "Founding a religion early gives you first pick of its beliefs.",
    "Roads cut movement cost — connect cities before a war, not during one.",
    "Every leader has an agenda. Matching it turns an AI rival into a lasting ally.",
};

namespace {

/// Monotonic seconds. Deliberately not GLFW so this stays linkable in the
/// headless configuration, which excludes the windowing layer.
double nowSeconds() {
    const std::chrono::steady_clock::duration d =
        std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<double>(d).count();
}

} // namespace

std::string LoadingScreen::currentTipText() const {
    return std::string("Tip: ") + TIPS[this->m_tipIndex % TIPS.size()];
}

void LoadingScreen::open(UIManager& ui, const std::string& title) {
    if (this->m_isOpen) {
        return;
    }
    this->m_title = title;

    Theme& t       = theme();
    const float sw = t.viewportW;
    const float sh = t.viewportH;

    this->m_rootPanel =
        ui.createPanel({0.0f, 0.0f, sw, sh}, PanelData{tokens::SURFACE_FROST_DIM, 0.0f});

    const float panelW = t.scaled(620.0f);
    const float panelH = t.scaled(210.0f); // room for the wrapped tip line

    WidgetId panel = ui.createPanel(this->m_rootPanel,
                                    {(sw - panelW) * 0.5f, (sh - panelH) * 0.5f, panelW, panelH},
                                    PanelData{PANEL_BG, t.cornerRadius()});
    {
        Widget* p = ui.getWidget(panel);
        if (p != nullptr) {
            p->padding = {t.panelPadding(), t.panelPadding(), t.panelPadding(), t.panelPadding()};
            p->childSpacing = t.scaled(10.0f);
        }
    }

    this->m_titleLabel =
        ui.createLabel(panel, {0.0f, 0.0f, panelW - t.scaled(30.0f), t.scaled(24.0f)},
                       LabelData{this->m_title, GOLDEN_TEXT, t.fontLarge()});

    ProgressBarData pb;
    pb.fillFraction     = 0.0f;
    pb.cornerRadius     = t.scaled(3.0f);
    this->m_progressBar = ui.createProgressBar(
        panel, {0.0f, 0.0f, panelW - t.scaled(30.0f), t.scaled(14.0f)}, std::move(pb));

    this->m_statusLabel =
        ui.createLabel(panel, {0.0f, 0.0f, panelW - t.scaled(30.0f), t.scaled(20.0f)},
                       LabelData{"Starting...", WHITE_TEXT, t.fontSmall()});

    // Tip line, visually subordinate to the status so the phase label stays
    // the primary read.
    this->m_tipLabel =
        ui.createLabel(panel, {0.0f, 0.0f, panelW - t.scaled(30.0f), t.scaled(44.0f)},
                       LabelData{this->currentTipText(), tokens::TEXT_HEADER, t.fontSmall()});

    this->m_lastTipSwapSec = 0.0; // tick() seeds this on its first call

    ui.layout();
    this->m_isOpen = true;
}

void LoadingScreen::close(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
    }
    this->m_rootPanel   = INVALID_WIDGET;
    this->m_progressBar = INVALID_WIDGET;
    this->m_statusLabel = INVALID_WIDGET;
    this->m_titleLabel  = INVALID_WIDGET;
    this->m_tipLabel    = INVALID_WIDGET;
    this->m_isOpen      = false;
}

void LoadingScreen::onResize(UIManager& ui, float /*width*/, float /*height*/) {
    if (!this->m_isOpen) {
        return;
    }
    const std::string title = this->m_title;
    this->close(ui);
    this->open(ui, title);
}

void LoadingScreen::setStatus(const std::string& status) {
    this->m_status = status;
}

void LoadingScreen::tick(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    Widget* bar = ui.getWidget(this->m_progressBar);
    if (bar != nullptr) {
        if (ProgressBarData* pb = std::get_if<ProgressBarData>(&bar->data)) {
            pb->fillFraction = this->m_progress.load(std::memory_order_relaxed);
        }
    }
    ui.setLabelText(this->m_statusLabel, this->m_status);

    // Rotate the tip on a wall clock rather than per phase: a single
    // generation phase can run for many seconds, and a frozen tip next to a
    // frozen bar reads as a hang.
    const double now = nowSeconds();
    if (this->m_lastTipSwapSec == 0.0) {
        this->m_lastTipSwapSec = now;
    } else if (now - this->m_lastTipSwapSec >= TIP_INTERVAL_SEC) {
        ++this->m_tipIndex;
        this->m_lastTipSwapSec = now;
        ui.setLabelText(this->m_tipLabel, this->currentTipText());
    }
}

} // namespace aoc::ui

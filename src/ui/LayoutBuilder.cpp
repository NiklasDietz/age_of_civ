/**
 * @file LayoutBuilder.cpp
 */

#include "aoc/ui/LayoutBuilder.hpp"
#include "aoc/ui/UIManager.hpp"

#include <cassert>
#include <cstdio>
#include <utility>
#include <vector>

namespace aoc::ui {

WidgetId buildConfirmDialog(UIManager& ui, const std::string& prompt,
                            std::function<void()> onYes,
                            std::function<void()> onNo,
                            const std::string& yesLabel,
                            const std::string& noLabel) {
    Theme& t = theme();

    const float screenW = t.viewportW;
    const float screenH = t.viewportH;

    // Fullscreen dim overlay anchors the dialog and captures clicks so
    // the map underneath stays inert.
    WidgetId overlay = ui.createPanel(
        {0.0f, 0.0f, screenW, screenH},
        PanelData{{0.0f, 0.0f, 0.0f, 0.5f}, 0.0f});

    const float dlgW = t.dialogW();
    const float dlgH = t.scaled(160.0f);

    WidgetId dlg = ui.createPanel(
        overlay,
        {(screenW - dlgW) * 0.5f, (screenH - dlgH) * 0.5f, dlgW, dlgH},
        PanelData{PANEL_BG, t.cornerRadius()});
    {
        Widget* dp = ui.getWidget(dlg);
        if (dp != nullptr) {
            dp->padding = {t.panelPadding(), t.panelPadding(),
                           t.panelPadding(), t.panelPadding()};
            dp->childSpacing = t.scaled(12.0f);
        }
    }

    (void)ui.createLabel(
        dlg, {0.0f, 0.0f, dlgW - t.scaled(30.0f), t.scaled(24.0f)},
        LabelData{prompt, {1.0f, 0.9f, 0.6f, 1.0f}, t.fontMedium()});

    WidgetId row = ui.createPanel(
        dlg, {0.0f, 0.0f, dlgW - t.scaled(30.0f), t.buttonH()},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
    {
        Widget* r = ui.getWidget(row);
        if (r != nullptr) {
            r->layoutDirection = LayoutDirection::Horizontal;
            r->childSpacing = t.scaled(10.0f);
        }
    }

    const float btnW = (dlgW - t.scaled(30.0f) - t.scaled(10.0f)) * 0.5f;

    // Both handlers close the overlay before firing the callback so the
    // user doesn't have to juggle cleanup from their handler.
    auto closeAnd = [&ui, overlay](std::function<void()> inner) {
        return [&ui, overlay, inner = std::move(inner)]() {
            ui.removeWidget(overlay);
            if (inner) { inner(); }
        };
    };

    {
        ButtonData btn;
        btn.label        = yesLabel;
        btn.fontSize     = t.fontMedium();
        btn.normalColor  = BTN_GREEN;
        btn.hoverColor   = BTN_GREEN_HOVER;
        btn.pressedColor = BTN_GREEN_PRESS;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = t.scaled(4.0f);
        btn.onClick      = closeAnd(std::move(onYes));
        (void)ui.createButton(row, {0.0f, 0.0f, btnW, t.buttonH()},
                              std::move(btn));
    }
    {
        ButtonData btn;
        btn.label        = noLabel;
        btn.fontSize     = t.fontMedium();
        btn.normalColor  = BTN_GREY;
        btn.hoverColor   = BTN_GREY_HOVER;
        btn.pressedColor = BTN_GREY_PRESS;
        btn.labelColor   = WHITE_TEXT;
        btn.cornerRadius = t.scaled(4.0f);
        btn.onClick      = closeAnd(std::move(onNo));
        (void)ui.createButton(row, {0.0f, 0.0f, btnW, t.buttonH()},
                              std::move(btn));
    }

    ui.layout();
    return overlay;
}

WidgetId buildContextMenu(UIManager& ui, float x, float y,
                          const std::vector<ContextMenuItem>& items) {
    Theme& t = theme();
    const float itemH = t.buttonH();
    const float menuW = t.scaled(180.0f);
    const float menuH = static_cast<float>(items.size()) * itemH + t.scaled(8.0f);

    WidgetId root = ui.createPanel(
        {x, y, menuW, menuH},
        PanelData{PANEL_BG, t.scaled(4.0f)});
    {
        Widget* w = ui.getWidget(root);
        if (w != nullptr) {
            w->padding = {t.scaled(4.0f), t.scaled(4.0f),
                          t.scaled(4.0f), t.scaled(4.0f)};
            w->childSpacing = t.scaled(2.0f);
        }
    }

    for (const ContextMenuItem& item : items) {
        ButtonData btn;
        btn.label        = item.label;
        btn.fontSize     = t.fontMedium();
        btn.normalColor  = item.enabled ? BTN_GREY : Color{0.15f, 0.15f, 0.18f, 0.9f};
        btn.hoverColor   = item.enabled ? BTN_GREY_HOVER : btn.normalColor;
        btn.pressedColor = item.enabled ? BTN_GREY_PRESS : btn.normalColor;
        btn.labelColor   = item.enabled ? WHITE_TEXT : Color{0.5f, 0.5f, 0.5f, 1.0f};
        btn.shortcut     = item.shortcut;
        if (item.enabled) {
            btn.onClick = [&ui, root, cb = item.onClick]() {
                if (cb) { cb(); }
                ui.removeWidget(root);
            };
        }
        (void)ui.createButton(root, {0.0f, 0.0f, menuW - t.scaled(8.0f), itemH},
                              std::move(btn));
    }
    ui.layout();
    return root;
}

WidgetId buildFrameTimeHUD(UIManager& ui,
                            const UIManager::FrameTimings& timings) {
    Theme& t = theme();
    const float w = t.scaled(220.0f);
    const float h = t.scaled(82.0f);

    WidgetId root = ui.createPanel(
        {0.0f, 0.0f, w, h},
        PanelData{{0.0f, 0.0f, 0.0f, 0.7f}, t.scaled(3.0f)});
    {
        Widget* p = ui.getWidget(root);
        if (p != nullptr) {
            p->padding = {t.scaled(6.0f), t.scaled(6.0f),
                          t.scaled(6.0f), t.scaled(6.0f)};
            p->childSpacing = t.scaled(2.0f);
            p->anchor = Anchor::TopRight;
            p->marginRight = t.scaled(10.0f);
        }
    }
    auto fmt = [](const char* k, float ms) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%-10s %5.2f ms", k, static_cast<double>(ms));
        return std::string(buf);
    };
    (void)ui.createLabel(root, {0.0f, 0.0f, w, t.scaled(12.0f)},
                          LabelData{fmt("layout", timings.layoutMs),
                                    {0.85f, 0.85f, 0.85f, 1.0f},
                                    t.fontSmall()});
    (void)ui.createLabel(root, {0.0f, 0.0f, w, t.scaled(12.0f)},
                          LabelData{fmt("render", timings.renderMs),
                                    {0.85f, 0.85f, 0.85f, 1.0f},
                                    t.fontSmall()});
    (void)ui.createLabel(root, {0.0f, 0.0f, w, t.scaled(12.0f)},
                          LabelData{fmt("input", timings.inputMs),
                                    {0.85f, 0.85f, 0.85f, 1.0f},
                                    t.fontSmall()});
    (void)ui.createLabel(root, {0.0f, 0.0f, w, t.scaled(12.0f)},
                          LabelData{fmt("binding", timings.bindingMs),
                                    {0.85f, 0.85f, 0.85f, 1.0f},
                                    t.fontSmall()});
    (void)ui.createLabel(root, {0.0f, 0.0f, w, t.scaled(12.0f)},
                          LabelData{fmt("TOTAL", timings.total()),
                                    {1.0f, 0.85f, 0.4f, 1.0f},
                                    t.fontSmall()});
    ui.layout();
    return root;
}

ImmediateBlock::ImmediateBlock(UIManager& ui, WidgetId parent, Rect bounds)
    : m_ui(ui)
    , m_root((parent == INVALID_WIDGET)
             ? ui.createPanel(bounds, PanelData{PANEL_BG, theme().cornerRadius()})
             : ui.createPanel(parent, bounds, PanelData{PANEL_BG, theme().cornerRadius()})) {
    Widget* w = ui.getWidget(this->m_root);
    if (w != nullptr) {
        Theme& t = theme();
        w->padding = {t.scaled(6.0f), t.scaled(6.0f),
                      t.scaled(6.0f), t.scaled(6.0f)};
        w->childSpacing = t.childSpacing();
    }
}

ImmediateBlock::~ImmediateBlock() {
    // Tear-down on scope exit so callers don't leak debug widgets.
    if (this->m_root != INVALID_WIDGET) {
        this->m_ui.removeWidget(this->m_root);
    }
}

void ImmediateBlock::label(const std::string& text) {
    Theme& t = theme();
    (void)this->m_ui.createLabel(
        this->m_root, {0.0f, 0.0f, 0.0f, t.scaled(14.0f)},
        LabelData{text, WHITE_TEXT, t.fontMedium()});
}

void ImmediateBlock::button(const std::string& text, std::function<void()> onClick) {
    Theme& t = theme();
    ButtonData btn;
    btn.label = text;
    btn.fontSize = t.fontMedium();
    btn.normalColor = BTN_GREY;
    btn.hoverColor  = BTN_GREY_HOVER;
    btn.pressedColor = BTN_GREY_PRESS;
    btn.labelColor = WHITE_TEXT;
    btn.cornerRadius = t.scaled(3.0f);
    btn.onClick = std::move(onClick);
    (void)this->m_ui.createButton(
        this->m_root, {0.0f, 0.0f, 0.0f, t.buttonH()},
        std::move(btn));
}

void ImmediateBlock::separator() {
    Theme& t = theme();
    (void)this->m_ui.createPanel(
        this->m_root, {0.0f, 0.0f, 0.0f, t.scaled(1.0f)},
        PanelData{{0.4f, 0.4f, 0.4f, 0.5f}, 0.0f});
}

} // namespace aoc::ui

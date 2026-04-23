#pragma once

/**
 * @file LayoutBuilder.hpp
 * @brief Fluent builders that cut widget-creation boilerplate.
 *
 * Before: every row of a screen repeated ~12 lines of
 * `createPanel({0,0,W,H}, {...})` + `getWidget()` + `layoutDirection =
 * Horizontal` + children. Same layout copy-pasted across 11 screens.
 *
 * After: `ui.row(parent).spacing(6).label("X").flex(1).end();`
 *
 * Builders return themselves so chaining is free. They commit to the
 * UIManager on construction — the returned ID is available via `id()`
 * for when a screen needs a handle to refresh, bind, or query.
 */

#include "aoc/ui/Widget.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/Theme.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace aoc::ui {

/// Thin builder around `createPanel`. Configures the result via chained
/// setters that return `*this`. `end()` is a no-op terminator that
/// reads nicely at the end of a chain.
class PanelBuilder {
public:
    PanelBuilder(UIManager& ui, WidgetId parent, Rect bounds, PanelData data)
        : m_ui(ui)
        , m_id((parent == INVALID_WIDGET)
               ? ui.createPanel(bounds, std::move(data))
               : ui.createPanel(parent, bounds, std::move(data))) {}

    PanelBuilder& padding(float all) {
        Widget* w = this->m_ui.getWidget(this->m_id);
        if (w != nullptr) { w->padding = {all, all, all, all}; }
        return *this;
    }
    PanelBuilder& padding(float h, float v) {
        Widget* w = this->m_ui.getWidget(this->m_id);
        if (w != nullptr) { w->padding = {h, v, h, v}; }
        return *this;
    }
    PanelBuilder& spacing(float s) {
        Widget* w = this->m_ui.getWidget(this->m_id);
        if (w != nullptr) { w->childSpacing = s; }
        return *this;
    }
    PanelBuilder& direction(LayoutDirection dir) {
        Widget* w = this->m_ui.getWidget(this->m_id);
        if (w != nullptr) { w->layoutDirection = dir; }
        return *this;
    }
    PanelBuilder& horizontal() { return this->direction(LayoutDirection::Horizontal); }
    PanelBuilder& vertical()   { return this->direction(LayoutDirection::Vertical); }
    PanelBuilder& anchor(Anchor a) {
        Widget* w = this->m_ui.getWidget(this->m_id);
        if (w != nullptr) { w->anchor = a; }
        return *this;
    }
    PanelBuilder& margin(float right, float bottom) {
        Widget* w = this->m_ui.getWidget(this->m_id);
        if (w != nullptr) {
            w->marginRight = right;
            w->marginBottom = bottom;
        }
        return *this;
    }
    PanelBuilder& flex(float weight) {
        Widget* w = this->m_ui.getWidget(this->m_id);
        if (w != nullptr) { w->flex = weight; }
        return *this;
    }
    PanelBuilder& grid(int32_t cols) {
        Widget* w = this->m_ui.getWidget(this->m_id);
        if (w != nullptr) { w->gridColumns = cols; }
        return *this;
    }
    PanelBuilder& tooltip(std::string text) {
        this->m_ui.setWidgetTooltip(this->m_id, std::move(text));
        return *this;
    }

    [[nodiscard]] WidgetId id() const { return this->m_id; }
    [[nodiscard]] WidgetId end() const { return this->m_id; }

private:
    UIManager& m_ui;
    WidgetId   m_id;
};

/// Factory helpers on `UIManager`-like surface. `Layout::row` starts a
/// horizontal container with theme defaults. `Layout::col` is the
/// vertical variant.
namespace Layout {

inline PanelBuilder row(UIManager& ui, WidgetId parent = INVALID_WIDGET) {
    Theme& t = theme();
    PanelBuilder b(ui, parent, {0.0f, 0.0f, 0.0f, t.buttonH()},
                   PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
    return b.horizontal().spacing(t.scaled(4.0f));
}

inline PanelBuilder col(UIManager& ui, WidgetId parent = INVALID_WIDGET) {
    Theme& t = theme();
    PanelBuilder b(ui, parent, {0.0f, 0.0f, 0.0f, 0.0f},
                   PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
    return b.vertical().spacing(t.childSpacing());
}

/// Standard panel with theme's neutral background + corner radius.
inline PanelBuilder panel(UIManager& ui, WidgetId parent, Rect bounds) {
    Theme& t = theme();
    PanelBuilder b(ui, parent, bounds,
                   PanelData{PANEL_BG, t.cornerRadius()});
    return b.padding(t.panelPadding()).spacing(t.childSpacing());
}

/// Quick label. Defaults to theme's medium font, white text.
inline WidgetId label(UIManager& ui, WidgetId parent, std::string text) {
    Theme& t = theme();
    return ui.createLabel(parent, {0.0f, 0.0f, 0.0f, t.scaled(16.0f)},
                          LabelData{std::move(text), WHITE_TEXT, t.fontMedium()});
}

/// Quick button with onClick handler. Default styling now includes a
/// subtle gradient + border so Layout::button callsites inherit the
/// richer chrome without passing extra fields.
inline WidgetId button(UIManager& ui, WidgetId parent, std::string label,
                        std::function<void()> onClick) {
    Theme& t = theme();
    ButtonData btn;
    btn.label        = std::move(label);
    btn.fontSize     = t.fontMedium();
    btn.normalColor     = {0.28f, 0.30f, 0.38f, 0.95f};
    btn.hoverColor      = {0.40f, 0.43f, 0.52f, 0.95f};
    btn.pressedColor    = {0.18f, 0.20f, 0.26f, 0.95f};
    btn.gradientBottom  = {0.14f, 0.16f, 0.22f, 0.95f};
    btn.borderColor     = {0.85f, 0.72f, 0.30f, 0.35f};
    btn.borderWidth     = 1.0f;
    btn.labelColor      = WHITE_TEXT;
    btn.cornerRadius    = t.scaled(3.0f);
    btn.onClick         = std::move(onClick);
    return ui.createButton(parent, {0.0f, 0.0f, 0.0f, t.buttonH()},
                           std::move(btn));
}

} // namespace Layout

// -------------------------------------------------------------------------
// Confirmation dialog helper
// -------------------------------------------------------------------------

/// Build a centred "Yes / No" confirmation overlay in one call. Returns
/// the root widget ID so the caller can remove it from both button
/// handlers (both Yes and No should close the dialog). The first arg
/// is the prompt shown above the button row.
///
/// Typical pattern:
/// ```
/// WidgetId dlg = 0;
/// dlg = buildConfirmDialog(ui, "Save and quit?",
///     [&, dlgRef = &dlg]() { ui.removeWidget(*dlgRef); saveAndQuit(); },
///     [&, dlgRef = &dlg]() { ui.removeWidget(*dlgRef); });
/// ```
[[nodiscard]] WidgetId buildConfirmDialog(UIManager& ui, const std::string& prompt,
                                          std::function<void()> onYes,
                                          std::function<void()> onNo,
                                          const std::string& yesLabel = "Yes",
                                          const std::string& noLabel  = "No");

// -------------------------------------------------------------------------
// Popup context menu (#15)
// -------------------------------------------------------------------------

struct ContextMenuItem {
    std::string label;
    std::function<void()> onClick;
    bool enabled = true;       ///< Greyed-out when false
    int32_t shortcut = 0;      ///< Optional ASCII key
};

/// Build a small floating menu at `(x, y)` with the given items. The
/// menu is a single root panel; clicking any item fires its handler
/// then removes the menu. Clicking outside is the caller's job
/// (typically: track the menu id and remove on next frame's
/// non-menu input).
[[nodiscard]] WidgetId buildContextMenu(UIManager& ui, float x, float y,
                                         const std::vector<ContextMenuItem>& items);

// -------------------------------------------------------------------------
// Frame-time HUD overlay (#21)
// -------------------------------------------------------------------------

/// Build a corner overlay showing layout/render/input/binding ms.
/// Bound to the global Theme. Returns the panel id; refresh by
/// recreating each frame, or leaving up and updating via labels.
[[nodiscard]] WidgetId buildFrameTimeHUD(UIManager& ui,
                                          const UIManager::FrameTimings& timings);

// -------------------------------------------------------------------------
// Immediate-mode wrapper (#29)
// -------------------------------------------------------------------------

/// Scoped immediate-mode helper. Builds and tears down a panel
/// + linear children list within one stack-scoped object. Useful for
/// debug overlays that don't need persistent widget IDs.
///
/// ```
/// {
///     ImmediateBlock im(ui, parent);
///     im.label("Score:");
///     im.label(std::to_string(player.score()));
/// }
/// // children removed automatically when block goes out of scope
/// ```
class ImmediateBlock {
public:
    ImmediateBlock(UIManager& ui, WidgetId parent, Rect bounds = {0, 0, 200, 0});
    ~ImmediateBlock();
    void label(const std::string& text);
    void button(const std::string& text, std::function<void()> onClick);
    void separator();
    [[nodiscard]] WidgetId root() const { return this->m_root; }
private:
    UIManager& m_ui;
    WidgetId   m_root;
};

} // namespace aoc::ui

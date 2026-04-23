#pragma once

/**
 * @file UIManager.hpp
 * @brief Manages the UI widget tree: creation, layout, input, rendering.
 *
 * Widgets are stored in a flat vector. The tree structure is maintained
 * via parent/children indices. Layout is computed top-down from root
 * widgets. Rendering is back-to-front (parent before children).
 */

#include "aoc/ui/Widget.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <functional>
#include <unordered_map>
#include <vector>

namespace vulkan_app::renderer {
class Renderer2D;
}

namespace aoc::ui {

class UIManager {
public:
    UIManager() = default;

    // ========================================================================
    // Widget creation
    // ========================================================================

    /// Create a root-level panel (no parent).
    [[nodiscard]] WidgetId createPanel(Rect bounds, PanelData panelData = {});

    /// Create a panel as child of another widget.
    [[nodiscard]] WidgetId createPanel(WidgetId parent, Rect bounds, PanelData panelData = {});

    /// Create a button.
    [[nodiscard]] WidgetId createButton(WidgetId parent, Rect bounds, ButtonData buttonData);

    /// Create a text label.
    [[nodiscard]] WidgetId createLabel(WidgetId parent, Rect bounds, LabelData labelData);

    /// Create a scrollable list container.
    [[nodiscard]] WidgetId createScrollList(WidgetId parent, Rect bounds, ScrollListData data = {});

    /// Adjust scroll offset on a ScrollList widget.
    void scrollWidget(WidgetId id, float delta);

    [[nodiscard]] WidgetId createTabBar(WidgetId parent, Rect bounds, TabBarData data);
    [[nodiscard]] WidgetId createProgressBar(WidgetId parent, Rect bounds, ProgressBarData data);
    [[nodiscard]] WidgetId createSlider(WidgetId parent, Rect bounds, SliderData data);
    [[nodiscard]] WidgetId createIcon(WidgetId parent, Rect bounds, IconData data);
    [[nodiscard]] WidgetId createRichText(WidgetId parent, Rect bounds, RichTextData data);
    [[nodiscard]] WidgetId createPortrait(WidgetId parent, Rect bounds, PortraitData data);
    [[nodiscard]] WidgetId createMarkdown(WidgetId parent, Rect bounds, MarkdownData data);
    [[nodiscard]] WidgetId createListRow(WidgetId parent, Rect bounds, ListRowData data);

    /// Wrap a `WidgetId` in a versioned handle — call when you want to
    /// cache a reference across potential `removeWidget` + reuse.
    [[nodiscard]] WidgetHandle toHandle(WidgetId id) const;

    /// True if the handle still points to the same widget it did at
    /// `toHandle()` time. Cheap — one vector lookup.
    [[nodiscard]] bool isLive(WidgetHandle handle) const;

    /// Remove a widget and all its children.
    void removeWidget(WidgetId id);

    // ========================================================================
    // Widget access
    // ========================================================================

    [[nodiscard]] Widget* getWidget(WidgetId id);
    [[nodiscard]] const Widget* getWidget(WidgetId id) const;

    /// Update the text of a label widget.
    void setLabelText(WidgetId id, std::string text);

    /// Update the label of a button widget.
    void setButtonLabel(WidgetId id, std::string label);

    /// Show or hide a widget.
    void setVisible(WidgetId id, bool visible);

    /// Attach hover tooltip text to a widget. Empty string clears it.
    /// Tooltip displays after the standard hover delay, handled by the
    /// same `TooltipManager` used for map-tile tooltips. A widget with
    /// tooltip overrides any map-tile tooltip while hovered.
    void setWidgetTooltip(WidgetId id, std::string text);

    /// Read the tooltip text currently attached to a widget. Returns
    /// empty string for unknown ids or widgets without tooltips.
    [[nodiscard]] std::string_view widgetTooltip(WidgetId id) const;

    // ========================================================================
    // Declarative data bindings
    // ========================================================================
    //
    // Screens used to hand-write a `refresh()` that re-reads game state
    // and called `setLabelText` / `setButtonLabel` / `setVisible` on a
    // dozen cached WidgetIds. Instead, register a supplier once and let
    // `UIManager::updateBindings()` do the plumbing every frame. Removes
    // ~50% of boilerplate per screen and kills the "forgot to refresh
    // this one" class of bugs.

    /// Bind a label widget to a text supplier. Supplier runs every
    /// call to `updateBindings`. Passing an empty supplier clears.
    void bindLabel(WidgetId id, std::function<std::string()> supplier);

    /// Bind a button's label text.
    void bindButtonLabel(WidgetId id, std::function<std::string()> supplier);

    /// Bind a widget's visibility flag.
    void bindVisibility(WidgetId id, std::function<bool()> supplier);

    /// Remove any bindings attached to the widget. Called automatically
    /// when `removeWidget(id)` fires.
    void clearBindings(WidgetId id);

    /// Evaluate all bindings and push results into widgets. Cheap when
    /// suppliers return the same value repeatedly; cache-friendly.
    /// Call once per frame (Application::updateHUD already does this).
    void updateBindings();

    /// Strict-layout debug mode. When on, layout passes LOG_WARN for
    /// any child whose intrinsic size exceeds its parent's content
    /// area. Clamp still applies, but the warning surfaces authoring
    /// bugs at runtime instead of only on visual inspection. Off by
    /// default; flip in dev builds.
    void setStrictLayout(bool on) { this->m_strictLayout = on; }
    [[nodiscard]] bool strictLayout() const { return this->m_strictLayout; }

    // ========================================================================
    // Keyboard navigation
    // ========================================================================

    /// Advance focus to the next focusable widget in creation order.
    /// Wraps around; safe to call on an empty tree. Returns the new
    /// focused id (or INVALID_WIDGET if none focusable).
    WidgetId focusNext();

    /// Reverse of focusNext (Shift+Tab).
    WidgetId focusPrev();

    /// Current focused widget.
    [[nodiscard]] WidgetId focusedWidget() const { return this->m_focusedWidget; }

    /// Activate the focused widget — fires onClick for buttons,
    /// toggles for sliders, etc. Bound to Enter/Space by the app.
    void activateFocused();

    /// Dispatch a keyboard shortcut. Walks every visible button with a
    /// matching `ButtonData.shortcut` and fires its onClick. Returns
    /// true if at least one shortcut fired.
    bool activateShortcut(int32_t key);

    /// Dump the widget tree to a JSON-formatted string. Used by the
    /// widget inspector overlay and also useful for post-mortem
    /// debugging. One widget per line, indented by depth.
    [[nodiscard]] std::string dumpTreeJson() const;

    // ========================================================================
    // Frame timing + dev tooling
    // ========================================================================

    /// Per-phase frame timing in milliseconds. Updated by `tick()` each
    /// frame; consumed by the frame-time HUD overlay.
    struct FrameTimings {
        float layoutMs   = 0.0f;
        float renderMs   = 0.0f;
        float inputMs    = 0.0f;
        float bindingMs  = 0.0f;
        float total() const { return layoutMs + renderMs + inputMs + bindingMs; }
    };
    [[nodiscard]] const FrameTimings& frameTimings() const { return this->m_frameTimings; }
    void recordFrameTiming(const FrameTimings& t) { this->m_frameTimings = t; }

    /// Recent widget events (callback fires) for the inspector. Bounded
    /// circular buffer; oldest dropped on overflow.
    struct WidgetEvent {
        WidgetId id = INVALID_WIDGET;
        const char* kind = "";   ///< Static string literal: "click", "right", "drop", "tab"
        float timestampSec = 0.0f;
    };
    void logEvent(WidgetEvent ev);
    [[nodiscard]] const std::vector<WidgetEvent>& recentEvents() const { return this->m_eventLog; }

    // ========================================================================
    // Animation
    // ========================================================================

    /// Schedule a fade tween toward `targetAlpha` over `seconds`.
    /// Subsequent calls override any in-flight tween. UIManager
    /// integrates `currentAlpha` toward target during `tick`.
    void tweenAlpha(WidgetId id, float targetAlpha, float seconds);

    /// Trigger a flash pulse: tint applied + linearly decays over
    /// `durationSec`. Useful for "city under attack", "tech complete",
    /// etc.
    void flash(WidgetId id, Color tint, float durationSec);

    /// Run all per-frame animation steps (alpha tweens, hover-scale,
    /// flash decay, hold-to-repeat fires). Pass `deltaSec` from the
    /// game clock. Application calls this once per frame after input,
    /// before render.
    void tickAnimations(float deltaSec);

    // ========================================================================
    // Drag-and-drop
    // ========================================================================

    /// Register a drop handler keyed by `dropTargetWidget`. Fires when
    /// a drag terminates over that widget; payload is the source's
    /// `Widget.dragPayload` value.
    void onDrop(WidgetId target, std::function<void(uint32_t payload)> handler);

    /// True while the user is mid-drag. Renderers may use this to
    /// show drop-target highlights.
    [[nodiscard]] bool isDragging() const { return this->m_dragSource != INVALID_WIDGET; }
    [[nodiscard]] uint32_t currentDragPayload() const { return this->m_dragPayload; }

    // ========================================================================
    // Multi-select
    // ========================================================================

    /// Selection helpers for any list of `selectable` siblings sharing
    /// `parent`. Application typically wires these from list rows.
    void selectOnly(WidgetId id);
    void selectToggle(WidgetId id);
    void selectRangeTo(WidgetId id);
    [[nodiscard]] std::vector<WidgetId> currentSelection(WidgetId parent) const;

    // ========================================================================
    // Networked UI events
    // ========================================================================

    /// Forward an event to a remote handler. Stored on a queue that
    /// the host (Application/GameServer) drains and dispatches over
    /// the wire. Strings are interpreted by the application layer.
    struct NetworkEvent {
        std::string kind;     ///< "vote", "trade-offer", "chat", ...
        std::string payload;  ///< Free-form (often JSON)
        uint8_t fromPlayer = 0;
    };
    void emitNetworkEvent(NetworkEvent ev);
    [[nodiscard]] const std::vector<NetworkEvent>& networkOutbox() const { return this->m_netOutbox; }
    void clearNetworkOutbox() { this->m_netOutbox.clear(); }

    // ========================================================================
    // Audio cue dispatch
    // ========================================================================

    /// Drained by the host audio system each frame. Filled when a
    /// button with `clickSound != 0` fires.
    [[nodiscard]] const std::vector<uint32_t>& audioOutbox() const { return this->m_audioOutbox; }
    void clearAudioOutbox() { this->m_audioOutbox.clear(); }

    // ========================================================================
    // Input handling
    // ========================================================================

    /**
     * @brief Process mouse input for the UI.
     *
     * Tests hover and click on all visible widgets. The right-button
     * args are optional so existing callsites that only track the left
     * button stay compatible; pass `false` for both to disable the
     * right-click path entirely. Returns true if the UI consumed the
     * input (click was on a widget).
     */
    bool handleInput(float mouseX, float mouseY,
                     bool mousePressed, bool mouseReleased,
                     float scrollDelta = 0.0f,
                     bool rightPressed = false, bool rightReleased = false);

    // ========================================================================
    // Layout
    // ========================================================================

    /// Update the stored screen dimensions used by the anchor system.
    /// Call this each frame (or on resize) before layout().
    void setScreenSize(float width, float height);

    /// Recompute layout for all root widgets and their children.
    void layout();

    // ========================================================================
    // Rendering
    // ========================================================================

    /**
     * @brief Render all visible widgets.
     */
    void render(vulkan_app::renderer::Renderer2D& renderer2d) const;

    /// Get the currently hovered widget ID (INVALID_WIDGET if none).
    [[nodiscard]] WidgetId hoveredWidget() const { return this->m_hoveredWidget; }

    /**
     * @brief Shift all widget computedBounds from screen-space to world-space.
     *
     * Call before render() when the Renderer2D has a camera set.
     * worldPos = cameraPos + screenPos / zoom
     */
    void transformBounds(float cameraX, float cameraY, float invZoom);

    /// Reverse the transform applied by transformBounds().
    void untransformBounds(float cameraX, float cameraY, float invZoom);

private:
    WidgetId allocateWidget();
    void layoutWidget(WidgetId id, float parentX, float parentY);
    void renderWidget(vulkan_app::renderer::Renderer2D& renderer2d,
                      WidgetId id) const;
    [[nodiscard]] WidgetId hitTest(float x, float y) const;
    [[nodiscard]] WidgetId hitTestWidget(WidgetId id, float x, float y) const;
    void shiftWidgetTree(WidgetId id, float deltaX, float deltaY);

    std::vector<Widget>   m_widgets;
    std::vector<WidgetId> m_rootWidgets;  ///< Top-level widgets (no parent)
    std::vector<WidgetId> m_freeList;     ///< Recycled widget slots
    WidgetId              m_nextId = 0;

    /// Current screen dimensions for anchor calculations.
    float m_screenWidth  = 1280.0f;
    float m_screenHeight = 720.0f;

    /// Currently hovered widget.
    WidgetId m_hoveredWidget = INVALID_WIDGET;

    /// Widget that received the mouse press (for matching with release).
    WidgetId m_pressedWidget = INVALID_WIDGET;

    /// Widget that received the right-button press. Separate from
    /// `m_pressedWidget` so left-drag and right-click are independent.
    WidgetId m_rightPressedWidget = INVALID_WIDGET;

    /// Keyboard-focused widget (Tab order). Renders with a focus ring
    /// and responds to Enter/Space via `activateFocused`.
    WidgetId m_focusedWidget = INVALID_WIDGET;

    /// Per-slot generation counters. Bumped on `removeWidget` so
    /// stale `WidgetHandle` instances compare unequal after reuse.
    std::vector<uint32_t> m_generations;

    /// Frame-time HUD source (filled per-frame by Application).
    FrameTimings m_frameTimings;

    /// Bounded ring of recent widget events for the inspector.
    std::vector<WidgetEvent> m_eventLog;
    static constexpr std::size_t MAX_EVENT_LOG = 50;

    // Drag-drop runtime state.
    WidgetId m_dragSource = INVALID_WIDGET;
    uint32_t m_dragPayload = 0;
    std::unordered_map<WidgetId, std::function<void(uint32_t)>> m_dropHandlers;

    // Animation runtime: held-button repeat scheduling.
    struct ButtonRepeatState { float waited = 0.0f; bool armed = false; };
    std::unordered_map<WidgetId, ButtonRepeatState> m_repeatStates;

    // Double-click detection: previous click timestamp per widget.
    std::unordered_map<WidgetId, float> m_lastClickTime;
    float m_clockSec = 0.0f;

    // Multi-select anchor — last single-click row used by Shift-extend.
    WidgetId m_selectAnchor = INVALID_WIDGET;

    // Network + audio outboxes drained by the host each frame.
    std::vector<NetworkEvent> m_netOutbox;
    std::vector<uint32_t>     m_audioOutbox;

    /// Strict layout mode — dev-only runtime overflow warnings.
    bool m_strictLayout = false;

    /// Binding maps. Keyed by WidgetId; suppliers run in
    /// `updateBindings`. Stored in three parallel maps rather than a
    /// single struct so empty bindings cost zero.
    std::unordered_map<WidgetId, std::function<std::string()>> m_labelBindings;
    std::unordered_map<WidgetId, std::function<std::string()>> m_buttonBindings;
    std::unordered_map<WidgetId, std::function<bool()>>        m_visibilityBindings;

    /// Scale factor applied to font sizes and corner radii during rendering.
    /// Set by transformBounds() to compensate for camera zoom.
    float m_renderScale = 1.0f;
};

} // namespace aoc::ui

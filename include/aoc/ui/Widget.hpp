#pragma once

/**
 * @file Widget.hpp
 * @brief Core UI types: Rect, Color, WidgetId, and the Widget variant struct.
 *
 * The UI is a retained widget tree. Each widget is a flat struct with a
 * variant payload for type-specific data. No inheritance -- all widgets
 * are stored in a contiguous vector inside UIManager.
 */

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <variant>
#include <vector>

namespace aoc::ui {

// ============================================================================
// Primitive types
// ============================================================================

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    [[nodiscard]] bool contains(float px, float py) const {
        return px >= this->x && px < this->x + this->w
            && py >= this->y && py < this->y + this->h;
    }
};

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct Padding {
    float top    = 0.0f;
    float right  = 0.0f;
    float bottom = 0.0f;
    float left   = 0.0f;
};

using WidgetId = uint32_t;
inline constexpr WidgetId INVALID_WIDGET = std::numeric_limits<WidgetId>::max();

/// Versioned handle used when callers need to detect widget reuse
/// across reallocation. Plain `WidgetId` still works everywhere — this
/// is opt-in. Pair `{id, generation}`: if the stored generation
/// matches the current per-slot generation, the handle is live.
/// Unused slots bump their generation on `removeWidget`.
struct WidgetHandle {
    WidgetId id = INVALID_WIDGET;
    uint32_t generation = 0;
    [[nodiscard]] bool isValid() const { return this->id != INVALID_WIDGET; }
};

// ============================================================================
// Widget type-specific data
// ============================================================================

/// Empty panel -- just a colored rectangle container.
struct PanelData {
    Color backgroundColor = {0.1f, 0.1f, 0.15f, 0.85f};
    float cornerRadius    = 4.0f;

    /// Optional second colour for a vertical two-band gradient. Alpha
    /// 0 = flat fill; non-zero draws a lower band blended with the
    /// main `backgroundColor`. Cheap alternative to a shader-side
    /// gradient and enough to break up the flat-grey look.
    Color gradientBottom  = {0.0f, 0.0f, 0.0f, 0.0f};

    /// Optional thin outline drawn just inside the bounds.
    Color borderColor     = {0.0f, 0.0f, 0.0f, 0.0f};
    float borderWidth     = 1.0f;

    /// Optional leading accent bar (left edge ribbon).
    Color accentBarColor  = {0.0f, 0.0f, 0.0f, 0.0f};
    float accentBarWidth  = 3.0f;

    /// Optional 1-px inner edges that fake depth: lighter top, darker
    /// bottom. Alpha 0 = off.
    Color topHighlight    = {0.0f, 0.0f, 0.0f, 0.0f};
    Color bottomShadow    = {0.0f, 0.0f, 0.0f, 0.0f};
};

/// Clickable button.
struct ButtonData {
    std::string label;
    Color normalColor   = {0.25f, 0.25f, 0.30f, 0.9f};
    Color hoverColor    = {0.35f, 0.35f, 0.40f, 0.9f};
    Color pressedColor  = {0.15f, 0.15f, 0.20f, 0.9f};
    Color labelColor    = {1.0f, 1.0f, 1.0f, 1.0f};
    float cornerRadius  = 3.0f;
    float fontSize      = 14.0f;
    std::function<void()> onClick;

    /// Optional right-click handler. When set, `UIManager::handleInput`
    /// fires this on a right-button release over the widget (in
    /// addition to the existing left-click path). Empty by default so
    /// existing callsites are unaffected.
    std::function<void()> onRightClick;

    /// Optional double-click handler. Fires on the second click within
    /// 350 ms of the first; the original `onClick` still fires on each
    /// individual press.
    std::function<void()> onDoubleClick;

    /// Hold-to-repeat configuration. When `repeatRateHz > 0`, holding
    /// the button down fires `onClick` again every 1 / repeatRateHz
    /// seconds after `repeatDelaySec` elapsed. Useful for +/- volume,
    /// scroll arrows, queue priority bumps.
    float repeatDelaySec = 0.0f;
    float repeatRateHz   = 0.0f;

    /// Optional scroll-wheel handler. When set, scrolling the wheel
    /// while hovering this button dispatches the wheel delta (yoffset)
    /// directly to this callback instead of bubbling up to the parent
    /// scroll/pan widget. Used by spinner-style number controls so the
    /// user can hover a +/- button and roll the wheel to scrub.
    std::function<void(float)> onScroll;

    /// Optional keyboard shortcut. ASCII key code; 0 = none. Renders
    /// an underline on the matching label letter (first occurrence,
    /// case-insensitive). Application dispatches the key through
    /// `UIManager::activateShortcut(key)` per frame.
    int32_t shortcut = 0;

    /// Audio cue fired when the button is clicked. 0 means silent;
    /// non-zero values are interpreted by the host as `SoundEffect`
    /// enum values via the SoundEventQueue.
    uint32_t clickSound = 0;

    /// Optional leading icon (IconAtlas sprite id). 0 = none. Rendered
    /// at the left edge before the label; label still centres in the
    /// remaining space. Used for tab buttons with category icons.
    uint32_t iconSpriteId = 0;
    float    iconSize     = 16.0f;

    /// Optional gradient bottom. When alpha > 0 the button renders
    /// with a vertical gradient from `normalColor`/`hoverColor`/
    /// `pressedColor` to this colour. Gives buttons glossy depth.
    Color gradientBottom  = {0.0f, 0.0f, 0.0f, 0.0f};

    /// Optional thin border. Alpha 0 = none.
    Color borderColor     = {0.0f, 0.0f, 0.0f, 0.0f};
    float borderWidth     = 1.0f;

    /// Persistent "this is the active choice" flag. Renderer uses
    /// `selectedColor` (falls back to hoverColor shifted brighter) so
    /// screens like tech/gov/tabs can indicate the current selection
    /// even after the cursor leaves the button. Caller toggles this
    /// in the onClick handler.
    bool  selected = false;
    Color selectedColor = {0.35f, 0.55f, 0.75f, 0.95f};

    /// Disabled state. Greys the button, suppresses onClick + hover
    /// styling. Paired with `hoverCursor = 0` to prevent the
    /// not-allowed cursor flicker.
    bool  disabled = false;
};

/// Text label.
struct LabelData {
    std::string text;
    Color       color    = {1.0f, 1.0f, 1.0f, 1.0f};
    float       fontSize = 14.0f;
    /// Optional 1-pixel outline drawn behind the glyphs in 8 directions.
    /// Alpha 0 = no outline. Use for titles + chip values laid over busy
    /// or low-contrast backgrounds where readability matters more than
    /// crispness. Cost is 8× drawText calls — fine for a handful of
    /// titles, avoid for body text. The outline draws the glyph in the
    /// outline colour at offsets of `pixelScale` along the 8 cardinal/
    /// diagonal directions, then the main fill on top.
    Color       outlineColor = {0.0f, 0.0f, 0.0f, 0.0f};
};

/// Scrollable list container. Children outside the visible window are skipped during rendering.
struct ScrollListData {
    Color backgroundColor = {0.12f, 0.12f, 0.16f, 0.9f};
    float scrollOffset = 0.0f;     ///< Pixels scrolled from top (0 = no scroll)
    float contentHeight = 0.0f;    ///< Total height of all children (computed during layout)
};

/// Horizontal bar at the top of a tabbed panel. One button per tab;
/// the active tab is highlighted. Children of a widget with
/// `TabBarData` are the tab labels (in order); `onTabSelected` fires
/// with the index when the user clicks.
struct TabBarData {
    std::vector<std::string> labels;
    int32_t activeTab = 0;
    Color activeColor   = {0.35f, 0.55f, 0.75f, 0.95f};
    Color inactiveColor = {0.20f, 0.20f, 0.28f, 0.9f};
    Color hoverColor    = {0.30f, 0.30f, 0.38f, 0.9f};
    Color labelColor    = {1.0f, 1.0f, 1.0f, 1.0f};
    float fontSize      = 13.0f;
    float tabWidth      = 100.0f;
    std::function<void(int32_t)> onTabSelected;

    /// Animated underline x-position (in tab-index units, fractional).
    /// Eases toward `activeTab` for a smooth slide effect. Bumped by
    /// `UIManager::tickAnimations` each frame. 0 by default so the
    /// static appearance is unchanged until animation ticks.
    float activeTabAnim = 0.0f;
    /// Thickness of the sliding underline in pixels.
    float underlineThickness = 3.0f;
};

/// Horizontal progress bar. Fill fraction 0..1 is rendered as the
/// leftmost portion of the widget in `fillColor`; the remainder shows
/// `backgroundColor`. Optional overlay text (e.g. "45 / 100").
struct ProgressBarData {
    float  fillFraction = 0.0f;
    Color  fillColor       = {0.2f, 0.7f, 0.3f, 0.9f};
    Color  backgroundColor = {0.15f, 0.15f, 0.20f, 0.8f};
    Color  textColor       = {1.0f, 1.0f, 1.0f, 1.0f};
    float  cornerRadius = 2.0f;
    std::string overlayText;  ///< Optional centred label
    float  fontSize = 11.0f;
};

/// Horizontal slider. Drag the thumb to change `value` within
/// `[minValue, maxValue]`. Step > 0 snaps; step = 0 is continuous.
struct SliderData {
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float value    = 0.0f;
    float step     = 0.0f;        ///< 0 = continuous
    Color trackColor = {0.20f, 0.20f, 0.25f, 0.9f};
    Color fillColor  = {0.35f, 0.55f, 0.75f, 0.95f};
    Color thumbColor = {0.9f, 0.9f, 0.9f, 1.0f};
    std::function<void(float)> onValueChanged;
    bool dragging = false;        ///< Managed by UIManager during drag
};

/// Icon / sprite widget. `spriteId` is an opaque handle the renderer
/// resolves through its sprite atlas. 0 = blank. The renderer falls
/// back to drawing a solid-colour rect sized like the icon when the
/// sprite path isn't wired yet, so screens can adopt this widget now
/// and swap in art later without touching callsites.
struct IconData {
    uint32_t spriteId = 0;
    Color    tint     = {1.0f, 1.0f, 1.0f, 1.0f};
    Color    fallbackColor = {0.5f, 0.5f, 0.5f, 1.0f};
    /// Optional click handler — fires on left-release over the icon.
    std::function<void()> onClick;
};

/// Rich text segment used by `RichTextData`. A single span carries
/// either text or an inline icon, with optional colour overrides.
struct RichTextSpan {
    enum class Kind : uint8_t { Text, Icon, LineBreak };
    Kind        kind = Kind::Text;
    std::string text;
    uint32_t    iconSpriteId = 0;
    Color       color  = {1.0f, 1.0f, 1.0f, 1.0f};
    bool        bold   = false;
    bool        italic = false;
};

/// Multi-segment text widget supporting inline colour, icons, and line
/// breaks. The rendering layer walks `spans` left-to-right, advancing
/// the cursor by each segment's measured width. Falls back to the
/// label renderer for plain text spans until the sprite path lands.
struct RichTextData {
    std::vector<RichTextSpan> spans;
    float fontSize = 14.0f;
    /// Maximum line width before wrapping. 0 = no wrap (single line).
    float wrapWidth = 0.0f;
};

/// Unit/city portrait card. Combines a sprite with a name + stats
/// strip. Stats are a small std::vector<pair<key,value>> rendered in
/// two columns. Acts as scaffold until the sprite asset path lands
/// — falls back to a tinted rect like `IconData`.
struct PortraitData {
    uint32_t spriteId = 0;
    std::string title;
    std::vector<std::pair<std::string, std::string>> stats;
    Color tint = {1.0f, 1.0f, 1.0f, 1.0f};
    Color fallbackColor = {0.30f, 0.35f, 0.45f, 1.0f};
    Color titleColor = {1.0f, 0.9f, 0.5f, 1.0f};
    float titleFontSize = 16.0f;
    float statsFontSize = 11.0f;
};

/// Rich list-row payload. One widget = one row with optional leading
/// icon, primary title, subtitle, right-aligned value, and a pressable
/// "selected" accent bar on the left edge (rendered when
/// `Widget.isSelected` is true). Used by EconomyScreen market list,
/// DiplomacyScreen relation list, etc. Renderer lays out spans left-
/// to-right: accent bar → icon → title/subtitle stack → right value.
struct ListRowData {
    uint32_t iconSpriteId = 0;      ///< 0 = skip icon column
    std::string title;
    std::string subtitle;
    std::string rightValue;
    Color titleColor    = {0.95f, 0.95f, 0.95f, 1.0f};
    Color subtitleColor = {0.70f, 0.70f, 0.75f, 1.0f};
    Color valueColor    = {1.0f, 0.9f, 0.4f, 1.0f};
    Color accentColor   = {0.35f, 0.55f, 0.75f, 0.95f};
    Color hoverBg       = {0.15f, 0.17f, 0.22f, 0.9f};
    Color pressedBg     = {0.10f, 0.11f, 0.14f, 0.9f};
    float iconSize      = 24.0f;
    float titleFont     = 13.0f;
    float subtitleFont  = 10.0f;
    float valueFont     = 13.0f;
    std::function<void()> onClick;
};

/// Markdown-ish help renderer payload. Storing the raw source lets the
/// renderer parse on demand each frame (cheap for small documents) so
/// content can change without rebuilding widgets. Supports headings
/// (#, ##), bullet lists (-), bold (**word**), and link refs ([Text]).
struct MarkdownData {
    std::string source;
    Color textColor   = {0.92f, 0.92f, 0.92f, 1.0f};
    Color headingColor = {1.0f, 0.85f, 0.4f, 1.0f};
    Color linkColor    = {0.5f, 0.75f, 1.0f, 1.0f};
    float fontSize = 12.0f;
    /// When set, link clicks are routed here. Argument is the link
    /// text in square brackets (e.g. "Mining" → look up encyclopedia
    /// entry). Empty handler means links render as plain text.
    std::function<void(const std::string&)> onLinkClicked;
};

/// Horizontal or vertical layout direction for children.
enum class LayoutDirection : uint8_t {
    Vertical,
    Horizontal,
    /// Flow horizontally until the next child would overflow the
    /// container's inner width, then wrap to a new row. Row height
    /// tracks the tallest child on the row; subsequent rows stack
    /// vertically with `childSpacing`. Use for chip lists, resource
    /// summaries, etc.
    HorizontalWrap,
    /// Absolute positioning: each child is placed at its
    /// `requestedBounds.x / y` relative to the parent's content
    /// origin. Layout pass skips cursor advance and cross-axis fill.
    /// Use for tech-tree node graphs, world-map overlays, etc.
    None,
};

/// Anchor point for root widget positioning relative to the screen.
/// Only meaningful for root widgets (no parent). Child widgets are always
/// positioned relative to their parent regardless of anchor.
enum class Anchor : uint8_t {
    None,         ///< Absolute position (legacy behavior)
    TopLeft,      ///< Position relative to top-left corner
    TopRight,     ///< Position relative to top-right corner
    BottomLeft,   ///< Position relative to bottom-left corner
    BottomRight,  ///< Position relative to bottom-right corner
    Center,       ///< Centered on screen
    TopCenter,    ///< Centered horizontally, top edge
    BottomCenter, ///< Centered horizontally, bottom edge
};

// ============================================================================
// Widget struct (variant-based, stored in flat vector)
// ============================================================================

struct Widget {
    WidgetId id = INVALID_WIDGET;

    /// Requested position/size (input to layout). Absolute or relative depending on parent.
    Rect requestedBounds = {};

    /// Computed absolute bounds (output of layout).
    Rect computedBounds = {};

    Padding padding = {};

    /// Layout direction for child arrangement.
    LayoutDirection layoutDirection = LayoutDirection::Vertical;
    float childSpacing = 4.0f;

    /// Flex weight for proportional sizing within a flex container.
    /// 0 = use `requestedBounds` intrinsic size. >0 = share of leftover
    /// space along the parent's `layoutDirection` axis. Two siblings
    /// with `flex = 1` each get 50% of the remaining space; `flex = 2`
    /// gets double a `flex = 1` sibling. Applied on top of padding and
    /// the sibling's intrinsic size (flex never shrinks below 0).
    float flex = 0.0f;

    /// Grid container: split children into `gridColumns`. 0 disables
    /// grid mode (default linear stack). `childSpacing` applies on both
    /// axes; rows and columns share it.
    int32_t gridColumns = 0;

    /// If true, size is auto-computed from children.
    bool autoWidth  = false;
    bool autoHeight = false;

    /// Auto-clamp children: during layout, any child whose computed
    /// right/bottom edge would exceed this panel's content area gets
    /// shrunk in-place (not repositioned) so it never spills. Defaults
    /// to true so screens get safe-by-default bounds; opt out by
    /// setting false when a ScrollList's content must exceed the frame.
    bool clampChildren = true;

    /// Min/max size constraints. Applied by the layout pass after
    /// flex/auto-width computation. 0 means "no constraint". Useful
    /// when a flex-distributed bar would otherwise shrink below a
    /// readable size, or when you want a fixed-cap panel that
    /// stops growing past a threshold.
    float minW = 0.0f;
    float maxW = 0.0f;
    float minH = 0.0f;
    float maxH = 0.0f;

    /// Fill-parent hint for children in a Vertical or Horizontal
    /// layout: when set, the child's width (in Vertical) or height (in
    /// Horizontal) expands to match the parent's content-axis size.
    /// Cheaper than `flex = 1` when there's only one fill-child.
    bool fillParentCross = false;

    /// Anchor point for root-level widgets. Determines how the widget is
    /// positioned relative to the screen edges on resize.
    Anchor anchor = Anchor::None;
    float marginRight  = 0.0f;  ///< Distance from right edge (right-anchored widgets)
    float marginBottom = 0.0f;  ///< Distance from bottom edge (bottom-anchored widgets)

    /// Visibility and interaction state.
    bool isVisible = true;
    bool isHovered = false;
    bool isPressed = false;

    /// If true, the widget (root only) can be drag-repositioned by the
    /// user. Drag is gated on Ctrl+Left-Drag so normal clicks still
    /// fire onClick. The UIManager tracks `dragAnchor` during a drag
    /// to preserve cursor offset relative to the widget origin.
    bool isDraggable = false;
    float dragAnchorX = 0.0f;
    float dragAnchorY = 0.0f;

    /// Tree structure (indices into the UIManager widget vector).
    WidgetId parent = INVALID_WIDGET;
    std::vector<WidgetId> children;

    /// Type-specific payload.
    std::variant<PanelData, ButtonData, LabelData, ScrollListData,
                 TabBarData, ProgressBarData, SliderData, IconData,
                 RichTextData, PortraitData, MarkdownData, ListRowData>
        data = PanelData{};

    /// Optional rich-tooltip text shown after a hover delay. Empty means
    /// no tooltip. Uses `\n` for line breaks (matches `TooltipManager`
    /// formatting). Future: promote to a richer struct so tooltips can
    /// carry icons, colour-keyed resource breakdowns, etc.
    std::string tooltip;

    /// Accessibility description for screen readers and the planned
    /// keyboard-nav "announce focused element" feature. Separate from
    /// `tooltip` so a button can have a short visual label and a more
    /// verbose spoken description.
    std::string ariaLabel;

    /// Participates in keyboard Tab-order. Focused widgets render with
    /// a highlight border; Enter / Space fires onClick (buttons) or
    /// toggles (checkbox-style toggles). Set by widget authors at
    /// creation; runtime focus state lives in `isFocused`.
    bool focusable = false;
    bool isFocused = false;

    // ------------------------------------------------------------------
    // Drag-and-drop
    // ------------------------------------------------------------------
    /// Mark the widget as a drag source. Callers populate `dragPayload`
    /// with an opaque uint32 tag (e.g. unit id, good id) — UIManager
    /// ferries it during a drag and hands it to the drop target.
    bool      canDrag      = false;
    uint32_t  dragPayload  = 0;

    /// True if the widget accepts drops. The runtime drop callback
    /// is on UIManager, not the widget, so callers can swap targets
    /// without rewriting the widget tree.
    bool      acceptsDrop  = false;

    // ------------------------------------------------------------------
    // Pan canvas (right-mouse drag + edge scroll)
    // ------------------------------------------------------------------
    /// Mark the widget as a pannable canvas. Right-mouse drag inside its
    /// bounds shifts `panX`/`panY`, and the layout pass applies that
    /// offset to children when `layoutDirection == None`. Use for the
    /// tech-tree graph, world-strategic overlays, etc. Edge-scroll
    /// (mouse near widget border) is enabled separately by callers via
    /// the per-frame tick.
    bool      canPan       = false;
    float     panX         = 0.0f;
    float     panY         = 0.0f;

    // ------------------------------------------------------------------
    // Multi-select
    // ------------------------------------------------------------------
    /// Mark a widget as part of a multi-select list. UIManager tracks
    /// the selection set and supports Shift-extend / Ctrl-toggle.
    bool      selectable   = false;
    bool      isSelected   = false;
    /// Sequence index within the parent list — used by Shift-extend
    /// to compute the inclusive range. Auto-assigned in creation order.
    int32_t   selectIndex  = 0;

    // ------------------------------------------------------------------
    // Resize handles
    // ------------------------------------------------------------------
    /// User can grab edges/corners to resize. Pairs with `isDraggable`
    /// for movable + resizable panels. UIManager renders 8 thin handle
    /// rects when this flag is set and the widget is hovered.
    bool      isResizable  = false;
    float     minResizeW   = 80.0f;
    float     minResizeH   = 60.0f;

    // ------------------------------------------------------------------
    // Animation hooks
    // ------------------------------------------------------------------
    /// Active fade tween: when alphaTarget != alpha, UIManager
    /// interpolates over `alphaTweenSec`. Setter helpers below.
    float     alpha          = 1.0f;
    float     alphaTarget    = 1.0f;
    float     alphaTweenSec  = 0.0f;
    float     alphaTweenLeft = 0.0f;

    /// Hover scale animation. 1.0 = no scaling. Set `hoverScale > 1.0`
    /// for a subtle pop on hover; UIManager animates `currentScale`
    /// toward 1 + (hoverScale - 1) * isHovered each frame.
    float     hoverScale     = 1.0f;
    float     currentScale   = 1.0f;

    // ------------------------------------------------------------------
    // Flash / mood pulses (#33)
    // ------------------------------------------------------------------
    /// Temporary tint applied on top of the widget colour. Decays over
    /// `flashDurationLeft` seconds. Useful for "city under attack"
    /// pulses without rebuilding widget trees.
    Color     flashColor    = {0.0f, 0.0f, 0.0f, 0.0f};
    float     flashDurationLeft = 0.0f;
    float     flashDurationTotal = 0.0f;

    // ------------------------------------------------------------------
    // Cursor change on hover (#14)
    // ------------------------------------------------------------------
    /// When hovered, the application requests this cursor shape. 0 =
    /// default. Values map to GLFW standard cursors (GLFW_HAND_CURSOR
    /// etc.) without including the GLFW header here.
    int32_t   hoverCursor   = 0;

    // ------------------------------------------------------------------
    // Networked event source (#32)
    // ------------------------------------------------------------------
    /// Origin tag for replicated/authoritative UI events. Local widgets
    /// leave this 0; networked panels fill it with the originating
    /// player id so the dispatcher can validate the source.
    uint8_t   eventOriginPlayer = 0;

    /// Scissor clip: when true, children render clipped to this
    /// widget's bounds via a Vulkan scissor rect. Hard guarantee over
    /// the layout-level clamp — geometry that still spills gets
    /// cropped at the panel edge. Opt-in per panel (off by default).
    bool clipChildren = false;

    /// Dirty-rect render hint (#16). When false the renderer may skip
    /// re-issuing draw calls if the swapchain still has the previous
    /// frame's pixels for this widget. Default true so existing screens
    /// (which mutate widgets every frame via bindings) keep redrawing.
    /// Setters that mutate visible state (`setLabelText`, etc.) reset
    /// this to true.
    bool      isDirty = true;

    /// Lazy-list culling hint (#18). When set on a child of a
    /// ScrollList, layout skips bounds computation while the row is
    /// outside the visible window. Set by the list builder when rows
    /// are uniform-height; layout flips it back to false after the
    /// row enters the viewport.
    bool      cullableRow = false;
};

} // namespace aoc::ui

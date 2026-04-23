#pragma once

/**
 * @file IScreen.hpp
 * @brief Unified interface for modal UI screens and menus.
 *
 * Implemented by both `ScreenBase` (in-game screens: Production, Tech, City,
 * etc.) and standalone menus that predate it (`SettingsMenu`). The common
 * contract lets `ScreenRegistry` query open/close state and forward resize
 * events without hand-maintaining per-screen lists in Application.
 *
 * `onResize` lets each screen reflow when the backing viewport changes.
 * Default implementations live on `ScreenBase` — menus that don't inherit
 * from it implement the three hooks directly via adapter shims.
 */

namespace aoc::ui {

class UIManager;
struct Theme;

class IScreen {
public:
    virtual ~IScreen() = default;

    /// True if the screen currently owns widgets in `UIManager`.
    [[nodiscard]] virtual bool isOpen() const = 0;

    /// Tear down all widgets belonging to this screen. Safe to call when
    /// `isOpen()` is false (no-op).
    virtual void close(UIManager& ui) = 0;

    /// React to a viewport-size change. Default: if open, tear down and
    /// rebuild with the new dimensions. Individual screens may override
    /// for cheaper reflow.
    virtual void onResize(UIManager& ui, float width, float height) = 0;

    /// Optional per-screen theme override. Returns nullptr (default)
    /// means "use the global theme". Screens that need a specialised
    /// palette (e.g., encyclopedia parchment look) return a pointer to
    /// their cached Theme instance. Non-owning.
    [[nodiscard]] virtual const Theme* themeOverride() const { return nullptr; }
};

} // namespace aoc::ui

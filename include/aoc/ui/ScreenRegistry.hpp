#pragma once

/**
 * @file ScreenRegistry.hpp
 * @brief Central registry of all modal UI screens.
 *
 * Replaces the pattern of hand-maintained screen lists in Application
 * (`anyScreenOpen`, `closeAllScreens`) that silently forgot the
 * SettingsMenu and broke input gating. Any `IScreen` that has been
 * registered participates automatically in:
 *
 *   - `anyOpen()` — gates game input when a screen is modal.
 *   - `closeAll()` — Esc-to-close handler.
 *   - `onResize()` — viewport change fanout so every screen can reflow.
 *
 * Adding a new screen only touches the registration call site, not the
 * query helpers.
 */

#include <vector>

namespace aoc::ui {

class IScreen;
class UIManager;

class ScreenRegistry {
public:
    /// Register a screen. The registry does not own the screen; caller
    /// must guarantee the pointer outlives the registry. Duplicates are
    /// rejected silently.
    void add(IScreen* screen);

    /// Unregister a screen. Safe to call if not registered.
    void remove(IScreen* screen);

    /// True if any registered screen is open.
    [[nodiscard]] bool anyOpen() const;

    /// Close every open screen.
    void closeAll(UIManager& ui);

    /// Broadcast a viewport size change to all registered screens.
    void onResize(UIManager& ui, float width, float height);

    /// Count of registered screens (diagnostic / tests only).
    [[nodiscard]] std::size_t size() const { return this->m_screens.size(); }

    // ---------------------------------------------------------------
    // Modal stack
    // ---------------------------------------------------------------
    // Modal screens can chain: pause menu → settings → graphics → back.
    // Pushing remembers the previous top so a later `popModal()` can
    // reopen it. Screens themselves manage their own open/close — the
    // stack is a pure bookkeeping aid for the "back" path.

    /// Push `screen` as the current modal. Its predecessor (if any)
    /// stays in the stack and will be reopened by `popModal`.
    void pushModal(IScreen* screen);

    /// Pop the top modal. Returns the screen popped (caller may
    /// reopen) or nullptr if the stack is empty.
    IScreen* popModal();

    /// Current top modal, or nullptr if the stack is empty.
    [[nodiscard]] IScreen* topModal() const;

    /// Depth of the modal stack. Each level corresponds to one open
    /// modal screen.
    [[nodiscard]] std::size_t modalDepth() const { return this->m_modalStack.size(); }

    /// Unified input-gate test: true when game input should be
    /// blocked. Replaces the two-step `anyScreenOpen() ||
    /// m_uiConsumedInput` pattern at Application callsites so callers
    /// don't have to remember both checks.
    [[nodiscard]] bool shouldBlockGameInput(bool uiConsumedInput) const {
        return this->anyOpen() || uiConsumedInput;
    }

    /// Close every entry on the modal stack (top-to-bottom) and clear
    /// the stack. Complements `closeAll` — which closes every screen
    /// including non-modal ones — with a modal-only variant.
    void closeModalStack(UIManager& ui);

private:
    std::vector<IScreen*> m_screens;
    std::vector<IScreen*> m_modalStack;
};

} // namespace aoc::ui

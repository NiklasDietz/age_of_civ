#pragma once

/**
 * @file HotkeyManager.hpp
 * @brief Rebindable hotkey system.
 *
 * All game hotkeys are stored in a table that can be modified at runtime.
 * Hotkeys are saved to a config file (hotkeys.cfg) and loaded at startup.
 *
 * Default bindings:
 *   T = Tech screen,  G = Government, E = Economy, P = Production
 *   W = Encyclopedia, M = Minimap toggle, ESC = Close screen
 *   Space = End turn, Enter = Confirm
 *   1-9 = Control groups, Ctrl+1-9 = Save control group
 *   F = Fortify, X = Delete unit, S = Sleep, A = Auto-explore
 *   Ctrl+S = Quicksave, Ctrl+L = Quickload
 */

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace aoc::app {

enum class GameAction : uint8_t {
    // Screens
    OpenTechScreen,
    OpenGovernmentScreen,
    OpenEconomyScreen,
    OpenProductionScreen,
    OpenEncyclopedia,
    OpenDiplomacy,
    OpenReligion,
    CloseScreen,

    // Turn
    EndTurn,
    Confirm,

    // Unit commands
    Fortify,
    Sleep,
    DeleteUnit,
    AutoExplore,
    AutoImprove,

    // Selection
    ControlGroup1, ControlGroup2, ControlGroup3,
    ControlGroup4, ControlGroup5, ControlGroup6,
    ControlGroup7, ControlGroup8, ControlGroup9,

    // System
    Quicksave,
    Quickload,
    ToggleMinimap,

    Count
};

inline constexpr int32_t ACTION_COUNT = static_cast<int32_t>(GameAction::Count);

/// Human-readable name for each action.
[[nodiscard]] const char* actionName(GameAction action);

struct HotkeyBinding {
    int32_t keyCode;      ///< GLFW key code
    bool    ctrl  = false;
    bool    shift = false;
    bool    alt   = false;
};

class HotkeyManager {
public:
    /// Load default bindings.
    void loadDefaults();

    /// Load bindings from a config file. Falls back to defaults on failure.
    bool loadFromFile(const std::string& filepath);

    /// Save current bindings to a config file.
    bool saveToFile(const std::string& filepath) const;

    /// Rebind an action to a new key.
    void rebind(GameAction action, HotkeyBinding binding);

    /// Get the current binding for an action.
    [[nodiscard]] const HotkeyBinding& getBinding(GameAction action) const;

    /// Check if a key event matches any action. Returns the matched action or Count if none.
    [[nodiscard]] GameAction matchKey(int32_t keyCode, bool ctrl, bool shift, bool alt) const;

private:
    std::array<HotkeyBinding, ACTION_COUNT> m_bindings = {};
};

} // namespace aoc::app

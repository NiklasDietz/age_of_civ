#pragma once

/**
 * @file InputActions.hpp
 * @brief Game-level input actions decoupled from raw key/button codes.
 */

#include <cstdint>

namespace aoc::app {

/// Logical game actions mapped from physical inputs.
enum class InputAction : uint8_t {
    // Camera
    PanLeft,
    PanRight,
    PanUp,
    PanDown,
    ZoomIn,
    ZoomOut,

    // Game
    Select,
    ContextAction,
    EndTurn,
    Cancel,

    // UI navigation
    OpenTechTree,
    OpenCivicTree,
    OpenDiplomacy,
    OpenEconomy,
    ToggleGrid,

    // Screens
    OpenProductionPicker,
    OpenGovernment,

    // Save/Load
    QuickSave,
    QuickLoad,

    // Unit actions
    UpgradeUnit,

    // Help
    ShowHelp,

    Count  ///< Sentinel -- must be last
};

} // namespace aoc::app

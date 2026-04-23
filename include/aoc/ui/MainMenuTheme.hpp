#pragma once

/**
 * @file MainMenuTheme.hpp
 * @brief Shared colour palette used by MainMenu, GameSetupScreen, and
 *        SettingsMenu.
 *
 * Previously these constants were file-static in MainMenu.cpp, which
 * blocked splitting SettingsMenu into its own translation unit without
 * duplicating them. Promoted here so any menu-layer screen can pick up
 * the same visual language.
 */

#include "aoc/ui/Widget.hpp"

namespace aoc::ui {

inline constexpr Color GOLDEN_TEXT      = {1.0f, 0.85f, 0.3f, 1.0f};
inline constexpr Color WHITE_TEXT       = {1.0f, 1.0f, 1.0f, 1.0f};
inline constexpr Color GREY_TEXT        = {0.7f, 0.7f, 0.7f, 1.0f};
inline constexpr Color SECTION_TEXT     = {0.8f, 0.8f, 0.6f, 1.0f};

inline constexpr Color BG_DARK          = {0.05f, 0.05f, 0.08f, 0.95f};
inline constexpr Color PANEL_BG         = {0.10f, 0.10f, 0.15f, 0.92f};

inline constexpr Color BTN_NORMAL       = {0.20f, 0.20f, 0.28f, 0.9f};
inline constexpr Color BTN_HOVER        = {0.30f, 0.30f, 0.38f, 0.9f};
inline constexpr Color BTN_PRESSED      = {0.12f, 0.12f, 0.18f, 0.9f};

inline constexpr Color BTN_SELECTED     = {0.35f, 0.55f, 0.75f, 0.95f};
inline constexpr Color BTN_SEL_HOVER    = {0.40f, 0.60f, 0.80f, 0.95f};
inline constexpr Color BTN_SEL_PRESSED  = {0.25f, 0.45f, 0.65f, 0.95f};

inline constexpr Color BTN_GREEN        = {0.15f, 0.45f, 0.15f, 0.9f};
inline constexpr Color BTN_GREEN_HOVER  = {0.20f, 0.55f, 0.20f, 0.9f};
inline constexpr Color BTN_GREEN_PRESS  = {0.10f, 0.35f, 0.10f, 0.9f};

inline constexpr Color BTN_RED          = {0.50f, 0.15f, 0.15f, 0.9f};
inline constexpr Color BTN_RED_HOVER    = {0.60f, 0.20f, 0.20f, 0.9f};
inline constexpr Color BTN_RED_PRESS    = {0.40f, 0.10f, 0.10f, 0.9f};

inline constexpr Color BTN_GREY         = {0.25f, 0.25f, 0.30f, 0.9f};
inline constexpr Color BTN_GREY_HOVER   = {0.35f, 0.35f, 0.40f, 0.9f};
inline constexpr Color BTN_GREY_PRESS   = {0.15f, 0.15f, 0.20f, 0.9f};

} // namespace aoc::ui

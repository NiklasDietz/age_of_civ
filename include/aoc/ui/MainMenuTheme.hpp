#pragma once

/**
 * @file MainMenuTheme.hpp
 * @brief Legacy menu palette names — now redirected to the unified
 *        parchment/bronze design tokens defined in StyleTokens.hpp.
 *
 * Existing call sites that reference GOLDEN_TEXT / PANEL_BG / BTN_GREY
 * continue to compile but render with the new visual language. Migrate
 * call sites to the explicit `aoc::ui::tokens::*` names over time, then
 * delete this header.
 */

#include "aoc/ui/StyleTokens.hpp"

namespace aoc::ui {

// Text aliases.
inline constexpr Color GOLDEN_TEXT      = tokens::TEXT_GILT;
inline constexpr Color WHITE_TEXT       = tokens::TEXT_PARCHMENT;
inline constexpr Color GREY_TEXT        = tokens::TEXT_DISABLED;
inline constexpr Color SECTION_TEXT     = tokens::TEXT_HEADER;

// Background / panel aliases.
inline constexpr Color BG_DARK          = tokens::SURFACE_INK;       // dark backdrop
inline constexpr Color PANEL_BG         = tokens::SURFACE_PARCHMENT; // primary panel face

// Primary button — bronze action.
inline constexpr Color BTN_NORMAL       = tokens::BRONZE_BASE;
inline constexpr Color BTN_HOVER        = tokens::BRONZE_LIGHT;
inline constexpr Color BTN_PRESSED      = tokens::STATE_PRESSED;

// Selected (azure accent for "current selection" semantics).
inline constexpr Color BTN_SELECTED     = tokens::DIPLO_ALLIED;
inline constexpr Color BTN_SEL_HOVER    = {0.296f, 0.522f, 0.789f, 1.0f}; // light azure
inline constexpr Color BTN_SEL_PRESSED  = {0.198f, 0.348f, 0.526f, 1.0f}; // deep azure

// Confirm / commit (olive success).
inline constexpr Color BTN_GREEN        = tokens::STATE_SUCCESS;
inline constexpr Color BTN_GREEN_HOVER  = {0.432f, 0.654f, 0.292f, 1.0f};
inline constexpr Color BTN_GREEN_PRESS  = {0.288f, 0.436f, 0.194f, 1.0f};

// Destructive (carmine danger).
inline constexpr Color BTN_RED          = tokens::STATE_DANGER;
inline constexpr Color BTN_RED_HOVER    = {0.767f, 0.272f, 0.197f, 1.0f};
inline constexpr Color BTN_RED_PRESS    = {0.511f, 0.182f, 0.131f, 1.0f};

// Neutral grey (now parchment-dim — for cancellation / back actions).
inline constexpr Color BTN_GREY         = tokens::SURFACE_PARCHMENT_DIM;
inline constexpr Color BTN_GREY_HOVER   = tokens::SURFACE_PARCHMENT;
inline constexpr Color BTN_GREY_PRESS   = tokens::BRONZE_DARK;

} // namespace aoc::ui

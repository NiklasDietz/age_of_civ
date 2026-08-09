#pragma once

/**
 * @file MainMenuTheme.hpp
 * @brief Legacy menu palette names, redirected to the design tokens in
 *        StyleTokens.hpp.
 *
 * These are aliases only — retinting `tokens::*` retints every call site here
 * for free, which is why the header is kept rather than migrated away.
 *
 * IMPORTANT: the names are historical and describe the ORIGINAL hue
 * ("BTN_GREEN", "BTN_RED"), not what they render as now. They have been
 * regrouped into a three-tier button system, because a menu where every entry
 * is a different hue has no hierarchy — the eye has nothing to land on:
 *
 *   PRIMARY   (BTN_GREEN*)  filled brass. At most one per screen.
 *   SECONDARY (BTN_NORMAL*, BTN_GREY*) recessed surface, cream label.
 *   DANGER    (BTN_RED*)    destructive only.
 *   SELECTED  (BTN_SEL*)    "this option is active" within a group.
 *
 * Prefer the tier that matches the action's WEIGHT, not its old colour name.
 */

#include "aoc/ui/StyleTokens.hpp"

namespace aoc::ui {

// Text aliases.
inline constexpr Color GOLDEN_TEXT  = tokens::TEXT_GILT;
inline constexpr Color WHITE_TEXT   = tokens::TEXT_INK;
inline constexpr Color GREY_TEXT    = tokens::TEXT_DISABLED;
inline constexpr Color SECTION_TEXT = tokens::TEXT_HEADER;

// Background / panel aliases.
inline constexpr Color BG_DARK  = tokens::SURFACE_INK;       // dark backdrop
inline constexpr Color PANEL_BG = tokens::SURFACE_PARCHMENT; // primary panel face

/// Label colour for the filled-brass PRIMARY tier: dark ink on light metal.
/// Cream-on-brass fails contrast badly.
inline constexpr Color BTN_PRIMARY_LABEL = tokens::SURFACE_INK;

// SECONDARY tier — the default. Recessed card that lifts toward brass on hover.
inline constexpr Color BTN_NORMAL  = tokens::SURFACE_MARBLE;
inline constexpr Color BTN_HOVER   = {0.318f, 0.255f, 0.188f, 1.0f}; // #514130 warm lift
inline constexpr Color BTN_PRESSED = tokens::SURFACE_PARCHMENT_DIM;

// SELECTED — "this option is active". Brass, matching the accent language
// rather than the old azure, which read as a foreign hue on this ground.
inline constexpr Color BTN_SELECTED    = tokens::BRONZE_DARK;
inline constexpr Color BTN_SEL_HOVER   = tokens::BRONZE_BASE;
inline constexpr Color BTN_SEL_PRESSED = {0.278f, 0.212f, 0.098f, 1.0f}; // deeper brass

// PRIMARY tier — filled brass, the single call to action.
inline constexpr Color BTN_GREEN       = tokens::BRONZE_LIGHT;
inline constexpr Color BTN_GREEN_HOVER = tokens::GOLD_HIGHLIGHT;
inline constexpr Color BTN_GREEN_PRESS = tokens::BRONZE_BASE;

// DANGER — destructive. Muted at rest so it does not compete with PRIMARY.
inline constexpr Color BTN_RED       = {0.451f, 0.192f, 0.153f, 1.0f}; // #733127 muted
inline constexpr Color BTN_RED_HOVER = tokens::STATE_DANGER;
inline constexpr Color BTN_RED_PRESS = {0.541f, 0.184f, 0.125f, 1.0f};

// Neutral / back / cancel — flattest of the secondary variants.
inline constexpr Color BTN_GREY       = tokens::SURFACE_PARCHMENT;
inline constexpr Color BTN_GREY_HOVER = tokens::SURFACE_MARBLE;
inline constexpr Color BTN_GREY_PRESS = tokens::SURFACE_PARCHMENT_DIM;

} // namespace aoc::ui

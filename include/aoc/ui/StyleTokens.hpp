#pragma once

/**
 * @file StyleTokens.hpp
 * @brief Centralized design tokens for the parchment/bronze UI system.
 *
 * Source of truth for spacing, typography, shadow tiers, corner radii,
 * and the full color palette. See docs/ui/style_guide.md for the
 * rationale and component-level usage.
 *
 * sRGB float values stored — the renderer applies gamma correction at
 * sample time. Hex equivalents in the style guide.
 */

#include "aoc/ui/Widget.hpp"

namespace aoc::ui::tokens {

// ============================================================================
// Spacing scale (4 px unit)
// ============================================================================
inline constexpr float S1 = 4.0f;
inline constexpr float S2 = 8.0f;
inline constexpr float S3 = 12.0f;
inline constexpr float S4 = 16.0f;
inline constexpr float S5 = 24.0f;
inline constexpr float S6 = 32.0f;
inline constexpr float S7 = 48.0f;
inline constexpr float S8 = 64.0f;

// ============================================================================
// Border + corner
// ============================================================================
inline constexpr float BORDER_HAIR    = 1.0f;   ///< Inner stroke
inline constexpr float BORDER_RAIL    = 4.0f;   ///< Bronze rail
inline constexpr float CORNER_PANEL   = 6.0f;   ///< Standard panel corner cartouche
inline constexpr float CORNER_TOOLTIP = 2.0f;
inline constexpr float CORNER_BUTTON  = 4.0f;
inline constexpr float CORNER_PILL    = 12.0f;  ///< Resource pill

// ============================================================================
// Shadow tiers (offsetY, blur, alpha)
// ============================================================================
struct Shadow { float oy; float blur; float a; };
inline constexpr Shadow SHADOW_INSET{1.0f,  0.0f, 0.30f};
inline constexpr Shadow SHADOW_HOVER{2.0f,  4.0f, 0.25f};
inline constexpr Shadow SHADOW_MODAL{4.0f, 12.0f, 0.35f};
inline constexpr Shadow SHADOW_HERO {8.0f, 24.0f, 0.45f};

// ============================================================================
// Typography sizes (px at 1.0 dpiScale)
// ============================================================================
inline constexpr float FS_H1       = 32.0f;
inline constexpr float FS_H2       = 24.0f;
inline constexpr float FS_H3       = 20.0f;
inline constexpr float FS_H4       = 16.0f;
inline constexpr float FS_BODY     = 14.0f;
inline constexpr float FS_SMALL    = 12.0f;
inline constexpr float FS_CAPTION  = 11.0f;
inline constexpr float FS_RES_NUM  = 18.0f;
inline constexpr float FS_TAB_DATA = 13.0f;

// ============================================================================
// Color palette
// ============================================================================

// --- Surfaces ---
// Civ-6 leather UI: dark wood/leather panels with gold filigree + cream
// body text. Light parchment surfaces glared over the 3D map. Tokens
// renamed to keep call-sites stable; the SURFACE_PARCHMENT family now
// reads as "panel face" (dark) rather than literal parchment.
inline constexpr Color SURFACE_PARCHMENT     = {0.184f, 0.131f, 0.078f, 1.00f}; // #2F2114 dark walnut
inline constexpr Color SURFACE_PARCHMENT_DIM = {0.117f, 0.082f, 0.046f, 1.00f}; // #1E150C deep shadow
inline constexpr Color SURFACE_MARBLE        = {0.262f, 0.196f, 0.121f, 1.00f}; // #43321F tooltip card
inline constexpr Color SURFACE_MAHOGANY      = {0.094f, 0.062f, 0.039f, 1.00f}; // #18100A
inline constexpr Color SURFACE_INK           = {0.039f, 0.027f, 0.011f, 1.00f}; // #0A0703
inline constexpr Color SURFACE_FROST_DIM     = {0.031f, 0.027f, 0.039f, 0.78f}; // map dim under modal

// --- Bronze / gilt ---
inline constexpr Color BRONZE_LIGHT      = {0.788f, 0.639f, 0.352f, 1.00f}; // #C9A35A
inline constexpr Color BRONZE_BASE       = {0.643f, 0.486f, 0.227f, 1.00f}; // #A47C3A
inline constexpr Color BRONZE_DARK       = {0.431f, 0.313f, 0.133f, 1.00f}; // #6E5022
inline constexpr Color GOLD_HIGHLIGHT    = {0.945f, 0.835f, 0.556f, 1.00f}; // #F1D58E

// --- Text ---
// Inverted to cream tones for the dark leather surfaces above. Token
// names retained for call-site stability; "TEXT_INK" is now the cream
// body tone, "TEXT_HEADER" is bright gilt, "TEXT_GILT" is hot highlight.
inline constexpr Color TEXT_INK          = {0.886f, 0.835f, 0.717f, 1.00f}; // #E2D5B7 cream body
inline constexpr Color TEXT_HEADER       = {0.945f, 0.850f, 0.572f, 1.00f}; // #F1D992 gilt header
inline constexpr Color TEXT_GILT         = {1.000f, 0.917f, 0.643f, 1.00f}; // #FFEAA4 hot gilt
inline constexpr Color TEXT_PARCHMENT    = {0.886f, 0.835f, 0.717f, 1.00f}; // matches body
inline constexpr Color TEXT_DISABLED     = {0.529f, 0.470f, 0.376f, 1.00f}; // #877860 muted cream

// --- Resources (8 hue families, parchment-tuned) ---
inline constexpr Color RES_FOOD       = {0.360f, 0.545f, 0.243f, 1.00f}; // #5C8B3E olive
inline constexpr Color RES_PRODUCTION = {0.658f, 0.431f, 0.180f, 1.00f}; // #A86E2E terracotta
inline constexpr Color RES_GOLD       = {0.788f, 0.639f, 0.352f, 1.00f}; // #C9A35A
inline constexpr Color RES_SCIENCE    = {0.247f, 0.435f, 0.658f, 1.00f}; // #3F6FA8 azure
inline constexpr Color RES_CULTURE    = {0.545f, 0.247f, 0.545f, 1.00f}; // #8B3F8B mulberry
inline constexpr Color RES_FAITH      = {0.784f, 0.784f, 0.784f, 1.00f}; // #C8C8C8 pearl
inline constexpr Color RES_POWER      = {0.839f, 0.701f, 0.255f, 1.00f}; // #D6B341 electric
inline constexpr Color RES_TOURISM    = {0.839f, 0.482f, 0.262f, 1.00f}; // #D67B43 coral

// --- States ---
inline constexpr Color STATE_PRESSED  = {0.556f, 0.419f, 0.180f, 1.00f}; // #8E6B2E
inline constexpr Color STATE_SUCCESS  = {0.360f, 0.545f, 0.243f, 1.00f}; // #5C8B3E
inline constexpr Color STATE_WARN     = {0.839f, 0.647f, 0.235f, 1.00f}; // #D6A53C
inline constexpr Color STATE_DANGER   = {0.639f, 0.227f, 0.164f, 1.00f}; // #A33A2A

// --- Diplomatic stance ---
inline constexpr Color DIPLO_ALLIED      = {0.247f, 0.435f, 0.658f, 1.00f}; // #3F6FA8
inline constexpr Color DIPLO_FRIENDLY    = {0.360f, 0.545f, 0.243f, 1.00f}; // #5C8B3E
inline constexpr Color DIPLO_NEUTRAL     = {0.658f, 0.545f, 0.360f, 1.00f}; // #A88B5C
inline constexpr Color DIPLO_UNFRIENDLY  = {0.760f, 0.415f, 0.180f, 1.00f}; // #C26A2E
inline constexpr Color DIPLO_HOSTILE     = {0.639f, 0.227f, 0.164f, 1.00f}; // #A33A2A
inline constexpr Color DIPLO_AT_WAR      = {0.376f, 0.082f, 0.082f, 1.00f}; // #601515

} // namespace aoc::ui::tokens

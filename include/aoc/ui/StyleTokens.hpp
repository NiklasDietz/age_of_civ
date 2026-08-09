#pragma once

/**
 * @file StyleTokens.hpp
 * @brief Centralized design tokens for the parchment/bronze UI system.
 *
 * Source of truth for spacing, typography, shadow tiers, corner radii,
 * and the full color palette. See docs/ui/style_guide.md for the
 * rationale and component-level usage.
 *
 * Colors are authored in sRGB, as normalized floats: {0.788f, 0.639f, 0.352f}
 * is #C9A35A. The fragment shader converts to linear at output when the target
 * is an _SRGB format, so these values reach the display as written. Hex
 * equivalents in the comments below.
 */

// Color only -- including Widget.hpp here would be circular, since Widget.hpp
// includes this header to default its *Data structs to these tokens.
#include "aoc/ui/Color.hpp"

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
inline constexpr float BORDER_HAIR    = 1.0f; ///< Inner stroke
inline constexpr float BORDER_RAIL    = 4.0f; ///< Bronze rail
inline constexpr float CORNER_PANEL   = 6.0f; ///< Standard panel corner cartouche
inline constexpr float CORNER_TOOLTIP = 2.0f;
inline constexpr float CORNER_BUTTON  = 4.0f;
inline constexpr float CORNER_PILL    = 12.0f; ///< Resource pill

// ============================================================================
// Shadow tiers (offsetY, blur, alpha)
// ============================================================================
struct Shadow {
    float oy;
    float blur;
    float a;
};
inline constexpr Shadow SHADOW_INSET{1.0f, 0.0f, 0.30f};
inline constexpr Shadow SHADOW_HOVER{2.0f, 4.0f, 0.25f};
inline constexpr Shadow SHADOW_MODAL{4.0f, 12.0f, 0.35f};
inline constexpr Shadow SHADOW_HERO{8.0f, 24.0f, 0.45f};

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
// "Leather & Ochre": warm dark brown surfaces carrying ochre/brass ornament and
// cream text. The map is bright and warm (#D1BF80 desert, #A6B359 plains,
// #4CA64C grass -- see map/Terrain.hpp), so these surfaces stay well below the
// terrain in value: separation comes from the value gap plus the BRONZE_BASE
// border and the modal scrim, not from hue contrast.
//
// (Superseded the earlier cool near-black "Obsidian" scheme, which read as
// plain and unwarm against the terrain.)
//
// Token names are historical and deliberately unchanged -- ~600 call sites
// across 20 files depend on them. Read SURFACE_PARCHMENT as "panel face".
inline constexpr Color SURFACE_PARCHMENT = {0.180f, 0.141f, 0.102f, 1.00f}; // #2E241A panel face
inline constexpr Color SURFACE_PARCHMENT_DIM = {0.129f, 0.102f, 0.071f, 1.00f}; // #211A12 sunken
inline constexpr Color SURFACE_MARBLE    = {0.243f, 0.192f, 0.141f, 1.00f}; // #3E3124 raised card
inline constexpr Color SURFACE_MAHOGANY  = {0.102f, 0.078f, 0.055f, 1.00f}; // #1A140E app bg
inline constexpr Color SURFACE_INK       = {0.071f, 0.055f, 0.039f, 1.00f}; // #120E0A void
inline constexpr Color SURFACE_FROST_DIM = {0.055f, 0.043f, 0.031f, 0.80f}; // scrim under modal

// --- Brass / gilt ---
// The hero accent. BRONZE_LIGHT (#C9A35A) is the ochre the whole scheme hangs
// on and is deliberately identical to RES_GOLD, so gold yield and UI accent
// are the same hue.
inline constexpr Color BRONZE_LIGHT   = {0.788f, 0.639f, 0.353f, 1.00f}; // #C9A35A OCHRE
inline constexpr Color BRONZE_BASE    = {0.541f, 0.420f, 0.200f, 1.00f}; // #8A6B33 brass
inline constexpr Color BRONZE_DARK    = {0.361f, 0.275f, 0.125f, 1.00f}; // #5C4620 brass deep
inline constexpr Color GOLD_HIGHLIGHT = {0.961f, 0.890f, 0.690f, 1.00f}; // #F5E3B0 gilt

// --- Text ---
// Cream on near-black. Token names retained for call-site stability:
// TEXT_INK is the body tone, TEXT_HEADER the brass heading, TEXT_GILT the
// hot highlight.
inline constexpr Color TEXT_INK       = {0.941f, 0.918f, 0.863f, 1.00f}; // #F0EADC cream body
inline constexpr Color TEXT_HEADER    = {0.878f, 0.745f, 0.486f, 1.00f}; // #E0BE7C brass heading
inline constexpr Color TEXT_GILT      = {0.961f, 0.890f, 0.690f, 1.00f}; // #F5E3B0 gilt
inline constexpr Color TEXT_PARCHMENT = {0.941f, 0.918f, 0.863f, 1.00f}; // matches body
inline constexpr Color TEXT_DISABLED  = {0.490f, 0.471f, 0.424f, 1.00f}; // #7D786C muted

// --- Resources (8 hue families) ---
// Chroma lifted relative to the old walnut scheme: these now sit on a
// near-black ground, where the previous darker values lost separation.
inline constexpr Color RES_FOOD       = {0.435f, 0.659f, 0.290f, 1.00f}; // #6FA84A leaf
inline constexpr Color RES_PRODUCTION = {0.776f, 0.498f, 0.208f, 1.00f}; // #C67F35 terracotta
inline constexpr Color RES_GOLD       = {0.788f, 0.639f, 0.353f, 1.00f}; // #C9A35A (== OCHRE)
inline constexpr Color RES_SCIENCE    = {0.306f, 0.545f, 0.769f, 1.00f}; // #4E8BC4 azure
inline constexpr Color RES_CULTURE    = {0.651f, 0.361f, 0.651f, 1.00f}; // #A65CA6 mulberry
inline constexpr Color RES_FAITH      = {0.847f, 0.863f, 0.878f, 1.00f}; // #D8DCE0 pearl
inline constexpr Color RES_POWER      = {0.898f, 0.761f, 0.278f, 1.00f}; // #E5C247 electric
inline constexpr Color RES_TOURISM    = {0.878f, 0.541f, 0.306f, 1.00f}; // #E08A4E coral

// --- States ---
inline constexpr Color STATE_PRESSED = {0.361f, 0.275f, 0.125f, 1.00f}; // #5C4620 brass deep
inline constexpr Color STATE_SUCCESS = {0.435f, 0.659f, 0.290f, 1.00f}; // #6FA84A
inline constexpr Color STATE_WARN    = {0.878f, 0.663f, 0.235f, 1.00f}; // #E0A93C
inline constexpr Color STATE_DANGER  = {0.769f, 0.271f, 0.184f, 1.00f}; // #C4452F

// --- Diplomatic stance ---
// A deliberate warm-to-cool ladder so stance reads at a glance, brightened to
// hold against the near-black ground.
inline constexpr Color DIPLO_ALLIED     = {0.306f, 0.545f, 0.769f, 1.00f}; // #4E8BC4
inline constexpr Color DIPLO_FRIENDLY   = {0.435f, 0.659f, 0.290f, 1.00f}; // #6FA84A
inline constexpr Color DIPLO_NEUTRAL    = {0.690f, 0.604f, 0.447f, 1.00f}; // #B09A72
inline constexpr Color DIPLO_UNFRIENDLY = {0.831f, 0.475f, 0.227f, 1.00f}; // #D4793A
inline constexpr Color DIPLO_HOSTILE    = {0.769f, 0.271f, 0.184f, 1.00f}; // #C4452F
inline constexpr Color DIPLO_AT_WAR     = {0.478f, 0.114f, 0.114f, 1.00f}; // #7A1D1D

} // namespace aoc::ui::tokens

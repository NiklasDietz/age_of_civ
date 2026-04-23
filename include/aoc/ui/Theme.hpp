#pragma once

/**
 * @file Theme.hpp
 * @brief Central registry of UI-layout constants + DPI scaling.
 *
 * Before this file, every screen carried its own magic 700×520,
 * 420×450, 130×40, padding {10,12,10,12} literals. Changing the visual
 * language meant grepping across eleven files; supporting a DPI scale
 * was impossible without finding every hardcoded pixel value.
 *
 * `Theme` now owns:
 *   - the current viewport dimensions (set once per frame from the
 *     framebuffer size);
 *   - a DPI scale factor (1.0 = 96 DPI reference; GLFW content scale
 *     fills this in);
 *   - panel / dialog / button size presets that scale with DPI;
 *   - the colour palette that was previously in `MainMenuTheme.hpp`
 *     (re-exported for backward compatibility).
 *
 * Screens call `theme.scaled(pixels)` for any size literal so DPI
 * scaling just works. UIManager can hand out a singleton, or callers
 * can hold a reference.
 */

#include "aoc/ui/Widget.hpp"
#include "aoc/ui/MainMenuTheme.hpp"

#include <algorithm>
#include <cstdint>

namespace aoc::ui {

/// Player-color palette variant. `Default` uses the distinct saturated
/// colours tuned for pleasant contrast; `Deuteranopia` / `Protanopia`
/// swap red/green slots for blue/yellow so common colourblind types
/// can still tell factions apart.
enum class ColorScheme : uint8_t {
    Default       = 0,
    Deuteranopia  = 1,
    Protanopia    = 2,
    Tritanopia    = 3,
    HighContrast  = 4,
};

struct Theme {
    /// Current framebuffer-space viewport. Updated from Window size.
    float viewportW = 1280.0f;
    float viewportH = 720.0f;

    /// DPI scale. 1.0 = 96 DPI reference; 2.0 = HiDPI (Retina / 4K).
    /// Populated from `glfwGetWindowContentScale` in Application.
    float dpiScale = 1.0f;

    /// Active colour scheme. Read by `playerColor(id)` to select
    /// colourblind-friendly palettes when requested.
    ColorScheme colorScheme = ColorScheme::Default;

    /// Hover delay before tooltip pops. Settings menu can expose this
    /// so users can tighten or relax it. Units: frames at 60 fps.
    float tooltipDelayFrames = 5.0f;

    /// User-facing UI scale override on top of the auto-DPI value.
    /// 1.0 = no override. Settings exposes a 0.5..2.0 slider; the
    /// effective scale used by `scaled()` is `dpiScale * userScale`.
    float userScale = 1.0f;

    /// Bump this counter from any callsite that mutates the theme so
    /// observers (HUD/screen-builders) can detect a need to relayout.
    /// Cheap monotonic int — not a timestamp.
    uint32_t revision = 0;

    /// Convenience: bump revision and recompute derived flags. Use
    /// after toggling colorScheme or dpiScale.
    void bumpRevision() { ++this->revision; }

    // ------------------------------------------------------------------
    // Presets. Use `scaled(x)` everywhere a literal pixel is needed so
    // DPI scaling is honoured without per-callsite math.
    // ------------------------------------------------------------------

    /// Multiply any pixel literal by the current DPI scale times the
    /// user override. `dpiScale` comes from GLFW; `userScale` is a
    /// settings slider (0.5..2.0).
    [[nodiscard]] constexpr float scaled(float pixels) const {
        return pixels * this->dpiScale * this->userScale;
    }

    /// Preferred width/height for a large modal screen (production,
    /// tech, city detail). Automatically clamped to viewport.
    [[nodiscard]] float modalW() const {
        return std::min(this->viewportW - this->scaled(40.0f),
                        this->scaled(860.0f));
    }
    [[nodiscard]] float modalH() const {
        return std::min(this->viewportH - this->scaled(40.0f),
                        this->scaled(640.0f));
    }

    /// Preferred width/height for a medium dialog (settings, confirm).
    [[nodiscard]] float dialogW() const { return this->scaled(420.0f); }
    [[nodiscard]] float dialogH() const { return this->scaled(380.0f); }

    /// Standard button height.
    [[nodiscard]] float buttonH() const { return this->scaled(28.0f); }

    /// Padding inside panels (uniform).
    [[nodiscard]] float panelPadding() const { return this->scaled(12.0f); }

    /// Vertical spacing between siblings inside a panel.
    [[nodiscard]] float childSpacing() const { return this->scaled(6.0f); }

    /// Standard corner radius for panels.
    [[nodiscard]] float cornerRadius() const { return this->scaled(6.0f); }

    /// Standard font sizes. Titles, labels, small text.
    [[nodiscard]] float fontLarge() const { return this->scaled(22.0f); }
    [[nodiscard]] float fontMedium() const { return this->scaled(14.0f); }
    [[nodiscard]] float fontSmall() const { return this->scaled(11.0f); }

    /// Look up a player colour swatch under the active colour scheme.
    /// 8-slot palettes cover the 16-player cap by wrapping (matches
    /// the existing UnitRenderer / Minimap pattern).
    [[nodiscard]] Color playerColor(uint8_t playerId) const {
        // Default palette: saturated, distinct hues.
        static constexpr Color DEFAULT[8] = {
            {0.20f, 0.55f, 0.85f, 1.0f},  // blue
            {0.85f, 0.20f, 0.20f, 1.0f},  // red
            {0.20f, 0.70f, 0.35f, 1.0f},  // green
            {0.95f, 0.75f, 0.20f, 1.0f},  // gold
            {0.70f, 0.30f, 0.80f, 1.0f},  // purple
            {0.85f, 0.50f, 0.20f, 1.0f},  // orange
            {0.20f, 0.80f, 0.85f, 1.0f},  // cyan
            {0.95f, 0.60f, 0.75f, 1.0f},  // pink
        };
        // Deuteranopia (no red-green): swap red/green for blue/yellow.
        static constexpr Color DEUT[8] = {
            {0.20f, 0.55f, 0.85f, 1.0f},
            {0.85f, 0.65f, 0.10f, 1.0f},
            {0.10f, 0.40f, 0.95f, 1.0f},
            {0.95f, 0.90f, 0.20f, 1.0f},
            {0.70f, 0.30f, 0.80f, 1.0f},
            {0.65f, 0.25f, 0.15f, 1.0f},
            {0.20f, 0.80f, 0.85f, 1.0f},
            {0.95f, 0.60f, 0.75f, 1.0f},
        };
        // High contrast: near-monochrome shapes rely on luminance not
        // hue. Each slot is a distinct lightness tier.
        static constexpr Color HC[8] = {
            {0.95f, 0.95f, 0.95f, 1.0f},
            {0.10f, 0.10f, 0.10f, 1.0f},
            {0.70f, 0.70f, 0.70f, 1.0f},
            {0.30f, 0.30f, 0.30f, 1.0f},
            {0.50f, 0.50f, 0.50f, 1.0f},
            {0.85f, 0.85f, 0.40f, 1.0f},
            {0.40f, 0.40f, 0.85f, 1.0f},
            {0.85f, 0.40f, 0.85f, 1.0f},
        };
        const std::size_t idx = static_cast<std::size_t>(playerId) % 8;
        switch (this->colorScheme) {
            case ColorScheme::Deuteranopia:
            case ColorScheme::Protanopia:
            case ColorScheme::Tritanopia:
                return DEUT[idx];
            case ColorScheme::HighContrast:
                return HC[idx];
            case ColorScheme::Default:
            default:
                return DEFAULT[idx];
        }
    }
};

// -------------------------------------------------------------------------
// Global accessor. A single Theme instance shared across UI callers. The
// Application updates its viewport / DPI fields on resize + DPI-change
// events; screens read without locking (single-threaded UI invariant).
// -------------------------------------------------------------------------

[[nodiscard]] Theme& theme();

} // namespace aoc::ui

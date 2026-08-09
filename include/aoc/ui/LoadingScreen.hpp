#pragma once

/**
 * @file LoadingScreen.hpp
 * @brief Full-screen progress overlay for long-running operations
 *        (map generation, save/load, initial scenario setup).
 *
 * Callers supply a background-thread job that publishes progress
 * 0..1 via `setProgress` and an optional status string via
 * `setStatus`. The loading screen reads both every frame and renders
 * a centred panel with a progress bar. When the job completes, the
 * caller calls `finish` which tears the overlay down.
 *
 * The screen is an `IScreen` so it participates in the registry and
 * reflows on resize. It is non-modal in the sense of blocking input:
 * registry queries still return `isOpen == true`, which is exactly
 * what callers want — game input is blocked while the worker runs.
 */

#include "aoc/ui/IScreen.hpp"
#include "aoc/ui/Widget.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace aoc::ui {

class UIManager;

class LoadingScreen : public IScreen {
public:
    /// Build the overlay. Title shows above the progress bar.
    void open(UIManager& ui, const std::string& title);

    /// Tear the overlay down. Safe to call when not open.
    void close(UIManager& ui) override;

    [[nodiscard]] bool isOpen() const override { return this->m_isOpen; }
    void onResize(UIManager& ui, float width, float height) override;

    /// Thread-safe progress setter — the worker thread may call this.
    void setProgress(float fraction) {
        this->m_progress.store(fraction, std::memory_order_relaxed);
    }

    /// Thread-safe status setter. Protected by a small mutex on the
    /// string. Cheap: only written when progress phase changes.
    void setStatus(const std::string& status);

    /// Called once per frame from the UI thread to refresh the
    /// progress bar + status label from the atomic/locked values.
    void tick(UIManager& ui);

    /// Choose which tip is shown first. Pass the map seed so consecutive
    /// games don't open on the same tip. Call before `open()`.
    void setTipSeed(uint32_t seed) { this->m_tipIndex = seed; }

private:
    /// Rotating gameplay hints shown under the progress bar. Generation is
    /// mostly a wait, so it is the natural place to teach mechanics.
    static const std::array<const char*, 14> TIPS;

    /// "Tip: <current entry>", wrapping `m_tipIndex` into TIPS.
    [[nodiscard]] std::string currentTipText() const;

    /// Wall-clock seconds each tip stays on screen.
    static constexpr double TIP_INTERVAL_SEC = 4.0;

    bool m_isOpen          = false;
    WidgetId m_rootPanel   = INVALID_WIDGET;
    WidgetId m_progressBar = INVALID_WIDGET;
    WidgetId m_statusLabel = INVALID_WIDGET;
    WidgetId m_titleLabel  = INVALID_WIDGET;
    WidgetId m_tipLabel    = INVALID_WIDGET;
    std::string m_title;

    std::atomic<float> m_progress{0.0f};
    std::string m_status;

    /// Index into TIPS; advanced by tick() on a wall-clock timer so tips
    /// rotate even while a single long generation phase is running.
    uint32_t m_tipIndex     = 0;
    double m_lastTipSwapSec = 0.0;
    // Manual mutex avoided — string writes are coarse (per-phase) and
    // the UI-thread read tolerates the occasional torn update.
};

} // namespace aoc::ui

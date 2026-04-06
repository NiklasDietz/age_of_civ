#pragma once

/**
 * @file Tutorial.hpp
 * @brief Interactive tutorial overlay guiding new players.
 *
 * Shows 10 sequential steps as a semi-transparent overlay at the bottom
 * of the screen with "Next" and "Skip Tutorial" buttons.
 */

#include <cstdint>
#include <string_view>

namespace vulkan_app::renderer {
class Renderer2D;
}

namespace aoc::ui {

struct TutorialStep {
    std::string_view message;
    std::string_view highlightAction;  ///< Which action to highlight
};

class TutorialManager {
public:
    /// Start the tutorial from step 0.
    void start();

    /// Advance to the next step. If at the last step, ends the tutorial.
    void advance();

    /// Skip the tutorial entirely.
    void skip();

    /// Render the tutorial overlay at the bottom of the screen.
    void render(vulkan_app::renderer::Renderer2D& renderer2d,
                float screenW, float screenH, float pixelScale) const;

    /// Returns true if the tutorial is currently active.
    [[nodiscard]] bool isActive() const;

    /// Current step index (0-based).
    [[nodiscard]] int32_t currentStep() const;

private:
    bool m_active = false;
    int32_t m_step = 0;
    static constexpr int32_t TOTAL_STEPS = 10;
};

} // namespace aoc::ui

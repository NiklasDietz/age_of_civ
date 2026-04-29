/**
 * @file Tutorial.cpp
 * @brief Interactive tutorial system implementation.
 */

#include "aoc/ui/Tutorial.hpp"
#include "aoc/ui/BitmapFont.hpp"
#include "aoc/ui/Widget.hpp"
#include "aoc/ui/StyleTokens.hpp"
#include "aoc/core/Log.hpp"

#include <renderer/Renderer2D.hpp>

#include <array>

namespace aoc::ui {

namespace {

constexpr std::array<TutorialStep, 10> TUTORIAL_STEPS = {{
    {"Welcome! Right-click drag to pan the camera.", "pan"},
    {"Scroll to zoom in and out.", "zoom"},
    {"Click your Settler unit to select it.", "select_settler"},
    {"Right-click on a tile to move your Settler there.", "move_settler"},
    {"Right-click on the Settler's tile to found a city.", "found_city"},
    {"Click your city to see its details.", "select_city"},
    {"Press P to open the production menu and build a Warrior.", "production"},
    {"Press T to open the technology tree and choose research.", "tech_tree"},
    {"Use your Warrior to explore and defend.", "explore"},
    {"Press End Turn to advance. Good luck!", "end_turn"},
}};

} // anonymous namespace

void TutorialManager::start() {
    this->m_active = true;
    this->m_step = 0;
    LOG_INFO("Tutorial started");
}

void TutorialManager::advance() {
    if (!this->m_active) {
        return;
    }
    ++this->m_step;
    if (this->m_step >= TOTAL_STEPS) {
        this->m_active = false;
        LOG_INFO("Tutorial completed");
    } else {
        LOG_INFO("Tutorial advanced to step %d", this->m_step);
    }
}

void TutorialManager::skip() {
    this->m_active = false;
    LOG_INFO("Tutorial skipped at step %d", this->m_step);
}

void TutorialManager::render(vulkan_app::renderer::Renderer2D& renderer2d,
                               float screenW, float screenH,
                               float pixelScale) const {
    if (!this->m_active || this->m_step >= TOTAL_STEPS) {
        return;
    }

    const TutorialStep& step = TUTORIAL_STEPS[static_cast<std::size_t>(this->m_step)];

    // Semi-transparent overlay at the bottom of the screen
    constexpr float OVERLAY_H = 80.0f;
    constexpr float PADDING   = 12.0f;
    constexpr float FONT_SIZE = 14.0f;
    constexpr float BTN_W     = 80.0f;
    constexpr float BTN_H     = 26.0f;

    const float overlayX = 0.0f;
    const float overlayY = (screenH - OVERLAY_H) * pixelScale;
    const float overlayW = screenW * pixelScale;
    const float overlayH = OVERLAY_H * pixelScale;

    // Background
    renderer2d.drawFilledRect(overlayX, overlayY, overlayW, overlayH,
                               tokens::SURFACE_INK.r, tokens::SURFACE_INK.g,
                               tokens::SURFACE_INK.b, 0.85f);

    // Step counter
    std::string stepText = "Step " + std::to_string(this->m_step + 1) + "/" +
                           std::to_string(TOTAL_STEPS);
    BitmapFont::drawText(renderer2d, stepText,
                          overlayX + PADDING * pixelScale,
                          overlayY + PADDING * 0.5f * pixelScale,
                          10.0f,
                          tokens::TEXT_DISABLED,
                          pixelScale);

    // Tutorial message
    BitmapFont::drawText(renderer2d, std::string(step.message),
                          overlayX + PADDING * pixelScale,
                          overlayY + PADDING * 2.0f * pixelScale,
                          FONT_SIZE,
                          tokens::TEXT_GILT,
                          pixelScale);

    // "Next" button indicator
    const float nextX = (screenW - BTN_W * 2.0f - PADDING) * pixelScale;
    const float nextY = overlayY + (OVERLAY_H - BTN_H - PADDING * 0.5f) * pixelScale;
    renderer2d.drawFilledRect(nextX, nextY, BTN_W * pixelScale, BTN_H * pixelScale,
                               tokens::STATE_SUCCESS.r, tokens::STATE_SUCCESS.g,
                               tokens::STATE_SUCCESS.b, 0.9f);
    BitmapFont::drawText(renderer2d, "Next",
                          nextX + 20.0f * pixelScale,
                          nextY + 6.0f * pixelScale,
                          12.0f,
                          tokens::TEXT_PARCHMENT,
                          pixelScale);

    // "Skip" button indicator
    const float skipX = nextX + (BTN_W + PADDING * 0.5f) * pixelScale;
    renderer2d.drawFilledRect(skipX, nextY, BTN_W * pixelScale, BTN_H * pixelScale,
                               tokens::STATE_DANGER.r, tokens::STATE_DANGER.g,
                               tokens::STATE_DANGER.b, 0.9f);
    BitmapFont::drawText(renderer2d, "Skip",
                          skipX + 20.0f * pixelScale,
                          nextY + 6.0f * pixelScale,
                          12.0f,
                          tokens::TEXT_PARCHMENT,
                          pixelScale);
}

bool TutorialManager::isActive() const {
    return this->m_active;
}

int32_t TutorialManager::currentStep() const {
    return this->m_step;
}

} // namespace aoc::ui

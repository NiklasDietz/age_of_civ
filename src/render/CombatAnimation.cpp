/**
 * @file CombatAnimation.cpp
 * @brief Combat animation rendering: melee lunges and ranged projectiles.
 */

#include "aoc/render/CombatAnimation.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/core/Log.hpp"

#include <renderer/Renderer2D.hpp>

#include <algorithm>
#include <cmath>

namespace aoc::render {

void CombatAnimator::startAnimation(hex::AxialCoord attacker,
                                     hex::AxialCoord defender,
                                     bool ranged) {
    CombatAnimEvent event{};
    event.attackerPos = attacker;
    event.defenderPos = defender;
    event.progress    = 0.0f;
    event.duration    = ranged ? 0.6f : 0.4f;
    event.isRanged    = ranged;
    this->m_animations.push_back(event);
}

void CombatAnimator::update(float deltaTime) {
    for (CombatAnimEvent& anim : this->m_animations) {
        anim.progress += deltaTime / anim.duration;
    }

    // Remove completed animations
    this->m_animations.erase(
        std::remove_if(this->m_animations.begin(), this->m_animations.end(),
            [](const CombatAnimEvent& anim) { return anim.progress >= 1.0f; }),
        this->m_animations.end());
}

void CombatAnimator::render(vulkan_app::renderer::Renderer2D& renderer2d,
                             float hexSize) const {
    for (const CombatAnimEvent& anim : this->m_animations) {
        float atkX = 0.0f, atkY = 0.0f;
        float defX = 0.0f, defY = 0.0f;
        hex::axialToPixel(anim.attackerPos, hexSize, atkX, atkY);
        hex::axialToPixel(anim.defenderPos, hexSize, defX, defY);

        const float t = std::min(anim.progress, 1.0f);

        if (anim.isRanged) {
            // Ranged: draw a projectile line moving from attacker to defender
            const float projX = atkX + (defX - atkX) * t;
            const float projY = atkY + (defY - atkY) * t;

            // Projectile trail
            const float trailT = std::max(0.0f, t - 0.15f);
            const float trailX = atkX + (defX - atkX) * trailT;
            const float trailY = atkY + (defY - atkY) * trailT;

            renderer2d.drawLine(trailX, trailY, projX, projY,
                                2.5f, 1.0f, 0.8f, 0.2f, 0.9f);

            // Projectile head
            renderer2d.drawFilledCircle(projX, projY, hexSize * 0.06f,
                                        1.0f, 1.0f, 0.4f, 1.0f);

            // Impact flash at defender when nearing completion
            if (t > 0.85f) {
                const float flashAlpha = (t - 0.85f) / 0.15f;
                renderer2d.drawFilledCircle(defX, defY, hexSize * 0.25f * flashAlpha,
                                            1.0f, 0.5f, 0.1f, 1.0f - flashAlpha);
            }
        } else {
            // Melee: attacker "lunges" toward defender
            const float lungeT = t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f;
            const float lungeX = atkX + (defX - atkX) * lungeT * 0.3f;
            const float lungeY = atkY + (defY - atkY) * lungeT * 0.3f;

            // Draw lunge indicator
            renderer2d.drawFilledCircle(lungeX, lungeY, hexSize * 0.12f,
                                        1.0f, 0.3f, 0.1f, 0.8f);

            // Impact flash at defender at peak
            if (t > 0.4f && t < 0.7f) {
                const float flashIntensity = 1.0f - std::abs(t - 0.55f) / 0.15f;
                renderer2d.drawFilledCircle(defX, defY, hexSize * 0.30f * flashIntensity,
                                            1.0f, 0.4f, 0.1f, flashIntensity * 0.9f);
            }
        }
    }
}

bool CombatAnimator::isPlaying() const {
    return !this->m_animations.empty();
}

} // namespace aoc::render

#pragma once

/**
 * @file CombatAnimation.hpp
 * @brief Visual combat animations: melee lunges and ranged projectiles.
 */

#include "aoc/map/HexCoord.hpp"

#include <cstdint>
#include <vector>

namespace vulkan_app::renderer {
class Renderer2D;
}

namespace aoc::render {

struct CombatAnimEvent {
    hex::AxialCoord attackerPos;
    hex::AxialCoord defenderPos;
    float progress = 0.0f;   ///< 0 to 1
    float duration = 0.5f;   ///< Total animation time in seconds
    bool isRanged  = false;
};

class CombatAnimator {
public:
    /// Start a new combat animation between two hex positions.
    void startAnimation(hex::AxialCoord attacker, hex::AxialCoord defender, bool ranged);

    /// Advance all active animations by deltaTime seconds.
    void update(float deltaTime);

    /// Render all active combat animations.
    void render(vulkan_app::renderer::Renderer2D& renderer2d, float hexSize) const;

    /// Returns true if any animation is currently playing.
    [[nodiscard]] bool isPlaying() const;

private:
    std::vector<CombatAnimEvent> m_animations;
};

} // namespace aoc::render

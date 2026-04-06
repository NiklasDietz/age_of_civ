#pragma once

/**
 * @file Particles.hpp
 * @brief Simple particle system for visual effects.
 *
 * Used for combat impacts (red), city founding (gold), tech completion (blue).
 */

#include <cstdint>
#include <vector>

namespace vulkan_app::renderer {
class Renderer2D;
}

namespace aoc::render {

struct Particle {
    float x  = 0.0f;
    float y  = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float life    = 0.0f;   ///< Seconds remaining
    float maxLife = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    float size = 2.0f;
};

class ParticleSystem {
public:
    /// Emit a burst of particles at the given world position.
    void emit(float x, float y, int32_t count, float r, float g, float b);

    /// Update all particles (apply velocity, reduce life, remove dead).
    void update(float deltaTime);

    /// Render all live particles.
    void render(vulkan_app::renderer::Renderer2D& renderer2d) const;

    /// Number of currently live particles.
    [[nodiscard]] std::size_t count() const { return this->m_particles.size(); }

private:
    std::vector<Particle> m_particles;
    uint64_t m_seed = 42;

    /// Simple internal PRNG for particle randomness (no dependency on game RNG).
    [[nodiscard]] float randomFloat();
};

} // namespace aoc::render

/**
 * @file Particles.cpp
 * @brief Simple particle system implementation.
 */

#include "aoc/render/Particles.hpp"

#include <renderer/Renderer2D.hpp>

#include <algorithm>
#include <cmath>

namespace aoc::render {

float ParticleSystem::randomFloat() {
    // xorshift64 for simple particle randomness
    this->m_seed ^= this->m_seed << 13;
    this->m_seed ^= this->m_seed >> 7;
    this->m_seed ^= this->m_seed << 17;
    return static_cast<float>(this->m_seed & 0xFFFFFF) / 16777216.0f;
}

void ParticleSystem::emit(float x, float y, int32_t count,
                           float r, float g, float b) {
    constexpr float SPEED = 40.0f;
    constexpr float LIFE_MIN = 0.3f;
    constexpr float LIFE_MAX = 0.8f;
    constexpr float PARTICLE_SIZE_MIN = 1.0f;
    constexpr float PARTICLE_SIZE_MAX = 3.0f;
    constexpr float PI2 = 6.2831853f;

    for (int32_t i = 0; i < count; ++i) {
        Particle p{};
        p.x = x;
        p.y = y;

        const float angle = this->randomFloat() * PI2;
        const float speed = SPEED * (0.3f + this->randomFloat() * 0.7f);
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed;

        p.life    = LIFE_MIN + this->randomFloat() * (LIFE_MAX - LIFE_MIN);
        p.maxLife = p.life;
        p.r = r;
        p.g = g;
        p.b = b;
        p.a = 1.0f;
        p.size = PARTICLE_SIZE_MIN + this->randomFloat() * (PARTICLE_SIZE_MAX - PARTICLE_SIZE_MIN);

        this->m_particles.push_back(p);
    }
}

void ParticleSystem::update(float deltaTime) {
    // Frame-rate-independent exponential drag. Old code did
    //   v *= (1 - 2*dt)
    // which only matched the design at 60 FPS (where it factored
    // 0.96667 per frame, i.e. v retained 0.1311 of its value over one
    // second). Switching to v *= pow(c, dt) with c = 0.135 reproduces
    // that 60 FPS rate exactly while staying correct at any dt.
    constexpr float DAMPING_PER_SECOND = 0.135f;
    const float dampingFactor = std::pow(DAMPING_PER_SECOND, deltaTime);

    for (Particle& p : this->m_particles) {
        p.x += p.vx * deltaTime;
        p.y += p.vy * deltaTime;
        p.life -= deltaTime;

        // Fade out as life decreases
        const float lifeRatio = std::max(0.0f, p.life / p.maxLife);
        p.a = lifeRatio;

        // Slow down over time (frame-rate independent)
        p.vx *= dampingFactor;
        p.vy *= dampingFactor;
    }

    // Remove dead particles
    this->m_particles.erase(
        std::remove_if(this->m_particles.begin(), this->m_particles.end(),
            [](const Particle& p) { return p.life <= 0.0f; }),
        this->m_particles.end());
}

void ParticleSystem::render(vulkan_app::renderer::Renderer2D& renderer2d) const {
    for (const Particle& p : this->m_particles) {
        renderer2d.drawFilledCircle(p.x, p.y, p.size,
                                     p.r, p.g, p.b, p.a);
    }
}

} // namespace aoc::render

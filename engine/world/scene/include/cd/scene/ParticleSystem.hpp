// =============================================================================
// CHROMODYNAMIC — cd/scene/ParticleSystem.hpp
// Phase 19.E / Wave 180 — header-only fixed-pool particle emitter.
//
// Pool-allocated particles with per-particle pos/vel/lifetime. The
// `tick(dt)` call ages every particle, recycles dead slots into the
// free list, and lets the caller spawn new particles via `spawn()`.
//
// Marathon scope: integration-only; no rendering path here (that
// belongs to a future PSO + per-instance vertex stream). The
// primitive's value is the pool layout + lifetime semantics that
// downstream tooling can build on.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <cstdint>
#include <vector>

namespace cd::scene
{

struct Particle
{
    cd::math::Vec3f position {};
    cd::math::Vec3f velocity {};
    float age { 0.0F };       // seconds since spawn
    float lifetime { 0.0F };  // seconds; particle dies when age >= lifetime
    bool alive { false };
};

class ParticleSystem
{
public:
    explicit ParticleSystem(std::size_t capacity = 1024)
        : pool_(capacity)
    {
        for (std::size_t i = 0; i < pool_.size(); ++i) free_list_.push_back(i);
    }

    /// Spawn a particle at `pos` with `vel` and a fixed lifetime.
    /// Returns false when the pool is exhausted.
    bool spawn(const cd::math::Vec3f& pos, const cd::math::Vec3f& vel, float lifetime)
    {
        if (free_list_.empty()) return false;
        const auto idx = free_list_.back();
        free_list_.pop_back();
        Particle& p = pool_[idx];
        p.position = pos;
        p.velocity = vel;
        p.age = 0.0F;
        p.lifetime = lifetime;
        p.alive = true;
        ++live_count_;
        return true;
    }

    /// Step every particle by `dt` seconds. Constant-acceleration
    /// integration: position += velocity*dt + 0.5*gravity*dt^2,
    /// velocity += gravity*dt. Default gravity = 0.
    void tick(float dt, const cd::math::Vec3f& gravity = cd::math::Vec3f { 0, 0, 0 })
    {
        for (std::size_t i = 0; i < pool_.size(); ++i)
        {
            Particle& p = pool_[i];
            if (!p.alive) continue;
            p.age += dt;
            if (p.age >= p.lifetime)
            {
                p.alive = false;
                free_list_.push_back(i);
                --live_count_;
                continue;
            }
            p.position.x += p.velocity.x * dt + 0.5F * gravity.x * dt * dt;
            p.position.y += p.velocity.y * dt + 0.5F * gravity.y * dt * dt;
            p.position.z += p.velocity.z * dt + 0.5F * gravity.z * dt * dt;
            p.velocity.x += gravity.x * dt;
            p.velocity.y += gravity.y * dt;
            p.velocity.z += gravity.z * dt;
        }
    }

    [[nodiscard]] std::size_t live_count() const noexcept { return live_count_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return pool_.size(); }
    [[nodiscard]] const std::vector<Particle>& particles() const noexcept { return pool_; }

private:
    std::vector<Particle> pool_;
    std::vector<std::size_t> free_list_;
    std::size_t live_count_ { 0 };
};

}  // namespace cd::scene

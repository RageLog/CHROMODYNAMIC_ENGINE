// =============================================================================
// CHROMODYNAMIC — cd/world2d/ParticleSystem2D.hpp
// Milestone 3 — High-Efficiency 2D Particle System with Zero-Allocation Pool,
// Point/Circle/Box/Cone Emitter Shapes, Velocity/Gravity/Color/Size Ramps,
// and Canvas2D Batching.
// =============================================================================
#pragma once

#include <cd/math/Random.hpp>
#include <cd/math/Vector.hpp>
#include <cd/render2d/Camera2D.hpp>
#include <cd/render2d/SpriteBatch2D.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cd::world2d
{

enum class ParticleEmitterShape2D : std::uint8_t
{
    kPoint  = 0,
    kCircle = 1,
    kBox    = 2,
    kCone   = 3,
};

using EmitterShape2D = ParticleEmitterShape2D;

enum class ParticleRenderStyle2D : std::uint8_t
{
    kSolidQuad    = 0,
    kTexturedQuad = 1,
    kRadialGlow   = 2,
};

struct ParticleEmitterConfig2D
{
    ParticleEmitterShape2D shape { ParticleEmitterShape2D::kPoint };
    ParticleRenderStyle2D  style { ParticleRenderStyle2D::kSolidQuad };

    // Shape dimensions
    float           shape_radius { 0.25F };
    cd::math::Vec2f shape_box_extents { 0.5F, 0.5F };
    float           cone_angle_rad { 1.5707963F };    // Upward (+Y)
    float           cone_spread_rad { 0.5235988F };   // +-30 degrees

    // Emission dynamics
    float           emission_rate { 20.0F };          // Particles per second (continuous)
    std::uint32_t   burst_count { 16U };

    // Lifetime
    float           lifetime_min { 0.25F };
    float           lifetime_max { 0.65F };

    // Kinematics
    float           speed_min { 2.0F };
    float           speed_max { 5.5F };
    cd::math::Vec2f velocity_min { -1.0F, 1.0F };
    cd::math::Vec2f velocity_max { 1.0F, 3.0F };
    cd::math::Vec2f gravity { 0.0F, -9.8F };
    float           drag { 0.4F };

    // Size ramps
    cd::math::Vec2f start_size_min { 0.12F, 0.12F };
    cd::math::Vec2f start_size_max { 0.18F, 0.18F };
    cd::math::Vec2f end_size_min { 0.0F, 0.0F };
    cd::math::Vec2f end_size_max { 0.04F, 0.04F };

    // Color ramps (RGBA in 0..1)
    cd::math::Vec4f start_color { 1.0F, 0.9F, 0.3F, 1.0F };
    cd::math::Vec4f end_color { 1.0F, 0.2F, 0.1F, 0.0F };

    // Rotation
    float           angular_speed_min { -3.14159F };
    float           angular_speed_max { 3.14159F };

    // Texture atlas binding
    std::uint32_t          texture_slot { 0xFFFFFFFFU };
    cd::render2d::UvRect2D uv_rect { 0.0F, 0.0F, 1.0F, 1.0F };
};

using ParticleConfig2D = ParticleEmitterConfig2D;

struct Particle2D
{
    cd::math::Vec2f        position { 0.0F, 0.0F };
    cd::math::Vec2f        velocity { 0.0F, 0.0F };
    float                  rotation_rad { 0.0F };
    float                  angular_velocity_rad { 0.0F };
    float                  age { 0.0F };
    float                  lifetime { 1.0F };
    cd::math::Vec2f        start_size { 0.1F, 0.1F };
    cd::math::Vec2f        end_size { 0.0F, 0.0F };
    cd::math::Vec4f        start_color { 1.0F, 1.0F, 1.0F, 1.0F };
    cd::math::Vec4f        end_color { 1.0F, 1.0F, 1.0F, 0.0F };
    cd::render2d::UvRect2D uv_rect {};
    std::uint32_t          texture_slot { 0xFFFFFFFFU };
    ParticleRenderStyle2D  style { ParticleRenderStyle2D::kSolidQuad };
    bool                   alive { false };
};

namespace ParticleEmitterPresets2D
{
    [[nodiscard]] ParticleEmitterConfig2D sparkle_burst() noexcept;
    [[nodiscard]] ParticleEmitterConfig2D jump_dust() noexcept;
    [[nodiscard]] ParticleEmitterConfig2D hit_impact() noexcept;
    [[nodiscard]] ParticleEmitterConfig2D enemy_defeat() noexcept;
}

class ParticleSystem2D
{
public:
    static constexpr std::size_t kDefaultCapacity = 1024U;

    explicit ParticleSystem2D(std::size_t max_particles = kDefaultCapacity);

    void set_config(const ParticleEmitterConfig2D& config) noexcept { default_config_ = config; }
    [[nodiscard]] ParticleEmitterConfig2D& config() noexcept { return default_config_; }
    [[nodiscard]] const ParticleEmitterConfig2D& config() const noexcept { return default_config_; }

    void emit(std::size_t count);
    void emit(std::size_t count, cd::math::Vec2f emitter_pos);

    void update(
        float                          dt,
        const ParticleEmitterConfig2D* continuous_emitter = nullptr,
        cd::math::Vec2f                emitter_pos        = {});

    std::size_t spawn_burst(
        cd::math::Vec2f                emitter_pos,
        std::size_t                    count,
        const ParticleEmitterConfig2D& config);

    bool emit_particle(cd::math::Vec2f emitter_pos, const ParticleEmitterConfig2D& config);

    void emit_to_canvas(
        cd::render2d::Canvas2D& canvas,
        std::uint8_t            sort_layer = 150U,
        std::int16_t            z_index    = 0) const;

    void emit_to_canvas(
        cd::render2d::Canvas2D&             canvas,
        const cd::render2d::Camera2D&       camera,
        const cd::render2d::ViewportRect2D& viewport,
        std::uint8_t                        sort_layer = 150U,
        std::int16_t                        z_index    = 0) const;

    void clear() noexcept;

    [[nodiscard]] std::size_t active_count() const noexcept { return active_count_; }
    [[nodiscard]] std::size_t active_particle_count() const noexcept { return active_count_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return particles_.size(); }
    [[nodiscard]] bool empty() const noexcept { return active_count_ == 0U; }

    [[nodiscard]] std::span<const Particle2D> active_particles() const noexcept
    {
        return std::span<const Particle2D>(particles_.data(), active_count_);
    }

private:
    cd::math::Random         prng_ { 0x853c49e6748fea9bULL };
    ParticleEmitterConfig2D  default_config_ {};
    std::vector<Particle2D>  particles_ {};
    std::size_t              active_count_ { 0U };
    float                    emission_accumulator_ { 0.0F };
};

}  // namespace cd::world2d

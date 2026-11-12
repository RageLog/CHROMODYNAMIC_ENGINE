// =============================================================================
// CHROMODYNAMIC — cd/world2d/ParticleSystem2D.cpp
// Milestone 3 — High-Efficiency 2D Particle System Implementation.
// =============================================================================
#include <cd/world2d/ParticleSystem2D.hpp>

#include <cd/render2d/SortKey2D.hpp>

#include <algorithm>
#include <cmath>

namespace cd::world2d
{

namespace ParticleEmitterPresets2D
{

ParticleEmitterConfig2D sparkle_burst() noexcept
{
    ParticleEmitterConfig2D cfg {};
    cfg.shape             = ParticleEmitterShape2D::kCircle;
    cfg.shape_radius      = 0.3F;
    cfg.burst_count       = 16U;
    cfg.lifetime_min      = 0.25F;
    cfg.lifetime_max      = 0.55F;
    cfg.speed_min         = 2.0F;
    cfg.speed_max         = 4.5F;
    cfg.gravity           = { 0.0F, -4.0F };
    cfg.drag              = 0.5F;
    cfg.start_size_min    = { 0.10F, 0.10F };
    cfg.start_size_max    = { 0.14F, 0.14F };
    cfg.end_size_min      = { 0.02F, 0.02F };
    cfg.end_size_max      = { 0.04F, 0.04F };
    cfg.start_color       = { 1.0F, 0.9F, 0.2F, 1.0F };
    cfg.end_color         = { 1.0F, 0.4F, 0.1F, 0.0F };
    return cfg;
}

ParticleEmitterConfig2D jump_dust() noexcept
{
    ParticleEmitterConfig2D cfg {};
    cfg.shape             = ParticleEmitterShape2D::kBox;
    cfg.shape_box_extents = { 0.35F, 0.08F };
    cfg.burst_count       = 8U;
    cfg.lifetime_min      = 0.20F;
    cfg.lifetime_max      = 0.40F;
    cfg.speed_min         = 1.0F;
    cfg.speed_max         = 2.2F;
    cfg.gravity           = { 0.0F, 0.8F };
    cfg.drag              = 0.6F;
    cfg.start_size_min    = { 0.10F, 0.10F };
    cfg.start_size_max    = { 0.14F, 0.14F };
    cfg.end_size_min      = { 0.18F, 0.18F };
    cfg.end_size_max      = { 0.22F, 0.22F };
    cfg.start_color       = { 0.80F, 0.85F, 0.95F, 0.75F };
    cfg.end_color         = { 0.65F, 0.70F, 0.80F, 0.0F };
    return cfg;
}

ParticleEmitterConfig2D hit_impact() noexcept
{
    ParticleEmitterConfig2D cfg {};
    cfg.shape             = ParticleEmitterShape2D::kCone;
    cfg.cone_angle_rad    = 1.5707963F;
    cfg.cone_spread_rad   = 0.7853982F;
    cfg.burst_count       = 12U;
    cfg.lifetime_min      = 0.15F;
    cfg.lifetime_max      = 0.35F;
    cfg.speed_min         = 3.5F;
    cfg.speed_max         = 7.0F;
    cfg.gravity           = { 0.0F, -12.0F };
    cfg.drag              = 0.8F;
    cfg.start_size_min    = { 0.12F, 0.12F };
    cfg.start_size_max    = { 0.16F, 0.16F };
    cfg.end_size_min      = { 0.0F, 0.0F };
    cfg.end_size_max      = { 0.03F, 0.03F };
    cfg.start_color       = { 1.0F, 0.3F, 0.2F, 1.0F };
    cfg.end_color         = { 0.5F, 0.1F, 0.1F, 0.0F };
    return cfg;
}

ParticleEmitterConfig2D enemy_defeat() noexcept
{
    ParticleEmitterConfig2D cfg {};
    cfg.shape             = ParticleEmitterShape2D::kCircle;
    cfg.shape_radius      = 0.45F;
    cfg.burst_count       = 20U;
    cfg.lifetime_min      = 0.35F;
    cfg.lifetime_max      = 0.70F;
    cfg.speed_min         = 3.0F;
    cfg.speed_max         = 6.5F;
    cfg.gravity           = { 0.0F, -6.0F };
    cfg.drag              = 0.4F;
    cfg.start_size_min    = { 0.16F, 0.16F };
    cfg.start_size_max    = { 0.22F, 0.22F };
    cfg.end_size_min      = { 0.03F, 0.03F };
    cfg.end_size_max      = { 0.06F, 0.06F };
    cfg.start_color       = { 1.0F, 0.2F, 0.35F, 1.0F };
    cfg.end_color         = { 0.3F, 0.05F, 0.10F, 0.0F };
    return cfg;
}

}  // namespace ParticleEmitterPresets2D

ParticleSystem2D::ParticleSystem2D(std::size_t max_particles)
    : particles_(std::max(std::size_t { 1U }, max_particles))
{
}

void ParticleSystem2D::emit(std::size_t count)
{
    spawn_burst({ 0.0F, 0.0F }, count, default_config_);
}

void ParticleSystem2D::emit(std::size_t count, cd::math::Vec2f emitter_pos)
{
    spawn_burst(emitter_pos, count, default_config_);
}

std::size_t ParticleSystem2D::spawn_burst(
    cd::math::Vec2f                emitter_pos,
    std::size_t                    count,
    const ParticleEmitterConfig2D& config)
{
    std::size_t spawned = 0U;
    for (std::size_t i = 0U; i < count; ++i)
    {
        if (!emit_particle(emitter_pos, config))
        {
            break;
        }
        ++spawned;
    }
    return spawned;
}

bool ParticleSystem2D::emit_particle(cd::math::Vec2f emitter_pos, const ParticleEmitterConfig2D& config)
{
    if (active_count_ >= particles_.size())
    {
        return false;
    }

    cd::math::Vec2f offset { 0.0F, 0.0F };
    cd::math::Vec2f vel { 0.0F, 0.0F };

    constexpr float kTwoPi = 6.283185307179586F;

    switch (config.shape)
    {
        case ParticleEmitterShape2D::kPoint:
        {
            offset = { 0.0F, 0.0F };
            break;
        }
        case ParticleEmitterShape2D::kCircle:
        {
            const float phi = prng_.next_float() * kTwoPi;
            const float r   = std::sqrt(prng_.next_float()) * config.shape_radius;
            offset = { r * std::cos(phi), r * std::sin(phi) };
            break;
        }
        case ParticleEmitterShape2D::kBox:
        {
            const float rx = (prng_.next_float() * 2.0F - 1.0F) * config.shape_box_extents.x;
            const float ry = (prng_.next_float() * 2.0F - 1.0F) * config.shape_box_extents.y;
            offset = { rx, ry };
            break;
        }
        case ParticleEmitterShape2D::kCone:
        {
            offset = { 0.0F, 0.0F };
            break;
        }
    }

    if (config.velocity_min.x != -1.0F || config.velocity_min.y != 1.0F ||
        config.velocity_max.x != 1.0F  || config.velocity_max.y != 3.0F)
    {
        vel = {
            prng_.range(config.velocity_min.x, config.velocity_max.x),
            prng_.range(config.velocity_min.y, config.velocity_max.y),
        };
    }
    else if (config.shape == ParticleEmitterShape2D::kCone)
    {
        const float angle = config.cone_angle_rad + prng_.range(-config.cone_spread_rad, config.cone_spread_rad);
        const float speed = prng_.range(config.speed_min, config.speed_max);
        vel = { std::cos(angle) * speed, std::sin(angle) * speed };
    }
    else if (config.shape == ParticleEmitterShape2D::kCircle && (offset.x != 0.0F || offset.y != 0.0F))
    {
        const float len = std::sqrt(offset.x * offset.x + offset.y * offset.y);
        const float speed = prng_.range(config.speed_min, config.speed_max);
        vel = { (offset.x / len) * speed, (offset.y / len) * speed };
    }
    else
    {
        const float angle = prng_.next_float() * kTwoPi;
        const float speed = prng_.range(config.speed_min, config.speed_max);
        vel = { std::cos(angle) * speed, std::sin(angle) * speed };
    }

    auto& p = particles_[active_count_];
    p.position             = emitter_pos + offset;
    p.velocity             = vel;
    p.rotation_rad         = prng_.next_float() * kTwoPi;
    p.angular_velocity_rad = prng_.range(config.angular_speed_min, config.angular_speed_max);
    p.age                  = 0.0F;
    p.lifetime             = std::max(0.01F, prng_.range(config.lifetime_min, config.lifetime_max));
    p.start_size           = {
        prng_.range(config.start_size_min.x, config.start_size_max.x),
        prng_.range(config.start_size_min.y, config.start_size_max.y),
    };
    p.end_size             = {
        prng_.range(config.end_size_min.x, config.end_size_max.x),
        prng_.range(config.end_size_min.y, config.end_size_max.y),
    };
    p.start_color          = config.start_color;
    p.end_color            = config.end_color;
    p.uv_rect              = config.uv_rect;
    p.texture_slot         = config.texture_slot;
    p.style                = config.style;
    p.alive                = true;

    ++active_count_;
    return true;
}

void ParticleSystem2D::update(
    float                          dt,
    const ParticleEmitterConfig2D* continuous_emitter,
    cd::math::Vec2f                emitter_pos)
{
    if (dt <= 0.0F)
    {
        return;
    }

    if (continuous_emitter != nullptr && continuous_emitter->emission_rate > 0.0F)
    {
        emission_accumulator_ += dt * continuous_emitter->emission_rate;
        while (emission_accumulator_ >= 1.0F)
        {
            if (!emit_particle(emitter_pos, *continuous_emitter))
            {
                break;
            }
            emission_accumulator_ -= 1.0F;
        }
    }

    for (std::size_t i = 0U; i < active_count_;)
    {
        auto& p = particles_[i];
        p.age += dt;
        if (p.age >= p.lifetime)
        {
            // O(1) swap with last active particle
            particles_[i] = particles_[active_count_ - 1U];
            particles_[active_count_ - 1U].alive = false;
            --active_count_;
            continue;
        }

        p.velocity += default_config_.gravity * dt;
        p.velocity *= std::max(0.0F, 1.0F - default_config_.drag * dt);
        p.position += p.velocity * dt;
        p.rotation_rad += p.angular_velocity_rad * dt;
        ++i;
    }
}

void ParticleSystem2D::emit_to_canvas(
    cd::render2d::Canvas2D& canvas,
    std::uint8_t            sort_layer,
    std::int16_t            z_index) const
{
    for (std::size_t i = 0U; i < active_count_; ++i)
    {
        const auto& p = particles_[i];
        const float t = std::clamp(p.age / p.lifetime, 0.0F, 1.0F);

        const float w = p.start_size.x + (p.end_size.x - p.start_size.x) * t;
        const float h = p.start_size.y + (p.end_size.y - p.start_size.y) * t;
        if (w <= 0.001F || h <= 0.001F)
        {
            continue;
        }

        const float cr = p.start_color.x + (p.end_color.x - p.start_color.x) * t;
        const float cg = p.start_color.y + (p.end_color.y - p.start_color.y) * t;
        const float cb = p.start_color.z + (p.end_color.z - p.start_color.z) * t;
        const float ca = p.start_color.w + (p.end_color.w - p.start_color.w) * t;

        const cd::render2d::ColorRgba8 tint = cd::render2d::ColorRgba8::from_floats(cr, cg, cb, ca);
        if (tint.a == 0U)
        {
            continue;
        }

        const std::uint64_t key = cd::render2d::make_sort_key_2d(sort_layer, z_index, p.position.y, true);

        const float x = p.position.x - w * 0.5F;
        const float y = p.position.y - h * 0.5F;

        if (p.style == ParticleRenderStyle2D::kTexturedQuad && p.texture_slot != 0xFFFFFFFFU)
        {
            canvas.add_textured_rect(x, y, w, h, p.texture_slot, p.uv_rect, tint, key);
        }
        else if (p.style == ParticleRenderStyle2D::kRadialGlow)
        {
            canvas.add_radial_glow(p.position.x, p.position.y, std::max(w, h) * 0.5F, tint, 12U, key);
        }
        else
        {
            canvas.add_solid_rect(x, y, w, h, tint, key);
        }
    }
}

void ParticleSystem2D::emit_to_canvas(
    cd::render2d::Canvas2D&             canvas,
    const cd::render2d::Camera2D&       camera,
    const cd::render2d::ViewportRect2D& viewport,
    std::uint8_t                        sort_layer,
    std::int16_t                        z_index) const
{
    const float px_per_unit = camera.effective_scale_px_per_unit();
    for (std::size_t i = 0U; i < active_count_; ++i)
    {
        const auto& p = particles_[i];
        const float t = std::clamp(p.age / p.lifetime, 0.0F, 1.0F);

        const float w_world = p.start_size.x + (p.end_size.x - p.start_size.x) * t;
        const float h_world = p.start_size.y + (p.end_size.y - p.start_size.y) * t;
        if (w_world <= 0.001F || h_world <= 0.001F)
        {
            continue;
        }

        const float cr = p.start_color.x + (p.end_color.x - p.start_color.x) * t;
        const float cg = p.start_color.y + (p.end_color.y - p.start_color.y) * t;
        const float cb = p.start_color.z + (p.end_color.z - p.start_color.z) * t;
        const float ca = p.start_color.w + (p.end_color.w - p.start_color.w) * t;

        const cd::render2d::ColorRgba8 tint = cd::render2d::ColorRgba8::from_floats(cr, cg, cb, ca);
        if (tint.a == 0U)
        {
            continue;
        }

        const cd::math::Vec2f sp  = camera.world_to_screen(p.position, viewport);
        const float           w   = std::max(2.0F, w_world * px_per_unit);
        const float           h   = std::max(2.0F, h_world * px_per_unit);
        const std::uint64_t   key = cd::render2d::make_sort_key_2d(sort_layer, z_index, p.position.y, true);

        const float x = sp.x - w * 0.5F;
        const float y = sp.y - h * 0.5F;

        if (p.style == ParticleRenderStyle2D::kTexturedQuad && p.texture_slot != 0xFFFFFFFFU)
        {
            canvas.add_textured_rect(x, y, w, h, p.texture_slot, p.uv_rect, tint, key);
        }
        else if (p.style == ParticleRenderStyle2D::kRadialGlow)
        {
            canvas.add_radial_glow(sp.x, sp.y, std::max(w, h) * 0.65F, tint, 12U, key);
        }
        else
        {
            canvas.add_solid_rect(x, y, w, h, tint, key);
        }
    }
}

void ParticleSystem2D::clear() noexcept
{
    active_count_         = 0U;
    emission_accumulator_ = 0.0F;
    for (auto& p : particles_)
    {
        p.alive = false;
    }
}

}  // namespace cd::world2d

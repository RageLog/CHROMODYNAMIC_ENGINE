// =============================================================================
// CHROMODYNAMIC — cd/render2d/Camera2D.hpp
// FAZ 34 (ADR-20261111) — Orthographic Subpixel-Smooth & Pixel-Snap 2D Camera
// with Critically-Damped Follow, Velocity Lookahead, Deadzone & Trauma Shake.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cd::render2d
{

struct Aabb2D
{
    cd::math::Vec2f min { 0.0F, 0.0F };
    cd::math::Vec2f max { 0.0F, 0.0F };

    [[nodiscard]] constexpr bool overlaps(const Aabb2D& other) const noexcept
    {
        return !(max.x < other.min.x || min.x > other.max.x ||
                 max.y < other.min.y || min.y > other.max.y);
    }

    [[nodiscard]] constexpr bool contains(cd::math::Vec2f p) const noexcept
    {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
    }
};

struct ViewportRect2D
{
    float x { 0.0F };
    float y { 0.0F };
    float width { 1280.0F };
    float height { 720.0F };
};

struct Camera2DConfig
{
    float           pixels_per_unit { 48.0F };
    float           zoom { 1.0F };
    bool            pixel_snap { false };
    float           follow_stiffness { 10.0F };
    float           lookahead_seconds { 0.16F };
    float           max_lookahead_units { 2.5F };
    cd::math::Vec2f deadzone_half_extents { 0.45F, 0.35F };
    float           max_shake_offset_units { 0.45F };
    float           trauma_decay_per_sec { 1.6F };
};

class Camera2D
{
public:
    Camera2D() noexcept = default;

    explicit constexpr Camera2D(const Camera2DConfig& cfg) noexcept
        : config_ { cfg }
    {
    }

    [[nodiscard]] constexpr const Camera2DConfig& config() const noexcept { return config_; }
    constexpr void set_config(const Camera2DConfig& cfg) noexcept { config_ = cfg; }

    [[nodiscard]] constexpr cd::math::Vec2f position() const noexcept { return position_; }
    constexpr void set_position(cd::math::Vec2f pos) noexcept
    {
        position_    = pos;
        follow_vel_  = { 0.0F, 0.0F };
    }

    [[nodiscard]] constexpr float zoom() const noexcept { return config_.zoom; }
    constexpr void set_zoom(float z) noexcept
    {
        config_.zoom = std::clamp(z, 0.10F, 16.0F);
    }

    [[nodiscard]] constexpr float effective_scale_px_per_unit() const noexcept
    {
        return std::max(1.0F, config_.pixels_per_unit * config_.zoom);
    }

    void add_trauma(float amount) noexcept
    {
        trauma_ = std::clamp(trauma_ + std::max(0.0F, amount), 0.0F, 1.0F);
    }

    [[nodiscard]] constexpr float trauma() const noexcept { return trauma_; }

    /// Critically-damped spring camera follow with deadzone + velocity lookahead.
    void follow_target(cd::math::Vec2f target_pos, cd::math::Vec2f target_vel, float dt) noexcept
    {
        if (dt <= 0.0F)
        {
            return;
        }

        const cd::math::Vec2f raw_look {
            std::clamp(target_vel.x * config_.lookahead_seconds,
                       -config_.max_lookahead_units,
                       config_.max_lookahead_units),
            std::clamp(target_vel.y * (config_.lookahead_seconds * 0.45F),
                       -config_.max_lookahead_units * 0.6F,
                       config_.max_lookahead_units * 0.6F),
        };

        const cd::math::Vec2f desired_anchor { target_pos.x + raw_look.x, target_pos.y + raw_look.y };
        cd::math::Vec2f       goal = position_;

        const float dx = desired_anchor.x - position_.x;
        const float dy = desired_anchor.y - position_.y;
        if (std::fabs(dx) > config_.deadzone_half_extents.x)
        {
            goal.x = desired_anchor.x - std::copysign(config_.deadzone_half_extents.x, dx);
        }
        if (std::fabs(dy) > config_.deadzone_half_extents.y)
        {
            goal.y = desired_anchor.y - std::copysign(config_.deadzone_half_extents.y, dy);
        }

        // Critically damped spring (omega = follow_stiffness)
        const float omega = std::max(0.5F, config_.follow_stiffness);
        const float x     = omega * dt;
        const float exp_f = 1.0F / (1.0F + x + 0.48F * x * x + 0.235F * x * x * x);

        const cd::math::Vec2f change { position_.x - goal.x, position_.y - goal.y };
        const cd::math::Vec2f temp {
            (follow_vel_.x + omega * change.x) * dt,
            (follow_vel_.y + omega * change.y) * dt,
        };
        follow_vel_.x = (follow_vel_.x - omega * temp.x) * exp_f;
        follow_vel_.y = (follow_vel_.y - omega * temp.y) * exp_f;
        position_.x   = goal.x + (change.x + temp.x) * exp_f;
        position_.y   = goal.y + (change.y + temp.y) * exp_f;

        step_shake(dt);
    }

    void step_shake(float dt) noexcept
    {
        if (dt <= 0.0F)
        {
            return;
        }
        shake_phase_ += dt * 37.0F;
        trauma_ = std::max(0.0F, trauma_ - config_.trauma_decay_per_sec * dt);
        const float shake_mag = trauma_ * trauma_ * config_.max_shake_offset_units;
        shake_offset_ = {
            std::sin(shake_phase_ * 1.7F) * shake_mag,
            std::cos(shake_phase_ * 2.3F) * shake_mag,
        };
    }

    [[nodiscard]] cd::math::Vec2f render_eye_world() const noexcept
    {
        cd::math::Vec2f eye { position_.x + shake_offset_.x, position_.y + shake_offset_.y };
        if (config_.pixel_snap)
        {
            const float ppu = effective_scale_px_per_unit();
            eye.x           = std::round(eye.x * ppu) / ppu;
            eye.y           = std::round(eye.y * ppu) / ppu;
        }
        return eye;
    }

    /// World (+X right, +Y up) -> Screen pixels (+X right, +Y down inside `vp`).
    [[nodiscard]] cd::math::Vec2f
    world_to_screen(cd::math::Vec2f world_pt, const ViewportRect2D& vp) const noexcept
    {
        const float           scale = effective_scale_px_per_unit();
        const cd::math::Vec2f eye   = render_eye_world();
        const float           cx    = vp.x + vp.width * 0.5F;
        const float           cy    = vp.y + vp.height * 0.5F;
        float                 sx    = cx + (world_pt.x - eye.x) * scale;
        float                 sy    = cy - (world_pt.y - eye.y) * scale;
        if (config_.pixel_snap)
        {
            sx = std::round(sx);
            sy = std::round(sy);
        }
        return { sx, sy };
    }

    /// Screen pixels (+X right, +Y down) -> World (+X right, +Y up).
    [[nodiscard]] cd::math::Vec2f
    screen_to_world(cd::math::Vec2f screen_pt, const ViewportRect2D& vp) const noexcept
    {
        const float           inv_scale = 1.0F / effective_scale_px_per_unit();
        const cd::math::Vec2f eye       = render_eye_world();
        const float           cx        = vp.x + vp.width * 0.5F;
        const float           cy        = vp.y + vp.height * 0.5F;
        return {
            eye.x + (screen_pt.x - cx) * inv_scale,
            eye.y - (screen_pt.y - cy) * inv_scale,
        };
    }

    [[nodiscard]] Aabb2D visible_world_bounds(const ViewportRect2D& vp, float margin_units = 0.5F) const noexcept
    {
        const float           inv_scale = 1.0F / effective_scale_px_per_unit();
        const cd::math::Vec2f eye       = render_eye_world();
        const float           half_w    = (vp.width * 0.5F) * inv_scale + margin_units;
        const float           half_h    = (vp.height * 0.5F) * inv_scale + margin_units;
        return Aabb2D {
            .min = { eye.x - half_w, eye.y - half_h },
            .max = { eye.x + half_w, eye.y + half_h },
        };
    }

private:
    Camera2DConfig  config_ {};
    cd::math::Vec2f position_ { 8.0F, 4.5F };
    cd::math::Vec2f follow_vel_ { 0.0F, 0.0F };
    cd::math::Vec2f shake_offset_ { 0.0F, 0.0F };
    float           trauma_ { 0.0F };
    float           shake_phase_ { 0.0F };
};

}  // namespace cd::render2d

// =============================================================================
// CHROMODYNAMIC — cd/physics/v2d/CharacterController2D.hpp
// Godot 4 CharacterBody2D grade kinematic character controller with
// move_and_slide, walkable slope classification, floor snap-down, skin width,
// coyote-time, and jump-buffering.
// =============================================================================
#pragma once

#include <cd/math/Constants.hpp>
#include <cd/math/Vec2Ops.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/v2d/Narrowphase2D.hpp>
#include <cd/physics/v2d/PhysicsWorld2D.hpp>
#include <cd/physics/v2d/SweepCcd2D.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cd::physics::v2d
{

struct CharacterControllerConfig2D
{
    float max_slope_deg { 46.0F };
    float snap_down_dist { 0.15F };
    float skin_width { 0.015F };
    float coyote_time_sec { 0.12F };
    float jump_buffer_sec { 0.12F };
};

class CharacterController2D
{
public:
    CharacterController2D() noexcept = default;

    explicit constexpr CharacterController2D(const CharacterControllerConfig2D& config) noexcept
        : config_ { config }
    {
    }

    [[nodiscard]] constexpr const CharacterControllerConfig2D& config() const noexcept
    {
        return config_;
    }

    constexpr void set_config(const CharacterControllerConfig2D& config) noexcept
    {
        config_ = config;
    }

    cd::math::Vec2f move_and_slide(
        PhysicsWorld2D& world,
        std::uint32_t body_id,
        cd::math::Vec2f desired_velocity,
        float dt) noexcept
    {
        RigidBody2D* char_body = world.get_body(body_id);
        if (char_body == nullptr || dt <= 0.0F)
        {
            return desired_velocity;
        }

        const bool was_on_floor  = on_floor_;
        on_floor_                = false;
        on_wall_                 = false;
        on_ceiling_              = false;

        const float max_slope_rad = config_.max_slope_deg * (cd::math::pi / 180.0F);
        const float min_floor_ny  = std::cos(max_slope_rad);

        cd::math::Vec2f current_vel      = desired_velocity;
        cd::math::Vec2f remaining_motion = desired_velocity * dt;

        constexpr int kMaxSlideIterations = 4;
        for (int iter = 0; iter < kMaxSlideIterations; ++iter)
        {
            if (cd::math::length_sq_v2(remaining_motion) <= 1.0e-8F && iter > 0)
            {
                break;
            }

            float earliest_toi = 1.0F;
            for (const auto& other : world.bodies())
            {
                if (other.id == body_id || other.is_sensor)
                {
                    continue;
                }
                if (!world.filter_matrix().should_collide(char_body->filter, other.filter))
                {
                    continue;
                }
                const float toi = sweep_shapes_toi_2d(
                    char_body->shape,
                    char_body->transform,
                    remaining_motion,
                    other.shape,
                    other.transform);
                earliest_toi = std::min(earliest_toi, toi);
            }

            // Advance along remaining_motion; if blocked, step slightly past TOI to evaluate contact manifold
            const float step_frac = (earliest_toi < 1.0F) ? std::min(1.0F, earliest_toi + 0.05F) : 1.0F;
            char_body->transform.position += remaining_motion * step_frac;
            remaining_motion *= (1.0F - step_frac);

            bool any_contact = false;
            for (const auto& other : world.bodies())
            {
                if (other.id == body_id || other.is_sensor)
                {
                    continue;
                }
                if (!world.filter_matrix().should_collide(char_body->filter, other.filter))
                {
                    continue;
                }

                const ContactManifold2D m = collide_shapes_2d(
                    char_body->shape, char_body->transform, other.shape, other.transform);
                if (!m.has_contacts())
                {
                    continue;
                }

                any_contact = true;
                // m.normal points from char_body (A) to other (B); outward surface normal is -m.normal
                const cd::math::Vec2f surf_normal { -m.normal.x, -m.normal.y };
                float max_pen = 0.0F;
                for (std::uint8_t i = 0u; i < m.point_count; ++i)
                {
                    max_pen = std::max(max_pen, m.points.at(i).penetration);
                }

                // Depenetrate leaving skin_width margin
                const float push_dist = std::max(0.0F, max_pen - config_.skin_width);
                char_body->transform.position += surf_normal * push_dist;

                // Classify surface normal
                if (surf_normal.y >= min_floor_ny)
                {
                    on_floor_     = true;
                    floor_normal_ = surf_normal;
                }
                else if (surf_normal.y <= -min_floor_ny)
                {
                    on_ceiling_ = true;
                }
                else
                {
                    on_wall_      = true;
                    wall_normal_  = surf_normal;
                }

                // Slide velocity and remaining motion along surface tangent
                const float vn = cd::math::dot_v2(current_vel, surf_normal);
                if (vn < 0.0F)
                {
                    current_vel -= surf_normal * vn;
                }
                const float rn = cd::math::dot_v2(remaining_motion, surf_normal);
                if (rn < 0.0F)
                {
                    remaining_motion -= surf_normal * rn;
                }
            }

            if (!any_contact && earliest_toi >= 1.0F)
            {
                break;
            }
        }

        // Downward floor snap when walking down a slope
        if (!on_floor_ && was_on_floor && desired_velocity.y <= 0.0F && config_.snap_down_dist > 0.0F)
        {
            core::Transform2D probe_xf = char_body->transform;
            probe_xf.position.y -= config_.snap_down_dist;

            for (const auto& other : world.bodies())
            {
                if (other.id == body_id || other.is_sensor)
                {
                    continue;
                }
                if (!world.filter_matrix().should_collide(char_body->filter, other.filter))
                {
                    continue;
                }
                const ContactManifold2D m =
                    collide_shapes_2d(char_body->shape, probe_xf, other.shape, other.transform);
                if (!m.has_contacts())
                {
                    continue;
                }
                const cd::math::Vec2f surf_normal { -m.normal.x, -m.normal.y };
                if (surf_normal.y >= min_floor_ny)
                {
                    const float pen        = m.points.at(0).penetration;
                    const float snap_delta = std::max(0.0F, config_.snap_down_dist - pen + config_.skin_width);
                    char_body->transform.position.y -= snap_delta;
                    on_floor_     = true;
                    floor_normal_ = surf_normal;
                    if (current_vel.y < 0.0F)
                    {
                        current_vel.y = 0.0F;
                    }
                    break;
                }
            }
        }

        // Update coyote time and jump buffer timers
        if (on_floor_)
        {
            coyote_timer_ = config_.coyote_time_sec;
        }
        else
        {
            coyote_timer_ = std::max(0.0F, coyote_timer_ - dt);
        }
        jump_buffer_timer_ = std::max(0.0F, jump_buffer_timer_ - dt);

        char_body->linear_velocity = current_vel;
        velocity_                  = current_vel;
        return current_vel;
    }

    void request_jump() noexcept
    {
        jump_buffer_timer_ = config_.jump_buffer_sec;
    }

    [[nodiscard]] bool can_jump() const noexcept
    {
        return on_floor_ || coyote_active();
    }

    [[nodiscard]] bool try_consume_jump() noexcept
    {
        if (jump_buffer_timer_ > 0.0F && can_jump())
        {
            jump_buffer_timer_ = 0.0F;
            coyote_timer_      = 0.0F;
            on_floor_          = false;
            return true;
        }
        return false;
    }

    [[nodiscard]] constexpr bool on_floor() const noexcept
    {
        return on_floor_;
    }

    [[nodiscard]] constexpr bool on_wall() const noexcept
    {
        return on_wall_;
    }

    [[nodiscard]] constexpr bool on_ceiling() const noexcept
    {
        return on_ceiling_;
    }

    [[nodiscard]] constexpr cd::math::Vec2f floor_normal() const noexcept
    {
        return floor_normal_;
    }

    [[nodiscard]] constexpr cd::math::Vec2f wall_normal() const noexcept
    {
        return wall_normal_;
    }

    [[nodiscard]] constexpr bool coyote_active() const noexcept
    {
        return on_floor_ || coyote_timer_ > 0.0F;
    }

    [[nodiscard]] constexpr bool jump_buffer_active() const noexcept
    {
        return jump_buffer_timer_ > 0.0F;
    }

    [[nodiscard]] constexpr cd::math::Vec2f velocity() const noexcept
    {
        return velocity_;
    }

private:
    CharacterControllerConfig2D config_ {};
    cd::math::Vec2f floor_normal_ { 0.0F, 1.0F };
    cd::math::Vec2f wall_normal_ { 1.0F, 0.0F };
    cd::math::Vec2f velocity_ { 0.0F, 0.0F };
    float coyote_timer_ { 0.0F };
    float jump_buffer_timer_ { 0.0F };
    bool on_floor_ { false };
    bool on_wall_ { false };
    bool on_ceiling_ { false };
};

}  // namespace cd::physics::v2d

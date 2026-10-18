// =============================================================================
// CHROMODYNAMIC — cd/physics/v2d/SoftStepSolver2D.hpp
// Box2D v3 (Erin Catto) Soft-Step / TGS Sequential Impulse velocity and
// position constraint solver with warm-starting and Coulomb friction.
// =============================================================================
#pragma once

#include <cd/math/Constants.hpp>
#include <cd/physics/v2d/ContactManifold2D.hpp>
#include <cd/physics/v2d/RigidBody2D.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace cd::physics::v2d
{

namespace detail
{

[[nodiscard]] inline RigidBody2D* find_body_2d(
    std::span<RigidBody2D> bodies,
    std::uint32_t body_id) noexcept
{
    if (body_id < bodies.size() && bodies[body_id].id == body_id)
    {
        return &bodies[body_id];
    }
    if (body_id > 0u && (body_id - 1u) < bodies.size() && bodies[body_id - 1u].id == body_id)
    {
        return &bodies[body_id - 1u];
    }
    for (auto& b : bodies)
    {
        if (b.id == body_id)
        {
            return &b;
        }
    }
    if (body_id < bodies.size())
    {
        return &bodies[body_id];
    }
    return nullptr;
}

}  // namespace detail

inline void warm_start(
    std::span<ContactManifold2D> manifolds,
    std::span<RigidBody2D> bodies,
    float warm_start_factor = 0.85F) noexcept
{
    for (auto& m : manifolds)
    {
        RigidBody2D* body_a = detail::find_body_2d(bodies, m.body_a);
        RigidBody2D* body_b = detail::find_body_2d(bodies, m.body_b);
        if (body_a == nullptr || body_b == nullptr)
        {
            continue;
        }
        if (body_a->is_sensor || body_b->is_sensor)
        {
            continue;
        }

        const float inv_ma = body_a->is_dynamic() ? body_a->inv_mass : 0.0F;
        const float inv_ia = body_a->is_dynamic() ? body_a->inv_inertia : 0.0F;
        const float inv_mb = body_b->is_dynamic() ? body_b->inv_mass : 0.0F;
        const float inv_ib = body_b->is_dynamic() ? body_b->inv_inertia : 0.0F;

        const cd::math::Vec2f n = m.normal;
        const cd::math::Vec2f t { -n.y, n.x };

        for (std::uint8_t i = 0u; i < m.point_count; ++i)
        {
            ContactPoint2D& cp = m.points.at(i);
            cp.normal_impulse *= warm_start_factor;
            cp.tangent_impulse *= warm_start_factor;

            const cd::math::Vec2f impulse = n * cp.normal_impulse + t * cp.tangent_impulse;
            const cd::math::Vec2f ra      = cp.world_point - body_a->transform.position;
            const cd::math::Vec2f rb      = cp.world_point - body_b->transform.position;

            body_a->linear_velocity -= impulse * inv_ma;
            body_a->angular_velocity -= detail::cross_2d(ra, impulse) * inv_ia;

            body_b->linear_velocity += impulse * inv_mb;
            body_b->angular_velocity += detail::cross_2d(rb, impulse) * inv_ib;
        }
    }
}

inline void solve_velocity_constraints(
    std::span<ContactManifold2D> manifolds,
    std::span<RigidBody2D> bodies,
    float dt,
    float hertz = 30.0F,
    float damping_ratio = 1.0F) noexcept
{
    if (dt <= 0.0F)
    {
        return;
    }

    // Box2D v3 Soft-Step harmonic oscillator parameters
    const float omega         = 2.0F * cd::math::pi * std::max(1.0F, hertz);
    const float a1            = 2.0F * std::max(0.0F, damping_ratio) + dt * omega;
    const float a2            = dt * omega * a1;
    const float a3            = 1.0F / (1.0F + a2);
    const float bias_rate     = omega / a1;
    const float mass_scale    = a2 * a3;
    const float impulse_scale = a3;
    constexpr float kSlop     = 0.005F;
    constexpr float kBounceThreshold = 1.0F;

    for (auto& m : manifolds)
    {
        RigidBody2D* body_a = detail::find_body_2d(bodies, m.body_a);
        RigidBody2D* body_b = detail::find_body_2d(bodies, m.body_b);
        if (body_a == nullptr || body_b == nullptr)
        {
            continue;
        }
        if (body_a->is_sensor || body_b->is_sensor)
        {
            continue;
        }

        const float inv_ma = body_a->is_dynamic() ? body_a->inv_mass : 0.0F;
        const float inv_ia = body_a->is_dynamic() ? body_a->inv_inertia : 0.0F;
        const float inv_mb = body_b->is_dynamic() ? body_b->inv_mass : 0.0F;
        const float inv_ib = body_b->is_dynamic() ? body_b->inv_inertia : 0.0F;

        if (inv_ma + inv_mb <= 0.0F && inv_ia + inv_ib <= 0.0F)
        {
            continue;
        }

        const cd::math::Vec2f n = m.normal;
        const cd::math::Vec2f t { -n.y, n.x };

        for (std::uint8_t i = 0u; i < m.point_count; ++i)
        {
            ContactPoint2D& cp       = m.points.at(i);
            const cd::math::Vec2f ra = cp.world_point - body_a->transform.position;
            const cd::math::Vec2f rb = cp.world_point - body_b->transform.position;

            // --- 1. Normal constraint (Soft-Step + Restitution) ---
            const float rn_a  = detail::cross_2d(ra, n);
            const float rn_b  = detail::cross_2d(rb, n);
            const float k_n   = inv_ma + inv_mb + inv_ia * rn_a * rn_a + inv_ib * rn_b * rn_b;
            const float eff_n = (k_n > 1.0e-8F) ? (1.0F / k_n) : 0.0F;

            const cd::math::Vec2f va =
                body_a->linear_velocity + detail::cross_sv_2d(body_a->angular_velocity, ra);
            const cd::math::Vec2f vb =
                body_b->linear_velocity + detail::cross_sv_2d(body_b->angular_velocity, rb);
            const cd::math::Vec2f v_rel = vb - va;
            const float vn              = cd::math::dot_v2(v_rel, n);

            const float pen_excess = std::max(0.0F, cp.penetration - kSlop);
            const float soft_bias  = -bias_rate * pen_excess;

            float delta_jn = 0.0F;
            if (vn < -kBounceThreshold && m.combined_restitution > 0.0F)
            {
                const float target_vn = -m.combined_restitution * vn;
                delta_jn              = -eff_n * (vn - target_vn);
            }
            else
            {
                delta_jn = -eff_n * mass_scale * (vn + soft_bias) - impulse_scale * cp.normal_impulse;
            }

            const float new_jn = std::max(0.0F, cp.normal_impulse + delta_jn);
            delta_jn           = new_jn - cp.normal_impulse;
            cp.normal_impulse  = new_jn;

            const cd::math::Vec2f pn = n * delta_jn;
            body_a->linear_velocity -= pn * inv_ma;
            body_a->angular_velocity -= detail::cross_2d(ra, pn) * inv_ia;
            body_b->linear_velocity += pn * inv_mb;
            body_b->angular_velocity += detail::cross_2d(rb, pn) * inv_ib;

            // --- 2. Tangent constraint (Coulomb Friction) ---
            const float rt_a  = detail::cross_2d(ra, t);
            const float rt_b  = detail::cross_2d(rb, t);
            const float k_t   = inv_ma + inv_mb + inv_ia * rt_a * rt_a + inv_ib * rt_b * rt_b;
            const float eff_t = (k_t > 1.0e-8F) ? (1.0F / k_t) : 0.0F;

            const cd::math::Vec2f va_post =
                body_a->linear_velocity + detail::cross_sv_2d(body_a->angular_velocity, ra);
            const cd::math::Vec2f vb_post =
                body_b->linear_velocity + detail::cross_sv_2d(body_b->angular_velocity, rb);
            const float vt = cd::math::dot_v2(vb_post - va_post, t);

            float delta_jt         = -eff_t * vt;
            const float max_fric   = m.combined_friction * cp.normal_impulse;
            const float new_jt     = std::clamp(cp.tangent_impulse + delta_jt, -max_fric, +max_fric);
            delta_jt               = new_jt - cp.tangent_impulse;
            cp.tangent_impulse     = new_jt;

            const cd::math::Vec2f pt = t * delta_jt;
            body_a->linear_velocity -= pt * inv_ma;
            body_a->angular_velocity -= detail::cross_2d(ra, pt) * inv_ia;
            body_b->linear_velocity += pt * inv_mb;
            body_b->angular_velocity += detail::cross_2d(rb, pt) * inv_ib;
        }
    }
}

inline bool solve_position_constraints(
    std::span<ContactManifold2D> manifolds,
    std::span<RigidBody2D> bodies) noexcept
{
    constexpr float kSlop          = 0.005F;
    constexpr float kBaumgarte     = 0.25F;
    constexpr float kMaxCorrection = 0.2F;
    float max_penetration          = 0.0F;

    for (auto& m : manifolds)
    {
        RigidBody2D* body_a = detail::find_body_2d(bodies, m.body_a);
        RigidBody2D* body_b = detail::find_body_2d(bodies, m.body_b);
        if (body_a == nullptr || body_b == nullptr)
        {
            continue;
        }
        if (body_a->is_sensor || body_b->is_sensor)
        {
            continue;
        }

        const float inv_ma = body_a->is_dynamic() ? body_a->inv_mass : 0.0F;
        const float inv_ia = body_a->is_dynamic() ? body_a->inv_inertia : 0.0F;
        const float inv_mb = body_b->is_dynamic() ? body_b->inv_mass : 0.0F;
        const float inv_ib = body_b->is_dynamic() ? body_b->inv_inertia : 0.0F;

        if (inv_ma + inv_mb <= 0.0F && inv_ia + inv_ib <= 0.0F)
        {
            continue;
        }

        const cd::math::Vec2f n = m.normal;

        for (std::uint8_t i = 0u; i < m.point_count; ++i)
        {
            ContactPoint2D& cp = m.points.at(i);
            max_penetration    = std::max(max_penetration, cp.penetration);
            if (cp.penetration <= kSlop)
            {
                continue;
            }

            const cd::math::Vec2f world_a = body_a->transform.transform_point(cp.local_anchor_a);
            const cd::math::Vec2f world_b = body_b->transform.transform_point(cp.local_anchor_b);
            const cd::math::Vec2f ra      = world_a - body_a->transform.position;
            const cd::math::Vec2f rb      = world_b - body_b->transform.position;

            const float rn_a  = detail::cross_2d(ra, n);
            const float rn_b  = detail::cross_2d(rb, n);
            const float k_n   = inv_ma + inv_mb + inv_ia * rn_a * rn_a + inv_ib * rn_b * rn_b;
            const float eff_n = (k_n > 1.0e-8F) ? (1.0F / k_n) : 0.0F;

            const float correction =
                std::clamp(kBaumgarte * (cp.penetration - kSlop), 0.0F, kMaxCorrection);
            const cd::math::Vec2f impulse = n * (eff_n * correction);

            body_a->transform.position -= impulse * inv_ma;
            body_a->transform.rotation_rad -= detail::cross_2d(ra, impulse) * inv_ia;

            body_b->transform.position += impulse * inv_mb;
            body_b->transform.rotation_rad += detail::cross_2d(rb, impulse) * inv_ib;

            cp.penetration = std::max(0.0F, cp.penetration - correction);
        }
    }

    return max_penetration <= 3.0F * kSlop;
}

class SoftStepSolver2D
{
public:
    static void warm_start(
        std::span<ContactManifold2D> manifolds,
        std::span<RigidBody2D> bodies,
        float warm_start_factor = 0.85F) noexcept
    {
        v2d::warm_start(manifolds, bodies, warm_start_factor);
    }

    static void solve_velocity_constraints(
        std::span<ContactManifold2D> manifolds,
        std::span<RigidBody2D> bodies,
        float dt,
        float hertz = 30.0F,
        float damping_ratio = 1.0F) noexcept
    {
        v2d::solve_velocity_constraints(manifolds, bodies, dt, hertz, damping_ratio);
    }

    static bool solve_position_constraints(
        std::span<ContactManifold2D> manifolds,
        std::span<RigidBody2D> bodies) noexcept
    {
        return v2d::solve_position_constraints(manifolds, bodies);
    }
};

}  // namespace cd::physics::v2d

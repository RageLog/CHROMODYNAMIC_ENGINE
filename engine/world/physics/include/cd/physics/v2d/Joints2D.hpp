// =============================================================================
// CHROMODYNAMIC — cd/physics/v2d/Joints2D.hpp
// Box2D v3 / Godot 4 grade 2D joint constraints:
// DistanceJoint2D, RevoluteJoint2D, PrismaticJoint2D, WeldJoint2D, WheelJoint2D.
// =============================================================================
#pragma once

#include <cd/math/Constants.hpp>
#include <cd/math/Vec2Ops.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/v2d/RigidBody2D.hpp>
#include <cd/physics/v2d/SoftStepSolver2D.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <variant>

namespace cd::physics::v2d
{

struct DistanceJoint2D
{
    std::uint32_t body_a { 0u };
    std::uint32_t body_b { 0u };
    cd::math::Vec2f local_anchor_a { 0.0F, 0.0F };
    cd::math::Vec2f local_anchor_b { 0.0F, 0.0F };
    float rest_length { 1.0F };
    float min_length { 0.0F };
    float max_length { 1000.0F };
    float hertz { 4.0F };
    float damping_ratio { 0.5F };
};

struct RevoluteJoint2D
{
    std::uint32_t body_a { 0u };
    std::uint32_t body_b { 0u };
    cd::math::Vec2f local_anchor_a { 0.0F, 0.0F };
    cd::math::Vec2f local_anchor_b { 0.0F, 0.0F };
    float reference_angle { 0.0F };
    bool enable_limit { false };
    float lower_angle { -0.5F };
    float upper_angle { +0.5F };
    bool enable_motor { false };
    float motor_speed { 0.0F };
    float max_motor_torque { 10.0F };
};

struct PrismaticJoint2D
{
    std::uint32_t body_a { 0u };
    std::uint32_t body_b { 0u };
    cd::math::Vec2f local_anchor_a { 0.0F, 0.0F };
    cd::math::Vec2f local_anchor_b { 0.0F, 0.0F };
    cd::math::Vec2f local_axis_a { 1.0F, 0.0F };
    float reference_angle { 0.0F };
    bool enable_limit { false };
    float lower_translation { -1.0F };
    float upper_translation { +1.0F };
    bool enable_motor { false };
    float motor_speed { 0.0F };
    float max_motor_force { 10.0F };
};

struct WeldJoint2D
{
    std::uint32_t body_a { 0u };
    std::uint32_t body_b { 0u };
    cd::math::Vec2f local_anchor_a { 0.0F, 0.0F };
    cd::math::Vec2f local_anchor_b { 0.0F, 0.0F };
    float reference_angle { 0.0F };
    float hertz { 0.0F };
    float damping_ratio { 1.0F };
};

struct WheelJoint2D
{
    std::uint32_t body_a { 0u };
    std::uint32_t body_b { 0u };
    cd::math::Vec2f local_anchor_a { 0.0F, 0.0F };
    cd::math::Vec2f local_anchor_b { 0.0F, 0.0F };
    cd::math::Vec2f local_axis_a { 0.0F, 1.0F };
    float hertz { 4.0F };
    float damping_ratio { 0.7F };
    bool enable_motor { false };
    float motor_speed { 0.0F };
    float max_motor_torque { 20.0F };
};

using JointVariant2D = std::variant<
    DistanceJoint2D,
    RevoluteJoint2D,
    PrismaticJoint2D,
    WeldJoint2D,
    WheelJoint2D>;

namespace detail
{

inline void apply_pair_impulse_2d(
    RigidBody2D& a,
    cd::math::Vec2f ra,
    RigidBody2D& b,
    cd::math::Vec2f rb,
    cd::math::Vec2f impulse) noexcept
{
    const float inv_ma = a.is_dynamic() ? a.inv_mass : 0.0F;
    const float inv_ia = a.is_dynamic() ? a.inv_inertia : 0.0F;
    const float inv_mb = b.is_dynamic() ? b.inv_mass : 0.0F;
    const float inv_ib = b.is_dynamic() ? b.inv_inertia : 0.0F;

    a.linear_velocity -= impulse * inv_ma;
    a.angular_velocity -= cross_2d(ra, impulse) * inv_ia;
    b.linear_velocity += impulse * inv_mb;
    b.angular_velocity += cross_2d(rb, impulse) * inv_ib;
}

inline void solve_point_to_point_2d(
    RigidBody2D& a,
    cd::math::Vec2f local_a,
    RigidBody2D& b,
    cd::math::Vec2f local_b,
    float dt,
    float baumgarte = 0.25F) noexcept
{
    const cd::math::Vec2f wa = a.transform.transform_point(local_a);
    const cd::math::Vec2f wb = b.transform.transform_point(local_b);
    const cd::math::Vec2f ra = wa - a.transform.position;
    const cd::math::Vec2f rb = wb - b.transform.position;

    const float inv_ma = a.is_dynamic() ? a.inv_mass : 0.0F;
    const float inv_ia = a.is_dynamic() ? a.inv_inertia : 0.0F;
    const float inv_mb = b.is_dynamic() ? b.inv_mass : 0.0F;
    const float inv_ib = b.is_dynamic() ? b.inv_inertia : 0.0F;

    const float kxx = inv_ma + inv_mb + inv_ia * ra.y * ra.y + inv_ib * rb.y * rb.y;
    const float kyy = inv_ma + inv_mb + inv_ia * ra.x * ra.x + inv_ib * rb.x * rb.x;
    const float kxy = -inv_ia * ra.x * ra.y - inv_ib * rb.x * rb.y;
    const float det = kxx * kyy - kxy * kxy;
    if (std::abs(det) <= 1.0e-8F)
    {
        return;
    }

    const cd::math::Vec2f va   = a.linear_velocity + cross_sv_2d(a.angular_velocity, ra);
    const cd::math::Vec2f vb   = b.linear_velocity + cross_sv_2d(b.angular_velocity, rb);
    const cd::math::Vec2f c_err = wb - wa;
    const cd::math::Vec2f rhs   = (vb - va) + c_err * (baumgarte / dt);

    const float inv_det = 1.0F / det;
    const cd::math::Vec2f impulse {
        -inv_det * (kyy * rhs.x - kxy * rhs.y),
        -inv_det * (-kxy * rhs.x + kxx * rhs.y)
    };

    apply_pair_impulse_2d(a, ra, b, rb, impulse);
}

}  // namespace detail

inline void solve_joint_2d(
    JointVariant2D& joint,
    std::span<RigidBody2D> bodies,
    float dt) noexcept
{
    if (dt <= 0.0F)
    {
        return;
    }

    std::visit(
        [&](auto& j) noexcept
        {
            using T = std::decay_t<decltype(j)>;
            RigidBody2D* a = detail::find_body_2d(bodies, j.body_a);
            RigidBody2D* b = detail::find_body_2d(bodies, j.body_b);
            if (a == nullptr || b == nullptr)
            {
                return;
            }

            const float inv_ma = a->is_dynamic() ? a->inv_mass : 0.0F;
            const float inv_ia = a->is_dynamic() ? a->inv_inertia : 0.0F;
            const float inv_mb = b->is_dynamic() ? b->inv_mass : 0.0F;
            const float inv_ib = b->is_dynamic() ? b->inv_inertia : 0.0F;

            if (inv_ma + inv_mb + inv_ia + inv_ib <= 0.0F)
            {
                return;
            }

            if constexpr (std::is_same_v<T, DistanceJoint2D>)
            {
                const cd::math::Vec2f wa = a->transform.transform_point(j.local_anchor_a);
                const cd::math::Vec2f wb = b->transform.transform_point(j.local_anchor_b);
                const cd::math::Vec2f ra = wa - a->transform.position;
                const cd::math::Vec2f rb = wb - b->transform.position;
                const cd::math::Vec2f d  = wb - wa;
                const float dist         = cd::math::length_v2(d);
                const cd::math::Vec2f u  = (dist > 1.0e-6F) ? (d / dist) : cd::math::Vec2f { 1.0F, 0.0F };

                const float ru_a  = detail::cross_2d(ra, u);
                const float ru_b  = detail::cross_2d(rb, u);
                const float k_inv = inv_ma + inv_mb + inv_ia * ru_a * ru_a + inv_ib * ru_b * ru_b;
                const float m_eff = (k_inv > 1.0e-8F) ? (1.0F / k_inv) : 0.0F;

                const cd::math::Vec2f va = a->linear_velocity + detail::cross_sv_2d(a->angular_velocity, ra);
                const cd::math::Vec2f vb = b->linear_velocity + detail::cross_sv_2d(b->angular_velocity, rb);
                const float vn           = cd::math::dot_v2(vb - va, u);

                if (j.hertz > 0.0F)
                {
                    const float omega  = 2.0F * cd::math::pi * j.hertz;
                    const float k_spr  = m_eff * omega * omega;
                    const float c_dmp  = 2.0F * m_eff * j.damping_ratio * omega;
                    const float denom  = c_dmp + dt * k_spr;
                    const float gamma  = (denom > 1.0e-8F) ? (1.0F / (dt * denom)) : 0.0F;
                    const float bias   = (denom > 1.0e-8F) ? ((dist - j.rest_length) * k_spr / denom) : 0.0F;
                    const float s_mass = (k_inv + gamma > 1.0e-8F) ? (1.0F / (k_inv + gamma)) : 0.0F;
                    const float imp    = -s_mass * (vn + bias);
                    detail::apply_pair_impulse_2d(*a, ra, *b, rb, u * imp);
                }

                // Enforce min / max length limits
                if (dist < j.min_length)
                {
                    const float bias = (dist - j.min_length) * (0.3F / dt);
                    const float imp  = std::max(0.0F, -m_eff * (vn + bias));
                    detail::apply_pair_impulse_2d(*a, ra, *b, rb, u * imp);
                }
                else if (dist > j.max_length && j.max_length >= j.min_length)
                {
                    const float bias = (dist - j.max_length) * (0.3F / dt);
                    const float imp  = std::min(0.0F, -m_eff * (vn + bias));
                    detail::apply_pair_impulse_2d(*a, ra, *b, rb, u * imp);
                }
            }
            else if constexpr (std::is_same_v<T, RevoluteJoint2D>)
            {
                const float inv_i_sum = inv_ia + inv_ib;
                if (j.enable_motor && inv_i_sum > 1.0e-8F)
                {
                    const float dw        = (b->angular_velocity - a->angular_velocity) - j.motor_speed;
                    const float max_imp   = j.max_motor_torque * dt;
                    const float motor_imp = std::clamp(-dw / inv_i_sum, -max_imp, +max_imp);
                    a->angular_velocity -= inv_ia * motor_imp;
                    b->angular_velocity += inv_ib * motor_imp;
                }

                if (j.enable_limit && inv_i_sum > 1.0e-8F)
                {
                    const float angle =
                        (b->transform.rotation_rad - a->transform.rotation_rad) - j.reference_angle;
                    const float dw = b->angular_velocity - a->angular_velocity;
                    if (angle < j.lower_angle)
                    {
                        const float bias = (angle - j.lower_angle) * (0.3F / dt);
                        const float imp  = std::max(0.0F, -(dw + bias) / inv_i_sum);
                        a->angular_velocity -= inv_ia * imp;
                        b->angular_velocity += inv_ib * imp;
                    }
                    else if (angle > j.upper_angle)
                    {
                        const float bias = (angle - j.upper_angle) * (0.3F / dt);
                        const float imp  = std::min(0.0F, -(dw + bias) / inv_i_sum);
                        a->angular_velocity -= inv_ia * imp;
                        b->angular_velocity += inv_ib * imp;
                    }
                }

                detail::solve_point_to_point_2d(*a, j.local_anchor_a, *b, j.local_anchor_b, dt);
            }
            else if constexpr (std::is_same_v<T, PrismaticJoint2D>)
            {
                const cd::math::Vec2f wa   = a->transform.transform_point(j.local_anchor_a);
                const cd::math::Vec2f wb   = b->transform.transform_point(j.local_anchor_b);
                const cd::math::Vec2f ra   = wa - a->transform.position;
                const cd::math::Vec2f rb   = wb - b->transform.position;
                const cd::math::Vec2f axis = a->transform.rotate_vector(cd::math::normalize_v2(j.local_axis_a));
                const cd::math::Vec2f perp { -axis.y, axis.x };

                // 1. Constrain perpendicular translation
                const float rp_a  = detail::cross_2d(ra, perp);
                const float rp_b  = detail::cross_2d(rb, perp);
                const float k_p   = inv_ma + inv_mb + inv_ia * rp_a * rp_a + inv_ib * rp_b * rp_b;
                const float eff_p = (k_p > 1.0e-8F) ? (1.0F / k_p) : 0.0F;

                const cd::math::Vec2f va = a->linear_velocity + detail::cross_sv_2d(a->angular_velocity, ra);
                const cd::math::Vec2f vb = b->linear_velocity + detail::cross_sv_2d(b->angular_velocity, rb);
                const float vp           = cd::math::dot_v2(vb - va, perp);
                const float cp_err       = cd::math::dot_v2(wb - wa, perp);
                const float imp_p        = -eff_p * (vp + cp_err * (0.25F / dt));
                detail::apply_pair_impulse_2d(*a, ra, *b, rb, perp * imp_p);

                // 2. Lock relative angle
                const float inv_i_sum = inv_ia + inv_ib;
                if (inv_i_sum > 1.0e-8F)
                {
                    const float ang_err =
                        (b->transform.rotation_rad - a->transform.rotation_rad) - j.reference_angle;
                    const float dw      = b->angular_velocity - a->angular_velocity;
                    const float imp_ang = -(dw + ang_err * (0.25F / dt)) / inv_i_sum;
                    a->angular_velocity -= inv_ia * imp_ang;
                    b->angular_velocity += inv_ib * imp_ang;
                }

                // 3. Axial motor and limits
                const float ra_ax = detail::cross_2d(ra, axis);
                const float rb_ax = detail::cross_2d(rb, axis);
                const float k_ax  = inv_ma + inv_mb + inv_ia * ra_ax * ra_ax + inv_ib * rb_ax * rb_ax;
                const float eff_a = (k_ax > 1.0e-8F) ? (1.0F / k_ax) : 0.0F;

                const cd::math::Vec2f va2 = a->linear_velocity + detail::cross_sv_2d(a->angular_velocity, ra);
                const cd::math::Vec2f vb2 = b->linear_velocity + detail::cross_sv_2d(b->angular_velocity, rb);
                const float v_ax          = cd::math::dot_v2(vb2 - va2, axis);

                if (j.enable_motor && eff_a > 0.0F)
                {
                    const float max_imp = j.max_motor_force * dt;
                    const float imp_m   = std::clamp(-eff_a * (v_ax - j.motor_speed), -max_imp, +max_imp);
                    detail::apply_pair_impulse_2d(*a, ra, *b, rb, axis * imp_m);
                }

                if (j.enable_limit && eff_a > 0.0F)
                {
                    const float trans = cd::math::dot_v2(wb - wa, axis);
                    if (trans < j.lower_translation)
                    {
                        const float bias = (trans - j.lower_translation) * (0.25F / dt);
                        const float imp  = std::max(0.0F, -eff_a * (v_ax + bias));
                        detail::apply_pair_impulse_2d(*a, ra, *b, rb, axis * imp);
                    }
                    else if (trans > j.upper_translation)
                    {
                        const float bias = (trans - j.upper_translation) * (0.25F / dt);
                        const float imp  = std::min(0.0F, -eff_a * (v_ax + bias));
                        detail::apply_pair_impulse_2d(*a, ra, *b, rb, axis * imp);
                    }
                }
            }
            else if constexpr (std::is_same_v<T, WeldJoint2D>)
            {
                detail::solve_point_to_point_2d(*a, j.local_anchor_a, *b, j.local_anchor_b, dt);

                const float inv_i_sum = inv_ia + inv_ib;
                if (inv_i_sum > 1.0e-8F)
                {
                    const float ang_err =
                        (b->transform.rotation_rad - a->transform.rotation_rad) - j.reference_angle;
                    const float dw    = b->angular_velocity - a->angular_velocity;
                    const float m_eff = 1.0F / inv_i_sum;

                    if (j.hertz > 0.0F)
                    {
                        const float omega  = 2.0F * cd::math::pi * j.hertz;
                        const float k_spr  = m_eff * omega * omega;
                        const float c_dmp  = 2.0F * m_eff * j.damping_ratio * omega;
                        const float denom  = c_dmp + dt * k_spr;
                        const float gamma  = (denom > 1.0e-8F) ? (1.0F / (dt * denom)) : 0.0F;
                        const float bias   = (denom > 1.0e-8F) ? (ang_err * k_spr / denom) : 0.0F;
                        const float s_mass = 1.0F / (inv_i_sum + gamma);
                        const float imp    = -s_mass * (dw + bias);
                        a->angular_velocity -= inv_ia * imp;
                        b->angular_velocity += inv_ib * imp;
                    }
                    else
                    {
                        const float imp = -m_eff * (dw + ang_err * (0.25F / dt));
                        a->angular_velocity -= inv_ia * imp;
                        b->angular_velocity += inv_ib * imp;
                    }
                }
            }
            else if constexpr (std::is_same_v<T, WheelJoint2D>)
            {
                const cd::math::Vec2f wa = a->transform.transform_point(j.local_anchor_a);
                const cd::math::Vec2f wb = b->transform.transform_point(j.local_anchor_b);
                const cd::math::Vec2f ra = wa - a->transform.position;
                const cd::math::Vec2f rb = wb - b->transform.position;
                const cd::math::Vec2f d  = wb - wa;

                // Suspension axis (ax) and longitudinal wheel axis (ay = perp(ax))
                const cd::math::Vec2f ax = a->transform.rotate_vector(cd::math::normalize_v2(j.local_axis_a));
                const cd::math::Vec2f ay { -ax.y, ax.x };

                // 1. Hard constraint perpendicular to suspension axis (along ay)
                const float ry_a  = detail::cross_2d(ra, ay);
                const float ry_b  = detail::cross_2d(rb, ay);
                const float k_y   = inv_ma + inv_mb + inv_ia * ry_a * ry_a + inv_ib * ry_b * ry_b;
                const float eff_y = (k_y > 1.0e-8F) ? (1.0F / k_y) : 0.0F;

                const cd::math::Vec2f va = a->linear_velocity + detail::cross_sv_2d(a->angular_velocity, ra);
                const cd::math::Vec2f vb = b->linear_velocity + detail::cross_sv_2d(b->angular_velocity, rb);
                const float vy           = cd::math::dot_v2(vb - va, ay);
                const float cy_err       = cd::math::dot_v2(d, ay);
                const float imp_y        = -eff_y * (vy + cy_err * (0.25F / dt));
                detail::apply_pair_impulse_2d(*a, ra, *b, rb, ay * imp_y);

                // 2. Soft suspension spring-damper along ax
                const float rx_a  = detail::cross_2d(ra, ax);
                const float rx_b  = detail::cross_2d(rb, ax);
                const float k_x   = inv_ma + inv_mb + inv_ia * rx_a * rx_a + inv_ib * rx_b * rx_b;
                const float eff_x = (k_x > 1.0e-8F) ? (1.0F / k_x) : 0.0F;

                if (j.hertz > 0.0F && eff_x > 0.0F)
                {
                    const cd::math::Vec2f va2 = a->linear_velocity + detail::cross_sv_2d(a->angular_velocity, ra);
                    const cd::math::Vec2f vb2 = b->linear_velocity + detail::cross_sv_2d(b->angular_velocity, rb);
                    const float vx            = cd::math::dot_v2(vb2 - va2, ax);
                    const float cx_err        = cd::math::dot_v2(d, ax);

                    const float omega  = 2.0F * cd::math::pi * j.hertz;
                    const float k_spr  = eff_x * omega * omega;
                    const float c_dmp  = 2.0F * eff_x * j.damping_ratio * omega;
                    const float denom  = c_dmp + dt * k_spr;
                    const float gamma  = (denom > 1.0e-8F) ? (1.0F / (dt * denom)) : 0.0F;
                    const float bias   = (denom > 1.0e-8F) ? (cx_err * k_spr / denom) : 0.0F;
                    const float s_mass = 1.0F / (k_x + gamma);
                    const float imp_x  = -s_mass * (vx + bias);
                    detail::apply_pair_impulse_2d(*a, ra, *b, rb, ax * imp_x);
                }

                // 3. Wheel angular motor
                const float inv_i_sum = inv_ia + inv_ib;
                if (j.enable_motor && inv_i_sum > 1.0e-8F)
                {
                    const float dw      = (b->angular_velocity - a->angular_velocity) - j.motor_speed;
                    const float max_imp = j.max_motor_torque * dt;
                    const float imp_m   = std::clamp(-dw / inv_i_sum, -max_imp, +max_imp);
                    a->angular_velocity -= inv_ia * imp_m;
                    b->angular_velocity += inv_ib * imp_m;
                }
            }
        },
        joint);
}

}  // namespace cd::physics::v2d

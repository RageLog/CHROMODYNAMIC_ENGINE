// =============================================================================
// CHROMODYNAMIC — cd/physics/v3d/Joints3D.hpp
// Jolt / PhysX 5 grade 3D constraint joints: FixedJoint3D, BallSocketJoint3D,
// HingeJoint3D (limits + motor), SliderJoint3D, ConeTwistJoint3D (ragdoll
// swing-twist cone), and SixDofSpringJoint3D.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/v3d/RigidBody3D.hpp>
#include <cd/physics/v3d/TgsContactSolver3D.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <variant>

namespace cd::physics::v3d
{

struct FixedJoint3D
{
    std::uint32_t body_a { 0u };
    std::uint32_t body_b { 0u };
    cd::math::Vec3f local_anchor_a {};
    cd::math::Vec3f local_anchor_b {};
    cd::math::Quatf ref_relative_rotation { cd::math::Quatf::identity() };
    float baumgarte_erp { 0.30F };
};

struct BallSocketJoint3D
{
    std::uint32_t body_a { 0u };
    std::uint32_t body_b { 0u };
    cd::math::Vec3f local_anchor_a {};
    cd::math::Vec3f local_anchor_b {};
    float baumgarte_erp { 0.30F };
};

struct HingeJoint3D
{
    std::uint32_t body_a { 0u };
    std::uint32_t body_b { 0u };
    cd::math::Vec3f local_anchor_a {};
    cd::math::Vec3f local_anchor_b {};
    cd::math::Vec3f local_axis_a { 0.0F, 1.0F, 0.0F };
    cd::math::Vec3f local_axis_b { 0.0F, 1.0F, 0.0F };
    bool enable_limit { false };
    float min_angle_rad { -1.5707963F };
    float max_angle_rad { +1.5707963F };
    bool enable_motor { false };
    float motor_speed_rad_s { 0.0F };
    float max_motor_torque { 50.0F };
    float baumgarte_erp { 0.30F };
};

struct SliderJoint3D
{
    std::uint32_t body_a { 0u };
    std::uint32_t body_b { 0u };
    cd::math::Vec3f local_anchor_a {};
    cd::math::Vec3f local_anchor_b {};
    cd::math::Vec3f local_axis_a { 1.0F, 0.0F, 0.0F };
    bool enable_limit { false };
    float min_translation { -1.0F };
    float max_translation { +1.0F };
    bool enable_motor { false };
    float motor_speed { 0.0F };
    float max_motor_force { 100.0F };
    float baumgarte_erp { 0.30F };
};

struct ConeTwistJoint3D
{
    std::uint32_t body_a { 0u };
    std::uint32_t body_b { 0u };
    cd::math::Vec3f local_anchor_a {};
    cd::math::Vec3f local_anchor_b {};
    cd::math::Vec3f local_twist_axis_a { 1.0F, 0.0F, 0.0F };
    cd::math::Vec3f local_twist_axis_b { 1.0F, 0.0F, 0.0F };
    float swing_span_rad { 0.785398F };
    float twist_limit_rad { 0.523599F };
    float baumgarte_erp { 0.30F };
};

struct SixDofSpringJoint3D
{
    std::uint32_t body_a { 0u };
    std::uint32_t body_b { 0u };
    cd::math::Vec3f local_anchor_a {};
    cd::math::Vec3f local_anchor_b {};
    cd::math::Vec3f linear_stiffness { 100.0F, 100.0F, 100.0F };
    cd::math::Vec3f linear_damping { 10.0F, 10.0F, 10.0F };
    cd::math::Vec3f angular_stiffness { 20.0F, 20.0F, 20.0F };
    cd::math::Vec3f angular_damping { 2.0F, 2.0F, 2.0F };
    cd::math::Vec3f target_offset { 0.0F, 0.0F, 0.0F };
    cd::math::Quatf target_rotation { cd::math::Quatf::identity() };
};

using JointVariant3D = std::variant<
    FixedJoint3D,
    BallSocketJoint3D,
    HingeJoint3D,
    SliderJoint3D,
    ConeTwistJoint3D,
    SixDofSpringJoint3D>;

namespace detail
{

[[nodiscard]] inline RigidBody3D* resolve_body_3d(std::span<RigidBody3D> bodies, std::uint32_t id_or_idx) noexcept
{
    if (id_or_idx < bodies.size() && bodies[id_or_idx].id == id_or_idx)
    {
        return &bodies[id_or_idx];
    }
    for (RigidBody3D& b : bodies)
    {
        if (b.id == id_or_idx)
        {
            return &b;
        }
    }
    if (id_or_idx < bodies.size())
    {
        return &bodies[id_or_idx];
    }
    return nullptr;
}

inline void solve_point_to_point_3d(
    RigidBody3D& ba,
    RigidBody3D& bb,
    cd::math::Vec3f local_a,
    cd::math::Vec3f local_b,
    float dt,
    float erp) noexcept
{
    if ((!ba.is_dynamic() && !bb.is_dynamic()) || dt <= 1.0e-6F)
    {
        return;
    }

    const cd::math::Vec3f wa = ba.transform.transform_point(local_a);
    const cd::math::Vec3f wb = bb.transform.transform_point(local_b);
    const cd::math::Vec3f ra = wa - ba.transform.position;
    const cd::math::Vec3f rb = wb - bb.transform.position;
    const cd::math::Vec3f pos_err = wb - wa;

    const cd::math::Vec3f va = ba.linear_velocity + cd::math::cross(ba.angular_velocity, ra);
    const cd::math::Vec3f vb = bb.linear_velocity + cd::math::cross(bb.angular_velocity, rb);
    const cd::math::Vec3f v_rel = vb - va;
    const cd::math::Vec3f bias = pos_err * (erp / dt);
    const cd::math::Vec3f target_dv = -(v_rel + bias);

    static constexpr std::array<cd::math::Vec3f, 3> kAxes { {
        { 1.0F, 0.0F, 0.0F },
        { 0.0F, 1.0F, 0.0F },
        { 0.0F, 0.0F, 1.0F }
    } };

    for (std::size_t i = 0u; i < 3u; ++i)
    {
        const float k = compute_effective_inv_mass_3d(ba, bb, ra, rb, kAxes[i]);
        if (k > 1.0e-8F)
        {
            const float j = target_dv[i] / k;
            apply_pair_impulse_3d(ba, bb, ra, rb, kAxes[i] * j);
        }
    }
}

inline void solve_angular_lock_3d(
    RigidBody3D& ba,
    RigidBody3D& bb,
    cd::math::Quatf target_rel_q,
    float dt,
    float erp) noexcept
{
    if ((!ba.is_dynamic() && !bb.is_dynamic()) || dt <= 1.0e-6F)
    {
        return;
    }

    const cd::math::Quatf q_target_b = ba.transform.rotation * target_rel_q;
    cd::math::Quatf q_err = bb.transform.rotation * cd::math::conjugate(q_target_b);
    if (q_err.w < 0.0F)
    {
        q_err = { -q_err.x, -q_err.y, -q_err.z, -q_err.w };
    }
    const cd::math::Vec3f ang_err { 2.0F * q_err.x, 2.0F * q_err.y, 2.0F * q_err.z };
    const cd::math::Vec3f w_rel = bb.angular_velocity - ba.angular_velocity;
    const cd::math::Vec3f desired_dw = -(w_rel + ang_err * (erp / dt));

    static constexpr std::array<cd::math::Vec3f, 3> kAxes { {
        { 1.0F, 0.0F, 0.0F },
        { 0.0F, 1.0F, 0.0F },
        { 0.0F, 0.0F, 1.0F }
    } };

    for (std::size_t i = 0u; i < 3u; ++i)
    {
        const float k =
            cd::math::dot(kAxes[i], ba.mul_world_inv_inertia(kAxes[i])) +
            cd::math::dot(kAxes[i], bb.mul_world_inv_inertia(kAxes[i]));
        if (k > 1.0e-8F)
        {
            const cd::math::Vec3f j = kAxes[i] * (desired_dw[i] / k);
            if (ba.is_dynamic())
            {
                ba.angular_velocity -= ba.mul_world_inv_inertia(j);
            }
            if (bb.is_dynamic())
            {
                bb.angular_velocity += bb.mul_world_inv_inertia(j);
            }
        }
    }
}

}  // namespace detail

inline void solve_joint_3d(
    JointVariant3D& joint,
    std::span<RigidBody3D> bodies,
    float dt) noexcept
{
    if (dt <= 1.0e-6F)
    {
        return;
    }

    std::visit(
        [&](auto& j) noexcept
        {
            using T = std::decay_t<decltype(j)>;
            RigidBody3D* pa = detail::resolve_body_3d(bodies, j.body_a);
            RigidBody3D* pb = detail::resolve_body_3d(bodies, j.body_b);
            if (pa == nullptr || pb == nullptr || pa == pb)
            {
                return;
            }
            RigidBody3D& ba = *pa;
            RigidBody3D& bb = *pb;

            if constexpr (std::is_same_v<T, FixedJoint3D>)
            {
                detail::solve_point_to_point_3d(ba, bb, j.local_anchor_a, j.local_anchor_b, dt, j.baumgarte_erp);
                detail::solve_angular_lock_3d(ba, bb, j.ref_relative_rotation, dt, j.baumgarte_erp);
            }
            else if constexpr (std::is_same_v<T, BallSocketJoint3D>)
            {
                detail::solve_point_to_point_3d(ba, bb, j.local_anchor_a, j.local_anchor_b, dt, j.baumgarte_erp);
            }
            else if constexpr (std::is_same_v<T, HingeJoint3D>)
            {
                detail::solve_point_to_point_3d(ba, bb, j.local_anchor_a, j.local_anchor_b, dt, j.baumgarte_erp);

                const cd::math::Vec3f axis_a = cd::math::normalize(ba.transform.rotate_vector(j.local_axis_a));
                const cd::math::Vec3f axis_b = cd::math::normalize(bb.transform.rotate_vector(j.local_axis_b));

                // Lock the 2 angular DOFs perpendicular to axis_a.
                const cd::math::Vec3f tilt_err = cd::math::cross(axis_a, axis_b);
                const cd::math::Vec3f w_rel = bb.angular_velocity - ba.angular_velocity;
                const cd::math::Vec3f w_perp = w_rel - axis_a * cd::math::dot(w_rel, axis_a);
                const cd::math::Vec3f corr_torque = -(w_perp + tilt_err * (j.baumgarte_erp / dt));

                const float k_axis =
                    cd::math::dot(axis_a, ba.mul_world_inv_inertia(axis_a)) +
                    cd::math::dot(axis_a, bb.mul_world_inv_inertia(axis_a));

                if (k_axis > 1.0e-8F)
                {
                    const cd::math::Vec3f ang_imp = corr_torque / k_axis;
                    if (ba.is_dynamic())
                    {
                        ba.angular_velocity -= ba.mul_world_inv_inertia(ang_imp);
                    }
                    if (bb.is_dynamic())
                    {
                        bb.angular_velocity += bb.mul_world_inv_inertia(ang_imp);
                    }

                    if (j.enable_motor)
                    {
                        const float w_hinge = cd::math::dot(bb.angular_velocity - ba.angular_velocity, axis_a);
                        const float max_imp = j.max_motor_torque * dt;
                        const float motor_imp = std::clamp((j.motor_speed_rad_s - w_hinge) / k_axis, -max_imp, max_imp);
                        const cd::math::Vec3f m_vec = axis_a * motor_imp;
                        if (ba.is_dynamic())
                        {
                            ba.angular_velocity -= ba.mul_world_inv_inertia(m_vec);
                        }
                        if (bb.is_dynamic())
                        {
                            bb.angular_velocity += bb.mul_world_inv_inertia(m_vec);
                        }
                    }

                    if (j.enable_limit)
                    {
                        cd::math::Quatf q_rel = cd::math::conjugate(ba.transform.rotation) * bb.transform.rotation;
                        if (q_rel.w < 0.0F)
                        {
                            q_rel = { -q_rel.x, -q_rel.y, -q_rel.z, -q_rel.w };
                        }
                        const float twist_proj = cd::math::dot(
                            cd::math::Vec3f { q_rel.x, q_rel.y, q_rel.z },
                            cd::math::normalize(j.local_axis_a));
                        const float angle = 2.0F * std::atan2(twist_proj, q_rel.w);
                        float limit_err = 0.0F;
                        if (angle < j.min_angle_rad)
                        {
                            limit_err = angle - j.min_angle_rad;
                        }
                        else if (angle > j.max_angle_rad)
                        {
                            limit_err = angle - j.max_angle_rad;
                        }
                        if (std::abs(limit_err) > 1.0e-5F)
                        {
                            const float w_hinge = cd::math::dot(bb.angular_velocity - ba.angular_velocity, axis_a);
                            const float j_lim = -(w_hinge + (j.baumgarte_erp / dt) * limit_err) / k_axis;
                            const cd::math::Vec3f lim_vec = axis_a * j_lim;
                            if (ba.is_dynamic())
                            {
                                ba.angular_velocity -= ba.mul_world_inv_inertia(lim_vec);
                            }
                            if (bb.is_dynamic())
                            {
                                bb.angular_velocity += bb.mul_world_inv_inertia(lim_vec);
                            }
                        }
                    }
                }
            }
            else if constexpr (std::is_same_v<T, SliderJoint3D>)
            {
                detail::solve_angular_lock_3d(ba, bb, cd::math::Quatf::identity(), dt, j.baumgarte_erp);

                const cd::math::Vec3f wa = ba.transform.transform_point(j.local_anchor_a);
                const cd::math::Vec3f wb = bb.transform.transform_point(j.local_anchor_b);
                const cd::math::Vec3f ra = wa - ba.transform.position;
                const cd::math::Vec3f rb = wb - bb.transform.position;
                const cd::math::Vec3f axis = cd::math::normalize(ba.transform.rotate_vector(j.local_axis_a));

                // Orthonormal basis perpendicular to slider axis.
                cd::math::Vec3f n1 {};
                if (std::abs(axis.x) >= 0.57735F)
                {
                    n1 = cd::math::normalize(cd::math::Vec3f { axis.y, -axis.x, 0.0F });
                }
                else
                {
                    n1 = cd::math::normalize(cd::math::Vec3f { 0.0F, axis.z, -axis.y });
                }
                const cd::math::Vec3f n2 = cd::math::cross(axis, n1);

                const cd::math::Vec3f d = wb - wa;
                const cd::math::Vec3f va = ba.linear_velocity + cd::math::cross(ba.angular_velocity, ra);
                const cd::math::Vec3f vb = bb.linear_velocity + cd::math::cross(bb.angular_velocity, rb);
                const cd::math::Vec3f v_rel = vb - va;

                for (const cd::math::Vec3f& perp : { n1, n2 })
                {
                    const float k = detail::compute_effective_inv_mass_3d(ba, bb, ra, rb, perp);
                    if (k > 1.0e-8F)
                    {
                        const float err = cd::math::dot(d, perp);
                        const float vn = cd::math::dot(v_rel, perp);
                        const float imp = -(vn + (j.baumgarte_erp / dt) * err) / k;
                        detail::apply_pair_impulse_3d(ba, bb, ra, rb, perp * imp);
                    }
                }

                const float k_ax = detail::compute_effective_inv_mass_3d(ba, bb, ra, rb, axis);
                if (k_ax > 1.0e-8F)
                {
                    if (j.enable_motor)
                    {
                        const cd::math::Vec3f v_now =
                            (bb.linear_velocity + cd::math::cross(bb.angular_velocity, rb)) -
                            (ba.linear_velocity + cd::math::cross(ba.angular_velocity, ra));
                        const float v_ax = cd::math::dot(v_now, axis);
                        const float max_imp = j.max_motor_force * dt;
                        const float m_imp = std::clamp((j.motor_speed - v_ax) / k_ax, -max_imp, max_imp);
                        detail::apply_pair_impulse_3d(ba, bb, ra, rb, axis * m_imp);
                    }

                    if (j.enable_limit)
                    {
                        const float slide_pos = cd::math::dot(d, axis);
                        float lim_err = 0.0F;
                        if (slide_pos < j.min_translation)
                        {
                            lim_err = slide_pos - j.min_translation;
                        }
                        else if (slide_pos > j.max_translation)
                        {
                            lim_err = slide_pos - j.max_translation;
                        }
                        if (std::abs(lim_err) > 1.0e-5F)
                        {
                            const cd::math::Vec3f v_now =
                                (bb.linear_velocity + cd::math::cross(bb.angular_velocity, rb)) -
                                (ba.linear_velocity + cd::math::cross(ba.angular_velocity, ra));
                            const float v_ax = cd::math::dot(v_now, axis);
                            const float imp = -(v_ax + (j.baumgarte_erp / dt) * lim_err) / k_ax;
                            detail::apply_pair_impulse_3d(ba, bb, ra, rb, axis * imp);
                        }
                    }
                }
            }
            else if constexpr (std::is_same_v<T, ConeTwistJoint3D>)
            {
                detail::solve_point_to_point_3d(ba, bb, j.local_anchor_a, j.local_anchor_b, dt, j.baumgarte_erp);

                const cd::math::Vec3f twist_a = cd::math::normalize(ba.transform.rotate_vector(j.local_twist_axis_a));
                const cd::math::Vec3f twist_b = cd::math::normalize(bb.transform.rotate_vector(j.local_twist_axis_b));
                const float cos_swing = std::clamp(cd::math::dot(twist_a, twist_b), -1.0F, 1.0F);
                const float swing_angle = std::acos(cos_swing);

                if (swing_angle > j.swing_span_rad)
                {
                    const cd::math::Vec3f swing_axis_raw = cd::math::cross(twist_a, twist_b);
                    const float len = cd::math::length(swing_axis_raw);
                    if (len > 1.0e-6F)
                    {
                        const cd::math::Vec3f swing_axis = swing_axis_raw / len;
                        const float k =
                            cd::math::dot(swing_axis, ba.mul_world_inv_inertia(swing_axis)) +
                            cd::math::dot(swing_axis, bb.mul_world_inv_inertia(swing_axis));
                        if (k > 1.0e-8F)
                        {
                            const float err = swing_angle - j.swing_span_rad;
                            const float w_swing = cd::math::dot(bb.angular_velocity - ba.angular_velocity, swing_axis);
                            const float imp = -(w_swing + (j.baumgarte_erp / dt) * err) / k;
                            const cd::math::Vec3f j_vec = swing_axis * imp;
                            if (ba.is_dynamic())
                            {
                                ba.angular_velocity -= ba.mul_world_inv_inertia(j_vec);
                            }
                            if (bb.is_dynamic())
                            {
                                bb.angular_velocity += bb.mul_world_inv_inertia(j_vec);
                            }
                        }
                    }
                }

                // Twist limit around twist_a
                cd::math::Quatf q_rel = cd::math::conjugate(ba.transform.rotation) * bb.transform.rotation;
                if (q_rel.w < 0.0F)
                {
                    q_rel = { -q_rel.x, -q_rel.y, -q_rel.z, -q_rel.w };
                }
                const float twist_proj = cd::math::dot(
                    cd::math::Vec3f { q_rel.x, q_rel.y, q_rel.z },
                    cd::math::normalize(j.local_twist_axis_a));
                const float twist_angle = 2.0F * std::atan2(twist_proj, q_rel.w);
                if (std::abs(twist_angle) > j.twist_limit_rad)
                {
                    const float err = (twist_angle > 0.0F)
                                          ? (twist_angle - j.twist_limit_rad)
                                          : (twist_angle + j.twist_limit_rad);
                    const float k =
                        cd::math::dot(twist_a, ba.mul_world_inv_inertia(twist_a)) +
                        cd::math::dot(twist_a, bb.mul_world_inv_inertia(twist_a));
                    if (k > 1.0e-8F)
                    {
                        const float w_twist = cd::math::dot(bb.angular_velocity - ba.angular_velocity, twist_a);
                        const float imp = -(w_twist + (j.baumgarte_erp / dt) * err) / k;
                        const cd::math::Vec3f j_vec = twist_a * imp;
                        if (ba.is_dynamic())
                        {
                            ba.angular_velocity -= ba.mul_world_inv_inertia(j_vec);
                        }
                        if (bb.is_dynamic())
                        {
                            bb.angular_velocity += bb.mul_world_inv_inertia(j_vec);
                        }
                    }
                }
            }
            else if constexpr (std::is_same_v<T, SixDofSpringJoint3D>)
            {
                const cd::math::Vec3f wa = ba.transform.transform_point(j.local_anchor_a);
                const cd::math::Vec3f wb = bb.transform.transform_point(j.local_anchor_b);
                const cd::math::Vec3f ra = wa - ba.transform.position;
                const cd::math::Vec3f rb = wb - bb.transform.position;
                const cd::math::Vec3f target_world = wa + ba.transform.rotate_vector(j.target_offset);
                const cd::math::Vec3f disp = wb - target_world;

                const cd::math::Vec3f va = ba.linear_velocity + cd::math::cross(ba.angular_velocity, ra);
                const cd::math::Vec3f vb = bb.linear_velocity + cd::math::cross(bb.angular_velocity, rb);
                const cd::math::Vec3f v_rel = vb - va;

                // Implicit spring impulse: J = -dt * (k * x + c * v) / (1 + dt * (c + dt * k) * inv_m_eff)
                static constexpr std::array<cd::math::Vec3f, 3> kAxes { {
                    { 1.0F, 0.0F, 0.0F },
                    { 0.0F, 1.0F, 0.0F },
                    { 0.0F, 0.0F, 1.0F }
                } };

                for (std::size_t i = 0u; i < 3u; ++i)
                {
                    const float k_eff = detail::compute_effective_inv_mass_3d(ba, bb, ra, rb, kAxes[i]);
                    const float ks = j.linear_stiffness[i];
                    const float kd = j.linear_damping[i];
                    const float denom = 1.0F + dt * (kd + dt * ks) * k_eff;
                    if (denom > 1.0e-8F)
                    {
                        const float imp = -dt * (ks * disp[i] + kd * v_rel[i]) / denom;
                        detail::apply_pair_impulse_3d(ba, bb, ra, rb, kAxes[i] * imp);
                    }
                }

                // Angular 3-DOF spring
                const cd::math::Quatf q_target_b = ba.transform.rotation * j.target_rotation;
                cd::math::Quatf q_err = bb.transform.rotation * cd::math::conjugate(q_target_b);
                if (q_err.w < 0.0F)
                {
                    q_err = { -q_err.x, -q_err.y, -q_err.z, -q_err.w };
                }
                const cd::math::Vec3f ang_disp { 2.0F * q_err.x, 2.0F * q_err.y, 2.0F * q_err.z };
                const cd::math::Vec3f w_rel = bb.angular_velocity - ba.angular_velocity;

                for (std::size_t i = 0u; i < 3u; ++i)
                {
                    const float k_ang =
                        cd::math::dot(kAxes[i], ba.mul_world_inv_inertia(kAxes[i])) +
                        cd::math::dot(kAxes[i], bb.mul_world_inv_inertia(kAxes[i]));
                    const float ks = j.angular_stiffness[i];
                    const float kd = j.angular_damping[i];
                    const float denom = 1.0F + dt * (kd + dt * ks) * k_ang;
                    if (denom > 1.0e-8F)
                    {
                        const float imp = -dt * (ks * ang_disp[i] + kd * w_rel[i]) / denom;
                        const cd::math::Vec3f j_vec = kAxes[i] * imp;
                        if (ba.is_dynamic())
                        {
                            ba.angular_velocity -= ba.mul_world_inv_inertia(j_vec);
                        }
                        if (bb.is_dynamic())
                        {
                            bb.angular_velocity += bb.mul_world_inv_inertia(j_vec);
                        }
                    }
                }
            }
        },
        joint);
}

}  // namespace cd::physics::v3d

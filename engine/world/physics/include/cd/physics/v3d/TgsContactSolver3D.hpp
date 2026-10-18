// =============================================================================
// CHROMODYNAMIC — cd/physics/v3d/TgsContactSolver3D.hpp
// Jolt / Source 2 Rubikon style 6-DOF Temporal Gauss-Seidel (TGS) contact
// impulse solver with isotropic 2-tangent Coulomb friction cone projection,
// warm-starting, and Baumgarte / ERP position error correction.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/v3d/ContactManifold3D.hpp>
#include <cd/physics/v3d/RigidBody3D.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace cd::physics::v3d
{

struct TgsSolverConfig3D
{
    float warm_start_factor { 0.85F };
    float baumgarte_erp { 0.25F };
    float position_erp { 0.35F };
    float linear_slop { 0.002F };
    float restitution_velocity_threshold { 1.0F };
    float max_penetration_correction { 0.25F };
};

namespace detail
{

[[nodiscard]] inline float compute_effective_inv_mass_3d(
    const RigidBody3D& body_a,
    const RigidBody3D& body_b,
    cd::math::Vec3f r_a,
    cd::math::Vec3f r_b,
    cd::math::Vec3f dir) noexcept
{
    const float inv_m = body_a.inv_mass + body_b.inv_mass;
    const cd::math::Vec3f ra_x_u = cd::math::cross(r_a, dir);
    const cd::math::Vec3f rb_x_u = cd::math::cross(r_b, dir);
    const float ang_a = cd::math::dot(ra_x_u, body_a.mul_world_inv_inertia(ra_x_u));
    const float ang_b = cd::math::dot(rb_x_u, body_b.mul_world_inv_inertia(rb_x_u));
    return inv_m + ang_a + ang_b;
}

inline void apply_pair_impulse_3d(
    RigidBody3D& body_a,
    RigidBody3D& body_b,
    cd::math::Vec3f r_a,
    cd::math::Vec3f r_b,
    cd::math::Vec3f impulse) noexcept
{
    if (body_a.is_dynamic())
    {
        body_a.linear_velocity -= impulse * body_a.inv_mass;
        body_a.angular_velocity -= body_a.mul_world_inv_inertia(cd::math::cross(r_a, impulse));
    }
    if (body_b.is_dynamic())
    {
        body_b.linear_velocity += impulse * body_b.inv_mass;
        body_b.angular_velocity += body_b.mul_world_inv_inertia(cd::math::cross(r_b, impulse));
    }
}

[[nodiscard]] inline RigidBody3D* resolve_body_for_solver_3d(
    std::span<RigidBody3D> bodies,
    std::uint32_t id_or_idx) noexcept
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

}  // namespace detail

inline void warm_start_3d(
    ContactManifold3D& manifold,
    RigidBody3D& body_a,
    RigidBody3D& body_b,
    float warm_start_factor = 0.85F) noexcept
{
    if (manifold.empty() || (!body_a.is_dynamic() && !body_b.is_dynamic()))
    {
        return;
    }

    manifold.compute_tangent_basis();

    for (std::uint8_t i = 0u; i < manifold.point_count; ++i)
    {
        ContactPoint3D& cp = manifold.points[i];
        cp.normal_impulse *= warm_start_factor;
        cp.tangent_impulse_1 *= warm_start_factor;
        cp.tangent_impulse_2 *= warm_start_factor;

        const cd::math::Vec3f r_a = cp.world_point - body_a.transform.position;
        const cd::math::Vec3f r_b = cp.world_point - body_b.transform.position;
        const cd::math::Vec3f impulse =
            manifold.normal * cp.normal_impulse +
            manifold.tangent_1 * cp.tangent_impulse_1 +
            manifold.tangent_2 * cp.tangent_impulse_2;

        detail::apply_pair_impulse_3d(body_a, body_b, r_a, r_b, impulse);
    }
}

inline void warm_start_3d(
    std::span<ContactManifold3D> manifolds,
    std::span<RigidBody3D> bodies,
    float warm_start_factor = 0.85F) noexcept
{
    for (ContactManifold3D& m : manifolds)
    {
        RigidBody3D* ba = detail::resolve_body_for_solver_3d(bodies, m.body_a);
        RigidBody3D* bb = detail::resolve_body_for_solver_3d(bodies, m.body_b);
        if (ba != nullptr && bb != nullptr && ba != bb)
        {
            warm_start_3d(m, *ba, *bb, warm_start_factor);
        }
    }
}

inline void solve_velocity_constraints_3d(
    ContactManifold3D& manifold,
    RigidBody3D& body_a,
    RigidBody3D& body_b,
    float dt,
    const TgsSolverConfig3D& cfg = {}) noexcept
{
    if (manifold.empty() || (!body_a.is_dynamic() && !body_b.is_dynamic()))
    {
        return;
    }

    const cd::math::Vec3f n = manifold.normal;
    const cd::math::Vec3f t1 = manifold.tangent_1;
    const cd::math::Vec3f t2 = manifold.tangent_2;
    const float mu = std::max(0.0F, manifold.combined_friction);
    const float e = std::clamp(manifold.combined_restitution, 0.0F, 1.0F);
    const float inv_dt = (dt > 1.0e-6F) ? (1.0F / dt) : 0.0F;

    for (std::uint8_t i = 0u; i < manifold.point_count; ++i)
    {
        ContactPoint3D& cp = manifold.points[i];
        const cd::math::Vec3f r_a = cp.world_point - body_a.transform.position;
        const cd::math::Vec3f r_b = cp.world_point - body_b.transform.position;

        // 1. Normal non-penetration + restitution impulse
        const cd::math::Vec3f v_a = body_a.linear_velocity + cd::math::cross(body_a.angular_velocity, r_a);
        const cd::math::Vec3f v_b = body_b.linear_velocity + cd::math::cross(body_b.angular_velocity, r_b);
        const cd::math::Vec3f v_rel = v_b - v_a;
        const float vn = cd::math::dot(v_rel, n);

        const float k_n = detail::compute_effective_inv_mass_3d(body_a, body_b, r_a, r_b, n);
        if (k_n > 1.0e-8F)
        {
            const float bounce = (vn < -cfg.restitution_velocity_threshold) ? (-e * vn) : 0.0F;
            const float pen_excess = std::max(0.0F, cp.penetration - cfg.linear_slop);
            const float baumgarte_bias = std::min(4.0F, cfg.baumgarte_erp * inv_dt * pen_excess);
            const float target_vn = std::max(bounce, baumgarte_bias);

            const float d_jn = (target_vn - vn) / k_n;
            const float old_jn = cp.normal_impulse;
            cp.normal_impulse = std::max(0.0F, old_jn + d_jn);
            const float applied_jn = cp.normal_impulse - old_jn;

            detail::apply_pair_impulse_3d(body_a, body_b, r_a, r_b, n * applied_jn);
        }

        // 2. 2-Tangent isotropic Coulomb friction cone
        const cd::math::Vec3f v_a_f = body_a.linear_velocity + cd::math::cross(body_a.angular_velocity, r_a);
        const cd::math::Vec3f v_b_f = body_b.linear_velocity + cd::math::cross(body_b.angular_velocity, r_b);
        const cd::math::Vec3f v_rel_f = v_b_f - v_a_f;

        const float vt1 = cd::math::dot(v_rel_f, t1);
        const float vt2 = cd::math::dot(v_rel_f, t2);
        const float k_t1 = detail::compute_effective_inv_mass_3d(body_a, body_b, r_a, r_b, t1);
        const float k_t2 = detail::compute_effective_inv_mass_3d(body_a, body_b, r_a, r_b, t2);

        const float d_jt1 = (k_t1 > 1.0e-8F) ? (-vt1 / k_t1) : 0.0F;
        const float d_jt2 = (k_t2 > 1.0e-8F) ? (-vt2 / k_t2) : 0.0F;

        const float old_jt1 = cp.tangent_impulse_1;
        const float old_jt2 = cp.tangent_impulse_2;
        float new_jt1 = old_jt1 + d_jt1;
        float new_jt2 = old_jt2 + d_jt2;

        const float max_friction = mu * cp.normal_impulse;
        const float mag_sq = new_jt1 * new_jt1 + new_jt2 * new_jt2;
        if (mag_sq > max_friction * max_friction && mag_sq > 1.0e-12F)
        {
            const float scale = max_friction / std::sqrt(mag_sq);
            new_jt1 *= scale;
            new_jt2 *= scale;
        }

        cp.tangent_impulse_1 = new_jt1;
        cp.tangent_impulse_2 = new_jt2;

        const cd::math::Vec3f friction_impulse =
            t1 * (new_jt1 - old_jt1) + t2 * (new_jt2 - old_jt2);
        detail::apply_pair_impulse_3d(body_a, body_b, r_a, r_b, friction_impulse);
    }
}

inline void solve_velocity_constraints_3d(
    std::span<ContactManifold3D> manifolds,
    std::span<RigidBody3D> bodies,
    float dt,
    const TgsSolverConfig3D& cfg = {}) noexcept
{
    for (ContactManifold3D& m : manifolds)
    {
        RigidBody3D* ba = detail::resolve_body_for_solver_3d(bodies, m.body_a);
        RigidBody3D* bb = detail::resolve_body_for_solver_3d(bodies, m.body_b);
        if (ba != nullptr && bb != nullptr && ba != bb)
        {
            solve_velocity_constraints_3d(m, *ba, *bb, dt, cfg);
        }
    }
}

inline void solve_position_constraints_3d(
    ContactManifold3D& manifold,
    RigidBody3D& body_a,
    RigidBody3D& body_b,
    const TgsSolverConfig3D& cfg = {}) noexcept
{
    if (manifold.empty() || (!body_a.is_dynamic() && !body_b.is_dynamic()))
    {
        return;
    }

    const float inv_mass_sum = body_a.inv_mass + body_b.inv_mass;
    if (inv_mass_sum <= 1.0e-8F)
    {
        return;
    }

    float max_pen = 0.0F;
    for (std::uint8_t i = 0u; i < manifold.point_count; ++i)
    {
        max_pen = std::max(max_pen, manifold.points[i].penetration);
    }

    const float slop_excess = std::max(0.0F, max_pen - cfg.linear_slop);
    if (slop_excess <= 0.0F)
    {
        return;
    }

    const float correction_mag = std::min(cfg.max_penetration_correction, cfg.position_erp * slop_excess);
    const cd::math::Vec3f impulse = manifold.normal * (correction_mag / inv_mass_sum);

    if (body_a.is_dynamic())
    {
        body_a.transform.position -= impulse * body_a.inv_mass;
    }
    if (body_b.is_dynamic())
    {
        body_b.transform.position += impulse * body_b.inv_mass;
    }

    for (std::uint8_t i = 0u; i < manifold.point_count; ++i)
    {
        manifold.points[i].penetration = std::max(0.0F, manifold.points[i].penetration - correction_mag);
    }
}

inline void solve_position_constraints_3d(
    std::span<ContactManifold3D> manifolds,
    std::span<RigidBody3D> bodies,
    const TgsSolverConfig3D& cfg = {}) noexcept
{
    for (ContactManifold3D& m : manifolds)
    {
        RigidBody3D* ba = detail::resolve_body_for_solver_3d(bodies, m.body_a);
        RigidBody3D* bb = detail::resolve_body_for_solver_3d(bodies, m.body_b);
        if (ba != nullptr && bb != nullptr && ba != bb)
        {
            solve_position_constraints_3d(m, *ba, *bb, cfg);
        }
    }
}

}  // namespace cd::physics::v3d

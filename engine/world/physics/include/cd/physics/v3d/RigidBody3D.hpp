// =============================================================================
// CHROMODYNAMIC — cd/physics/v3d/RigidBody3D.hpp
// 6-DOF 3D rigid body state, world-space inverse inertia tensor transformation,
// implicit gyroscopic torque (Dzhanibekov effect) solver, and quaternion
// semi-implicit symplectic Euler integration.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/core/CollisionFilterMatrix.hpp>
#include <cd/physics/core/PhysicsTypes.hpp>
#include <cd/physics/v3d/Shapes3D.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cd::physics::v3d
{

struct RigidBody3D
{
    std::uint32_t id { 0u };
    core::BodyMotionType motion_type { core::BodyMotionType::kDynamic };
    core::Transform3D transform {};
    cd::math::Vec3f linear_velocity {};
    cd::math::Vec3f angular_velocity {};
    cd::math::Vec3f force_accum {};
    cd::math::Vec3f torque_accum {};
    float mass { 1.0F };
    float inv_mass { 1.0F };
    cd::math::Vec3f inv_inertia_local { 1.0F, 1.0F, 1.0F };
    std::array<float, 9> inv_inertia_world {
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 1.0F
    };
    float gravity_scale { 1.0F };
    float linear_damping { 0.01F };
    float angular_damping { 0.02F };
    core::PhysicsMaterial material {};
    core::CollisionFilter filter {};
    ShapeVariant3D shape { SphereShape3D {} };
    bool is_sensor { false };
    bool enable_ccd { false };
    bool enable_gyroscopic_torque { true };
    bool awake { true };
    float sleep_timer { 0.0F };
    std::int32_t bvh_proxy { -1 };

    [[nodiscard]] constexpr bool is_dynamic() const noexcept
    {
        return motion_type == core::BodyMotionType::kDynamic && inv_mass > 0.0F;
    }

    [[nodiscard]] constexpr bool is_kinematic() const noexcept
    {
        return motion_type == core::BodyMotionType::kKinematic;
    }

    [[nodiscard]] constexpr bool is_static() const noexcept
    {
        return motion_type == core::BodyMotionType::kStatic;
    }

    void update_world_inertia_tensor() noexcept
    {
        if (!is_dynamic())
        {
            inv_inertia_world.fill(0.0F);
            return;
        }

        const cd::math::Vec3f c0 = transform.rotate_vector({ 1.0F, 0.0F, 0.0F });
        const cd::math::Vec3f c1 = transform.rotate_vector({ 0.0F, 1.0F, 0.0F });
        const cd::math::Vec3f c2 = transform.rotate_vector({ 0.0F, 0.0F, 1.0F });

        const float ix = inv_inertia_local.x;
        const float iy = inv_inertia_local.y;
        const float iz = inv_inertia_local.z;

        // Symmetric 3x3 world inverse inertia: R * diag(ix, iy, iz) * R^T
        inv_inertia_world[0] = c0.x * c0.x * ix + c1.x * c1.x * iy + c2.x * c2.x * iz;
        inv_inertia_world[1] = c0.x * c0.y * ix + c1.x * c1.y * iy + c2.x * c2.y * iz;
        inv_inertia_world[2] = c0.x * c0.z * ix + c1.x * c1.z * iy + c2.x * c2.z * iz;

        inv_inertia_world[3] = inv_inertia_world[1];
        inv_inertia_world[4] = c0.y * c0.y * ix + c1.y * c1.y * iy + c2.y * c2.y * iz;
        inv_inertia_world[5] = c0.y * c0.z * ix + c1.y * c1.z * iy + c2.y * c2.z * iz;

        inv_inertia_world[6] = inv_inertia_world[2];
        inv_inertia_world[7] = inv_inertia_world[5];
        inv_inertia_world[8] = c0.z * c0.z * ix + c1.z * c1.z * iy + c2.z * c2.z * iz;
    }

    void recompute_mass_properties() noexcept
    {
        if (motion_type != core::BodyMotionType::kDynamic || mass <= 0.0F)
        {
            inv_mass = 0.0F;
            inv_inertia_local = { 0.0F, 0.0F, 0.0F };
            inv_inertia_world.fill(0.0F);
            return;
        }

        inv_mass = 1.0F / mass;
        const cd::math::Vec3f diag = compute_local_inertia_diagonal_3d(shape, mass);
        inv_inertia_local = {
            diag.x > 1.0e-7F ? (1.0F / diag.x) : 0.0F,
            diag.y > 1.0e-7F ? (1.0F / diag.y) : 0.0F,
            diag.z > 1.0e-7F ? (1.0F / diag.z) : 0.0F
        };
        update_world_inertia_tensor();
    }

    [[nodiscard]] constexpr cd::math::Vec3f mul_world_inv_inertia(cd::math::Vec3f v) const noexcept
    {
        return {
            inv_inertia_world[0] * v.x + inv_inertia_world[1] * v.y + inv_inertia_world[2] * v.z,
            inv_inertia_world[3] * v.x + inv_inertia_world[4] * v.y + inv_inertia_world[5] * v.z,
            inv_inertia_world[6] * v.x + inv_inertia_world[7] * v.y + inv_inertia_world[8] * v.z
        };
    }

    [[nodiscard]] constexpr cd::math::Vec3f velocity_at_world_point(cd::math::Vec3f world_point) const noexcept
    {
        return linear_velocity + cd::math::cross(angular_velocity, world_point - transform.position);
    }

    constexpr void apply_force(cd::math::Vec3f force) noexcept
    {
        if (!is_dynamic())
        {
            return;
        }
        force_accum += force;
        awake = true;
        sleep_timer = 0.0F;
    }

    constexpr void apply_torque(cd::math::Vec3f torque) noexcept
    {
        if (!is_dynamic())
        {
            return;
        }
        torque_accum += torque;
        awake = true;
        sleep_timer = 0.0F;
    }

    constexpr void apply_force_at_point(cd::math::Vec3f force, cd::math::Vec3f world_point) noexcept
    {
        if (!is_dynamic())
        {
            return;
        }
        force_accum += force;
        torque_accum += cd::math::cross(world_point - transform.position, force);
        awake = true;
        sleep_timer = 0.0F;
    }

    constexpr void apply_linear_impulse(cd::math::Vec3f impulse) noexcept
    {
        if (!is_dynamic())
        {
            return;
        }
        linear_velocity += impulse * inv_mass;
        awake = true;
        sleep_timer = 0.0F;
    }

    constexpr void apply_angular_impulse(cd::math::Vec3f angular_impulse) noexcept
    {
        if (!is_dynamic())
        {
            return;
        }
        angular_velocity += mul_world_inv_inertia(angular_impulse);
        awake = true;
        sleep_timer = 0.0F;
    }

    constexpr void apply_impulse_at_point(cd::math::Vec3f impulse, cd::math::Vec3f world_point) noexcept
    {
        if (!is_dynamic())
        {
            return;
        }
        linear_velocity += impulse * inv_mass;
        angular_velocity += mul_world_inv_inertia(cd::math::cross(world_point - transform.position, impulse));
        awake = true;
        sleep_timer = 0.0F;
    }

    void integrate_velocities(float dt, cd::math::Vec3f gravity) noexcept
    {
        if (!is_dynamic() || !awake || dt <= 0.0F)
        {
            force_accum = {};
            torque_accum = {};
            return;
        }

        linear_velocity += (gravity * gravity_scale + force_accum * inv_mass) * dt;
        angular_velocity += mul_world_inv_inertia(torque_accum) * dt;

        // Implicit Euler gyroscopic torque correction: I * dw/dt + w x (I * w) = 0
        if (enable_gyroscopic_torque &&
            inv_inertia_local.x > 1.0e-7F &&
            inv_inertia_local.y > 1.0e-7F &&
            inv_inertia_local.z > 1.0e-7F)
        {
            const cd::math::Vec3f ib {
                1.0F / inv_inertia_local.x,
                1.0F / inv_inertia_local.y,
                1.0F / inv_inertia_local.z
            };
            cd::math::Vec3f wb = transform.inverse_rotate_vector(angular_velocity);

            // Residual f(wb) = dt * (wb x (Ib * wb))
            const cd::math::Vec3f l_b { ib.x * wb.x, ib.y * wb.y, ib.z * wb.z };
            const cd::math::Vec3f f = cd::math::cross(wb, l_b) * dt;
            wb.x -= f.x * inv_inertia_local.x;
            wb.y -= f.y * inv_inertia_local.y;
            wb.z -= f.z * inv_inertia_local.z;
            angular_velocity = transform.rotate_vector(wb);
        }

        const float lin_damp = 1.0F / (1.0F + dt * std::max(0.0F, linear_damping));
        const float ang_damp = 1.0F / (1.0F + dt * std::max(0.0F, angular_damping));
        linear_velocity *= lin_damp;
        angular_velocity *= ang_damp;

        force_accum = {};
        torque_accum = {};
    }

    void integrate_positions(float dt) noexcept
    {
        if (is_static() || !awake || dt <= 0.0F)
        {
            return;
        }

        transform.position += linear_velocity * dt;

        if (cd::math::length_squared(angular_velocity) > 1.0e-12F)
        {
            // Quaternion kinematic differential equation: dq/dt = 0.5 * omega_quat * q
            const cd::math::Quatf w_q { angular_velocity.x, angular_velocity.y, angular_velocity.z, 0.0F };
            const cd::math::Quatf dq = w_q * transform.rotation;
            const float half_dt = 0.5F * dt;
            transform.rotation = cd::math::normalize(cd::math::Quatf {
                transform.rotation.x + half_dt * dq.x,
                transform.rotation.y + half_dt * dq.y,
                transform.rotation.z + half_dt * dq.z,
                transform.rotation.w + half_dt * dq.w
            });
        }

        update_world_inertia_tensor();
    }
};

}  // namespace cd::physics::v3d

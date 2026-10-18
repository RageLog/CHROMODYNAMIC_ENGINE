// =============================================================================
// CHROMODYNAMIC — cd/physics/v2d/RigidBody2D.hpp
// Box2D v3 / Godot 4 grade 2D rigid body state, force/impulse accumulators,
// and semi-implicit Euler velocity/position integration with implicit damping.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>
#include <cd/physics/core/CollisionFilterMatrix.hpp>
#include <cd/physics/core/PhysicsTypes.hpp>
#include <cd/physics/v2d/Shapes2D.hpp>

#include <algorithm>
#include <cstdint>

namespace cd::physics::v2d
{

struct RigidBody2D
{
    std::uint32_t id { 0u };
    core::BodyMotionType motion_type { core::BodyMotionType::kDynamic };
    core::Transform2D transform {};
    cd::math::Vec2f linear_velocity { 0.0F, 0.0F };
    float angular_velocity { 0.0F };
    cd::math::Vec2f force_accum { 0.0F, 0.0F };
    float torque_accum { 0.0F };
    float mass { 1.0F };
    float inv_mass { 1.0F };
    float inertia { 1.0F };
    float inv_inertia { 1.0F };
    float gravity_scale { 1.0F };
    float linear_damping { 0.0F };
    float angular_damping { 0.0F };
    core::PhysicsMaterial material {};
    core::CollisionFilter filter {};
    ShapeVariant2D shape { CircleShape2D {} };
    bool is_sensor { false };
    bool enable_ccd { false };
    bool awake { true };
    float sleep_timer { 0.0F };
    std::int32_t bvh_proxy { -1 };

    [[nodiscard]] constexpr bool is_dynamic() const noexcept
    {
        return motion_type == core::BodyMotionType::kDynamic;
    }

    [[nodiscard]] constexpr bool is_kinematic() const noexcept
    {
        return motion_type == core::BodyMotionType::kKinematic;
    }

    [[nodiscard]] constexpr bool is_static() const noexcept
    {
        return motion_type == core::BodyMotionType::kStatic;
    }

    void recompute_mass_properties() noexcept
    {
        if (motion_type != core::BodyMotionType::kDynamic)
        {
            mass        = 0.0F;
            inv_mass    = 0.0F;
            inertia     = 0.0F;
            inv_inertia = 0.0F;
            return;
        }
        const MassProperties2D mp = compute_mass_properties_2d(shape, material.density);
        mass                      = mp.mass;
        inv_mass                  = mp.inv_mass;
        inertia                   = mp.inertia;
        inv_inertia               = mp.inv_inertia;
    }

    void clear_forces() noexcept
    {
        force_accum  = { 0.0F, 0.0F };
        torque_accum = 0.0F;
    }

    void apply_force(cd::math::Vec2f force) noexcept
    {
        if (!is_dynamic())
        {
            return;
        }
        awake       = true;
        sleep_timer = 0.0F;
        force_accum += force;
    }

    void apply_force_at_point(cd::math::Vec2f force, cd::math::Vec2f world_point) noexcept
    {
        if (!is_dynamic())
        {
            return;
        }
        awake       = true;
        sleep_timer = 0.0F;
        force_accum += force;
        const cd::math::Vec2f r = world_point - transform.position;
        torque_accum += detail::cross_2d(r, force);
    }

    void apply_linear_impulse(cd::math::Vec2f impulse) noexcept
    {
        if (!is_dynamic())
        {
            return;
        }
        awake       = true;
        sleep_timer = 0.0F;
        linear_velocity += impulse * inv_mass;
    }

    void apply_impulse_at_point(cd::math::Vec2f impulse, cd::math::Vec2f world_point) noexcept
    {
        if (!is_dynamic())
        {
            return;
        }
        awake       = true;
        sleep_timer = 0.0F;
        linear_velocity += impulse * inv_mass;
        const cd::math::Vec2f r = world_point - transform.position;
        angular_velocity += detail::cross_2d(r, impulse) * inv_inertia;
    }

    void integrate_velocities(float dt, cd::math::Vec2f gravity) noexcept
    {
        if (!is_dynamic() || !awake || dt <= 0.0F)
        {
            clear_forces();
            return;
        }

        linear_velocity += (gravity * gravity_scale + force_accum * inv_mass) * dt;
        angular_velocity += (torque_accum * inv_inertia) * dt;

        if (linear_damping > 0.0F)
        {
            linear_velocity *= 1.0F / (1.0F + dt * linear_damping);
        }
        if (angular_damping > 0.0F)
        {
            angular_velocity *= 1.0F / (1.0F + dt * angular_damping);
        }

        clear_forces();
    }

    void integrate_positions(float dt) noexcept
    {
        if (is_static() || !awake || dt <= 0.0F)
        {
            return;
        }
        transform.position += linear_velocity * dt;
        transform.rotation_rad += angular_velocity * dt;
    }
};

}  // namespace cd::physics::v2d

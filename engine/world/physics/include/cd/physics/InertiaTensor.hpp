// =============================================================================
// CHROMODYNAMIC — cd/physics/InertiaTensor.hpp
// Phase 61.A / Wave 229 — rigid-body mass + diagonal inertia tensor.
//
// For most primitive shapes the inertia tensor (in the local body
// frame, computed around the center of mass) is diagonal. We store
// it as a `Vec3f { Ixx, Iyy, Izz }` plus a scalar mass.
//
// Reference closed forms for unit-density:
//   * solid sphere (radius r):
//       I = (2/5) m r²   on every axis
//   * solid box (full extents x, y, z, total mass m):
//       Ixx = (1/12) m (y² + z²)
//       Iyy = (1/12) m (x² + z²)
//       Izz = (1/12) m (x² + y²)
//   * solid cylinder (radius r, height h, mass m, Y-axis):
//       Ixx = Izz = (1/12) m (3 r² + h²)
//       Iyy = (1/2) m r²
//
// `RigidBodyMass { mass, inertia, inv_mass, inv_inertia }` is the
// final data the solver consumes. inv_* are precomputed reciprocals
// (or 0 when mass / Ixx are zero → kinematic body).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

namespace cd::physics
{

struct RigidBodyMass
{
    float            mass        { 0.0F };
    cd::math::Vec3f  inertia     {};
    float            inv_mass    { 0.0F };
    cd::math::Vec3f  inv_inertia {};
};

namespace detail
{
[[nodiscard]] constexpr float inv_or_zero(float x) noexcept
{
    return (x > 0.0F) ? (1.0F / x) : 0.0F;
}
}  // namespace detail

[[nodiscard]] inline RigidBodyMass solid_sphere(float mass, float radius) noexcept
{
    const float i = 0.4F * mass * radius * radius;
    RigidBodyMass m;
    m.mass = mass;
    m.inertia = { i, i, i };
    m.inv_mass = detail::inv_or_zero(mass);
    m.inv_inertia = { detail::inv_or_zero(i), detail::inv_or_zero(i), detail::inv_or_zero(i) };
    return m;
}

[[nodiscard]] inline RigidBodyMass solid_box(float mass, float x, float y, float z) noexcept
{
    const float c = mass / 12.0F;
    const float ix = c * (y * y + z * z);
    const float iy = c * (x * x + z * z);
    const float iz = c * (x * x + y * y);
    RigidBodyMass m;
    m.mass = mass;
    m.inertia = { ix, iy, iz };
    m.inv_mass = detail::inv_or_zero(mass);
    m.inv_inertia = { detail::inv_or_zero(ix), detail::inv_or_zero(iy), detail::inv_or_zero(iz) };
    return m;
}

[[nodiscard]] inline RigidBodyMass solid_cylinder_y(float mass, float radius, float height) noexcept
{
    const float ixz = mass * (3.0F * radius * radius + height * height) / 12.0F;
    const float iy  = 0.5F * mass * radius * radius;
    RigidBodyMass m;
    m.mass = mass;
    m.inertia = { ixz, iy, ixz };
    m.inv_mass = detail::inv_or_zero(mass);
    m.inv_inertia = { detail::inv_or_zero(ixz), detail::inv_or_zero(iy), detail::inv_or_zero(ixz) };
    return m;
}

[[nodiscard]] inline RigidBodyMass kinematic() noexcept
{
    return RigidBodyMass {};   // all-zero — infinite mass solver convention
}

}  // namespace cd::physics

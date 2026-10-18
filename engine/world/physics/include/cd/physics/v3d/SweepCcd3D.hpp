// =============================================================================
// CHROMODYNAMIC — cd/physics/v3d/SweepCcd3D.hpp
// 3D analytical + GJK raycasting and Continuous Collision Detection (CCD)
// Time-of-Impact (TOI) conservative advancement solver.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/core/PhysicsTypes.hpp>
#include <cd/physics/v3d/Narrowphase3D.hpp>
#include <cd/physics/v3d/Shapes3D.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <variant>

namespace cd::physics::v3d
{

struct RaycastHit3D
{
    bool hit { false };
    std::uint32_t body_id { 0u };
    float fraction { 1.0F };
    cd::math::Vec3f point {};
    cd::math::Vec3f normal {};
};

namespace detail
{

[[nodiscard]] inline bool raycast_sphere_local(
    cd::math::Vec3f o,
    cd::math::Vec3f d,
    float max_dist,
    float radius,
    float& out_t,
    cd::math::Vec3f& out_normal) noexcept
{
    const float b = cd::math::dot(o, d);
    const float c = cd::math::dot(o, o) - radius * radius;
    if (c <= 0.0F)
    {
        out_t = 0.0F;
        out_normal = (cd::math::length_squared(o) > 1.0e-8F) ? cd::math::normalize(o) : -d;
        return true;
    }
    if (b > 0.0F)
    {
        return false;
    }
    const float disc = b * b - c;
    if (disc < 0.0F)
    {
        return false;
    }
    const float t = -b - std::sqrt(disc);
    if (t < 0.0F || t > max_dist)
    {
        return false;
    }
    out_t = t;
    out_normal = cd::math::normalize(o + d * t);
    return true;
}

[[nodiscard]] inline bool raycast_box_local(
    cd::math::Vec3f o,
    cd::math::Vec3f d,
    float max_dist,
    cd::math::Vec3f he,
    float& out_t,
    cd::math::Vec3f& out_normal) noexcept
{
    float tmin = 0.0F;
    float tmax = max_dist;
    cd::math::Vec3f hit_n { 0.0F, 1.0F, 0.0F };
    bool inside = true;

    for (std::size_t i = 0u; i < 3u; ++i)
    {
        if (std::abs(o[i]) > he[i])
        {
            inside = false;
        }
        if (std::abs(d[i]) < 1.0e-8F)
        {
            if (o[i] < -he[i] || o[i] > he[i])
            {
                return false;
            }
        }
        else
        {
            const float inv_d = 1.0F / d[i];
            float t1 = (-he[i] - o[i]) * inv_d;
            float t2 = (+he[i] - o[i]) * inv_d;
            float n_sign = -1.0F;
            if (t1 > t2)
            {
                std::swap(t1, t2);
                n_sign = 1.0F;
            }
            if (t1 > tmin)
            {
                tmin = t1;
                hit_n = { 0.0F, 0.0F, 0.0F };
                hit_n[i] = n_sign;
            }
            tmax = std::min(tmax, t2);
            if (tmin > tmax)
            {
                return false;
            }
        }
    }

    if (inside)
    {
        out_t = 0.0F;
        out_normal = -d;
        return true;
    }
    out_t = tmin;
    out_normal = hit_n;
    return true;
}

[[nodiscard]] inline bool raycast_cylinder_local(
    cd::math::Vec3f o,
    cd::math::Vec3f d,
    float max_dist,
    float hh,
    float radius,
    float& out_t,
    cd::math::Vec3f& out_normal) noexcept
{
    float best_t = std::numeric_limits<float>::max();
    cd::math::Vec3f best_n { 0.0F, 1.0F, 0.0F };
    bool hit = false;

    // Lateral infinite cylinder x^2 + z^2 = r^2 clamped to y in [-hh, +hh].
    const float a = d.x * d.x + d.z * d.z;
    const float b = o.x * d.x + o.z * d.z;
    const float c = o.x * o.x + o.z * o.z - radius * radius;

    if (c <= 0.0F && std::abs(o.y) <= hh)
    {
        out_t = 0.0F;
        out_normal = -d;
        return true;
    }

    if (a > 1.0e-8F)
    {
        const float disc = b * b - a * c;
        if (disc >= 0.0F)
        {
            const float t = (-b - std::sqrt(disc)) / a;
            if (t >= 0.0F && t <= max_dist)
            {
                const float y = o.y + d.y * t;
                if (std::abs(y) <= hh)
                {
                    best_t = t;
                    best_n = cd::math::normalize(cd::math::Vec3f { o.x + d.x * t, 0.0F, o.z + d.z * t });
                    hit = true;
                }
            }
        }
    }

    // Top and bottom disc caps at y = +hh and y = -hh.
    if (std::abs(d.y) > 1.0e-8F)
    {
        for (int s = -1; s <= 1; s += 2)
        {
            const float cap_y = static_cast<float>(s) * hh;
            const float t = (cap_y - o.y) / d.y;
            if (t >= 0.0F && t <= max_dist && t < best_t)
            {
                const float x = o.x + d.x * t;
                const float z = o.z + d.z * t;
                if (x * x + z * z <= radius * radius)
                {
                    best_t = t;
                    best_n = { 0.0F, static_cast<float>(s), 0.0F };
                    hit = true;
                }
            }
        }
    }

    if (hit)
    {
        out_t = best_t;
        out_normal = best_n;
    }
    return hit;
}

[[nodiscard]] inline bool raycast_capsule_local(
    cd::math::Vec3f o,
    cd::math::Vec3f d,
    float max_dist,
    float hh,
    float radius,
    float& out_t,
    cd::math::Vec3f& out_normal) noexcept
{
    float best_t = std::numeric_limits<float>::max();
    cd::math::Vec3f best_n { 0.0F, 1.0F, 0.0F };
    bool hit = false;

    // Lateral cylinder barrel.
    const float a = d.x * d.x + d.z * d.z;
    const float b = o.x * d.x + o.z * d.z;
    const float c = o.x * o.x + o.z * o.z - radius * radius;
    if (a > 1.0e-8F)
    {
        const float disc = b * b - a * c;
        if (disc >= 0.0F)
        {
            const float t = std::max(0.0F, (-b - std::sqrt(disc)) / a);
            if (t <= max_dist)
            {
                const float y = o.y + d.y * t;
                if (std::abs(y) <= hh && (c > 0.0F || std::abs(o.y) <= hh))
                {
                    best_t = t;
                    best_n = cd::math::normalize(cd::math::Vec3f { o.x + d.x * t, 0.0F, o.z + d.z * t });
                    hit = true;
                }
            }
        }
    }

    // Two hemisphere caps at (0, -hh, 0) and (0, +hh, 0).
    for (int s = -1; s <= 1; s += 2)
    {
        const cd::math::Vec3f cap_center { 0.0F, static_cast<float>(s) * hh, 0.0F };
        float t_cap = 0.0F;
        cd::math::Vec3f n_cap {};
        if (raycast_sphere_local(o - cap_center, d, max_dist, radius, t_cap, n_cap) && t_cap < best_t)
        {
            best_t = t_cap;
            best_n = n_cap;
            hit = true;
        }
    }

    if (hit)
    {
        out_t = best_t;
        out_normal = best_n;
    }
    return hit;
}

}  // namespace detail

[[nodiscard]] inline RaycastHit3D raycast_shape_3d(
    cd::math::Vec3f origin,
    cd::math::Vec3f dir,
    float max_dist,
    const ShapeVariant3D& shape,
    const core::Transform3D& xf) noexcept
{
    RaycastHit3D result {};
    if (max_dist <= 0.0F)
    {
        return result;
    }

    const float dir_len = cd::math::length(dir);
    if (dir_len <= 1.0e-8F)
    {
        return result;
    }
    const cd::math::Vec3f unit_dir = dir / dir_len;
    const float total_dist = max_dist * dir_len;

    const cd::math::Vec3f local_o = xf.inverse_transform_point(origin);
    const cd::math::Vec3f local_d = xf.inverse_rotate_vector(unit_dir);

    float hit_t = 0.0F;
    cd::math::Vec3f local_n { 0.0F, 1.0F, 0.0F };

    const bool did_hit = std::visit(
        [&](const auto& s) noexcept -> bool
        {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, SphereShape3D>)
            {
                return detail::raycast_sphere_local(local_o, local_d, total_dist, s.radius, hit_t, local_n);
            }
            else if constexpr (std::is_same_v<T, BoxShape3D>)
            {
                return detail::raycast_box_local(local_o, local_d, total_dist, s.half_extents, hit_t, local_n);
            }
            else if constexpr (std::is_same_v<T, CapsuleShape3D>)
            {
                return detail::raycast_capsule_local(
                    local_o, local_d, total_dist, s.half_height, s.radius, hit_t, local_n);
            }
            else if constexpr (std::is_same_v<T, CylinderShape3D>)
            {
                return detail::raycast_cylinder_local(
                    local_o, local_d, total_dist, s.half_height, s.radius, hit_t, local_n);
            }
            else if constexpr (std::is_same_v<T, HeightfieldShape3D>)
            {
                constexpr int kSteps = 64;
                const float dt = total_dist / static_cast<float>(kSteps);
                float prev_t = 0.0F;
                for (int i = 0; i <= kSteps; ++i)
                {
                    const float t = static_cast<float>(i) * dt;
                    const cd::math::Vec3f p = local_o + local_d * t;
                    float h = 0.0F;
                    cd::math::Vec3f n {};
                    if (s.sample_local(p.x, p.z, h, n) && p.y <= h)
                    {
                        float lo = prev_t;
                        float hi = t;
                        for (int b = 0; b < 8; ++b)
                        {
                            const float mid = 0.5F * (lo + hi);
                            const cd::math::Vec3f mp = local_o + local_d * mid;
                            float mh = 0.0F;
                            cd::math::Vec3f mn {};
                            if (s.sample_local(mp.x, mp.z, mh, mn) && mp.y <= mh)
                            {
                                hi = mid;
                                local_n = mn;
                            }
                            else
                            {
                                lo = mid;
                            }
                        }
                        hit_t = hi;
                        return true;
                    }
                    prev_t = t;
                }
                return false;
            }
            else if constexpr (std::is_same_v<T, ConvexHullShape3D>)
            {
                if (s.vertices.empty())
                {
                    return false;
                }
                const core::SpatialAabb3D aabb = compute_aabb_3d(shape, core::Transform3D::identity());
                return detail::raycast_box_local(
                    local_o - aabb.center(), local_d, total_dist, aabb.extents(), hit_t, local_n);
            }
        },
        shape);

    if (did_hit)
    {
        result.hit = true;
        result.fraction = std::clamp(hit_t / total_dist, 0.0F, 1.0F);
        result.point = origin + unit_dir * hit_t;
        result.normal = cd::math::normalize(xf.rotate_vector(local_n));
    }
    return result;
}

/// Computes the Continuous Collision Detection (CCD) Time-of-Impact (TOI) in [0, 1] over timestep `dt`.
[[nodiscard]] inline float sweep_shapes_toi_3d(
    const ShapeVariant3D& shape_a,
    const core::Transform3D& xf_a,
    cd::math::Vec3f vel_a,
    const ShapeVariant3D& shape_b,
    const core::Transform3D& xf_b,
    cd::math::Vec3f vel_b,
    float dt) noexcept
{
    if (dt <= 0.0F)
    {
        return 1.0F;
    }

    if (!collide_shapes_3d(shape_a, xf_a, shape_b, xf_b).empty())
    {
        return 0.0F;
    }

    const cd::math::Vec3f rel_disp = (vel_a - vel_b) * dt;
    const float disp_len = cd::math::length(rel_disp);
    if (disp_len <= 1.0e-6F)
    {
        return 1.0F;
    }

    // Fast exact analytical Minkowski sum path when A is a sphere.
    if (const auto* sph_a = std::get_if<SphereShape3D>(&shape_a))
    {
        if (const auto* sph_b = std::get_if<SphereShape3D>(&shape_b))
        {
            const SphereShape3D inflated { sph_a->radius + sph_b->radius };
            const RaycastHit3D hit = raycast_shape_3d(xf_a.position, rel_disp / disp_len, disp_len, inflated, xf_b);
            if (hit.hit)
            {
                return hit.fraction;
            }
            return 1.0F;
        }
        if (const auto* box_b = std::get_if<BoxShape3D>(&shape_b))
        {
            const BoxShape3D inflated {
                box_b->half_extents + cd::math::Vec3f { sph_a->radius, sph_a->radius, sph_a->radius }
            };
            const RaycastHit3D hit = raycast_shape_3d(xf_a.position, rel_disp / disp_len, disp_len, inflated, xf_b);
            if (hit.hit)
            {
                return hit.fraction;
            }
        }
    }

    // General Conservative Advancement + bisection root-finding over [0, 1].
    const core::SpatialAabb3D aabb_a = compute_aabb_3d(shape_a, xf_a);
    const cd::math::Vec3f ext_a = aabb_a.extents();
    const float min_feat = std::max(0.02F, std::min({ ext_a.x, ext_a.y, ext_a.z }) * 0.5F);
    const std::uint32_t steps = std::clamp(static_cast<std::uint32_t>(disp_len / min_feat) + 8u, 16u, 128u);

    float prev_t = 0.0F;
    for (std::uint32_t i = 1u; i <= steps; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        core::Transform3D step_a = xf_a;
        core::Transform3D step_b = xf_b;
        step_a.position += vel_a * (dt * t);
        step_b.position += vel_b * (dt * t);

        if (!collide_shapes_3d(shape_a, step_a, shape_b, step_b).empty())
        {
            float lo = prev_t;
            float hi = t;
            for (int iter = 0; iter < 10; ++iter)
            {
                const float mid = 0.5F * (lo + hi);
                core::Transform3D mid_a = xf_a;
                core::Transform3D mid_b = xf_b;
                mid_a.position += vel_a * (dt * mid);
                mid_b.position += vel_b * (dt * mid);
                if (!collide_shapes_3d(shape_a, mid_a, shape_b, mid_b).empty())
                {
                    hi = mid;
                }
                else
                {
                    lo = mid;
                }
            }
            return lo;
        }
        prev_t = t;
    }

    return 1.0F;
}

}  // namespace cd::physics::v3d

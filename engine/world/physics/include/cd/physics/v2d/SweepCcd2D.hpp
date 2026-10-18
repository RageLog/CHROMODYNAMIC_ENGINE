// =============================================================================
// CHROMODYNAMIC — cd/physics/v2d/SweepCcd2D.hpp
// Box2D v3 / Godot 4 grade 2D exact shape raycasting and Continuous Collision
// Detection (CCD) Time-of-Impact (TOI) solver for anti-tunneling.
// =============================================================================
#pragma once

#include <cd/physics/v2d/Narrowphase2D.hpp>
#include <cd/physics/v2d/Shapes2D.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace cd::physics::v2d
{

struct RaycastHit2D
{
    bool hit { false };
    std::uint32_t body_id { 0u };
    float fraction { 1.0F };
    cd::math::Vec2f point {};
    cd::math::Vec2f normal {};
};

[[nodiscard]] inline RaycastHit2D raycast_shape_2d(
    cd::math::Vec2f origin,
    cd::math::Vec2f dir,
    float max_dist,
    const ShapeVariant2D& shape,
    const core::Transform2D& xf) noexcept
{
    RaycastHit2D out {};
    const float dir_len = cd::math::length_v2(dir);
    if (dir_len <= 1.0e-8F || max_dist <= 0.0F)
    {
        return out;
    }
    const cd::math::Vec2f unit_dir = dir / dir_len;

    return std::visit(
        [&](const auto& s) noexcept -> RaycastHit2D
        {
            using T = std::decay_t<decltype(s)>;
            RaycastHit2D hit {};

            if constexpr (std::is_same_v<T, CircleShape2D>)
            {
                const float r           = std::max(0.0F, s.radius);
                const cd::math::Vec2f m = origin - xf.position;
                const float b           = cd::math::dot_v2(m, unit_dir);
                const float c           = cd::math::dot_v2(m, m) - r * r;

                if (c <= 0.0F)
                {
                    hit.hit      = true;
                    hit.fraction = 0.0F;
                    hit.point    = origin;
                    hit.normal   = { -unit_dir.x, -unit_dir.y };
                    return hit;
                }
                if (b > 0.0F)
                {
                    return hit;
                }
                const float disc = b * b - c;
                if (disc < 0.0F)
                {
                    return hit;
                }
                const float t = -b - std::sqrt(disc);
                if (t >= 0.0F && t <= max_dist)
                {
                    hit.hit      = true;
                    hit.fraction = t / max_dist;
                    hit.point    = origin + unit_dir * t;
                    hit.normal   = cd::math::normalize_v2(hit.point - xf.position);
                }
                return hit;
            }
            else if constexpr (std::is_same_v<T, BoxShape2D>)
            {
                const cd::math::Vec2f o_local = detail::inv_transform_point_2d(xf, origin);
                const cd::math::Vec2f d_local = detail::inv_rotate_2d(xf, unit_dir);
                const float hx                = std::max(0.0F, s.half_extents.x);
                const float hy                = std::max(0.0F, s.half_extents.y);

                float tmin = 0.0F;
                float tmax = max_dist;
                cd::math::Vec2f local_n { -d_local.x, -d_local.y };

                const std::array<float, 2> bmin { -hx, -hy };
                const std::array<float, 2> bmax { +hx, +hy };

                for (std::size_t axis = 0u; axis < 2u; ++axis)
                {
                    if (std::abs(d_local[axis]) < 1.0e-8F)
                    {
                        if (o_local[axis] < bmin.at(axis) || o_local[axis] > bmax.at(axis))
                        {
                            return hit;
                        }
                    }
                    else
                    {
                        const float inv_d = 1.0F / d_local[axis];
                        float t1          = (bmin.at(axis) - o_local[axis]) * inv_d;
                        float t2          = (bmax.at(axis) - o_local[axis]) * inv_d;
                        float sign        = -1.0F;
                        if (t1 > t2)
                        {
                            std::swap(t1, t2);
                            sign = +1.0F;
                        }
                        if (t1 > tmin)
                        {
                            tmin          = t1;
                            local_n       = { 0.0F, 0.0F };
                            local_n[axis] = sign;
                        }
                        tmax = std::min(tmax, t2);
                        if (tmin > tmax)
                        {
                            return hit;
                        }
                    }
                }

                hit.hit      = true;
                hit.fraction = tmin / max_dist;
                hit.point    = origin + unit_dir * tmin;
                hit.normal   = detail::rotate_2d(xf, cd::math::normalize_v2(local_n));
                return hit;
            }
            else if constexpr (std::is_same_v<T, CapsuleShape2D>)
            {
                const float hh = std::max(0.0F, s.half_height);
                const float r  = std::max(0.0F, s.radius);

                // Test bounding box in capsule local space for body + end circles
                core::Transform2D xf0 = xf;
                xf0.position          = detail::transform_point_2d(xf, { 0.0F, -hh });
                core::Transform2D xf1 = xf;
                xf1.position          = detail::transform_point_2d(xf, { 0.0F, +hh });

                const ShapeVariant2D cap_circle = CircleShape2D { .radius = r };
                const RaycastHit2D h0 = raycast_shape_2d(origin, unit_dir, max_dist, cap_circle, xf0);
                const RaycastHit2D h1 = raycast_shape_2d(origin, unit_dir, max_dist, cap_circle, xf1);

                RaycastHit2D best = h0;
                if (h1.hit && (!best.hit || h1.fraction < best.fraction))
                {
                    best = h1;
                }

                if (hh > 1.0e-6F)
                {
                    const ShapeVariant2D middle_box = BoxShape2D { .half_extents = { r, hh } };
                    const RaycastHit2D hm = raycast_shape_2d(origin, unit_dir, max_dist, middle_box, xf);
                    if (hm.hit)
                    {
                        const cd::math::Vec2f p_local = detail::inv_transform_point_2d(xf, hm.point);
                        if (std::abs(p_local.y) <= hh + 1.0e-4F &&
                            (!best.hit || hm.fraction < best.fraction))
                        {
                            best = hm;
                        }
                    }
                }
                return best;
            }
            else if constexpr (std::is_same_v<T, SegmentShape2D>)
            {
                const cd::math::Vec2f p0   = detail::transform_point_2d(xf, s.point_a);
                const cd::math::Vec2f p1   = detail::transform_point_2d(xf, s.point_b);
                const cd::math::Vec2f edge = p1 - p0;
                const float denom          = detail::cross_2d(unit_dir, edge);
                if (std::abs(denom) <= 1.0e-8F)
                {
                    return hit;
                }
                const cd::math::Vec2f qp = p0 - origin;
                const float t            = detail::cross_2d(qp, edge) / denom;
                const float u            = detail::cross_2d(qp, unit_dir) / denom;
                if (t >= 0.0F && t <= max_dist && u >= 0.0F && u <= 1.0F)
                {
                    cd::math::Vec2f n = cd::math::normalize_v2({ -edge.y, edge.x });
                    if (s.one_sided && cd::math::dot_v2(unit_dir, n) > 0.0F)
                    {
                        return hit;
                    }
                    if (cd::math::dot_v2(unit_dir, n) > 0.0F)
                    {
                        n = { -n.x, -n.y };
                    }
                    hit.hit      = true;
                    hit.fraction = t / max_dist;
                    hit.point    = origin + unit_dir * t;
                    hit.normal   = n;
                }
                return hit;
            }
            else if constexpr (std::is_same_v<T, ConvexPolygonShape2D>)
            {
                const std::uint8_t count = std::min<std::uint8_t>(s.vertex_count, 8u);
                if (count < 3u)
                {
                    return hit;
                }
                float t_enter = 0.0F;
                float t_exit  = max_dist;
                cd::math::Vec2f enter_normal { -unit_dir.x, -unit_dir.y };

                for (std::uint8_t i = 0u; i < count; ++i)
                {
                    const cd::math::Vec2f v0 = detail::transform_point_2d(xf, s.vertices.at(i));
                    const cd::math::Vec2f v1 =
                        detail::transform_point_2d(xf, s.vertices.at(static_cast<std::size_t>((i + 1u) % count)));
                    const cd::math::Vec2f e = v1 - v0;
                    const cd::math::Vec2f n = cd::math::normalize_v2({ e.y, -e.x });

                    const float numer = cd::math::dot_v2(n, v0 - origin);
                    const float denom = cd::math::dot_v2(n, unit_dir);

                    if (std::abs(denom) <= 1.0e-8F)
                    {
                        if (numer < 0.0F)
                        {
                            return hit;
                        }
                    }
                    else
                    {
                        const float t = numer / denom;
                        if (denom < 0.0F)
                        {
                            if (t > t_enter)
                            {
                                t_enter      = t;
                                enter_normal = n;
                            }
                        }
                        else
                        {
                            t_exit = std::min(t_exit, t);
                        }
                        if (t_enter > t_exit)
                        {
                            return hit;
                        }
                    }
                }

                hit.hit      = true;
                hit.fraction = t_enter / max_dist;
                hit.point    = origin + unit_dir * t_enter;
                hit.normal   = enter_normal;
                return hit;
            }
        },
        shape);
}

namespace detail
{

[[nodiscard]] inline float shape_swept_radius_2d(const ShapeVariant2D& shape) noexcept
{
    return std::visit(
        [](const auto& s) noexcept -> float
        {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, CircleShape2D>)
            {
                return std::max(0.01F, s.radius);
            }
            else if constexpr (std::is_same_v<T, BoxShape2D>)
            {
                return std::max(0.01F, std::min(s.half_extents.x, s.half_extents.y));
            }
            else if constexpr (std::is_same_v<T, CapsuleShape2D>)
            {
                return std::max(0.01F, s.radius);
            }
            else
            {
                return 0.05F;
            }
        },
        shape);
}

[[nodiscard]] inline core::Transform2D interpolate_transform_2d(
    const core::Transform2D& xf0,
    const core::Transform2D& xf1,
    float alpha) noexcept
{
    core::Transform2D out {};
    out.position = cd::math::lerp_v2(xf0.position, xf1.position, alpha);
    const float r0 = get_rotation_rad(xf0);
    const float r1 = get_rotation_rad(xf1);
    set_rotation_rad(out, r0 + (r1 - r0) * alpha);
    return out;
}

}  // namespace detail

/// Computes the normalized Time of Impact (TOI) in [0, 1] for two moving 2D shapes.
/// Returns 1.0F if no collision occurs along the sweep.
[[nodiscard]] inline float sweep_shapes_toi_2d(
    const ShapeVariant2D& shape_a,
    const core::Transform2D& xf_a0,
    const core::Transform2D& xf_a1,
    const ShapeVariant2D& shape_b,
    const core::Transform2D& xf_b0,
    const core::Transform2D& xf_b1) noexcept
{
    if (collide_shapes_2d(shape_a, xf_a0, shape_b, xf_b0).has_contacts())
    {
        return 0.0F;
    }

    const cd::math::Vec2f rel_disp =
        (xf_a1.position - xf_a0.position) - (xf_b1.position - xf_b0.position);
    const float rel_dist = cd::math::length_v2(rel_disp);

    // Fast raycast bracket for high-speed projectiles against static/moving obstacles
    if (rel_dist > 1.0e-5F)
    {
        const RaycastHit2D center_ray =
            raycast_shape_2d(xf_a0.position, rel_disp / rel_dist, rel_dist, shape_b, xf_b0);
        if (center_ray.hit)
        {
            const float r_eff      = detail::shape_swept_radius_2d(shape_a);
            const float pull_back  = std::min(center_ray.fraction, r_eff / rel_dist);
            float lo               = std::max(0.0F, center_ray.fraction - pull_back);
            float hi               = center_ray.fraction;

            for (int iter = 0; iter < 16; ++iter)
            {
                const float mid               = 0.5F * (lo + hi);
                const core::Transform2D xfa_m = detail::interpolate_transform_2d(xf_a0, xf_a1, mid);
                const core::Transform2D xfb_m = detail::interpolate_transform_2d(xf_b0, xf_b1, mid);
                if (collide_shapes_2d(shape_a, xfa_m, shape_b, xfb_m).has_contacts())
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
    }

    // Adaptive substepping + bisection refinement
    const float char_size         = detail::shape_swept_radius_2d(shape_a);
    const std::uint32_t num_steps = std::clamp<std::uint32_t>(
        static_cast<std::uint32_t>(std::ceil(rel_dist / std::max(0.02F, 0.5F * char_size))),
        16u,
        256u);

    float prev_t = 0.0F;
    for (std::uint32_t step = 1u; step <= num_steps; ++step)
    {
        const float curr_t            = static_cast<float>(step) / static_cast<float>(num_steps);
        const core::Transform2D xfa_c = detail::interpolate_transform_2d(xf_a0, xf_a1, curr_t);
        const core::Transform2D xfb_c = detail::interpolate_transform_2d(xf_b0, xf_b1, curr_t);

        if (collide_shapes_2d(shape_a, xfa_c, shape_b, xfb_c).has_contacts())
        {
            float lo = prev_t;
            float hi = curr_t;
            for (int iter = 0; iter < 16; ++iter)
            {
                const float mid               = 0.5F * (lo + hi);
                const core::Transform2D xfa_m = detail::interpolate_transform_2d(xf_a0, xf_a1, mid);
                const core::Transform2D xfb_m = detail::interpolate_transform_2d(xf_b0, xf_b1, mid);
                if (collide_shapes_2d(shape_a, xfa_m, shape_b, xfb_m).has_contacts())
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
        prev_t = curr_t;
    }

    return 1.0F;
}

/// Convenience overload taking start transforms and linear displacement vectors.
[[nodiscard]] inline float sweep_shapes_toi_2d(
    const ShapeVariant2D& shape_a,
    const core::Transform2D& xf_a_start,
    cd::math::Vec2f disp_a,
    const ShapeVariant2D& shape_b,
    const core::Transform2D& xf_b_start,
    cd::math::Vec2f disp_b = { 0.0F, 0.0F }) noexcept
{
    core::Transform2D xf_a_end = xf_a_start;
    xf_a_end.position          = xf_a_start.position + disp_a;
    core::Transform2D xf_b_end = xf_b_start;
    xf_b_end.position          = xf_b_start.position + disp_b;
    return sweep_shapes_toi_2d(shape_a, xf_a_start, xf_a_end, shape_b, xf_b_start, xf_b_end);
}

}  // namespace cd::physics::v2d

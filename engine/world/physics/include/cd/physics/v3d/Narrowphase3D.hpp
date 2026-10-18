// =============================================================================
// CHROMODYNAMIC — cd/physics/v3d/Narrowphase3D.hpp
// Analytical 3D narrowphase collision detection (Sphere-Sphere, Sphere-Box,
// Sphere-Capsule, Capsule-Capsule, Capsule-Box, 15-axis OBB SAT Box-Box with
// Sutherland-Hodgman multi-point face clipping, and Heightfield collisions)
// with GJK/EPA fallback for general convex shapes.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/core/PhysicsTypes.hpp>
#include <cd/physics/v3d/ContactManifold3D.hpp>
#include <cd/physics/v3d/GjkEpa3D.hpp>
#include <cd/physics/v3d/Shapes3D.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <variant>

namespace cd::physics::v3d
{

namespace detail
{

inline void flip_manifold_3d(
    ContactManifold3D& m,
    const core::Transform3D& xf_new_a,
    const core::Transform3D& xf_new_b) noexcept
{
    m.normal = -m.normal;
    m.compute_tangent_basis();
    for (std::uint8_t i = 0u; i < m.point_count; ++i)
    {
        m.points[i].local_anchor_a = xf_new_a.inverse_transform_point(m.points[i].world_point);
        m.points[i].local_anchor_b = xf_new_b.inverse_transform_point(m.points[i].world_point);
    }
}

inline void closest_points_segments_3d(
    cd::math::Vec3f p1,
    cd::math::Vec3f q1,
    cd::math::Vec3f p2,
    cd::math::Vec3f q2,
    float& s,
    float& t,
    cd::math::Vec3f& c1,
    cd::math::Vec3f& c2) noexcept
{
    const cd::math::Vec3f d1 = q1 - p1;
    const cd::math::Vec3f d2 = q2 - p2;
    const cd::math::Vec3f r = p1 - p2;
    const float a = cd::math::dot(d1, d1);
    const float e = cd::math::dot(d2, d2);
    const float f = cd::math::dot(d2, r);

    if (a <= 1.0e-8F && e <= 1.0e-8F)
    {
        s = 0.0F;
        t = 0.0F;
        c1 = p1;
        c2 = p2;
        return;
    }
    if (a <= 1.0e-8F)
    {
        s = 0.0F;
        t = std::clamp(f / e, 0.0F, 1.0F);
    }
    else
    {
        const float c = cd::math::dot(d1, r);
        if (e <= 1.0e-8F)
        {
            t = 0.0F;
            s = std::clamp(-c / a, 0.0F, 1.0F);
        }
        else
        {
            const float b = cd::math::dot(d1, d2);
            const float denom = a * e - b * b;
            if (denom != 0.0F)
            {
                s = std::clamp((b * f - c * e) / denom, 0.0F, 1.0F);
            }
            else
            {
                s = 0.0F;
            }
            t = (b * s + f) / e;
            if (t < 0.0F)
            {
                t = 0.0F;
                s = std::clamp(-c / a, 0.0F, 1.0F);
            }
            else if (t > 1.0F)
            {
                t = 1.0F;
                s = std::clamp((b - c) / a, 0.0F, 1.0F);
            }
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

[[nodiscard]] inline ContactManifold3D collide_sphere_sphere_3d(
    const SphereShape3D& sa,
    const core::Transform3D& xf_a,
    const SphereShape3D& sb,
    const core::Transform3D& xf_b) noexcept
{
    ContactManifold3D out {};
    const cd::math::Vec3f d = xf_b.position - xf_a.position;
    const float dist_sq = cd::math::length_squared(d);
    const float r_sum = sa.radius + sb.radius;
    if (dist_sq > r_sum * r_sum)
    {
        return out;
    }

    const float dist = std::sqrt(dist_sq);
    out.normal = (dist > 1.0e-6F) ? (d / dist) : cd::math::Vec3f { 0.0F, 1.0F, 0.0F };
    out.compute_tangent_basis();

    const cd::math::Vec3f pt_a = xf_a.position + out.normal * sa.radius;
    const cd::math::Vec3f pt_b = xf_b.position - out.normal * sb.radius;
    ContactPoint3D cp {};
    cp.world_point = (pt_a + pt_b) * 0.5F;
    cp.local_anchor_a = xf_a.inverse_transform_point(pt_a);
    cp.local_anchor_b = xf_b.inverse_transform_point(pt_b);
    cp.penetration = r_sum - dist;
    cp.feature_key = 0x1100u;
    out.add_point(cp);
    return out;
}

[[nodiscard]] inline ContactManifold3D collide_sphere_box_3d(
    const SphereShape3D& sa,
    const core::Transform3D& xf_a,
    const BoxShape3D& sb,
    const core::Transform3D& xf_b) noexcept
{
    ContactManifold3D out {};
    const cd::math::Vec3f local_c = xf_b.inverse_transform_point(xf_a.position);
    const cd::math::Vec3f he = sb.half_extents;
    const cd::math::Vec3f closest {
        std::clamp(local_c.x, -he.x, he.x),
        std::clamp(local_c.y, -he.y, he.y),
        std::clamp(local_c.z, -he.z, he.z)
    };

    const cd::math::Vec3f diff = local_c - closest;
    const float dist_sq = cd::math::length_squared(diff);
    if (dist_sq > sa.radius * sa.radius)
    {
        return out;
    }

    cd::math::Vec3f local_normal {};
    cd::math::Vec3f local_box_pt = closest;
    float penetration = 0.0F;

    if (dist_sq > 1.0e-10F)
    {
        const float dist = std::sqrt(dist_sq);
        // diff points from box B to sphere A; normal must point from A to B.
        local_normal = -(diff / dist);
        penetration = sa.radius - dist;
    }
    else
    {
        // Sphere center is inside the box: find minimum distance to a box face.
        const float dx = he.x - std::abs(local_c.x);
        const float dy = he.y - std::abs(local_c.y);
        const float dz = he.z - std::abs(local_c.z);
        if (dx <= dy && dx <= dz)
        {
            const float s = (local_c.x >= 0.0F) ? 1.0F : -1.0F;
            local_normal = { -s, 0.0F, 0.0F };
            local_box_pt.x = s * he.x;
            penetration = sa.radius + dx;
        }
        else if (dy <= dz)
        {
            const float s = (local_c.y >= 0.0F) ? 1.0F : -1.0F;
            local_normal = { 0.0F, -s, 0.0F };
            local_box_pt.y = s * he.y;
            penetration = sa.radius + dy;
        }
        else
        {
            const float s = (local_c.z >= 0.0F) ? 1.0F : -1.0F;
            local_normal = { 0.0F, 0.0F, -s };
            local_box_pt.z = s * he.z;
            penetration = sa.radius + dz;
        }
    }

    out.normal = xf_b.rotate_vector(local_normal);
    out.compute_tangent_basis();

    const cd::math::Vec3f world_box_pt = xf_b.transform_point(local_box_pt);
    const cd::math::Vec3f world_sph_pt = xf_a.position + out.normal * sa.radius;
    ContactPoint3D cp {};
    cp.world_point = (world_sph_pt + world_box_pt) * 0.5F;
    cp.local_anchor_a = xf_a.inverse_transform_point(world_sph_pt);
    cp.local_anchor_b = local_box_pt;
    cp.penetration = penetration;
    cp.feature_key = 0x1200u;
    out.add_point(cp);
    return out;
}

[[nodiscard]] inline ContactManifold3D collide_sphere_capsule_3d(
    const SphereShape3D& sa,
    const core::Transform3D& xf_a,
    const CapsuleShape3D& sb,
    const core::Transform3D& xf_b) noexcept
{
    const cd::math::Vec3f b0 = xf_b.transform_point({ 0.0F, -sb.half_height, 0.0F });
    const cd::math::Vec3f b1 = xf_b.transform_point({ 0.0F, +sb.half_height, 0.0F });
    const cd::math::Vec3f ab = b1 - b0;
    const float ab_sq = cd::math::length_squared(ab);
    float t = 0.0F;
    if (ab_sq > 1.0e-8F)
    {
        t = std::clamp(cd::math::dot(xf_a.position - b0, ab) / ab_sq, 0.0F, 1.0F);
    }
    const cd::math::Vec3f closest_b = b0 + ab * t;

    ContactManifold3D out {};
    const cd::math::Vec3f d = closest_b - xf_a.position;
    const float dist_sq = cd::math::length_squared(d);
    const float r_sum = sa.radius + sb.radius;
    if (dist_sq > r_sum * r_sum)
    {
        return out;
    }

    const float dist = std::sqrt(dist_sq);
    out.normal = (dist > 1.0e-6F) ? (d / dist) : cd::math::Vec3f { 0.0F, 1.0F, 0.0F };
    out.compute_tangent_basis();

    const cd::math::Vec3f pt_a = xf_a.position + out.normal * sa.radius;
    const cd::math::Vec3f pt_b = closest_b - out.normal * sb.radius;
    ContactPoint3D cp {};
    cp.world_point = (pt_a + pt_b) * 0.5F;
    cp.local_anchor_a = xf_a.inverse_transform_point(pt_a);
    cp.local_anchor_b = xf_b.inverse_transform_point(pt_b);
    cp.penetration = r_sum - dist;
    cp.feature_key = 0x1300u;
    out.add_point(cp);
    return out;
}

[[nodiscard]] inline ContactManifold3D collide_capsule_capsule_3d(
    const CapsuleShape3D& sa,
    const core::Transform3D& xf_a,
    const CapsuleShape3D& sb,
    const core::Transform3D& xf_b) noexcept
{
    const cd::math::Vec3f a0 = xf_a.transform_point({ 0.0F, -sa.half_height, 0.0F });
    const cd::math::Vec3f a1 = xf_a.transform_point({ 0.0F, +sa.half_height, 0.0F });
    const cd::math::Vec3f b0 = xf_b.transform_point({ 0.0F, -sb.half_height, 0.0F });
    const cd::math::Vec3f b1 = xf_b.transform_point({ 0.0F, +sb.half_height, 0.0F });

    float s = 0.0F;
    float t = 0.0F;
    cd::math::Vec3f c1 {};
    cd::math::Vec3f c2 {};
    closest_points_segments_3d(a0, a1, b0, b1, s, t, c1, c2);

    ContactManifold3D out {};
    const cd::math::Vec3f d = c2 - c1;
    const float dist_sq = cd::math::length_squared(d);
    const float r_sum = sa.radius + sb.radius;
    if (dist_sq > r_sum * r_sum)
    {
        return out;
    }

    const float dist = std::sqrt(dist_sq);
    out.normal = (dist > 1.0e-6F) ? (d / dist) : cd::math::Vec3f { 0.0F, 1.0F, 0.0F };
    out.compute_tangent_basis();

    const cd::math::Vec3f pt_a = c1 + out.normal * sa.radius;
    const cd::math::Vec3f pt_b = c2 - out.normal * sb.radius;
    ContactPoint3D cp {};
    cp.world_point = (pt_a + pt_b) * 0.5F;
    cp.local_anchor_a = xf_a.inverse_transform_point(pt_a);
    cp.local_anchor_b = xf_b.inverse_transform_point(pt_b);
    cp.penetration = r_sum - dist;
    cp.feature_key = 0x3300u;
    out.add_point(cp);
    return out;
}

[[nodiscard]] inline ContactManifold3D collide_capsule_box_3d(
    const CapsuleShape3D& sa,
    const core::Transform3D& xf_a,
    const BoxShape3D& sb,
    const core::Transform3D& xf_b) noexcept
{
    const cd::math::Vec3f a0_w = xf_a.transform_point({ 0.0F, -sa.half_height, 0.0F });
    const cd::math::Vec3f a1_w = xf_a.transform_point({ 0.0F, +sa.half_height, 0.0F });
    const cd::math::Vec3f a0 = xf_b.inverse_transform_point(a0_w);
    const cd::math::Vec3f a1 = xf_b.inverse_transform_point(a1_w);
    const cd::math::Vec3f he = sb.half_extents;

    // Sample candidate points along the capsule spine to find the deepest point(s) against the box.
    float best_dist_sq = std::numeric_limits<float>::max();
    float best_t = 0.5F;
    constexpr int kSamples = 8;
    for (int i = 0; i <= kSamples; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kSamples);
        const cd::math::Vec3f p = a0 + (a1 - a0) * t;
        const cd::math::Vec3f cl {
            std::clamp(p.x, -he.x, he.x),
            std::clamp(p.y, -he.y, he.y),
            std::clamp(p.z, -he.z, he.z)
        };
        const float d2 = cd::math::length_squared(p - cl);
        if (d2 < best_dist_sq)
        {
            best_dist_sq = d2;
            best_t = t;
        }
    }

    // Refine around best_t using closest point on segment to the clamped box point.
    const cd::math::Vec3f p_init = a0 + (a1 - a0) * best_t;
    const cd::math::Vec3f cl_init {
        std::clamp(p_init.x, -he.x, he.x),
        std::clamp(p_init.y, -he.y, he.y),
        std::clamp(p_init.z, -he.z, he.z)
    };
    const cd::math::Vec3f seg = a1 - a0;
    const float seg_sq = cd::math::length_squared(seg);
    if (seg_sq > 1.0e-8F)
    {
        best_t = std::clamp(cd::math::dot(cl_init - a0, seg) / seg_sq, 0.0F, 1.0F);
    }

    core::Transform3D sphere_xf = xf_a;
    sphere_xf.position = a0_w + (a1_w - a0_w) * best_t;
    ContactManifold3D out = collide_sphere_box_3d(SphereShape3D { sa.radius }, sphere_xf, sb, xf_b);
    for (std::uint8_t i = 0u; i < out.point_count; ++i)
    {
        out.points[i].local_anchor_a = xf_a.inverse_transform_point(out.points[i].world_point);
        out.points[i].feature_key = 0x3200u + i;
    }
    return out;
}

/// Clip a polygon in-place against the plane `dot(plane_normal, p) <= plane_offset`.
inline void clip_polygon_against_plane_3d(
    std::array<cd::math::Vec3f, 16>& poly,
    std::uint8_t& count,
    cd::math::Vec3f plane_normal,
    float plane_offset) noexcept
{
    if (count == 0u)
    {
        return;
    }
    std::array<cd::math::Vec3f, 16> out_poly {};
    std::uint8_t out_count = 0u;

    cd::math::Vec3f v_prev = poly[count - 1u];
    float d_prev = cd::math::dot(plane_normal, v_prev) - plane_offset;

    for (std::uint8_t i = 0u; i < count; ++i)
    {
        const cd::math::Vec3f v_curr = poly[i];
        const float d_curr = cd::math::dot(plane_normal, v_curr) - plane_offset;

        if (d_curr <= 0.0F)
        {
            if (d_prev > 0.0F && out_count < 16u)
            {
                const float t = d_prev / (d_prev - d_curr);
                out_poly[out_count++] = v_prev + (v_curr - v_prev) * t;
            }
            if (out_count < 16u)
            {
                out_poly[out_count++] = v_curr;
            }
        }
        else if (d_prev <= 0.0F && out_count < 16u)
        {
            const float t = d_prev / (d_prev - d_curr);
            out_poly[out_count++] = v_prev + (v_curr - v_prev) * t;
        }

        v_prev = v_curr;
        d_prev = d_curr;
    }

    poly = out_poly;
    count = out_count;
}

[[nodiscard]] inline ContactManifold3D collide_box_box_3d(
    const BoxShape3D& sa,
    const core::Transform3D& xf_a,
    const BoxShape3D& sb,
    const core::Transform3D& xf_b) noexcept
{
    ContactManifold3D out {};

    const std::array<cd::math::Vec3f, 3> u_a {
        xf_a.rotate_vector({ 1.0F, 0.0F, 0.0F }),
        xf_a.rotate_vector({ 0.0F, 1.0F, 0.0F }),
        xf_a.rotate_vector({ 0.0F, 0.0F, 1.0F })
    };
    const std::array<cd::math::Vec3f, 3> u_b {
        xf_b.rotate_vector({ 1.0F, 0.0F, 0.0F }),
        xf_b.rotate_vector({ 0.0F, 1.0F, 0.0F }),
        xf_b.rotate_vector({ 0.0F, 0.0F, 1.0F })
    };

    const cd::math::Vec3f he_a = sa.half_extents;
    const cd::math::Vec3f he_b = sb.half_extents;
    const cd::math::Vec3f d_ab = xf_b.position - xf_a.position;

    float min_pen = std::numeric_limits<float>::max();
    cd::math::Vec3f best_axis { 0.0F, 1.0F, 0.0F };
    int best_axis_id = 0;

    auto test_axis = [&](cd::math::Vec3f axis, int axis_id, float bias) noexcept -> bool
    {
        const float len_sq = cd::math::length_squared(axis);
        if (len_sq < 1.0e-8F)
        {
            return true;
        }
        axis = axis / std::sqrt(len_sq);

        const float proj_a =
            he_a.x * std::abs(cd::math::dot(u_a[0], axis)) +
            he_a.y * std::abs(cd::math::dot(u_a[1], axis)) +
            he_a.z * std::abs(cd::math::dot(u_a[2], axis));
        const float proj_b =
            he_b.x * std::abs(cd::math::dot(u_b[0], axis)) +
            he_b.y * std::abs(cd::math::dot(u_b[1], axis)) +
            he_b.z * std::abs(cd::math::dot(u_b[2], axis));
        const float dist = std::abs(cd::math::dot(d_ab, axis));
        const float pen = (proj_a + proj_b) - dist;
        if (pen < 0.0F)
        {
            return false;
        }

        if (pen * bias < min_pen)
        {
            min_pen = pen;
            best_axis = (cd::math::dot(d_ab, axis) >= 0.0F) ? axis : -axis;
            best_axis_id = axis_id;
        }
        return true;
    };

    // 3 face axes of A, 3 face axes of B, and 9 edge-edge cross product axes.
    for (int i = 0; i < 3; ++i)
    {
        if (!test_axis(u_a[static_cast<std::size_t>(i)], i, 1.0F))
        {
            return out;
        }
    }
    for (int i = 0; i < 3; ++i)
    {
        if (!test_axis(u_b[static_cast<std::size_t>(i)], 3 + i, 1.0F))
        {
            return out;
        }
    }
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            // Slight preference for face normals over edge-edge axes to prevent stacking jitter.
            if (!test_axis(
                    cd::math::cross(u_a[static_cast<std::size_t>(i)], u_b[static_cast<std::size_t>(j)]),
                    6 + i * 3 + j,
                    1.02F))
            {
                return out;
            }
        }
    }

    out.normal = best_axis;
    out.compute_tangent_basis();

    // Determine Reference Box and Incident Box for multi-point face clipping.
    const bool ref_is_b = (best_axis_id >= 3 && best_axis_id < 6);
    const core::Transform3D& xf_ref = ref_is_b ? xf_b : xf_a;
    const core::Transform3D& xf_inc = ref_is_b ? xf_a : xf_b;
    const cd::math::Vec3f he_ref = ref_is_b ? he_b : he_a;
    const cd::math::Vec3f he_inc = ref_is_b ? he_a : he_b;
    const auto& u_ref = ref_is_b ? u_b : u_a;
    const auto& u_inc = ref_is_b ? u_a : u_b;

    // Normal pointing outward from the reference face toward the incident box.
    const cd::math::Vec3f ref_out_normal = ref_is_b ? -out.normal : out.normal;

    // Pick reference face index (0, 1, or 2) most aligned with ref_out_normal.
    std::size_t ref_axis = 0u;
    float best_ref_dot = std::abs(cd::math::dot(u_ref[0], ref_out_normal));
    for (std::size_t i = 1u; i < 3u; ++i)
    {
        const float d = std::abs(cd::math::dot(u_ref[i], ref_out_normal));
        if (d > best_ref_dot)
        {
            best_ref_dot = d;
            ref_axis = i;
        }
    }
    const float ref_sign = (cd::math::dot(u_ref[ref_axis], ref_out_normal) >= 0.0F) ? 1.0F : -1.0F;
    const cd::math::Vec3f ref_face_normal = u_ref[ref_axis] * ref_sign;
    const cd::math::Vec3f ref_face_center = xf_ref.position + ref_face_normal * he_ref[ref_axis];

    const std::size_t side_axis_1 = (ref_axis + 1u) % 3u;
    const std::size_t side_axis_2 = (ref_axis + 2u) % 3u;

    // Find incident face on incident box most anti-parallel to ref_face_normal.
    std::size_t inc_axis = 0u;
    float min_inc_dot = cd::math::dot(u_inc[0], ref_face_normal);
    float inc_sign = (min_inc_dot > 0.0F) ? -1.0F : 1.0F;
    float best_abs_inc = std::abs(min_inc_dot);
    for (std::size_t i = 1u; i < 3u; ++i)
    {
        const float d = cd::math::dot(u_inc[i], ref_face_normal);
        if (std::abs(d) > best_abs_inc)
        {
            best_abs_inc = std::abs(d);
            inc_axis = i;
            inc_sign = (d > 0.0F) ? -1.0F : 1.0F;
        }
    }

    const std::size_t inc_u = (inc_axis + 1u) % 3u;
    const std::size_t inc_v = (inc_axis + 2u) % 3u;
    const cd::math::Vec3f inc_center = xf_inc.position + u_inc[inc_axis] * (inc_sign * he_inc[inc_axis]);
    const cd::math::Vec3f du = u_inc[inc_u] * he_inc[inc_u];
    const cd::math::Vec3f dv = u_inc[inc_v] * he_inc[inc_v];

    std::array<cd::math::Vec3f, 16> poly {};
    poly[0] = inc_center - du - dv;
    poly[1] = inc_center + du - dv;
    poly[2] = inc_center + du + dv;
    poly[3] = inc_center - du + dv;
    std::uint8_t poly_count = 4u;

    // Clip incident quad against the 4 side planes of the reference face.
    const cd::math::Vec3f s1 = u_ref[side_axis_1];
    const cd::math::Vec3f s2 = u_ref[side_axis_2];
    const float off1_pos = cd::math::dot(s1, xf_ref.position) + he_ref[side_axis_1];
    const float off1_neg = cd::math::dot(-s1, xf_ref.position) + he_ref[side_axis_1];
    const float off2_pos = cd::math::dot(s2, xf_ref.position) + he_ref[side_axis_2];
    const float off2_neg = cd::math::dot(-s2, xf_ref.position) + he_ref[side_axis_2];

    clip_polygon_against_plane_3d(poly, poly_count, s1, off1_pos);
    clip_polygon_against_plane_3d(poly, poly_count, -s1, off1_neg);
    clip_polygon_against_plane_3d(poly, poly_count, s2, off2_pos);
    clip_polygon_against_plane_3d(poly, poly_count, -s2, off2_neg);

    const float ref_plane_d = cd::math::dot(ref_face_normal, ref_face_center);
    for (std::uint8_t i = 0u; i < poly_count; ++i)
    {
        const float pen = ref_plane_d - cd::math::dot(ref_face_normal, poly[i]);
        if (pen >= -1.0e-4F)
        {
            const float clamped_pen = std::max(0.0001F, pen);
            const cd::math::Vec3f proj_ref = poly[i] + ref_face_normal * pen;
            const cd::math::Vec3f world_pt = (poly[i] + proj_ref) * 0.5F;

            ContactPoint3D cp {};
            cp.world_point = world_pt;
            cp.local_anchor_a = xf_a.inverse_transform_point(world_pt);
            cp.local_anchor_b = xf_b.inverse_transform_point(world_pt);
            cp.penetration = clamped_pen;
            cp.feature_key = 0x2200u + static_cast<std::uint32_t>(best_axis_id * 16 + i);
            out.add_point(cp);
        }
    }

    // Fallback single witness point if edge-edge contact produced no clipped face vertices.
    if (out.empty())
    {
        const cd::math::Vec3f sup_a = support_point_world_3d(sa, xf_a, out.normal);
        const cd::math::Vec3f sup_b = support_point_world_3d(sb, xf_b, -out.normal);
        ContactPoint3D cp {};
        cp.world_point = (sup_a + sup_b) * 0.5F;
        cp.local_anchor_a = xf_a.inverse_transform_point(sup_a);
        cp.local_anchor_b = xf_b.inverse_transform_point(sup_b);
        cp.penetration = std::max(0.0001F, min_pen);
        cp.feature_key = 0x22FFu;
        out.add_point(cp);
    }

    return out;
}

[[nodiscard]] inline ContactManifold3D collide_sphere_heightfield_3d(
    const SphereShape3D& sa,
    const core::Transform3D& xf_a,
    const HeightfieldShape3D& hf,
    const core::Transform3D& xf_hf) noexcept
{
    ContactManifold3D out {};
    const cd::math::Vec3f local_c = xf_hf.inverse_transform_point(xf_a.position);
    float terrain_y = 0.0F;
    cd::math::Vec3f local_n { 0.0F, 1.0F, 0.0F };
    if (!hf.sample_local(local_c.x, local_c.z, terrain_y, local_n))
    {
        return out;
    }

    const float bottom_y = local_c.y - sa.radius;
    if (bottom_y > terrain_y)
    {
        return out;
    }

    const float pen = (terrain_y - bottom_y) * local_n.y;
    // Normal points from Sphere(A) to Heightfield(B), which is downward into the terrain.
    out.normal = -xf_hf.rotate_vector(local_n);
    out.compute_tangent_basis();

    const cd::math::Vec3f local_pt_hf { local_c.x, terrain_y, local_c.z };
    const cd::math::Vec3f world_pt_hf = xf_hf.transform_point(local_pt_hf);
    const cd::math::Vec3f world_pt_sph = xf_a.position + out.normal * sa.radius;

    ContactPoint3D cp {};
    cp.world_point = (world_pt_sph + world_pt_hf) * 0.5F;
    cp.local_anchor_a = xf_a.inverse_transform_point(world_pt_sph);
    cp.local_anchor_b = local_pt_hf;
    cp.penetration = std::max(0.0001F, pen);
    cp.feature_key = 0x1600u;
    out.add_point(cp);
    return out;
}

[[nodiscard]] inline ContactManifold3D collide_capsule_heightfield_3d(
    const CapsuleShape3D& sa,
    const core::Transform3D& xf_a,
    const HeightfieldShape3D& hf,
    const core::Transform3D& xf_hf) noexcept
{
    ContactManifold3D out {};
    const std::array<cd::math::Vec3f, 2> ends {
        xf_a.transform_point({ 0.0F, -sa.half_height, 0.0F }),
        xf_a.transform_point({ 0.0F, +sa.half_height, 0.0F })
    };

    cd::math::Vec3f n_accum { 0.0F, 0.0F, 0.0F };
    for (std::uint8_t i = 0u; i < 2u; ++i)
    {
        const cd::math::Vec3f local_c = xf_hf.inverse_transform_point(ends[i]);
        float terrain_y = 0.0F;
        cd::math::Vec3f local_n { 0.0F, 1.0F, 0.0F };
        if (!hf.sample_local(local_c.x, local_c.z, terrain_y, local_n))
        {
            continue;
        }
        const float bottom_y = local_c.y - sa.radius;
        if (bottom_y <= terrain_y)
        {
            const cd::math::Vec3f wn = -xf_hf.rotate_vector(local_n);
            n_accum += wn;
            const cd::math::Vec3f local_pt_hf { local_c.x, terrain_y, local_c.z };
            const cd::math::Vec3f world_pt_hf = xf_hf.transform_point(local_pt_hf);
            const cd::math::Vec3f world_pt_cap = ends[i] + wn * sa.radius;

            ContactPoint3D cp {};
            cp.world_point = (world_pt_cap + world_pt_hf) * 0.5F;
            cp.local_anchor_a = xf_a.inverse_transform_point(world_pt_cap);
            cp.local_anchor_b = local_pt_hf;
            cp.penetration = std::max(0.0001F, (terrain_y - bottom_y) * local_n.y);
            cp.feature_key = 0x3600u + i;
            out.add_point(cp);
        }
    }

    if (!out.empty())
    {
        out.normal = cd::math::normalize(n_accum);
        out.compute_tangent_basis();
    }
    return out;
}

[[nodiscard]] inline ContactManifold3D collide_box_heightfield_3d(
    const BoxShape3D& sa,
    const core::Transform3D& xf_a,
    const HeightfieldShape3D& hf,
    const core::Transform3D& xf_hf) noexcept
{
    ContactManifold3D out {};
    const cd::math::Vec3f he = sa.half_extents;
    cd::math::Vec3f n_accum { 0.0F, 0.0F, 0.0F };

    std::uint32_t corner_idx = 0u;
    for (int ix = -1; ix <= 1; ix += 2)
    {
        for (int iy = -1; iy <= 1; iy += 2)
        {
            for (int iz = -1; iz <= 1; iz += 2)
            {
                const cd::math::Vec3f local_box_pt {
                    static_cast<float>(ix) * he.x,
                    static_cast<float>(iy) * he.y,
                    static_cast<float>(iz) * he.z
                };
                const cd::math::Vec3f world_pt = xf_a.transform_point(local_box_pt);
                const cd::math::Vec3f hf_local = xf_hf.inverse_transform_point(world_pt);

                float terrain_y = 0.0F;
                cd::math::Vec3f local_n { 0.0F, 1.0F, 0.0F };
                if (hf.sample_local(hf_local.x, hf_local.z, terrain_y, local_n) && hf_local.y <= terrain_y)
                {
                    const cd::math::Vec3f wn = -xf_hf.rotate_vector(local_n);
                    n_accum += wn;
                    const cd::math::Vec3f local_pt_hf { hf_local.x, terrain_y, hf_local.z };
                    const cd::math::Vec3f world_pt_hf = xf_hf.transform_point(local_pt_hf);

                    ContactPoint3D cp {};
                    cp.world_point = (world_pt + world_pt_hf) * 0.5F;
                    cp.local_anchor_a = local_box_pt;
                    cp.local_anchor_b = local_pt_hf;
                    cp.penetration = std::max(0.0001F, (terrain_y - hf_local.y) * local_n.y);
                    cp.feature_key = 0x2600u + corner_idx;
                    out.add_point(cp);
                }
                ++corner_idx;
            }
        }
    }

    if (!out.empty())
    {
        out.normal = cd::math::normalize(n_accum);
        out.compute_tangent_basis();
    }
    return out;
}

}  // namespace detail

[[nodiscard]] inline ContactManifold3D collide_shapes_3d(
    const ShapeVariant3D& shape_a,
    const core::Transform3D& xf_a,
    const ShapeVariant3D& shape_b,
    const core::Transform3D& xf_b) noexcept
{
    return std::visit(
        [&](const auto& a, const auto& b) noexcept -> ContactManifold3D
        {
            using TA = std::decay_t<decltype(a)>;
            using TB = std::decay_t<decltype(b)>;

            if constexpr (std::is_same_v<TA, SphereShape3D> && std::is_same_v<TB, SphereShape3D>)
            {
                return detail::collide_sphere_sphere_3d(a, xf_a, b, xf_b);
            }
            else if constexpr (std::is_same_v<TA, SphereShape3D> && std::is_same_v<TB, BoxShape3D>)
            {
                return detail::collide_sphere_box_3d(a, xf_a, b, xf_b);
            }
            else if constexpr (std::is_same_v<TA, BoxShape3D> && std::is_same_v<TB, SphereShape3D>)
            {
                ContactManifold3D m = detail::collide_sphere_box_3d(b, xf_b, a, xf_a);
                detail::flip_manifold_3d(m, xf_a, xf_b);
                return m;
            }
            else if constexpr (std::is_same_v<TA, SphereShape3D> && std::is_same_v<TB, CapsuleShape3D>)
            {
                return detail::collide_sphere_capsule_3d(a, xf_a, b, xf_b);
            }
            else if constexpr (std::is_same_v<TA, CapsuleShape3D> && std::is_same_v<TB, SphereShape3D>)
            {
                ContactManifold3D m = detail::collide_sphere_capsule_3d(b, xf_b, a, xf_a);
                detail::flip_manifold_3d(m, xf_a, xf_b);
                return m;
            }
            else if constexpr (std::is_same_v<TA, CapsuleShape3D> && std::is_same_v<TB, CapsuleShape3D>)
            {
                return detail::collide_capsule_capsule_3d(a, xf_a, b, xf_b);
            }
            else if constexpr (std::is_same_v<TA, CapsuleShape3D> && std::is_same_v<TB, BoxShape3D>)
            {
                return detail::collide_capsule_box_3d(a, xf_a, b, xf_b);
            }
            else if constexpr (std::is_same_v<TA, BoxShape3D> && std::is_same_v<TB, CapsuleShape3D>)
            {
                ContactManifold3D m = detail::collide_capsule_box_3d(b, xf_b, a, xf_a);
                detail::flip_manifold_3d(m, xf_a, xf_b);
                return m;
            }
            else if constexpr (std::is_same_v<TA, BoxShape3D> && std::is_same_v<TB, BoxShape3D>)
            {
                return detail::collide_box_box_3d(a, xf_a, b, xf_b);
            }
            else if constexpr (std::is_same_v<TA, SphereShape3D> && std::is_same_v<TB, HeightfieldShape3D>)
            {
                return detail::collide_sphere_heightfield_3d(a, xf_a, b, xf_b);
            }
            else if constexpr (std::is_same_v<TA, HeightfieldShape3D> && std::is_same_v<TB, SphereShape3D>)
            {
                ContactManifold3D m = detail::collide_sphere_heightfield_3d(b, xf_b, a, xf_a);
                detail::flip_manifold_3d(m, xf_a, xf_b);
                return m;
            }
            else if constexpr (std::is_same_v<TA, CapsuleShape3D> && std::is_same_v<TB, HeightfieldShape3D>)
            {
                return detail::collide_capsule_heightfield_3d(a, xf_a, b, xf_b);
            }
            else if constexpr (std::is_same_v<TA, HeightfieldShape3D> && std::is_same_v<TB, CapsuleShape3D>)
            {
                ContactManifold3D m = detail::collide_capsule_heightfield_3d(b, xf_b, a, xf_a);
                detail::flip_manifold_3d(m, xf_a, xf_b);
                return m;
            }
            else if constexpr (std::is_same_v<TA, BoxShape3D> && std::is_same_v<TB, HeightfieldShape3D>)
            {
                return detail::collide_box_heightfield_3d(a, xf_a, b, xf_b);
            }
            else if constexpr (std::is_same_v<TA, HeightfieldShape3D> && std::is_same_v<TB, BoxShape3D>)
            {
                ContactManifold3D m = detail::collide_box_heightfield_3d(b, xf_b, a, xf_a);
                detail::flip_manifold_3d(m, xf_a, xf_b);
                return m;
            }
            else
            {
                ContactManifold3D m {};
                (void)gjk_epa_intersect_3d(shape_a, xf_a, shape_b, xf_b, m);
                return m;
            }
        },
        shape_a,
        shape_b);
}

}  // namespace cd::physics::v3d

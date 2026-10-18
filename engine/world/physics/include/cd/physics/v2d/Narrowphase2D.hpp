// =============================================================================
// CHROMODYNAMIC — cd/physics/v2d/Narrowphase2D.hpp
// Box2D v3 / Godot 4 grade 2D narrowphase collision detection:
// Analytical Circle-Circle, Circle-Box, Circle-Capsule, Circle-Segment,
// Capsule-Box, and 2D SAT + 2-point Sutherland-Hodgman clipping for
// Box-Box and ConvexPolygon-ConvexPolygon pairs.
// =============================================================================
#pragma once

#include <cd/physics/v2d/ContactManifold2D.hpp>
#include <cd/physics/v2d/Shapes2D.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace cd::physics::v2d
{

namespace detail
{

[[nodiscard]] inline cd::math::Vec2f closest_point_on_segment_2d(
    cd::math::Vec2f p,
    cd::math::Vec2f a,
    cd::math::Vec2f b) noexcept
{
    const cd::math::Vec2f ab = b - a;
    const float len_sq       = cd::math::length_sq_v2(ab);
    if (len_sq <= 1.0e-8F)
    {
        return a;
    }
    const float t = std::clamp(cd::math::dot_v2(p - a, ab) / len_sq, 0.0F, 1.0F);
    return a + ab * t;
}

[[nodiscard]] inline std::pair<cd::math::Vec2f, cd::math::Vec2f> closest_points_segments_2d(
    cd::math::Vec2f p1,
    cd::math::Vec2f q1,
    cd::math::Vec2f p2,
    cd::math::Vec2f q2) noexcept
{
    const cd::math::Vec2f d1 = q1 - p1;
    const cd::math::Vec2f d2 = q2 - p2;
    const cd::math::Vec2f r  = p1 - p2;
    const float a            = cd::math::dot_v2(d1, d1);
    const float e            = cd::math::dot_v2(d2, d2);
    const float f            = cd::math::dot_v2(d2, r);

    float s = 0.0F;
    float t = 0.0F;

    if (a <= 1.0e-8F && e <= 1.0e-8F)
    {
        return { p1, p2 };
    }
    if (a <= 1.0e-8F)
    {
        t = std::clamp(f / e, 0.0F, 1.0F);
    }
    else
    {
        const float c = cd::math::dot_v2(d1, r);
        if (e <= 1.0e-8F)
        {
            s = std::clamp(-c / a, 0.0F, 1.0F);
        }
        else
        {
            const float b     = cd::math::dot_v2(d1, d2);
            const float denom = a * e - b * b;
            if (denom > 1.0e-8F)
            {
                s = std::clamp((b * f - c * e) / denom, 0.0F, 1.0F);
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

    return { p1 + d1 * s, p2 + d2 * t };
}

[[nodiscard]] inline ContactManifold2D flip_manifold_2d(ContactManifold2D m) noexcept
{
    std::swap(m.body_a, m.body_b);
    m.normal = { -m.normal.x, -m.normal.y };
    for (std::uint8_t i = 0u; i < m.point_count; ++i)
    {
        std::swap(m.points.at(i).local_anchor_a, m.points.at(i).local_anchor_b);
    }
    return m;
}

[[nodiscard]] inline ContactManifold2D collide_spheres_with_radii_2d(
    cd::math::Vec2f center_a,
    float radius_a,
    const core::Transform2D& xf_a,
    cd::math::Vec2f center_b,
    float radius_b,
    const core::Transform2D& xf_b,
    std::uint32_t feature_key = 0u) noexcept
{
    ContactManifold2D m {};
    const cd::math::Vec2f d = center_b - center_a;
    const float dist_sq     = cd::math::length_sq_v2(d);
    const float r_sum       = radius_a + radius_b;

    if (dist_sq > r_sum * r_sum)
    {
        return m;
    }

    const float dist = std::sqrt(dist_sq);
    m.normal         = (dist > 1.0e-6F) ? (d / dist) : cd::math::Vec2f { 0.0F, 1.0F };

    const cd::math::Vec2f pa = center_a + m.normal * radius_a;
    const cd::math::Vec2f pb = center_b - m.normal * radius_b;

    ContactPoint2D cp {};
    cp.world_point    = (pa + pb) * 0.5F;
    cp.local_anchor_a = inv_transform_point_2d(xf_a, pa);
    cp.local_anchor_b = inv_transform_point_2d(xf_b, pb);
    cp.penetration    = r_sum - dist;
    cp.feature_key    = feature_key;
    m.add_point(cp);
    return m;
}

[[nodiscard]] inline ContactManifold2D collide_circle_circle_2d(
    const CircleShape2D& a,
    const core::Transform2D& xf_a,
    const CircleShape2D& b,
    const core::Transform2D& xf_b) noexcept
{
    return collide_spheres_with_radii_2d(xf_a.position, a.radius, xf_a, xf_b.position, b.radius, xf_b, 1u);
}

[[nodiscard]] inline ContactManifold2D collide_circle_box_2d(
    const CircleShape2D& circle_a,
    const core::Transform2D& xf_a,
    const BoxShape2D& box_b,
    const core::Transform2D& xf_b) noexcept
{
    ContactManifold2D m {};
    const cd::math::Vec2f c_world = xf_a.position;
    const cd::math::Vec2f c_local = inv_transform_point_2d(xf_b, c_world);

    const float hx = std::max(0.0F, box_b.half_extents.x);
    const float hy = std::max(0.0F, box_b.half_extents.y);

    cd::math::Vec2f closest_local {
        std::clamp(c_local.x, -hx, hx),
        std::clamp(c_local.y, -hy, hy)
    };

    const cd::math::Vec2f diff = c_local - closest_local;
    const float dist_sq        = cd::math::length_sq_v2(diff);
    const float r              = std::max(0.0F, circle_a.radius);

    if (dist_sq > r * r)
    {
        return m;
    }

    cd::math::Vec2f local_normal_ab {};
    float penetration = 0.0F;

    if (dist_sq > 1.0e-8F)
    {
        const float dist = std::sqrt(dist_sq);
        // diff points from B (closest_local) to A (c_local); normal from A to B is -diff/dist
        local_normal_ab = { -diff.x / dist, -diff.y / dist };
        penetration     = r - dist;
    }
    else
    {
        // Circle center is inside the box; push out along shallowest axis
        const float dx = hx - std::abs(c_local.x);
        const float dy = hy - std::abs(c_local.y);
        if (dx < dy)
        {
            const float sx  = (c_local.x >= 0.0F) ? 1.0F : -1.0F;
            closest_local.x = sx * hx;
            local_normal_ab = { -sx, 0.0F };
            penetration     = r + dx;
        }
        else
        {
            const float sy  = (c_local.y >= 0.0F) ? 1.0F : -1.0F;
            closest_local.y = sy * hy;
            local_normal_ab = { 0.0F, -sy };
            penetration     = r + dy;
        }
    }

    m.normal                     = rotate_2d(xf_b, local_normal_ab);
    const cd::math::Vec2f world_b = transform_point_2d(xf_b, closest_local);
    const cd::math::Vec2f world_a = c_world + m.normal * r;

    ContactPoint2D cp {};
    cp.world_point    = (world_a + world_b) * 0.5F;
    cp.local_anchor_a = inv_transform_point_2d(xf_a, world_a);
    cp.local_anchor_b = closest_local;
    cp.penetration    = penetration;
    cp.feature_key    = 2u;
    m.add_point(cp);
    return m;
}

[[nodiscard]] inline ContactManifold2D collide_circle_capsule_2d(
    const CircleShape2D& circle_a,
    const core::Transform2D& xf_a,
    const CapsuleShape2D& cap_b,
    const core::Transform2D& xf_b) noexcept
{
    const float hh               = std::max(0.0F, cap_b.half_height);
    const cd::math::Vec2f b0     = transform_point_2d(xf_b, { 0.0F, -hh });
    const cd::math::Vec2f b1     = transform_point_2d(xf_b, { 0.0F, +hh });
    const cd::math::Vec2f best_b = closest_point_on_segment_2d(xf_a.position, b0, b1);
    return collide_spheres_with_radii_2d(
        xf_a.position, circle_a.radius, xf_a, best_b, cap_b.radius, xf_b, 3u);
}

[[nodiscard]] inline ContactManifold2D collide_circle_segment_2d(
    const CircleShape2D& circle_a,
    const core::Transform2D& xf_a,
    const SegmentShape2D& seg_b,
    const core::Transform2D& xf_b) noexcept
{
    const cd::math::Vec2f b0 = transform_point_2d(xf_b, seg_b.point_a);
    const cd::math::Vec2f b1 = transform_point_2d(xf_b, seg_b.point_b);
    if (seg_b.one_sided)
    {
        const cd::math::Vec2f edge = b1 - b0;
        const cd::math::Vec2f outward_normal { -edge.y, edge.x };
        if (cd::math::dot_v2(xf_a.position - b0, outward_normal) < 0.0F)
        {
            return ContactManifold2D {};
        }
    }
    const cd::math::Vec2f best_b = closest_point_on_segment_2d(xf_a.position, b0, b1);
    return collide_spheres_with_radii_2d(
        xf_a.position, circle_a.radius, xf_a, best_b, 0.0F, xf_b, 4u);
}

[[nodiscard]] inline ConvexPolygonShape2D box_to_polygon_2d(const BoxShape2D& box) noexcept
{
    const float hx = std::max(0.0F, box.half_extents.x);
    const float hy = std::max(0.0F, box.half_extents.y);
    ConvexPolygonShape2D poly {};
    poly.vertex_count = 4u;
    poly.vertices.at(0) = { -hx, -hy };
    poly.vertices.at(1) = { +hx, -hy };
    poly.vertices.at(2) = { +hx, +hy };
    poly.vertices.at(3) = { -hx, +hy };
    return poly;
}

struct ClipVertex2D
{
    cd::math::Vec2f v {};
    std::uint32_t key { 0u };
};

[[nodiscard]] inline std::uint8_t clip_segment_to_line_2d(
    std::array<ClipVertex2D, 2>& out_verts,
    const std::array<ClipVertex2D, 2>& in_verts,
    cd::math::Vec2f normal,
    float offset,
    std::uint32_t clip_edge) noexcept
{
    std::uint8_t num_out = 0u;
    const float d0       = cd::math::dot_v2(normal, in_verts.at(0).v) - offset;
    const float d1       = cd::math::dot_v2(normal, in_verts.at(1).v) - offset;

    if (d0 <= 0.0F)
    {
        out_verts.at(num_out++) = in_verts.at(0);
    }
    if (d1 <= 0.0F)
    {
        out_verts.at(num_out++) = in_verts.at(1);
    }
    if (d0 * d1 < 0.0F && num_out < 2u)
    {
        const float interp = d0 / (d0 - d1);
        ClipVertex2D cv {};
        cv.v   = in_verts.at(0).v + (in_verts.at(1).v - in_verts.at(0).v) * interp;
        cv.key = (in_verts.at(0).key & 0xFFFF0000u) | (clip_edge & 0x0000FFFFu);
        out_verts.at(num_out++) = cv;
    }
    return num_out;
}

struct SatQuery2D
{
    float max_separation { -std::numeric_limits<float>::infinity() };
    std::uint8_t best_edge { 0u };
};

[[nodiscard]] inline SatQuery2D find_max_separation_2d(
    const std::array<cd::math::Vec2f, 8>& verts_a,
    const std::array<cd::math::Vec2f, 8>& norms_a,
    std::uint8_t count_a,
    const std::array<cd::math::Vec2f, 8>& verts_b,
    std::uint8_t count_b) noexcept
{
    SatQuery2D res {};
    for (std::uint8_t i = 0u; i < count_a; ++i)
    {
        const cd::math::Vec2f n  = norms_a.at(i);
        const cd::math::Vec2f v1 = verts_a.at(i);
        float min_sep            = std::numeric_limits<float>::infinity();
        for (std::uint8_t j = 0u; j < count_b; ++j)
        {
            const float sep = cd::math::dot_v2(n, verts_b.at(j) - v1);
            min_sep         = std::min(min_sep, sep);
        }
        if (min_sep > res.max_separation)
        {
            res.max_separation = min_sep;
            res.best_edge      = i;
        }
    }
    return res;
}

[[nodiscard]] inline ContactManifold2D collide_polygons_2d(
    const ConvexPolygonShape2D& poly_a,
    const core::Transform2D& xf_a,
    const ConvexPolygonShape2D& poly_b,
    const core::Transform2D& xf_b) noexcept
{
    ContactManifold2D m {};
    const std::uint8_t count_a = std::min<std::uint8_t>(poly_a.vertex_count, 8u);
    const std::uint8_t count_b = std::min<std::uint8_t>(poly_b.vertex_count, 8u);
    if (count_a < 3u || count_b < 3u)
    {
        return m;
    }

    std::array<cd::math::Vec2f, 8> verts_a {};
    std::array<cd::math::Vec2f, 8> norms_a {};
    std::array<cd::math::Vec2f, 8> verts_b {};
    std::array<cd::math::Vec2f, 8> norms_b {};

    for (std::uint8_t i = 0u; i < count_a; ++i)
    {
        verts_a.at(i) = transform_point_2d(xf_a, poly_a.vertices.at(i));
    }
    for (std::uint8_t i = 0u; i < count_a; ++i)
    {
        const cd::math::Vec2f e =
            verts_a.at(static_cast<std::size_t>((i + 1u) % count_a)) - verts_a.at(i);
        norms_a.at(i) = cd::math::normalize_v2({ e.y, -e.x });
    }

    for (std::uint8_t i = 0u; i < count_b; ++i)
    {
        verts_b.at(i) = transform_point_2d(xf_b, poly_b.vertices.at(i));
    }
    for (std::uint8_t i = 0u; i < count_b; ++i)
    {
        const cd::math::Vec2f e =
            verts_b.at(static_cast<std::size_t>((i + 1u) % count_b)) - verts_b.at(i);
        norms_b.at(i) = cd::math::normalize_v2({ e.y, -e.x });
    }

    const SatQuery2D sat_a = find_max_separation_2d(verts_a, norms_a, count_a, verts_b, count_b);
    if (sat_a.max_separation > 0.0F)
    {
        return m;
    }

    const SatQuery2D sat_b = find_max_separation_2d(verts_b, norms_b, count_b, verts_a, count_a);
    if (sat_b.max_separation > 0.0F)
    {
        return m;
    }

    // Bias slightly toward A as reference face for frame-to-frame coherence (Box2D k_tol)
    const bool flip = (sat_b.max_separation > sat_a.max_separation + 1.0e-4F);

    const auto& ref_verts = flip ? verts_b : verts_a;
    const auto& ref_norms = flip ? norms_b : norms_a;
    const std::uint8_t ref_count = flip ? count_b : count_a;
    const std::uint8_t ref_edge  = flip ? sat_b.best_edge : sat_a.best_edge;

    const auto& inc_verts = flip ? verts_a : verts_b;
    const auto& inc_norms = flip ? norms_a : norms_b;
    const std::uint8_t inc_count = flip ? count_a : count_b;

    const cd::math::Vec2f ref_normal = ref_norms.at(ref_edge);

    // Find incident edge on incident polygon (most anti-parallel to ref_normal)
    std::uint8_t inc_edge = 0u;
    float min_dot         = std::numeric_limits<float>::infinity();
    for (std::uint8_t i = 0u; i < inc_count; ++i)
    {
        const float d = cd::math::dot_v2(ref_normal, inc_norms.at(i));
        if (d < min_dot)
        {
            min_dot  = d;
            inc_edge = i;
        }
    }

    const auto inc_next = static_cast<std::uint8_t>((inc_edge + 1u) % inc_count);
    std::array<ClipVertex2D, 2> incident_edge {
        ClipVertex2D { .v = inc_verts.at(inc_edge), .key = (static_cast<std::uint32_t>(ref_edge) << 16u) | inc_edge },
        ClipVertex2D { .v = inc_verts.at(inc_next), .key = (static_cast<std::uint32_t>(ref_edge) << 16u) | inc_next }
    };

    const cd::math::Vec2f v1      = ref_verts.at(ref_edge);
    const cd::math::Vec2f v2      = ref_verts.at(static_cast<std::size_t>((ref_edge + 1u) % ref_count));
    const cd::math::Vec2f tangent = cd::math::normalize_v2(v2 - v1);

    const float side_offset1 = -cd::math::dot_v2(tangent, v1);
    const float side_offset2 = +cd::math::dot_v2(tangent, v2);

    std::array<ClipVertex2D, 2> clip_points1 {};
    std::array<ClipVertex2D, 2> clip_points2 {};

    const std::uint8_t np1 = clip_segment_to_line_2d(
        clip_points1, incident_edge, { -tangent.x, -tangent.y }, side_offset1, ref_edge);
    if (np1 < 2u)
    {
        return m;
    }

    const std::uint8_t np2 = clip_segment_to_line_2d(
        clip_points2, clip_points1, tangent, side_offset2, ref_edge);
    if (np2 < 1u)
    {
        return m;
    }

    const float front_offset = cd::math::dot_v2(ref_normal, v1);
    // Normal always points from shape_a to shape_b
    m.normal = flip ? cd::math::Vec2f { -ref_normal.x, -ref_normal.y } : ref_normal;

    for (std::uint8_t i = 0u; i < np2; ++i)
    {
        const float separation = cd::math::dot_v2(ref_normal, clip_points2.at(i).v) - front_offset;
        if (separation <= 0.0F)
        {
            ContactPoint2D cp {};
            cp.world_point             = clip_points2.at(i).v;
            cp.penetration             = -separation;
            cp.feature_key             = clip_points2.at(i).key;
            const cd::math::Vec2f proj = clip_points2.at(i).v - ref_normal * separation;
            const cd::math::Vec2f pa   = flip ? clip_points2.at(i).v : proj;
            const cd::math::Vec2f pb   = flip ? proj : clip_points2.at(i).v;
            cp.local_anchor_a          = inv_transform_point_2d(xf_a, pa);
            cp.local_anchor_b          = inv_transform_point_2d(xf_b, pb);
            m.add_point(cp);
        }
    }

    return m;
}

[[nodiscard]] inline ContactManifold2D collide_capsule_capsule_2d(
    const CapsuleShape2D& a,
    const core::Transform2D& xf_a,
    const CapsuleShape2D& b,
    const core::Transform2D& xf_b) noexcept
{
    const float hha          = std::max(0.0F, a.half_height);
    const float hhb          = std::max(0.0F, b.half_height);
    const cd::math::Vec2f a0 = transform_point_2d(xf_a, { 0.0F, -hha });
    const cd::math::Vec2f a1 = transform_point_2d(xf_a, { 0.0F, +hha });
    const cd::math::Vec2f b0 = transform_point_2d(xf_b, { 0.0F, -hhb });
    const cd::math::Vec2f b1 = transform_point_2d(xf_b, { 0.0F, +hhb });

    const auto [pa, pb] = closest_points_segments_2d(a0, a1, b0, b1);
    return collide_spheres_with_radii_2d(pa, a.radius, xf_a, pb, b.radius, xf_b, 5u);
}

[[nodiscard]] inline ContactManifold2D collide_capsule_box_2d(
    const CapsuleShape2D& cap_a,
    const core::Transform2D& xf_a,
    const BoxShape2D& box_b,
    const core::Transform2D& xf_b) noexcept
{
    const float hh           = std::max(0.0F, cap_a.half_height);
    const float r            = std::max(0.0F, cap_a.radius);
    const cd::math::Vec2f p0 = transform_point_2d(xf_a, { 0.0F, -hh });
    const cd::math::Vec2f p1 = transform_point_2d(xf_a, { 0.0F, +hh });

    // Evaluate both capsule endpoints and the point on the capsule spine closest to box center
    const cd::math::Vec2f p_mid = closest_point_on_segment_2d(xf_b.position, p0, p1);

    core::Transform2D xf0 = xf_a;
    xf0.position          = p0;
    core::Transform2D xf1 = xf_a;
    xf1.position          = p1;
    core::Transform2D xfm = xf_a;
    xfm.position          = p_mid;

    const CircleShape2D end_circle { .radius = r };
    const ContactManifold2D m0  = collide_circle_box_2d(end_circle, xf0, box_b, xf_b);
    const ContactManifold2D m1  = collide_circle_box_2d(end_circle, xf1, box_b, xf_b);
    const ContactManifold2D mm  = collide_circle_box_2d(end_circle, xfm, box_b, xf_b);

    ContactManifold2D out {};
    float best_pen = -1.0F;

    auto consider = [&](const ContactManifold2D& cand, std::uint32_t key_tag) noexcept
    {
        if (cand.has_contacts())
        {
            const float pen = cand.points.at(0).penetration;
            if (pen > best_pen)
            {
                best_pen   = pen;
                out.normal = cand.normal;
            }
            if (out.point_count < 2u)
            {
                ContactPoint2D cp = cand.points.at(0);
                const cd::math::Vec2f world_a =
                    cp.world_point - cand.normal * (0.5F * cp.penetration);
                cp.local_anchor_a = inv_transform_point_2d(xf_a, world_a);
                cp.feature_key    = key_tag;
                out.add_point(cp);
            }
        }
    };

    consider(m0, 60u);
    consider(m1, 61u);
    if (!out.has_contacts())
    {
        consider(mm, 62u);
    }
    return out;
}

[[nodiscard]] inline ContactManifold2D collide_capsule_polygon_2d(
    const CapsuleShape2D& cap_a,
    const core::Transform2D& xf_a,
    const ConvexPolygonShape2D& poly_b,
    const core::Transform2D& xf_b) noexcept
{
    const float hh    = std::max(0.0F, cap_a.half_height);
    const float r     = std::max(0.0F, cap_a.radius);
    const std::uint8_t count_b = std::min<std::uint8_t>(poly_b.vertex_count, 8u);
    if (count_b == 0u)
    {
        return ContactManifold2D {};
    }

    const cd::math::Vec2f a0 = transform_point_2d(xf_a, { 0.0F, -hh });
    const cd::math::Vec2f a1 = transform_point_2d(xf_a, { 0.0F, +hh });

    float best_dist_sq = std::numeric_limits<float>::infinity();
    cd::math::Vec2f best_pa = a0;
    cd::math::Vec2f best_pb = transform_point_2d(xf_b, poly_b.vertices.at(0));

    for (std::uint8_t i = 0u; i < count_b; ++i)
    {
        const cd::math::Vec2f b0 = transform_point_2d(xf_b, poly_b.vertices.at(i));
        const cd::math::Vec2f b1 =
            transform_point_2d(xf_b, poly_b.vertices.at(static_cast<std::size_t>((i + 1u) % count_b)));
        const auto [pa, pb] = closest_points_segments_2d(a0, a1, b0, b1);
        const float d2      = cd::math::length_sq_v2(pb - pa);
        if (d2 < best_dist_sq)
        {
            best_dist_sq = d2;
            best_pa      = pa;
            best_pb      = pb;
        }
    }

    return collide_spheres_with_radii_2d(best_pa, r, xf_a, best_pb, 0.0F, xf_b, 7u);
}

[[nodiscard]] inline ContactManifold2D collide_capsule_segment_2d(
    const CapsuleShape2D& cap_a,
    const core::Transform2D& xf_a,
    const SegmentShape2D& seg_b,
    const core::Transform2D& xf_b) noexcept
{
    const float hh           = std::max(0.0F, cap_a.half_height);
    const cd::math::Vec2f a0 = transform_point_2d(xf_a, { 0.0F, -hh });
    const cd::math::Vec2f a1 = transform_point_2d(xf_a, { 0.0F, +hh });
    const cd::math::Vec2f b0 = transform_point_2d(xf_b, seg_b.point_a);
    const cd::math::Vec2f b1 = transform_point_2d(xf_b, seg_b.point_b);
    const auto [pa, pb]      = closest_points_segments_2d(a0, a1, b0, b1);
    return collide_spheres_with_radii_2d(pa, cap_a.radius, xf_a, pb, 0.0F, xf_b, 8u);
}

[[nodiscard]] inline ContactManifold2D collide_segment_polygon_2d(
    const SegmentShape2D& seg_a,
    const core::Transform2D& xf_a,
    const ConvexPolygonShape2D& poly_b,
    const core::Transform2D& xf_b) noexcept
{
    // Treat segment as a zero-radius capsule along segment endpoints
    const std::uint8_t count_b = std::min<std::uint8_t>(poly_b.vertex_count, 8u);
    if (count_b < 3u)
    {
        return ContactManifold2D {};
    }
    const cd::math::Vec2f a0   = transform_point_2d(xf_a, seg_a.point_a);
    const cd::math::Vec2f a1   = transform_point_2d(xf_a, seg_a.point_b);
    const cd::math::Vec2f edge = a1 - a0;
    const cd::math::Vec2f n    = cd::math::normalize_v2({ -edge.y, edge.x });

    ContactManifold2D m {};
    for (std::uint8_t i = 0u; i < count_b; ++i)
    {
        const cd::math::Vec2f v  = transform_point_2d(xf_b, poly_b.vertices.at(i));
        const cd::math::Vec2f cp = closest_point_on_segment_2d(v, a0, a1);
        const float signed_d     = cd::math::dot_v2(v - cp, n);
        if (signed_d <= 0.0F && signed_d >= -0.25F)
        {
            m.normal = { -n.x, -n.y };
            ContactPoint2D pt {};
            pt.world_point    = v;
            pt.local_anchor_a = inv_transform_point_2d(xf_a, cp);
            pt.local_anchor_b = poly_b.vertices.at(i);
            pt.penetration    = -signed_d;
            pt.feature_key    = 90u + i;
            m.add_point(pt);
        }
    }
    return m;
}

}  // namespace detail

[[nodiscard]] inline ContactManifold2D collide_shapes_2d(
    const ShapeVariant2D& shape_a,
    const core::Transform2D& xf_a,
    const ShapeVariant2D& shape_b,
    const core::Transform2D& xf_b) noexcept
{
    return std::visit(
        [&xf_a, &xf_b](const auto& a, const auto& b) noexcept -> ContactManifold2D
        {
            using TA = std::decay_t<decltype(a)>;
            using TB = std::decay_t<decltype(b)>;

            // Circle - X
            if constexpr (std::is_same_v<TA, CircleShape2D> && std::is_same_v<TB, CircleShape2D>)
            {
                return detail::collide_circle_circle_2d(a, xf_a, b, xf_b);
            }
            else if constexpr (std::is_same_v<TA, CircleShape2D> && std::is_same_v<TB, BoxShape2D>)
            {
                return detail::collide_circle_box_2d(a, xf_a, b, xf_b);
            }
            else if constexpr (std::is_same_v<TA, BoxShape2D> && std::is_same_v<TB, CircleShape2D>)
            {
                return detail::flip_manifold_2d(detail::collide_circle_box_2d(b, xf_b, a, xf_a));
            }
            else if constexpr (std::is_same_v<TA, CircleShape2D> && std::is_same_v<TB, CapsuleShape2D>)
            {
                return detail::collide_circle_capsule_2d(a, xf_a, b, xf_b);
            }
            else if constexpr (std::is_same_v<TA, CapsuleShape2D> && std::is_same_v<TB, CircleShape2D>)
            {
                return detail::flip_manifold_2d(detail::collide_circle_capsule_2d(b, xf_b, a, xf_a));
            }
            else if constexpr (std::is_same_v<TA, CircleShape2D> && std::is_same_v<TB, SegmentShape2D>)
            {
                return detail::collide_circle_segment_2d(a, xf_a, b, xf_b);
            }
            else if constexpr (std::is_same_v<TA, SegmentShape2D> && std::is_same_v<TB, CircleShape2D>)
            {
                return detail::flip_manifold_2d(detail::collide_circle_segment_2d(b, xf_b, a, xf_a));
            }
            else if constexpr (std::is_same_v<TA, CircleShape2D> && std::is_same_v<TB, ConvexPolygonShape2D>)
            {
                const CapsuleShape2D cap_a { .half_height = 0.0F, .radius = a.radius };
                return detail::collide_capsule_polygon_2d(cap_a, xf_a, b, xf_b);
            }
            else if constexpr (std::is_same_v<TA, ConvexPolygonShape2D> && std::is_same_v<TB, CircleShape2D>)
            {
                const CapsuleShape2D cap_b { .half_height = 0.0F, .radius = b.radius };
                return detail::flip_manifold_2d(detail::collide_capsule_polygon_2d(cap_b, xf_b, a, xf_a));
            }
            // Box - Box & Polygon
            else if constexpr (std::is_same_v<TA, BoxShape2D> && std::is_same_v<TB, BoxShape2D>)
            {
                return detail::collide_polygons_2d(
                    detail::box_to_polygon_2d(a), xf_a, detail::box_to_polygon_2d(b), xf_b);
            }
            else if constexpr (std::is_same_v<TA, BoxShape2D> && std::is_same_v<TB, ConvexPolygonShape2D>)
            {
                return detail::collide_polygons_2d(detail::box_to_polygon_2d(a), xf_a, b, xf_b);
            }
            else if constexpr (std::is_same_v<TA, ConvexPolygonShape2D> && std::is_same_v<TB, BoxShape2D>)
            {
                return detail::collide_polygons_2d(a, xf_a, detail::box_to_polygon_2d(b), xf_b);
            }
            else if constexpr (std::is_same_v<TA, ConvexPolygonShape2D> && std::is_same_v<TB, ConvexPolygonShape2D>)
            {
                return detail::collide_polygons_2d(a, xf_a, b, xf_b);
            }
            // Capsule - X
            else if constexpr (std::is_same_v<TA, CapsuleShape2D> && std::is_same_v<TB, CapsuleShape2D>)
            {
                return detail::collide_capsule_capsule_2d(a, xf_a, b, xf_b);
            }
            else if constexpr (std::is_same_v<TA, CapsuleShape2D> && std::is_same_v<TB, BoxShape2D>)
            {
                return detail::collide_capsule_box_2d(a, xf_a, b, xf_b);
            }
            else if constexpr (std::is_same_v<TA, BoxShape2D> && std::is_same_v<TB, CapsuleShape2D>)
            {
                return detail::flip_manifold_2d(detail::collide_capsule_box_2d(b, xf_b, a, xf_a));
            }
            else if constexpr (std::is_same_v<TA, CapsuleShape2D> && std::is_same_v<TB, ConvexPolygonShape2D>)
            {
                return detail::collide_capsule_polygon_2d(a, xf_a, b, xf_b);
            }
            else if constexpr (std::is_same_v<TA, ConvexPolygonShape2D> && std::is_same_v<TB, CapsuleShape2D>)
            {
                return detail::flip_manifold_2d(detail::collide_capsule_polygon_2d(b, xf_b, a, xf_a));
            }
            else if constexpr (std::is_same_v<TA, CapsuleShape2D> && std::is_same_v<TB, SegmentShape2D>)
            {
                return detail::collide_capsule_segment_2d(a, xf_a, b, xf_b);
            }
            else if constexpr (std::is_same_v<TA, SegmentShape2D> && std::is_same_v<TB, CapsuleShape2D>)
            {
                return detail::flip_manifold_2d(detail::collide_capsule_segment_2d(b, xf_b, a, xf_a));
            }
            // Segment - Box / Polygon
            else if constexpr (std::is_same_v<TA, SegmentShape2D> && std::is_same_v<TB, BoxShape2D>)
            {
                return detail::collide_segment_polygon_2d(a, xf_a, detail::box_to_polygon_2d(b), xf_b);
            }
            else if constexpr (std::is_same_v<TA, BoxShape2D> && std::is_same_v<TB, SegmentShape2D>)
            {
                return detail::flip_manifold_2d(
                    detail::collide_segment_polygon_2d(b, xf_b, detail::box_to_polygon_2d(a), xf_a));
            }
            else if constexpr (std::is_same_v<TA, SegmentShape2D> && std::is_same_v<TB, ConvexPolygonShape2D>)
            {
                return detail::collide_segment_polygon_2d(a, xf_a, b, xf_b);
            }
            else if constexpr (std::is_same_v<TA, ConvexPolygonShape2D> && std::is_same_v<TB, SegmentShape2D>)
            {
                return detail::flip_manifold_2d(detail::collide_segment_polygon_2d(b, xf_b, a, xf_a));
            }
            else
            {
                return ContactManifold2D {};
            }
        },
        shape_a,
        shape_b);
}

}  // namespace cd::physics::v2d

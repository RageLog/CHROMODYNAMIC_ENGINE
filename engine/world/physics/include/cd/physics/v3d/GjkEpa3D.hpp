// =============================================================================
// CHROMODYNAMIC — cd/physics/v3d/GjkEpa3D.hpp
// 3D Gilbert-Johnson-Keerthi (GJK) intersection + Expanding Polytope Algorithm
// (EPA) penetration depth, contact normal, and witness point solver.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/core/PhysicsTypes.hpp>
#include <cd/physics/v3d/ContactManifold3D.hpp>
#include <cd/physics/v3d/Shapes3D.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace cd::physics::v3d
{

namespace detail
{

struct MinkowskiVertex3D
{
    cd::math::Vec3f v {};
    cd::math::Vec3f sup_a {};
    cd::math::Vec3f sup_b {};
};

[[nodiscard]] inline MinkowskiVertex3D support_minkowski_3d(
    const ShapeVariant3D& shape_a,
    const core::Transform3D& xf_a,
    const ShapeVariant3D& shape_b,
    const core::Transform3D& xf_b,
    cd::math::Vec3f dir) noexcept
{
    const float len = cd::math::length(dir);
    const cd::math::Vec3f unit_dir = (len > 1.0e-6F) ? (dir / len) : cd::math::Vec3f { 0.0F, 1.0F, 0.0F };
    MinkowskiVertex3D out {};
    out.sup_a = support_point_world_3d(shape_a, xf_a, unit_dir);
    out.sup_b = support_point_world_3d(shape_b, xf_b, -unit_dir);
    out.v = out.sup_a - out.sup_b;
    return out;
}

[[nodiscard]] inline cd::math::Vec3f any_perpendicular_3d(cd::math::Vec3f v) noexcept
{
    if (std::abs(v.x) < 0.57735F)
    {
        return cd::math::normalize(cd::math::cross(v, cd::math::Vec3f { 1.0F, 0.0F, 0.0F }));
    }
    return cd::math::normalize(cd::math::cross(v, cd::math::Vec3f { 0.0F, 1.0F, 0.0F }));
}

struct GjkSimplex3D
{
    std::array<MinkowskiVertex3D, 4> pts {};
    std::uint8_t count { 0u };

    constexpr void push_front(const MinkowskiVertex3D& p) noexcept
    {
        pts[3] = pts[2];
        pts[2] = pts[1];
        pts[1] = pts[0];
        pts[0] = p;
        if (count < 4u)
        {
            ++count;
        }
    }
};

[[nodiscard]] inline bool gjk_line_case(GjkSimplex3D& s, cd::math::Vec3f& dir) noexcept
{
    const cd::math::Vec3f a = s.pts[0].v;
    const cd::math::Vec3f b = s.pts[1].v;
    const cd::math::Vec3f ab = b - a;
    const cd::math::Vec3f ao = -a;

    if (cd::math::dot(ab, ao) > 0.0F)
    {
        dir = cd::math::cross(cd::math::cross(ab, ao), ab);
        if (cd::math::length_squared(dir) < 1.0e-10F)
        {
            dir = any_perpendicular_3d(ab);
        }
    }
    else
    {
        s.count = 1u;
        dir = ao;
    }
    return false;
}

[[nodiscard]] inline bool gjk_triangle_case(GjkSimplex3D& s, cd::math::Vec3f& dir) noexcept
{
    const cd::math::Vec3f a = s.pts[0].v;
    const cd::math::Vec3f b = s.pts[1].v;
    const cd::math::Vec3f c = s.pts[2].v;

    const cd::math::Vec3f ab = b - a;
    const cd::math::Vec3f ac = c - a;
    const cd::math::Vec3f ao = -a;
    const cd::math::Vec3f abc = cd::math::cross(ab, ac);

    if (cd::math::dot(cd::math::cross(abc, ac), ao) > 0.0F)
    {
        if (cd::math::dot(ac, ao) > 0.0F)
        {
            s.pts[1] = s.pts[2];
            s.count = 2u;
            dir = cd::math::cross(cd::math::cross(ac, ao), ac);
            if (cd::math::length_squared(dir) < 1.0e-10F)
            {
                dir = any_perpendicular_3d(ac);
            }
        }
        else
        {
            s.count = 2u;
            return gjk_line_case(s, dir);
        }
    }
    else
    {
        if (cd::math::dot(cd::math::cross(ab, abc), ao) > 0.0F)
        {
            s.count = 2u;
            return gjk_line_case(s, dir);
        }
        if (cd::math::dot(abc, ao) > 0.0F)
        {
            dir = abc;
        }
        else
        {
            std::swap(s.pts[1], s.pts[2]);
            dir = -abc;
        }
    }
    return false;
}

[[nodiscard]] inline bool gjk_tetrahedron_case(GjkSimplex3D& s, cd::math::Vec3f& dir) noexcept
{
    const cd::math::Vec3f a = s.pts[0].v;
    const cd::math::Vec3f b = s.pts[1].v;
    const cd::math::Vec3f c = s.pts[2].v;
    const cd::math::Vec3f d = s.pts[3].v;

    const cd::math::Vec3f ab = b - a;
    const cd::math::Vec3f ac = c - a;
    const cd::math::Vec3f ad = d - a;
    const cd::math::Vec3f ao = -a;

    const cd::math::Vec3f abc = cd::math::cross(ab, ac);
    const cd::math::Vec3f acd = cd::math::cross(ac, ad);
    const cd::math::Vec3f adb = cd::math::cross(ad, ab);

    if (cd::math::dot(abc, ao) > 0.0F)
    {
        s.count = 3u;
        return gjk_triangle_case(s, dir);
    }
    if (cd::math::dot(acd, ao) > 0.0F)
    {
        s.pts[1] = s.pts[2];
        s.pts[2] = s.pts[3];
        s.count = 3u;
        return gjk_triangle_case(s, dir);
    }
    if (cd::math::dot(adb, ao) > 0.0F)
    {
        s.pts[2] = s.pts[1];
        s.pts[1] = s.pts[3];
        s.count = 3u;
        return gjk_triangle_case(s, dir);
    }
    return true;
}

[[nodiscard]] inline bool update_gjk_simplex(GjkSimplex3D& s, cd::math::Vec3f& dir) noexcept
{
    switch (s.count)
    {
        case 2u:
            return gjk_line_case(s, dir);
        case 3u:
            return gjk_triangle_case(s, dir);
        case 4u:
            return gjk_tetrahedron_case(s, dir);
        default:
            return false;
    }
}

struct EpaFace3D
{
    std::uint8_t i0 { 0u };
    std::uint8_t i1 { 0u };
    std::uint8_t i2 { 0u };
    cd::math::Vec3f normal { 0.0F, 1.0F, 0.0F };
    float distance { 0.0F };
    bool active { false };
};

[[nodiscard]] inline EpaFace3D make_epa_face(
    const std::array<MinkowskiVertex3D, 64>& verts,
    std::uint8_t i0,
    std::uint8_t i1,
    std::uint8_t i2) noexcept
{
    EpaFace3D f {};
    f.i0 = i0;
    f.i1 = i1;
    f.i2 = i2;
    const cd::math::Vec3f a = verts[i0].v;
    const cd::math::Vec3f b = verts[i1].v;
    const cd::math::Vec3f c = verts[i2].v;
    const cd::math::Vec3f cr = cd::math::cross(b - a, c - a);
    const float len = cd::math::length(cr);
    if (len > 1.0e-7F)
    {
        f.normal = cr / len;
        f.distance = cd::math::dot(f.normal, a);
        if (f.distance < 0.0F)
        {
            f.normal = -f.normal;
            f.distance = -f.distance;
            std::swap(f.i1, f.i2);
        }
        f.active = true;
    }
    return f;
}

[[nodiscard]] inline cd::math::Vec3f closest_barycentric_on_triangle(
    cd::math::Vec3f a,
    cd::math::Vec3f b,
    cd::math::Vec3f c,
    cd::math::Vec3f p) noexcept
{
    const cd::math::Vec3f v0 = b - a;
    const cd::math::Vec3f v1 = c - a;
    const cd::math::Vec3f v2 = p - a;
    const float d00 = cd::math::dot(v0, v0);
    const float d01 = cd::math::dot(v0, v1);
    const float d11 = cd::math::dot(v1, v1);
    const float d20 = cd::math::dot(v2, v0);
    const float d21 = cd::math::dot(v2, v1);
    const float denom = d00 * d11 - d01 * d01;
    if (std::abs(denom) <= 1.0e-10F)
    {
        return { 1.0F, 0.0F, 0.0F };
    }
    float v = (d11 * d20 - d01 * d21) / denom;
    float w = (d00 * d21 - d01 * d20) / denom;
    v = std::clamp(v, 0.0F, 1.0F);
    w = std::clamp(w, 0.0F, 1.0F - v);
    const float u = 1.0F - v - w;
    return { u, v, w };
}

}  // namespace detail

[[nodiscard]] inline bool gjk_epa_intersect_3d(
    const ShapeVariant3D& shape_a,
    const core::Transform3D& xf_a,
    const ShapeVariant3D& shape_b,
    const core::Transform3D& xf_b,
    ContactManifold3D& out) noexcept
{
    out.clear();

    cd::math::Vec3f dir = xf_b.position - xf_a.position;
    if (cd::math::length_squared(dir) < 1.0e-8F)
    {
        dir = { 1.0F, 0.0F, 0.0F };
    }

    detail::GjkSimplex3D simplex {};
    simplex.push_front(detail::support_minkowski_3d(shape_a, xf_a, shape_b, xf_b, dir));
    if (cd::math::dot(simplex.pts[0].v, dir) <= 0.0F)
    {
        return false;
    }

    dir = -simplex.pts[0].v;
    bool intersected = false;

    for (std::uint32_t iter = 0u; iter < 48u; ++iter)
    {
        if (cd::math::length_squared(dir) < 1.0e-10F)
        {
            intersected = true;
            break;
        }
        const detail::MinkowskiVertex3D p = detail::support_minkowski_3d(shape_a, xf_a, shape_b, xf_b, dir);
        if (cd::math::dot(p.v, dir) < -1.0e-6F)
        {
            return false;
        }
        simplex.push_front(p);
        if (detail::update_gjk_simplex(simplex, dir))
        {
            intersected = true;
            break;
        }
    }

    if (!intersected)
    {
        return false;
    }

    // Expand simplex to 4 vertices if GJK terminated early on a degenerate simplex.
    static constexpr std::array<cd::math::Vec3f, 14> kProbeDirs { {
        { 1.0F, 0.0F, 0.0F },  { -1.0F, 0.0F, 0.0F },
        { 0.0F, 1.0F, 0.0F },  { 0.0F, -1.0F, 0.0F },
        { 0.0F, 0.0F, 1.0F },  { 0.0F, 0.0F, -1.0F },
        { 0.57735F, 0.57735F, 0.57735F },   { -0.57735F, 0.57735F, 0.57735F },
        { 0.57735F, -0.57735F, 0.57735F },  { 0.57735F, 0.57735F, -0.57735F },
        { -0.57735F, -0.57735F, 0.57735F }, { -0.57735F, 0.57735F, -0.57735F },
        { 0.57735F, -0.57735F, -0.57735F }, { -0.57735F, -0.57735F, -0.57735F }
    } };

    for (const auto& pdir : kProbeDirs)
    {
        if (simplex.count >= 4u)
        {
            break;
        }
        const detail::MinkowskiVertex3D cand = detail::support_minkowski_3d(shape_a, xf_a, shape_b, xf_b, pdir);
        bool duplicate = false;
        for (std::uint8_t i = 0u; i < simplex.count; ++i)
        {
            if (cd::math::length_squared(cand.v - simplex.pts[i].v) < 1.0e-6F)
            {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
        {
            simplex.pts[simplex.count++] = cand;
        }
    }

    // Run EPA on the 4-vertex polytope; fallback to 14-direction SAT probe if degenerate.
    std::array<detail::MinkowskiVertex3D, 64> verts {};
    std::uint8_t vert_count = simplex.count;
    for (std::uint8_t i = 0u; i < vert_count; ++i)
    {
        verts[i] = simplex.pts[i];
    }

    std::array<detail::EpaFace3D, 64> faces {};
    std::uint8_t face_count = 0u;
    if (vert_count == 4u)
    {
        faces[0] = detail::make_epa_face(verts, 0u, 1u, 2u);
        faces[1] = detail::make_epa_face(verts, 0u, 3u, 1u);
        faces[2] = detail::make_epa_face(verts, 0u, 2u, 3u);
        faces[3] = detail::make_epa_face(verts, 1u, 3u, 2u);
        face_count = 4u;
    }

    bool epa_valid = (face_count == 4u) && faces[0].active && faces[1].active && faces[2].active && faces[3].active;
    std::uint8_t best_face_idx = 0u;

    if (epa_valid)
    {
        for (std::uint32_t iter = 0u; iter < 32u; ++iter)
        {
            float min_dist = std::numeric_limits<float>::max();
            best_face_idx = 0u;
            bool found_face = false;
            for (std::uint8_t i = 0u; i < face_count; ++i)
            {
                if (faces[i].active && faces[i].distance < min_dist)
                {
                    min_dist = faces[i].distance;
                    best_face_idx = i;
                    found_face = true;
                }
            }
            if (!found_face)
            {
                epa_valid = false;
                break;
            }

            const detail::EpaFace3D& closest = faces[best_face_idx];
            const detail::MinkowskiVertex3D w = detail::support_minkowski_3d(
                shape_a, xf_a, shape_b, xf_b, closest.normal);

            const float w_dist = cd::math::dot(w.v, closest.normal);
            if (w_dist - closest.distance <= 1.0e-4F || vert_count >= 60u)
            {
                break;
            }

            // Collect horizon edges of faces visible from w.v.
            struct Edge
            {
                std::uint8_t a;
                std::uint8_t b;
            };
            std::array<Edge, 64> horizon {};
            std::uint8_t horizon_count = 0u;

            auto add_edge = [&](std::uint8_t a, std::uint8_t b) noexcept
            {
                for (std::uint8_t k = 0u; k < horizon_count; ++k)
                {
                    if (horizon[k].a == b && horizon[k].b == a)
                    {
                        horizon[k] = horizon[--horizon_count];
                        return;
                    }
                }
                if (horizon_count < 64u)
                {
                    horizon[horizon_count++] = Edge { a, b };
                }
            };

            for (std::uint8_t i = 0u; i < face_count; ++i)
            {
                if (!faces[i].active)
                {
                    continue;
                }
                if (cd::math::dot(faces[i].normal, w.v - verts[faces[i].i0].v) > 1.0e-6F)
                {
                    faces[i].active = false;
                    add_edge(faces[i].i0, faces[i].i1);
                    add_edge(faces[i].i1, faces[i].i2);
                    add_edge(faces[i].i2, faces[i].i0);
                }
            }

            if (horizon_count == 0u)
            {
                break;
            }

            const std::uint8_t w_idx = vert_count++;
            verts[w_idx] = w;

            for (std::uint8_t k = 0u; k < horizon_count; ++k)
            {
                if (face_count < 64u)
                {
                    faces[face_count++] = detail::make_epa_face(verts, horizon[k].a, horizon[k].b, w_idx);
                }
            }
        }
    }

    if (epa_valid && faces[best_face_idx].active)
    {
        const detail::EpaFace3D& bf = faces[best_face_idx];
        const detail::MinkowskiVertex3D& v0 = verts[bf.i0];
        const detail::MinkowskiVertex3D& v1 = verts[bf.i1];
        const detail::MinkowskiVertex3D& v2 = verts[bf.i2];

        const cd::math::Vec3f proj = bf.normal * bf.distance;
        const cd::math::Vec3f bary = detail::closest_barycentric_on_triangle(v0.v, v1.v, v2.v, proj);
        const cd::math::Vec3f pt_a = v0.sup_a * bary.x + v1.sup_a * bary.y + v2.sup_a * bary.z;
        const cd::math::Vec3f pt_b = v0.sup_b * bary.x + v1.sup_b * bary.y + v2.sup_b * bary.z;

        cd::math::Vec3f n = bf.normal;
        const cd::math::Vec3f ab_center = xf_b.position - xf_a.position;
        if (cd::math::dot(n, ab_center) < 0.0F && cd::math::length_squared(ab_center) > 1.0e-6F)
        {
            n = -n;
        }

        out.normal = n;
        out.compute_tangent_basis();

        ContactPoint3D cp {};
        cp.world_point = (pt_a + pt_b) * 0.5F;
        cp.local_anchor_a = xf_a.inverse_transform_point(pt_a);
        cp.local_anchor_b = xf_b.inverse_transform_point(pt_b);
        cp.penetration = std::max(0.0001F, bf.distance);
        cp.feature_key = 1u;
        out.add_point(cp);
        return true;
    }

    // Fallback 14-direction minimum penetration probe if polytope is degenerate.
    float best_pen = std::numeric_limits<float>::max();
    cd::math::Vec3f best_normal { 0.0F, 1.0F, 0.0F };
    detail::MinkowskiVertex3D best_mv {};

    for (const auto& pdir : kProbeDirs)
    {
        const detail::MinkowskiVertex3D mv = detail::support_minkowski_3d(shape_a, xf_a, shape_b, xf_b, pdir);
        const float pen = cd::math::dot(mv.v, pdir);
        if (pen < best_pen && pen > 0.0F)
        {
            best_pen = pen;
            best_normal = pdir;
            best_mv = mv;
        }
    }

    if (best_pen == std::numeric_limits<float>::max())
    {
        return false;
    }

    const cd::math::Vec3f ab_center = xf_b.position - xf_a.position;
    if (cd::math::dot(best_normal, ab_center) < 0.0F && cd::math::length_squared(ab_center) > 1.0e-6F)
    {
        best_normal = -best_normal;
    }

    out.normal = best_normal;
    out.compute_tangent_basis();

    ContactPoint3D cp {};
    cp.world_point = (best_mv.sup_a + best_mv.sup_b) * 0.5F;
    cp.local_anchor_a = xf_a.inverse_transform_point(best_mv.sup_a);
    cp.local_anchor_b = xf_b.inverse_transform_point(best_mv.sup_b);
    cp.penetration = std::max(0.0001F, best_pen);
    cp.feature_key = 2u;
    out.add_point(cp);
    return true;
}

}  // namespace cd::physics::v3d

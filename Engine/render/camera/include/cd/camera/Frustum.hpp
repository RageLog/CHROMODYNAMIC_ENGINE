// =============================================================================
// CHROMODYNAMIC — cd/camera/Frustum.hpp
//
// View-frustum extraction + AABB intersection test for cull-on-the-CPU
// passes. Header-only because every consumer needs the same templated
// math and pulling cd::camera in is already a one-line dep.
//
// Plane extraction follows the well-known Gribb-Hartmann method
// ("Fast Extraction of Viewing Frustum Planes from the World-View-
// Projection Matrix", 2001): rows of the view-projection matrix combined
// give the six plane equations. The implementation here is column-major,
// matching cd::math::Mat4f's storage.
//
// Plane convention: each plane's normal points INWARD (into the visible
// volume). An AABB is visible if it is on the positive side of every
// plane. We use the "p-vertex" test — pick the AABB corner furthest in
// the direction of the plane normal; if THAT vertex is behind the plane,
// the entire AABB is outside.
// =============================================================================
#pragma once

#include <cd/camera/Camera.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>

#include <array>
#include <cmath>
#include <cstddef>  // std::size_t for kFrustumPlaneCount
#include <cstdint>  // std::uint8_t — required for enum class underlying-type clause under GCC

namespace cd::camera
{

/// One plane of the frustum, stored in `ax + by + cz + d = 0` form with
/// `(a, b, c)` already normalised. Inward-facing normal.
struct Plane
{
    cd::math::Vec3f normal { 0.0F, 0.0F, 1.0F };
    float d { 0.0F };
};

/// Six planes ordered: left, right, bottom, top, near, far. The Gribb-
/// Hartmann derivation pairs them with matrix-row sums/differences; the
/// ordering chosen here matches DirectX/Vulkan convention.
///
/// Strongly-typed `enum class` (not bare `enum`) so the enumerators do
/// not pollute the surrounding namespace — important because GCC 15 + MinGW
/// rejected the prior unscoped form here. Integer-indexed access is the
/// expected usage pattern; the namespace-scope `kFrustumPlaneCount`
/// constant gives the array bound without forcing a `static_cast` at
/// every declaration.
inline constexpr std::size_t kFrustumPlaneCount = 6;

enum class FrustumFace : std::uint8_t
{
    kLeft = 0,
    kRight = 1,
    kBottom = 2,
    kTop = 3,
    kNear = 4,
    kFar = 5,
};

struct Frustum
{
    std::array<Plane, kFrustumPlaneCount> planes {};
};

/// Build a frustum from a view-projection matrix. Works for both
/// perspective and orthographic projections — the math is purely on the
/// MVP rows.
[[nodiscard]] inline Frustum extract_frustum(const cd::math::Mat4f& vp) noexcept
{
    // Helper: column-major mat[col][row] access. Compose row vectors
    // r0 .. r3 from the matrix columns so the plane formulas read like
    // the reference paper.
    auto row = [&](std::size_t r) noexcept
    {
        return cd::math::Vec4f { vp[0][r], vp[1][r], vp[2][r], vp[3][r] };
    };
    const auto r0 = row(0);
    const auto r1 = row(1);
    const auto r2 = row(2);
    const auto r3 = row(3);

    auto plane = [](const cd::math::Vec4f& v) noexcept
    {
        Plane p {};
        p.normal = { v[0], v[1], v[2] };
        p.d = v[3];
        const float len = std::sqrt(p.normal[0] * p.normal[0] + p.normal[1] * p.normal[1] + p.normal[2] * p.normal[2]);
        if (len > 1e-6F)
        {
            p.normal[0] /= len;
            p.normal[1] /= len;
            p.normal[2] /= len;
            p.d /= len;
        }
        return p;
    };

    Frustum f {};
    // Vulkan/D3D clip space (depth in [0, 1]):
    //   left  = r3 + r0    right = r3 - r0
    //   bot   = r3 + r1    top   = r3 - r1
    //   near  = r2         far   = r3 - r2
    auto face = [](FrustumFace fc) noexcept
    {
        return static_cast<std::size_t>(fc);
    };
    f.planes[face(FrustumFace::kLeft)] = plane({ r3[0] + r0[0], r3[1] + r0[1], r3[2] + r0[2], r3[3] + r0[3] });
    f.planes[face(FrustumFace::kRight)] = plane({ r3[0] - r0[0], r3[1] - r0[1], r3[2] - r0[2], r3[3] - r0[3] });
    f.planes[face(FrustumFace::kBottom)] = plane({ r3[0] + r1[0], r3[1] + r1[1], r3[2] + r1[2], r3[3] + r1[3] });
    f.planes[face(FrustumFace::kTop)] = plane({ r3[0] - r1[0], r3[1] - r1[1], r3[2] - r1[2], r3[3] - r1[3] });
    f.planes[face(FrustumFace::kNear)] = plane({ r2[0], r2[1], r2[2], r2[3] });
    f.planes[face(FrustumFace::kFar)] = plane({ r3[0] - r2[0], r3[1] - r2[1], r3[2] - r2[2], r3[3] - r2[3] });
    return f;
}

/// Build the frustum directly from a Camera + aspect — convenience
/// wrapper that composes the MVP for you.
[[nodiscard]] inline Frustum extract_frustum(const Camera& c, float aspect) noexcept
{
    return extract_frustum(view_projection(c, aspect));
}

/// Result of culling one AABB. We return a 3-valued result instead of a
/// plain bool so renderers can opt into the "inside" fast-path that
/// skips per-child re-culling for fully-inside parent volumes.
enum class CullResult : std::uint8_t
{
    kOutside,
    kIntersecting,
    kInside,
};

/// Test an axis-aligned bounding box against the frustum. p-vertex /
/// n-vertex two-corner trick: each plane needs only two of the eight
/// AABB corners — the one furthest WITH and the one furthest AGAINST
/// the plane normal. Outside if even one plane rejects the p-vertex,
/// intersecting if any plane rejects the n-vertex while accepting p.
[[nodiscard]] inline CullResult
test_aabb(const Frustum& f, const cd::math::Vec3f& bb_min, const cd::math::Vec3f& bb_max) noexcept
{
    bool intersect = false;
    for (const auto& p : f.planes)
    {
        cd::math::Vec3f pv {};
        cd::math::Vec3f nv {};
        for (std::size_t k = 0; k < 3; ++k)
        {
            if (p.normal[k] >= 0.0F)
            {
                pv[k] = bb_max[k];
                nv[k] = bb_min[k];
            }
            else
            {
                pv[k] = bb_min[k];
                nv[k] = bb_max[k];
            }
        }
        // Distance from origin to plane along normal: dot(n, v) + d.
        const float dp = p.normal[0] * pv[0] + p.normal[1] * pv[1] + p.normal[2] * pv[2] + p.d;
        if (dp < 0.0F)
            return CullResult::kOutside;
        const float dn = p.normal[0] * nv[0] + p.normal[1] * nv[1] + p.normal[2] * nv[2] + p.d;
        if (dn < 0.0F)
            intersect = true;
    }
    return intersect ? CullResult::kIntersecting : CullResult::kInside;
}

}  // namespace cd::camera

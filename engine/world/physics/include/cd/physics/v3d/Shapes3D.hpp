// =============================================================================
// CHROMODYNAMIC — cd/physics/v3d/Shapes3D.hpp
// Jolt / Rubikon / PhysX 5 grade 3D collision shape primitives, world AABB,
// analytical local inertia tensors, and GJK support mappings.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Constants.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/core/PhysicsTypes.hpp>
#include <cd/physics/core/SpatialAabb3D.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <variant>
#include <vector>

namespace cd::physics::v3d
{

struct SphereShape3D
{
    float radius { 0.5F };
};

struct BoxShape3D
{
    cd::math::Vec3f half_extents { 0.5F, 0.5F, 0.5F };
};

/// Capsule aligned with the local Y-axis (spine from (0, -half_height, 0) to (0, +half_height, 0)).
struct CapsuleShape3D
{
    float half_height { 0.5F };
    float radius { 0.25F };
};

/// Cylinder aligned with the local Y-axis (height = 2 * half_height).
struct CylinderShape3D
{
    float half_height { 0.5F };
    float radius { 0.5F };
};

struct ConvexHullShape3D
{
    std::vector<cd::math::Vec3f> vertices {};
};

struct HeightfieldShape3D
{
    std::vector<float> heights {};
    std::uint32_t rows { 0u };
    std::uint32_t cols { 0u };
    cd::math::Vec3f scale { 1.0F, 1.0F, 1.0F };

    [[nodiscard]] float sample_height_at_cell(std::uint32_t r, std::uint32_t c) const noexcept
    {
        if (rows == 0u || cols == 0u || heights.empty())
        {
            return 0.0F;
        }
        const std::uint32_t rr = std::min(r, rows - 1u);
        const std::uint32_t cc = std::min(c, cols - 1u);
        const std::size_t idx = static_cast<std::size_t>(rr) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(cc);
        if (idx >= heights.size())
        {
            return 0.0F;
        }
        return heights[idx] * scale.y;
    }

    /// Bilinearly interpolated height and surface normal in local heightfield coordinates.
    [[nodiscard]] bool sample_local(float local_x, float local_z, float& out_y, cd::math::Vec3f& out_normal) const noexcept
    {
        if (rows < 2u || cols < 2u || heights.size() < static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols))
        {
            out_y = sample_height_at_cell(0u, 0u);
            out_normal = { 0.0F, 1.0F, 0.0F };
            return !heights.empty();
        }

        const float sx = std::max(1.0e-5F, std::abs(scale.x));
        const float sz = std::max(1.0e-5F, std::abs(scale.z));
        const float total_w = static_cast<float>(cols - 1u) * sx;
        const float total_d = static_cast<float>(rows - 1u) * sz;

        // Support centered local coordinates [-0.5*W, +0.5*W] with seamless fallback to [0, W].
        float gx = (local_x + 0.5F * total_w) / sx;
        float gz = (local_z + 0.5F * total_d) / sz;
        if (gx < 0.0F || gx > static_cast<float>(cols - 1u) || gz < 0.0F || gz > static_cast<float>(rows - 1u))
        {
            if (local_x >= 0.0F && local_x <= total_w && local_z >= 0.0F && local_z <= total_d)
            {
                gx = local_x / sx;
                gz = local_z / sz;
            }
            else
            {
                return false;
            }
        }

        gx = std::clamp(gx, 0.0F, static_cast<float>(cols - 1u) - 1.0e-5F);
        gz = std::clamp(gz, 0.0F, static_cast<float>(rows - 1u) - 1.0e-5F);

        const auto c0 = static_cast<std::uint32_t>(gx);
        const auto r0 = static_cast<std::uint32_t>(gz);
        const std::uint32_t c1 = std::min(c0 + 1u, cols - 1u);
        const std::uint32_t r1 = std::min(r0 + 1u, rows - 1u);

        const float tx = gx - static_cast<float>(c0);
        const float tz = gz - static_cast<float>(r0);

        const float h00 = sample_height_at_cell(r0, c0);
        const float h10 = sample_height_at_cell(r0, c1);
        const float h01 = sample_height_at_cell(r1, c0);
        const float h11 = sample_height_at_cell(r1, c1);

        const float h0 = h00 + (h10 - h00) * tx;
        const float h1 = h01 + (h11 - h01) * tx;
        out_y = h0 + (h1 - h0) * tz;

        const float dhdx = ((1.0F - tz) * (h10 - h00) + tz * (h11 - h01)) / sx;
        const float dhdz = ((1.0F - tx) * (h01 - h00) + tx * (h11 - h10)) / sz;
        out_normal = cd::math::normalize(cd::math::Vec3f { -dhdx, 1.0F, -dhdz });
        return true;
    }
};

using ShapeVariant3D = std::variant<
    SphereShape3D,
    BoxShape3D,
    CapsuleShape3D,
    CylinderShape3D,
    ConvexHullShape3D,
    HeightfieldShape3D>;

[[nodiscard]] inline cd::math::Vec3f support_point_local_3d(
    const ShapeVariant3D& shape,
    cd::math::Vec3f local_dir) noexcept
{
    return std::visit(
        [local_dir](const auto& s) noexcept -> cd::math::Vec3f
        {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, SphereShape3D>)
            {
                const float r = std::max(0.0F, s.radius);
                const float len = cd::math::length(local_dir);
                if (len > 1.0e-6F)
                {
                    return local_dir * (r / len);
                }
                return { 0.0F, r, 0.0F };
            }
            else if constexpr (std::is_same_v<T, BoxShape3D>)
            {
                return {
                    local_dir.x >= 0.0F ? s.half_extents.x : -s.half_extents.x,
                    local_dir.y >= 0.0F ? s.half_extents.y : -s.half_extents.y,
                    local_dir.z >= 0.0F ? s.half_extents.z : -s.half_extents.z
                };
            }
            else if constexpr (std::is_same_v<T, CapsuleShape3D>)
            {
                const float hh = std::max(0.0F, s.half_height);
                const float r = std::max(0.0F, s.radius);
                const cd::math::Vec3f spine { 0.0F, local_dir.y >= 0.0F ? hh : -hh, 0.0F };
                const float len = cd::math::length(local_dir);
                if (len > 1.0e-6F)
                {
                    return spine + local_dir * (r / len);
                }
                return spine + cd::math::Vec3f { 0.0F, r, 0.0F };
            }
            else if constexpr (std::is_same_v<T, CylinderShape3D>)
            {
                const float hh = std::max(0.0F, s.half_height);
                const float r = std::max(0.0F, s.radius);
                const float y = local_dir.y >= 0.0F ? hh : -hh;
                const float radial_len = std::sqrt(local_dir.x * local_dir.x + local_dir.z * local_dir.z);
                if (radial_len > 1.0e-6F)
                {
                    const float s_rad = r / radial_len;
                    return { local_dir.x * s_rad, y, local_dir.z * s_rad };
                }
                return { 0.0F, y, 0.0F };
            }
            else if constexpr (std::is_same_v<T, ConvexHullShape3D>)
            {
                if (s.vertices.empty())
                {
                    return { 0.0F, 0.0F, 0.0F };
                }
                cd::math::Vec3f best = s.vertices[0];
                float best_dot = cd::math::dot(best, local_dir);
                for (std::size_t i = 1u; i < s.vertices.size(); ++i)
                {
                    const float d = cd::math::dot(s.vertices[i], local_dir);
                    if (d > best_dot)
                    {
                        best_dot = d;
                        best = s.vertices[i];
                    }
                }
                return best;
            }
            else if constexpr (std::is_same_v<T, HeightfieldShape3D>)
            {
                if (s.rows == 0u || s.cols == 0u || s.heights.empty())
                {
                    return { 0.0F, 0.0F, 0.0F };
                }
                const float hw = 0.5F * static_cast<float>(s.cols > 1u ? s.cols - 1u : 0u) * std::abs(s.scale.x);
                const float hd = 0.5F * static_cast<float>(s.rows > 1u ? s.rows - 1u : 0u) * std::abs(s.scale.z);
                float min_h = s.heights[0] * s.scale.y;
                float max_h = min_h;
                for (const float h : s.heights)
                {
                    const float sh = h * s.scale.y;
                    min_h = std::min(min_h, sh);
                    max_h = std::max(max_h, sh);
                }
                return {
                    local_dir.x >= 0.0F ? hw : -hw,
                    local_dir.y >= 0.0F ? max_h : min_h,
                    local_dir.z >= 0.0F ? hd : -hd
                };
            }
        },
        shape);
}

[[nodiscard]] inline cd::math::Vec3f support_point_world_3d(
    const ShapeVariant3D& shape,
    const core::Transform3D& xf,
    cd::math::Vec3f dir) noexcept
{
    const cd::math::Vec3f local_dir = xf.inverse_rotate_vector(dir);
    const cd::math::Vec3f local_pt = support_point_local_3d(shape, local_dir);
    return xf.transform_point(local_pt);
}

[[nodiscard]] inline core::SpatialAabb3D compute_aabb_3d(
    const ShapeVariant3D& shape,
    const core::Transform3D& xf) noexcept
{
    return std::visit(
        [&xf](const auto& s) noexcept -> core::SpatialAabb3D
        {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, SphereShape3D>)
            {
                const float r = std::max(0.0F, s.radius);
                const cd::math::Vec3f rad { r, r, r };
                return core::SpatialAabb3D { xf.position - rad, xf.position + rad };
            }
            else if constexpr (std::is_same_v<T, BoxShape3D>)
            {
                const cd::math::Vec3f ax = xf.rotate_vector({ s.half_extents.x, 0.0F, 0.0F });
                const cd::math::Vec3f ay = xf.rotate_vector({ 0.0F, s.half_extents.y, 0.0F });
                const cd::math::Vec3f az = xf.rotate_vector({ 0.0F, 0.0F, s.half_extents.z });
                const cd::math::Vec3f ext {
                    std::abs(ax.x) + std::abs(ay.x) + std::abs(az.x),
                    std::abs(ax.y) + std::abs(ay.y) + std::abs(az.y),
                    std::abs(ax.z) + std::abs(ay.z) + std::abs(az.z)
                };
                return core::SpatialAabb3D { xf.position - ext, xf.position + ext };
            }
            else if constexpr (std::is_same_v<T, CapsuleShape3D>)
            {
                const float hh = std::max(0.0F, s.half_height);
                const float r = std::max(0.0F, s.radius);
                const cd::math::Vec3f p0 = xf.transform_point({ 0.0F, -hh, 0.0F });
                const cd::math::Vec3f p1 = xf.transform_point({ 0.0F, +hh, 0.0F });
                const cd::math::Vec3f rad { r, r, r };
                return core::SpatialAabb3D {
                    cd::math::min(p0, p1) - rad,
                    cd::math::max(p0, p1) + rad
                };
            }
            else if constexpr (std::is_same_v<T, CylinderShape3D>)
            {
                const float hh = std::max(0.0F, s.half_height);
                const float r = std::max(0.0F, s.radius);
                const cd::math::Vec3f u = xf.rotate_vector({ 0.0F, 1.0F, 0.0F });
                const cd::math::Vec3f ext {
                    hh * std::abs(u.x) + r * std::sqrt(std::max(0.0F, 1.0F - u.x * u.x)),
                    hh * std::abs(u.y) + r * std::sqrt(std::max(0.0F, 1.0F - u.y * u.y)),
                    hh * std::abs(u.z) + r * std::sqrt(std::max(0.0F, 1.0F - u.z * u.z))
                };
                return core::SpatialAabb3D { xf.position - ext, xf.position + ext };
            }
            else if constexpr (std::is_same_v<T, ConvexHullShape3D>)
            {
                if (s.vertices.empty())
                {
                    return core::SpatialAabb3D { xf.position, xf.position };
                }
                cd::math::Vec3f bmin = xf.transform_point(s.vertices[0]);
                cd::math::Vec3f bmax = bmin;
                for (std::size_t i = 1u; i < s.vertices.size(); ++i)
                {
                    const cd::math::Vec3f wp = xf.transform_point(s.vertices[i]);
                    bmin = cd::math::min(bmin, wp);
                    bmax = cd::math::max(bmax, wp);
                }
                return core::SpatialAabb3D { bmin, bmax };
            }
            else if constexpr (std::is_same_v<T, HeightfieldShape3D>)
            {
                if (s.rows == 0u || s.cols == 0u || s.heights.empty())
                {
                    return core::SpatialAabb3D { xf.position, xf.position };
                }
                const float w = static_cast<float>(s.cols > 1u ? s.cols - 1u : 1u) * std::abs(s.scale.x);
                const float d = static_cast<float>(s.rows > 1u ? s.rows - 1u : 1u) * std::abs(s.scale.z);
                float min_h = s.heights[0] * s.scale.y;
                float max_h = min_h;
                for (const float h : s.heights)
                {
                    const float sh = h * s.scale.y;
                    min_h = std::min(min_h, sh);
                    max_h = std::max(max_h, sh);
                }
                const cd::math::Vec3f local_min { -0.5F * w, min_h - 0.5F, -0.5F * d };
                const cd::math::Vec3f local_max { w, max_h + 0.5F, d };
                cd::math::Vec3f bmin = xf.transform_point(local_min);
                cd::math::Vec3f bmax = bmin;
                for (int ix = 0; ix < 2; ++ix)
                {
                    for (int iy = 0; iy < 2; ++iy)
                    {
                        for (int iz = 0; iz < 2; ++iz)
                        {
                            const cd::math::Vec3f corner {
                                ix == 0 ? local_min.x : local_max.x,
                                iy == 0 ? local_min.y : local_max.y,
                                iz == 0 ? local_min.z : local_max.z
                            };
                            const cd::math::Vec3f wp = xf.transform_point(corner);
                            bmin = cd::math::min(bmin, wp);
                            bmax = cd::math::max(bmax, wp);
                        }
                    }
                }
                return core::SpatialAabb3D { bmin, bmax };
            }
        },
        shape);
}

[[nodiscard]] inline cd::math::Vec3f compute_local_inertia_diagonal_3d(
    const ShapeVariant3D& shape,
    float mass) noexcept
{
    if (mass <= 0.0F)
    {
        return { 0.0F, 0.0F, 0.0F };
    }

    return std::visit(
        [mass](const auto& s) noexcept -> cd::math::Vec3f
        {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, SphereShape3D>)
            {
                const float r = std::max(1.0e-4F, s.radius);
                const float i = 0.4F * mass * r * r;
                return { i, i, i };
            }
            else if constexpr (std::is_same_v<T, BoxShape3D>)
            {
                const float wx = 2.0F * std::max(1.0e-4F, s.half_extents.x);
                const float wy = 2.0F * std::max(1.0e-4F, s.half_extents.y);
                const float wz = 2.0F * std::max(1.0e-4F, s.half_extents.z);
                const float c = mass / 12.0F;
                return {
                    c * (wy * wy + wz * wz),
                    c * (wx * wx + wz * wz),
                    c * (wx * wx + wy * wy)
                };
            }
            else if constexpr (std::is_same_v<T, CapsuleShape3D>)
            {
                const float r = std::max(1.0e-4F, s.radius);
                const float h = 2.0F * std::max(0.0F, s.half_height);
                constexpr float kPi = 3.14159265358979323846F;
                const float v_cyl = kPi * r * r * h;
                const float v_sph = (4.0F / 3.0F) * kPi * r * r * r;
                const float v_tot = std::max(1.0e-6F, v_cyl + v_sph);
                const float m_cyl = mass * (v_cyl / v_tot);
                const float m_sph = mass - m_cyl;

                const float i_yy = 0.5F * m_cyl * r * r + 0.4F * m_sph * r * r;
                const float i_xx_zz =
                    m_cyl * ((1.0F / 12.0F) * h * h + 0.25F * r * r) +
                    m_sph * (0.4F * r * r + 0.25F * h * h + 0.375F * h * r);
                return { i_xx_zz, i_yy, i_xx_zz };
            }
            else if constexpr (std::is_same_v<T, CylinderShape3D>)
            {
                const float r = std::max(1.0e-4F, s.radius);
                const float h = 2.0F * std::max(1.0e-4F, s.half_height);
                const float i_xz = (mass / 12.0F) * (3.0F * r * r + h * h);
                const float i_y = 0.5F * mass * r * r;
                return { i_xz, i_y, i_xz };
            }
            else if constexpr (std::is_same_v<T, ConvexHullShape3D>)
            {
                if (s.vertices.size() < 2u)
                {
                    const float i = mass / 6.0F;
                    return { i, i, i };
                }
                cd::math::Vec3f bmin = s.vertices[0];
                cd::math::Vec3f bmax = bmin;
                for (std::size_t k = 1u; k < s.vertices.size(); ++k)
                {
                    bmin = cd::math::min(bmin, s.vertices[k]);
                    bmax = cd::math::max(bmax, s.vertices[k]);
                }
                const cd::math::Vec3f d = bmax - bmin;
                const float wx = std::max(1.0e-3F, d.x);
                const float wy = std::max(1.0e-3F, d.y);
                const float wz = std::max(1.0e-3F, d.z);
                const float c = mass / 12.0F;
                return {
                    c * (wy * wy + wz * wz),
                    c * (wx * wx + wz * wz),
                    c * (wx * wx + wy * wy)
                };
            }
            else if constexpr (std::is_same_v<T, HeightfieldShape3D>)
            {
                const float wx = std::max(1.0F, static_cast<float>(s.cols) * std::abs(s.scale.x));
                const float wy = std::max(1.0F, std::abs(s.scale.y));
                const float wz = std::max(1.0F, static_cast<float>(s.rows) * std::abs(s.scale.z));
                const float c = mass / 12.0F;
                return {
                    c * (wy * wy + wz * wz),
                    c * (wx * wx + wz * wz),
                    c * (wx * wx + wy * wy)
                };
            }
        },
        shape);
}

}  // namespace cd::physics::v3d

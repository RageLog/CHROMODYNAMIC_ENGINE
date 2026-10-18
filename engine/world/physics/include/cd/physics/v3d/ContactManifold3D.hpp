// =============================================================================
// CHROMODYNAMIC — cd/physics/v3d/ContactManifold3D.hpp
// Jolt / Rubikon / PhysX 5 grade 3D contact manifold with 4-point contact
// reduction, feature-key warm-starting, and Erin Catto orthonormal tangent basis.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cd::physics::v3d
{

struct ContactPoint3D
{
    cd::math::Vec3f world_point {};
    cd::math::Vec3f local_anchor_a {};
    cd::math::Vec3f local_anchor_b {};
    float penetration { 0.0F };
    float normal_impulse { 0.0F };
    float tangent_impulse_1 { 0.0F };
    float tangent_impulse_2 { 0.0F };
    std::uint32_t feature_key { 0u };
};

struct ContactManifold3D
{
    std::uint32_t body_a { 0u };
    std::uint32_t body_b { 0u };
    cd::math::Vec3f normal { 0.0F, 1.0F, 0.0F };
    cd::math::Vec3f tangent_1 { 1.0F, 0.0F, 0.0F };
    cd::math::Vec3f tangent_2 { 0.0F, 0.0F, 1.0F };
    std::array<ContactPoint3D, 4> points {};
    std::uint8_t point_count { 0u };
    float combined_friction { 0.5F };
    float combined_restitution { 0.2F };

    void compute_tangent_basis() noexcept
    {
        const float n_len = cd::math::length(normal);
        if (n_len > 1.0e-6F)
        {
            normal = normal / n_len;
        }
        else
        {
            normal = { 0.0F, 1.0F, 0.0F };
        }

        // Erin Catto / Box2D branchless-friendly orthonormal tangent basis.
        if (std::abs(normal.x) >= 0.57735027F)
        {
            tangent_1 = cd::math::normalize(cd::math::Vec3f { normal.y, -normal.x, 0.0F });
        }
        else
        {
            tangent_1 = cd::math::normalize(cd::math::Vec3f { 0.0F, normal.z, -normal.y });
        }
        tangent_2 = cd::math::cross(normal, tangent_1);
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return point_count == 0u;
    }

    constexpr void clear() noexcept
    {
        point_count = 0u;
    }

    void add_point(const ContactPoint3D& pt) noexcept
    {
        // Merge nearby points within 1cm squared threshold.
        constexpr float kMergeThresholdSq = 1.0e-4F;
        for (std::uint8_t i = 0u; i < point_count; ++i)
        {
            if (cd::math::length_squared(points[i].world_point - pt.world_point) < kMergeThresholdSq)
            {
                if (pt.penetration > points[i].penetration)
                {
                    const float ni = points[i].normal_impulse;
                    const float t1 = points[i].tangent_impulse_1;
                    const float t2 = points[i].tangent_impulse_2;
                    points[i] = pt;
                    points[i].normal_impulse = ni;
                    points[i].tangent_impulse_1 = t1;
                    points[i].tangent_impulse_2 = t2;
                }
                return;
            }
        }

        if (point_count < 4u)
        {
            points[point_count++] = pt;
            return;
        }

        // Manifold is full (4 points): keep the deepest point and replace the point
        // that maximizes the remaining support polygon area.
        std::uint8_t deepest_idx = 0u;
        float max_pen = pt.penetration;
        for (std::uint8_t i = 0u; i < 4u; ++i)
        {
            if (points[i].penetration > max_pen)
            {
                max_pen = points[i].penetration;
                deepest_idx = static_cast<std::uint8_t>(i + 1u);
            }
        }

        float best_area_sq = -1.0F;
        std::uint8_t replace_idx = 0u;
        for (std::uint8_t i = 0u; i < 4u; ++i)
        {
            if (deepest_idx == static_cast<std::uint8_t>(i + 1u))
            {
                continue;
            }
            std::array<cd::math::Vec3f, 4> quad {
                points[0].world_point,
                points[1].world_point,
                points[2].world_point,
                points[3].world_point
            };
            quad[i] = pt.world_point;
            const cd::math::Vec3f diag0 = quad[2] - quad[0];
            const cd::math::Vec3f diag1 = quad[3] - quad[1];
            const float area_sq = cd::math::length_squared(cd::math::cross(diag0, diag1));
            if (area_sq > best_area_sq)
            {
                best_area_sq = area_sq;
                replace_idx = i;
            }
        }
        points[replace_idx] = pt;
    }
};

}  // namespace cd::physics::v3d

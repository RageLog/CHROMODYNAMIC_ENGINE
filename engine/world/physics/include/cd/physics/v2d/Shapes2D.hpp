// =============================================================================
// CHROMODYNAMIC — cd/physics/v2d/Shapes2D.hpp
// Box2D v3 / Godot 4 grade 2D collision shape primitives, AABB computation,
// and analytical mass + rotational inertia (I_z) properties.
// =============================================================================
#pragma once

#include <cd/math/Constants.hpp>
#include <cd/math/Vec2Ops.hpp>
#include <cd/math/Vector.hpp>

#include <cd/physics/core/PhysicsTypes.hpp>
#include <cd/physics/core/SpatialAabb2D.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <variant>

namespace cd::physics::v2d
{

namespace detail
{

template <class Xf>
[[nodiscard]] inline float get_rotation_rad(const Xf& xf) noexcept
{
    if constexpr (requires { static_cast<float>(xf.rotation); })
    {
        return static_cast<float>(xf.rotation);
    }
    else if constexpr (requires { static_cast<float>(xf.angle); })
    {
        return static_cast<float>(xf.angle);
    }
    else if constexpr (requires { static_cast<float>(xf.rotation_rad); })
    {
        return static_cast<float>(xf.rotation_rad);
    }
    else
    {
        return 0.0F;
    }
}

template <class Xf>
inline void set_rotation_rad(Xf& xf, float rad) noexcept
{
    if constexpr (requires { xf.rotation = rad; })
    {
        xf.rotation = rad;
    }
    else if constexpr (requires { xf.angle = rad; })
    {
        xf.angle = rad;
    }
    else if constexpr (requires { xf.rotation_rad = rad; })
    {
        xf.rotation_rad = rad;
    }
}

template <class Xf>
[[nodiscard]] inline cd::math::Vec2f rotate_2d(const Xf& xf, cd::math::Vec2f v) noexcept
{
    const float r = get_rotation_rad(xf);
    const float c = std::cos(r);
    const float s = std::sin(r);
    return { c * v.x - s * v.y, s * v.x + c * v.y };
}

template <class Xf>
[[nodiscard]] inline cd::math::Vec2f inv_rotate_2d(const Xf& xf, cd::math::Vec2f v) noexcept
{
    const float r = get_rotation_rad(xf);
    const float c = std::cos(r);
    const float s = std::sin(r);
    return { c * v.x + s * v.y, -s * v.x + c * v.y };
}

template <class Xf>
[[nodiscard]] inline cd::math::Vec2f transform_point_2d(const Xf& xf, cd::math::Vec2f local_pt) noexcept
{
    const cd::math::Vec2f rot = rotate_2d(xf, local_pt);
    return { xf.position.x + rot.x, xf.position.y + rot.y };
}

template <class Xf>
[[nodiscard]] inline cd::math::Vec2f inv_transform_point_2d(const Xf& xf, cd::math::Vec2f world_pt) noexcept
{
    return inv_rotate_2d(xf, { world_pt.x - xf.position.x, world_pt.y - xf.position.y });
}

[[nodiscard]] constexpr float cross_2d(cd::math::Vec2f a, cd::math::Vec2f b) noexcept
{
    return a.x * b.y - a.y * b.x;
}

[[nodiscard]] constexpr cd::math::Vec2f cross_sv_2d(float s, cd::math::Vec2f v) noexcept
{
    return { -s * v.y, s * v.x };
}

[[nodiscard]] constexpr cd::math::Vec2f cross_vs_2d(cd::math::Vec2f v, float s) noexcept
{
    return { s * v.y, -s * v.x };
}

}  // namespace detail

struct CircleShape2D
{
    float radius { 0.5F };
};

struct BoxShape2D
{
    cd::math::Vec2f half_extents { 0.5F, 0.5F };
};

/// Capsule oriented along the local Y axis: spine endpoints at (0, -half_height) and (0, +half_height).
struct CapsuleShape2D
{
    float half_height { 0.5F };
    float radius { 0.25F };
};

struct SegmentShape2D
{
    cd::math::Vec2f point_a { -0.5F, 0.0F };
    cd::math::Vec2f point_b { 0.5F, 0.0F };
    bool one_sided { false };
};

struct ConvexPolygonShape2D
{
    std::array<cd::math::Vec2f, 8> vertices {};
    std::uint8_t vertex_count { 0u };
};

using ShapeVariant2D = std::variant<
    CircleShape2D,
    BoxShape2D,
    CapsuleShape2D,
    SegmentShape2D,
    ConvexPolygonShape2D>;

struct MassProperties2D
{
    float mass { 0.0F };
    float inv_mass { 0.0F };
    float inertia { 0.0F };
    float inv_inertia { 0.0F };
    cd::math::Vec2f center_of_mass { 0.0F, 0.0F };
};

[[nodiscard]] inline core::SpatialAabb2D compute_aabb_2d(
    const ShapeVariant2D& shape,
    const core::Transform2D& xf) noexcept
{
    return std::visit(
        [&xf](const auto& s) noexcept -> core::SpatialAabb2D
        {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, CircleShape2D>)
            {
                const float r = std::max(0.0F, s.radius);
                return core::SpatialAabb2D {
                    .min = { xf.position.x - r, xf.position.y - r },
                    .max = { xf.position.x + r, xf.position.y + r }
                };
            }
            else if constexpr (std::is_same_v<T, BoxShape2D>)
            {
                const float rot  = detail::get_rotation_rad(xf);
                const float ac   = std::abs(std::cos(rot));
                const float as   = std::abs(std::sin(rot));
                const float hx   = std::max(0.0F, s.half_extents.x);
                const float hy   = std::max(0.0F, s.half_extents.y);
                const float ex   = ac * hx + as * hy;
                const float ey   = as * hx + ac * hy;
                return core::SpatialAabb2D {
                    .min = { xf.position.x - ex, xf.position.y - ey },
                    .max = { xf.position.x + ex, xf.position.y + ey }
                };
            }
            else if constexpr (std::is_same_v<T, CapsuleShape2D>)
            {
                const float hh = std::max(0.0F, s.half_height);
                const float r  = std::max(0.0F, s.radius);
                const cd::math::Vec2f p0 = detail::transform_point_2d(xf, { 0.0F, -hh });
                const cd::math::Vec2f p1 = detail::transform_point_2d(xf, { 0.0F, +hh });
                return core::SpatialAabb2D {
                    .min = { std::min(p0.x, p1.x) - r, std::min(p0.y, p1.y) - r },
                    .max = { std::max(p0.x, p1.x) + r, std::max(p0.y, p1.y) + r }
                };
            }
            else if constexpr (std::is_same_v<T, SegmentShape2D>)
            {
                const cd::math::Vec2f p0 = detail::transform_point_2d(xf, s.point_a);
                const cd::math::Vec2f p1 = detail::transform_point_2d(xf, s.point_b);
                return core::SpatialAabb2D {
                    .min = { std::min(p0.x, p1.x), std::min(p0.y, p1.y) },
                    .max = { std::max(p0.x, p1.x), std::max(p0.y, p1.y) }
                };
            }
            else if constexpr (std::is_same_v<T, ConvexPolygonShape2D>)
            {
                const std::uint8_t count = std::min<std::uint8_t>(s.vertex_count, 8u);
                if (count == 0u)
                {
                    return core::SpatialAabb2D { .min = xf.position, .max = xf.position };
                }
                const cd::math::Vec2f first = detail::transform_point_2d(xf, s.vertices.at(0));
                cd::math::Vec2f bmin = first;
                cd::math::Vec2f bmax = first;
                for (std::uint8_t i = 1u; i < count; ++i)
                {
                    const cd::math::Vec2f p = detail::transform_point_2d(xf, s.vertices.at(i));
                    bmin.x = std::min(bmin.x, p.x);
                    bmin.y = std::min(bmin.y, p.y);
                    bmax.x = std::max(bmax.x, p.x);
                    bmax.y = std::max(bmax.y, p.y);
                }
                return core::SpatialAabb2D { .min = bmin, .max = bmax };
            }
        },
        shape);
}

[[nodiscard]] inline MassProperties2D compute_mass_properties_2d(
    const ShapeVariant2D& shape,
    float density) noexcept
{
    if (density <= 0.0F)
    {
        return MassProperties2D {};
    }

    return std::visit(
        [density](const auto& s) noexcept -> MassProperties2D
        {
            using T = std::decay_t<decltype(s)>;
            MassProperties2D out {};
            if constexpr (std::is_same_v<T, CircleShape2D>)
            {
                const float r    = std::max(0.0F, s.radius);
                const float area = cd::math::pi * r * r;
                out.mass         = density * area;
                out.inertia      = 0.5F * out.mass * r * r;
            }
            else if constexpr (std::is_same_v<T, BoxShape2D>)
            {
                const float hx   = std::max(0.0F, s.half_extents.x);
                const float hy   = std::max(0.0F, s.half_extents.y);
                const float w    = 2.0F * hx;
                const float h    = 2.0F * hy;
                const float area = w * h;
                out.mass         = density * area;
                out.inertia      = (out.mass / 12.0F) * (w * w + h * h);
            }
            else if constexpr (std::is_same_v<T, CapsuleShape2D>)
            {
                const float hh     = std::max(0.0F, s.half_height);
                const float r      = std::max(0.0F, s.radius);
                const float w_rect = 2.0F * r;
                const float h_rect = 2.0F * hh;
                const float m_rect = density * (w_rect * h_rect);
                const float m_circ = density * (cd::math::pi * r * r);
                out.mass           = m_rect + m_circ;
                const float i_rect = (m_rect / 12.0F) * (w_rect * w_rect + h_rect * h_rect);
                const float i_circ = m_circ * (0.5F * r * r + hh * hh + (8.0F * hh * r) / (3.0F * cd::math::pi));
                out.inertia        = i_rect + i_circ;
            }
            else if constexpr (std::is_same_v<T, SegmentShape2D>)
            {
                out.mass    = 0.0F;
                out.inertia = 0.0F;
            }
            else if constexpr (std::is_same_v<T, ConvexPolygonShape2D>)
            {
                const std::uint8_t count = std::min<std::uint8_t>(s.vertex_count, 8u);
                if (count >= 3u)
                {
                    float area2      = 0.0F;
                    float inertia_acc = 0.0F;
                    cd::math::Vec2f centroid { 0.0F, 0.0F };
                    for (std::uint8_t i = 0u; i < count; ++i)
                    {
                        const cd::math::Vec2f p0 = s.vertices.at(i);
                        const cd::math::Vec2f p1 = s.vertices.at(static_cast<std::size_t>((i + 1u) % count));
                        const float d            = detail::cross_2d(p0, p1);
                        area2 += d;
                        centroid.x += d * (p0.x + p1.x);
                        centroid.y += d * (p0.y + p1.y);
                        const float ix2 = p0.x * p0.x + p0.x * p1.x + p1.x * p1.x;
                        const float iy2 = p0.y * p0.y + p0.y * p1.y + p1.y * p1.y;
                        inertia_acc += d * (ix2 + iy2);
                    }
                    const float area = 0.5F * std::abs(area2);
                    if (area > 1.0e-6F)
                    {
                        const float inv_3area2 = 1.0F / (3.0F * area2);
                        out.center_of_mass     = { centroid.x * inv_3area2, centroid.y * inv_3area2 };
                        out.mass               = density * area;
                        out.inertia            = (density / 12.0F) * std::abs(inertia_acc);
                    }
                }
            }

            out.inv_mass    = (out.mass > 1.0e-7F) ? (1.0F / out.mass) : 0.0F;
            out.inv_inertia = (out.inertia > 1.0e-7F) ? (1.0F / out.inertia) : 0.0F;
            return out;
        },
        shape);
}

}  // namespace cd::physics::v2d

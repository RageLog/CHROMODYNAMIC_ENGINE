// =============================================================================
// CHROMODYNAMIC — cd/physics/v2d/ContactManifold2D.hpp
// Box2D v3 / Godot 4 grade 2D contact manifold & persistent contact point
// cache structures for warm-started Soft-Step TGS impulse solving.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <array>
#include <cstdint>

namespace cd::physics::v2d
{

struct ContactPoint2D
{
    cd::math::Vec2f world_point {};
    cd::math::Vec2f local_anchor_a {};
    cd::math::Vec2f local_anchor_b {};
    float penetration { 0.0F };
    float normal_impulse { 0.0F };
    float tangent_impulse { 0.0F };
    std::uint32_t feature_key { 0u };
};

struct ContactManifold2D
{
    std::uint32_t body_a { 0u };
    std::uint32_t body_b { 0u };
    cd::math::Vec2f normal { 0.0F, 1.0F };
    std::array<ContactPoint2D, 2> points {};
    std::uint8_t point_count { 0u };
    float combined_friction { 0.5F };
    float combined_restitution { 0.2F };

    [[nodiscard]] constexpr bool has_contacts() const noexcept
    {
        return point_count > 0u;
    }

    constexpr void add_point(const ContactPoint2D& cp) noexcept
    {
        if (point_count < 2u)
        {
            points.at(point_count) = cp;
            ++point_count;
        }
    }
};

}  // namespace cd::physics::v2d

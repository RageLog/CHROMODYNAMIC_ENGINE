// =============================================================================
// CHROMODYNAMIC — cd/physics/core/PhysicsTypes.hpp
// Shared 2D/3D foundational physics types, material combine rules, transforms,
// and sleep configuration.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cd::physics::core
{

enum class BodyMotionType : std::uint8_t
{
    kStatic    = 0,
    kKinematic = 1,
    kDynamic   = 2,
};

enum class CombineMode : std::uint8_t
{
    kAverage  = 0,
    kMinimum  = 1,
    kMultiply = 2,
    kMaximum  = 3,
};

struct PhysicsMaterial
{
    float       friction { 0.5F };
    float       restitution { 0.2F };
    float       density { 1000.0F };
    CombineMode friction_combine { CombineMode::kAverage };
    CombineMode restitution_combine { CombineMode::kMaximum };

    friend constexpr bool operator==(const PhysicsMaterial&, const PhysicsMaterial&) noexcept = default;
};

[[nodiscard]] constexpr float combine_material_scalar(float a, float b, CombineMode mode) noexcept
{
    switch (mode)
    {
        case CombineMode::kAverage:
            return 0.5F * (a + b);
        case CombineMode::kMinimum:
            return a < b ? a : b;
        case CombineMode::kMultiply:
            return a * b;
        case CombineMode::kMaximum:
            return a > b ? a : b;
    }
    return 0.5F * (a + b);
}

[[nodiscard]] constexpr CombineMode select_combine_mode(CombineMode a, CombineMode b) noexcept
{
    return static_cast<std::uint8_t>(a) >= static_cast<std::uint8_t>(b) ? a : b;
}

[[nodiscard]] constexpr float combine_friction(const PhysicsMaterial& a, const PhysicsMaterial& b) noexcept
{
    return combine_material_scalar(a.friction, b.friction, select_combine_mode(a.friction_combine, b.friction_combine));
}

[[nodiscard]] constexpr float combine_restitution(const PhysicsMaterial& a, const PhysicsMaterial& b) noexcept
{
    return combine_material_scalar(
        a.restitution, b.restitution, select_combine_mode(a.restitution_combine, b.restitution_combine));
}

struct Transform2D
{
    cd::math::Vec2f position { 0.0F, 0.0F };
    float           rotation_rad { 0.0F };

    [[nodiscard]] static constexpr Transform2D identity() noexcept
    {
        return Transform2D {};
    }

    [[nodiscard]] cd::math::Vec2f rotate_vector(cd::math::Vec2f v) const noexcept
    {
        const float c = std::cos(rotation_rad);
        const float s = std::sin(rotation_rad);
        return cd::math::Vec2f { c * v.x - s * v.y, s * v.x + c * v.y };
    }

    [[nodiscard]] cd::math::Vec2f inverse_rotate_vector(cd::math::Vec2f v) const noexcept
    {
        const float c = std::cos(rotation_rad);
        const float s = std::sin(rotation_rad);
        return cd::math::Vec2f { c * v.x + s * v.y, -s * v.x + c * v.y };
    }

    [[nodiscard]] cd::math::Vec2f transform_point(cd::math::Vec2f local) const noexcept
    {
        return rotate_vector(local) + position;
    }

    [[nodiscard]] cd::math::Vec2f inverse_transform_point(cd::math::Vec2f world) const noexcept
    {
        return inverse_rotate_vector(world - position);
    }

    friend constexpr bool operator==(const Transform2D&, const Transform2D&) noexcept = default;
};

struct Transform3D
{
    cd::math::Vec3f position { 0.0F, 0.0F, 0.0F };
    cd::math::Quatf rotation { cd::math::Quatf::identity() };

    [[nodiscard]] static constexpr Transform3D identity() noexcept
    {
        return Transform3D {};
    }

    [[nodiscard]] constexpr cd::math::Vec3f rotate_vector(cd::math::Vec3f v) const noexcept
    {
        return cd::math::rotate(rotation, v);
    }

    [[nodiscard]] constexpr cd::math::Vec3f inverse_rotate_vector(cd::math::Vec3f v) const noexcept
    {
        return cd::math::rotate(cd::math::conjugate(rotation), v);
    }

    [[nodiscard]] constexpr cd::math::Vec3f transform_point(cd::math::Vec3f local) const noexcept
    {
        return rotate_vector(local) + position;
    }

    [[nodiscard]] constexpr cd::math::Vec3f inverse_transform_point(cd::math::Vec3f world) const noexcept
    {
        return inverse_rotate_vector(world - position);
    }

    friend constexpr bool operator==(const Transform3D&, const Transform3D&) noexcept = default;
};

struct SleepSettings
{
    bool  allow_sleep { true };
    float linear_sleep_threshold { 0.05F };
    float angular_sleep_threshold { 0.05F };
    float time_to_sleep_sec { 0.5F };

    friend constexpr bool operator==(const SleepSettings&, const SleepSettings&) noexcept = default;
};

}  // namespace cd::physics::core

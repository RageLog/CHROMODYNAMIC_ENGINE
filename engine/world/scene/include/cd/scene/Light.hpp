// =============================================================================
// CHROMODYNAMIC — cd/scene/Light.hpp
// Phase 83.A / Wave 251 — point / directional / spot light struct.
//
// `Light` is the artist-facing tagged-union for the three classic
// shading light types. Renderer cluster/tile light pass iterates a
// Light array and dispatches by `kind`.
//
//   kPoint        : color * intensity / dist²  attenuated
//   kDirectional  : color * intensity, constant across the scene
//   kSpot         : point + cone falloff between inner / outer angles
//
// All angles in radians.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <cstdint>

namespace cd::scene
{

enum class LightKind : std::uint8_t
{
    kPoint       = 0,
    kDirectional = 1,
    kSpot        = 2,
};

struct Light
{
    LightKind        kind { LightKind::kPoint };
    cd::math::Vec3f  position    {};            // world-space (kPoint/kSpot)
    cd::math::Vec3f  direction   { 0, -1, 0 };  // unit, kDirectional/kSpot
    cd::math::Vec3f  color       { 1, 1, 1 };
    float            intensity   { 1.0F };
    float            range       { 10.0F };      // kPoint/kSpot
    float            inner_cone  { 0.523F };     // ~30° kSpot
    float            outer_cone  { 0.785F };     // ~45° kSpot
};

[[nodiscard]] inline Light point_light(cd::math::Vec3f pos,
                                       cd::math::Vec3f color,
                                       float intensity = 1.0F,
                                       float range = 10.0F) noexcept
{
    Light l;
    l.kind = LightKind::kPoint;
    l.position = pos;
    l.color = color;
    l.intensity = intensity;
    l.range = range;
    return l;
}

[[nodiscard]] inline Light directional_light(cd::math::Vec3f dir,
                                             cd::math::Vec3f color,
                                             float intensity = 1.0F) noexcept
{
    Light l;
    l.kind = LightKind::kDirectional;
    l.direction = dir;
    l.color = color;
    l.intensity = intensity;
    return l;
}

}  // namespace cd::scene

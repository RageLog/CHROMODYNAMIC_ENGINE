// =============================================================================
// CHROMODYNAMIC — cd/light/Light.hpp
// Phase 165 / v0.99.87 — state-of-the-art light primitives.
//
// Replaces the minimal cd::scene::Light (Phase 83.A) with the
// production engine model used by Frostbite (Real Shading 2014),
// Filament 1.x, and . Key differences:
//
//   * Physical units. Point/spot intensity in LUMENS, directional in
//     LUX (illuminance). Conversion → radiant energy at shade time
//     follows the Frostbite "Moving Frostbite to PBR" white paper.
//   * Color temperature. Lights can be specified by correlated color
//     temperature (Kelvin) in addition to a per-channel multiplier;
//     Krystek's 1985 approximation converts CCT → CIE chromaticity →
//     linear RGB.
//   * Frostbite range attenuation:
//
//         f(d) = saturate(1 - (d/range)^4)^2 / (d^2 + epsilon)
//
//     — physically motivated inverse-square plus a smooth range
//     cutoff so the light goes to exactly 0 at `range` without
//     popping. Same falloff as Filament + production engines + .
//   * Spot light angle terms pre-computed (cos_inner / cos_outer +
//     reciprocal) so the shader only needs two multiply-adds.
//   * Area lights. Rectangular (LTC — Heitz 2016) and disk.
//   * Shadow-map slot. -1 = no shadow; otherwise an index into the
//     renderer's shadow atlas / cascade table.
//   * IES profile slot. AssetId reference to a parsed photometric
//     web; 0 = no profile (use cone falloff).
//
// All data is plain-old-data — pluggable into GPU UBO/SSBO with
// std140 packing. The renderer's clustered light pass iterates
// `vector<Light>` and dispatches per `type`.
//
// Backward compat: `cd::scene::Light` (Phase 83.A) is preserved
// as a thin shim that constructs the equivalent `cd::light::Light`.
// =============================================================================
#pragma once

#include <cd/asset/AssetId.hpp>
#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace cd::light
{

enum class LightType : std::uint8_t
{
    kDirectional = 0,  ///< Distant sun-like; uses `direction` + lux
    kPoint       = 1,  ///< Omnidirectional point; lumens + range
    kSpot        = 2,  ///< Cone with inner/outer angle; lumens + range
    kRectArea    = 3,  ///< Filament/Heitz LTC; rectangle in world space
    kDiskArea    = 4,  ///< Disk area light; for fluorescent tubes / discs
};

/// Compact GPU-friendly light record. 96 bytes; aligned for std140
/// (round up to multiples of 16). Field order chosen so vec4 packing
/// holds without padding gaps.
struct Light
{
    // ---- Pose --------------------------------------------------------
    cd::math::Vec3f position  { 0.0F, 0.0F, 0.0F };
    float           range     { 10.0F };  // 0 for directional/area

    cd::math::Vec3f direction { 0.0F, -1.0F, 0.0F };
    float           intensity { 1.0F };   // lumens (point/spot/area), lux (directional)

    cd::math::Vec3f color     { 1.0F, 1.0F, 1.0F };  // linear RGB multiplier
    float           color_kelvin { 6500.0F };        // 0 = use color verbatim; else CCT

    // ---- Cone (kSpot only) -------------------------------------------
    float           cos_inner_cone { 0.866F };  // cos(30°)
    float           cos_outer_cone { 0.707F };  // cos(45°)
    float           inv_cone_range { 4.0F };    // 1 / (cos_inner - cos_outer)
    float           _spot_pad      { 0.0F };

    // ---- Area light (kRectArea/kDiskArea) ----------------------------
    cd::math::Vec3f area_tangent { 1.0F, 0.0F, 0.0F };  // first edge / disk +X
    float           area_width   { 1.0F };

    cd::math::Vec3f area_bitangent { 0.0F, 0.0F, 1.0F };  // second edge / disk +Z
    float           area_height  { 1.0F };  // disk radius for kDiskArea

    // ---- Slots -------------------------------------------------------
    std::int32_t    shadow_slot   { -1 };  // -1 = no shadow
    std::uint32_t   ies_profile   { 0 };   // AssetId.value() — 0 = no profile
    LightType       type          { LightType::kPoint };
    std::uint8_t    flags         { 0 };
    std::uint16_t   _slot_pad     { 0 };
    std::uint32_t   _tail_pad     { 0 };   // round to multiple of 16 (std140)

    // ---- Flags -------------------------------------------------------
    static constexpr std::uint8_t kFlagCastsShadow      = 1u << 0;
    static constexpr std::uint8_t kFlagAffectsDiffuse   = 1u << 1;
    static constexpr std::uint8_t kFlagAffectsSpecular  = 1u << 2;
    static constexpr std::uint8_t kFlagAffectsVolume    = 1u << 3;
    static constexpr std::uint8_t kFlagDefault =
        kFlagAffectsDiffuse | kFlagAffectsSpecular;
};

static_assert(sizeof(Light) == 112, "Light packed layout drifted; expected 112 bytes (7x16-byte vec4s)");

// ---- Construction helpers --------------------------------------------------

[[nodiscard]] inline Light directional(cd::math::Vec3f dir,
                                       cd::math::Vec3f color = { 1, 1, 1 },
                                       float lux = 100000.0F) noexcept
{
    Light l;
    l.type = LightType::kDirectional;
    // Normalize direction; renderer expects unit.
    const float len = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
    if (len > 1e-6F) { dir.x/=len; dir.y/=len; dir.z/=len; }
    l.direction = dir;
    l.color = color;
    l.intensity = lux;
    l.range = 0.0F;
    l.flags = Light::kFlagDefault;
    return l;
}

[[nodiscard]] inline Light point(cd::math::Vec3f pos,
                                 cd::math::Vec3f color = { 1, 1, 1 },
                                 float lumens = 800.0F,
                                 float range = 10.0F) noexcept
{
    Light l;
    l.type = LightType::kPoint;
    l.position = pos;
    l.color = color;
    l.intensity = lumens;
    l.range = range;
    l.flags = Light::kFlagDefault;
    return l;
}

[[nodiscard]] inline Light spot(cd::math::Vec3f pos,
                                cd::math::Vec3f dir,
                                cd::math::Vec3f color = { 1, 1, 1 },
                                float lumens = 800.0F,
                                float range = 10.0F,
                                float inner_angle_rad = 0.523F,   // ~30°
                                float outer_angle_rad = 0.785F) noexcept  // ~45°
{
    Light l;
    l.type = LightType::kSpot;
    l.position = pos;
    const float len = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
    if (len > 1e-6F) { dir.x/=len; dir.y/=len; dir.z/=len; }
    l.direction = dir;
    l.color = color;
    l.intensity = lumens;
    l.range = range;
    l.cos_inner_cone = std::cos(inner_angle_rad);
    l.cos_outer_cone = std::cos(outer_angle_rad);
    const float denom = l.cos_inner_cone - l.cos_outer_cone;
    l.inv_cone_range = denom > 1e-5F ? 1.0F / denom : 0.0F;
    l.flags = Light::kFlagDefault;
    return l;
}

[[nodiscard]] inline Light rect_area(cd::math::Vec3f pos,
                                     cd::math::Vec3f normal,
                                     cd::math::Vec3f tangent,
                                     float width, float height,
                                     cd::math::Vec3f color = { 1, 1, 1 },
                                     float lumens = 800.0F) noexcept
{
    Light l;
    l.type = LightType::kRectArea;
    l.position = pos;
    // Normalize normal + tangent; bitangent = normal × tangent.
    auto norm = [](cd::math::Vec3f v) {
        const float len = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
        return (len > 1e-6F) ? cd::math::Vec3f { v.x/len, v.y/len, v.z/len } : v;
    };
    normal  = norm(normal);
    tangent = norm(tangent);
    cd::math::Vec3f bitangent {
        normal.y*tangent.z - normal.z*tangent.y,
        normal.z*tangent.x - normal.x*tangent.z,
        normal.x*tangent.y - normal.y*tangent.x };
    bitangent = norm(bitangent);
    l.direction = normal;
    l.area_tangent = tangent;
    l.area_bitangent = bitangent;
    l.area_width  = width;
    l.area_height = height;
    l.color = color;
    l.intensity = lumens;
    l.range = 20.0F;  // area lights cull at ~20 m by default
    l.flags = Light::kFlagDefault;
    return l;
}

}  // namespace cd::light

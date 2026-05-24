// =============================================================================
// CHROMODYNAMIC — cd/scene/LightProbe.hpp
// Phase 75.B / Wave 243 — first-order SH9 ambient light probe.
//
// First-order spherical harmonic ambient probe — 9 RGB coefficients
// describing low-frequency lighting around a point in space:
//
//   irradiance(n) ≈ Σ_l Σ_m c_{l,m} · Y_{l,m}(n)   for l ∈ {0, 1, 2}
//
// 9 SH9 coefficients × 3 channels = 27 floats per probe. Stored as
// `Vec3f coefficients[9]` so shaders can vec3-dot per channel.
//
// Bake-side: caller integrates incoming radiance from a cubemap and
// fills the coefficients. Sample-side: GPU shader interpolates
// between nearest probes (probe volume).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

namespace cd::scene
{

struct LightProbe
{
    cd::math::Vec3f position {};                      // world-space center
    cd::math::Vec3f coefficients[9] {};               // SH9 RGB
};

/// Evaluate SH9 irradiance from a probe in direction `n` (unit).
/// Uses Ramamoorthi & Hanrahan diffuse-only constants — for ambient
/// lighting, ignoring specular.
[[nodiscard]] inline cd::math::Vec3f evaluate(const LightProbe& p,
                                              const cd::math::Vec3f& n) noexcept
{
    // SH basis functions Y_0_0, Y_1_-1, Y_1_0, Y_1_1, Y_2_-2, Y_2_-1, Y_2_0, Y_2_1, Y_2_2
    const float Y00 = 0.282095F;
    const float Y1m1 = 0.488603F * n.y;
    const float Y10  = 0.488603F * n.z;
    const float Y11  = 0.488603F * n.x;
    const float Y2m2 = 1.092548F * n.x * n.y;
    const float Y2m1 = 1.092548F * n.y * n.z;
    const float Y20  = 0.315392F * (3.0F * n.z * n.z - 1.0F);
    const float Y21  = 1.092548F * n.x * n.z;
    const float Y22  = 0.546274F * (n.x * n.x - n.y * n.y);
    const float bases[9] = { Y00, Y1m1, Y10, Y11, Y2m2, Y2m1, Y20, Y21, Y22 };

    cd::math::Vec3f out {};
    for (int i = 0; i < 9; ++i)
    {
        out.x += p.coefficients[i].x * bases[i];
        out.y += p.coefficients[i].y * bases[i];
        out.z += p.coefficients[i].z * bases[i];
    }
    return out;
}

}  // namespace cd::scene

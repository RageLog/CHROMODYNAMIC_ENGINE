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
    const float y00 = 0.282095F;
    const float y1m1 = 0.488603F * n.y;
    const float y10  = 0.488603F * n.z;
    const float y11  = 0.488603F * n.x;
    const float y2m2 = 1.092548F * n.x * n.y;
    const float y2m1 = 1.092548F * n.y * n.z;
    const float y20  = 0.315392F * (3.0F * n.z * n.z - 1.0F);
    const float y21  = 1.092548F * n.x * n.z;
    const float y22  = 0.546274F * (n.x * n.x - n.y * n.y);
    const float bases[9] = { y00, y1m1, y10, y11, y2m2, y2m1, y20, y21, y22 };

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

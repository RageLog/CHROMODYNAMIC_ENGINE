// =============================================================================
// CHROMODYNAMIC — cd/ibl/IrradianceConvolution.hpp
// Phase 155-full / v0.99.92 — diffuse irradiance cubemap convolution.
//
// The diffuse irradiance term in the split-sum approximation is:
//
//   E(N) = (1/π) ∫_Ω L_i(ω) (N·ω) dω
//
// One precomputed cubemap holds E for every normal N (one texel per
// face-direction). Runtime PBR shader does a single cubemap lookup
// for the diffuse indirect term — no convolution at draw time.
//
// CPU bake samples a hemisphere of directions around each texel's
// world direction (using uniform spherical coordinates with sin(θ)
// d(θ) d(φ) measure) and accumulates cos-weighted irradiance.
//
// Reference: Filament documentation §8.6.1 + Karis 2013 split-sum.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ibl/Cubemap.hpp>
#include <cd/math/Vector.hpp>

#include <cmath>
#include <cstdint>
#include <numbers>

namespace cd::ibl
{

/// Build a diffuse irradiance cubemap from a fully-lit environment
/// cubemap. Output size is typically 32-64 — the irradiance is very
/// low-frequency.
///
/// `sample_step_degrees`: hemisphere quadrature step. 5° → 1296
/// samples per output texel (default). 2° → 32400 (highest-quality,
/// slow). Filament uses 1° for shipping assets.
[[nodiscard]] inline CubeMapRgbF
convolve_irradiance(const CubeMapRgbF& env, std::uint32_t out_size,
                    float sample_step_degrees = 5.0F)
{
    auto out = CubeMapRgbF::allocate(out_size);
    if (env.face_size == 0) return out;

    const float step = sample_step_degrees * std::numbers::pi_v<float> / 180.0F;
    for (std::uint8_t f = 0; f < kCubeFaceCount; ++f)
    {
        const auto face = static_cast<CubeFace>(f);
        for (std::uint32_t y = 0; y < out_size; ++y)
        {
            for (std::uint32_t x = 0; x < out_size; ++x)
            {
                const float u = (static_cast<float>(x) + 0.5F) / static_cast<float>(out_size);
                const float v = (static_cast<float>(y) + 0.5F) / static_cast<float>(out_size);
                const auto N = cube_uv_to_world_dir(face, u, v);

                // Build an orthonormal frame around N for hemisphere sampling.
                cd::math::Vec3f up { 0.0F, 1.0F, 0.0F };
                if (std::abs(N.y) > 0.99F) up = { 0.0F, 0.0F, 1.0F };
                // right = normalize(cross(up, N))
                cd::math::Vec3f right {
                    up.y*N.z - up.z*N.y,
                    up.z*N.x - up.x*N.z,
                    up.x*N.y - up.y*N.x };
                {
                    const float l = std::sqrt(right.x*right.x + right.y*right.y + right.z*right.z);
                    if (l > 1e-6F) { right.x/=l; right.y/=l; right.z/=l; }
                }
                cd::math::Vec3f up_n {
                    N.y*right.z - N.z*right.y,
                    N.z*right.x - N.x*right.z,
                    N.x*right.y - N.y*right.x };

                cd::math::Vec3f irr { 0, 0, 0 };
                std::uint32_t n_samples = 0;
                // float-counter loops are part of the calibrated IBL bake;
                // switching to integer count would change n_samples per
                // output texel and rebake the IBL. The W8-AW chrome-mirror
                // calibration is fixed against these exact float-counter
                // loops; do not migrate without a rebake pass. See CLAUDE.md
                // marathon rule "DON'T regenerate IBL bake".
                // NOLINTNEXTLINE(cert-flp30-c)
                for (float phi = 0.0F; phi < 2.0F * std::numbers::pi_v<float>; phi += step)
                {
                    const float cos_phi = std::cos(phi);
                    const float sin_phi = std::sin(phi);
                    // NOLINTNEXTLINE(cert-flp30-c)
                    for (float theta = 0.0F; theta < 0.5F * std::numbers::pi_v<float>; theta += step)
                    {
                        const float cos_theta = std::cos(theta);
                        const float sin_theta = std::sin(theta);
                        // tangent-space sample (z=up) → world.
                        cd::math::Vec3f L_tan {
                            sin_theta * cos_phi,
                            sin_theta * sin_phi,
                            cos_theta };
                        cd::math::Vec3f L {
                            L_tan.x * right.x + L_tan.y * up_n.x + L_tan.z * N.x,
                            L_tan.x * right.y + L_tan.y * up_n.y + L_tan.z * N.y,
                            L_tan.x * right.z + L_tan.y * up_n.z + L_tan.z * N.z };
                        const auto Li = sample_cubemap_dir(env, L);
                        // Cosine-weighted (Lambert) + Jacobian (sin θ).
                        const float w = cos_theta * sin_theta;
                        irr.x += Li.x * w;
                        irr.y += Li.y * w;
                        irr.z += Li.z * w;
                        ++n_samples;
                    }
                }
                if (n_samples > 0)
                {
                    const float k = std::numbers::pi_v<float> / static_cast<float>(n_samples);
                    irr.x *= k; irr.y *= k; irr.z *= k;
                }
                out.store(face, x, y, irr);
            }
        }
    }
    return out;
}

}  // namespace cd::ibl

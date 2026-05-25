// =============================================================================
// CHROMODYNAMIC — cd/ibl/PrefilteredSpecular.hpp
// Phase 155-full / v0.99.92 — pre-filtered specular cubemap mip chain.
//
// The second half of the split-sum specular IBL: per-mip cubemap
// where each mip level corresponds to a roughness in [0, 1] and the
// face texels store the GGX-importance-sampled environment radiance
// at that roughness. Runtime fragment shader does:
//
//     float mip = roughness * (num_mips - 1);
//     vec3 prefiltered = textureLod(specular_cube, reflect_dir, mip).rgb;
//     vec3 specular = (F0 * brdf_lut.r + brdf_lut.g) * prefiltered;
//
// Reference: Karis 2013 §4.4, Filament docs §8.6.2.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ibl/Cubemap.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace cd::ibl
{

constexpr std::uint32_t kMaxSpecularMips = 8;

struct PrefilteredSpecularCube
{
    /// mips[0] = roughness 0 (mirror), highest resolution.
    /// mips[N-1] = roughness 1, lowest resolution (typically 4-8 px/face).
    std::array<CubeMapRgbF, kMaxSpecularMips> mips {};
    std::uint32_t mip_count { 0 };
};

namespace detail
{

[[nodiscard]] inline float radical_inverse_vdc_(std::uint32_t bits) noexcept
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u)  | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u)  | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u)  | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u)  | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10F;
}

[[nodiscard]] inline cd::math::Vec3f
importance_sample_ggx_(std::uint32_t i, std::uint32_t N, float roughness,
                       const cd::math::Vec3f& normal) noexcept
{
    const float xi1 = static_cast<float>(i) / static_cast<float>(N);
    const float xi2 = radical_inverse_vdc_(i);
    const float a   = roughness * roughness;
    const float phi = 2.0F * std::numbers::pi_v<float> * xi1;
    const float cos_theta = std::sqrt((1.0F - xi2) / (1.0F + (a*a - 1.0F) * xi2));
    const float sin_theta = std::sqrt(std::max(0.0F, 1.0F - cos_theta * cos_theta));
    // tangent space H
    cd::math::Vec3f H { sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta };
    // build TBN from normal
    cd::math::Vec3f up { 0.0F, 1.0F, 0.0F };
    if (std::abs(normal.y) > 0.99F) up = { 0.0F, 0.0F, 1.0F };
    cd::math::Vec3f T {
        up.y*normal.z - up.z*normal.y,
        up.z*normal.x - up.x*normal.z,
        up.x*normal.y - up.y*normal.x };
    {
        const float l = std::sqrt(T.x*T.x + T.y*T.y + T.z*T.z);
        if (l > 1e-6F) { T.x/=l; T.y/=l; T.z/=l; }
    }
    cd::math::Vec3f B {
        normal.y*T.z - normal.z*T.y,
        normal.z*T.x - normal.x*T.z,
        normal.x*T.y - normal.y*T.x };
    return {
        H.x*T.x + H.y*B.x + H.z*normal.x,
        H.x*T.y + H.y*B.y + H.z*normal.y,
        H.x*T.z + H.y*B.z + H.z*normal.z,
    };
}

}  // namespace detail

/// Build a pre-filtered specular cubemap mip chain. `base_face_size`
/// is the size of mip 0 (typically 128 or 256). Each subsequent
/// mip halves the resolution. `num_mips` ≤ kMaxSpecularMips.
[[nodiscard]] inline PrefilteredSpecularCube
prefilter_specular(const CubeMapRgbF& env, std::uint32_t base_face_size,
                   std::uint32_t num_mips, std::uint32_t samples_per_texel = 64)
{
    PrefilteredSpecularCube out {};
    num_mips = std::min(num_mips, kMaxSpecularMips);
    out.mip_count = num_mips;
    for (std::uint32_t mip = 0; mip < num_mips; ++mip)
    {
        const float roughness = num_mips <= 1 ? 0.0F
            : static_cast<float>(mip) / static_cast<float>(num_mips - 1);
        const std::uint32_t size = std::max(1u, base_face_size >> mip);
        out.mips[mip] = CubeMapRgbF::allocate(size);

        for (std::uint8_t f = 0; f < kCubeFaceCount; ++f)
        {
            const auto face = static_cast<CubeFace>(f);
            for (std::uint32_t y = 0; y < size; ++y)
            {
                for (std::uint32_t x = 0; x < size; ++x)
                {
                    const float u = (static_cast<float>(x) + 0.5F) / static_cast<float>(size);
                    const float v = (static_cast<float>(y) + 0.5F) / static_cast<float>(size);
                    const auto N = cube_uv_to_world_dir(face, u, v);
                    // Karis approximation: V = R = N (good enough for IBL).
                    const auto V = N;

                    cd::math::Vec3f acc { 0, 0, 0 };
                    float total_weight = 0.0F;
                    for (std::uint32_t s = 0; s < samples_per_texel; ++s)
                    {
                        const auto H = detail::importance_sample_ggx_(s, samples_per_texel,
                                                                     roughness, N);
                        const float v_dot_h = V.x*H.x + V.y*H.y + V.z*H.z;
                        // L = reflect(-V, H) = 2(V·H)H - V
                        cd::math::Vec3f L {
                            2.0F * v_dot_h * H.x - V.x,
                            2.0F * v_dot_h * H.y - V.y,
                            2.0F * v_dot_h * H.z - V.z };
                        const float n_dot_l = N.x*L.x + N.y*L.y + N.z*L.z;
                        if (n_dot_l > 0.0F)
                        {
                            const auto Li = sample_cubemap_dir(env, L);
                            acc.x += Li.x * n_dot_l;
                            acc.y += Li.y * n_dot_l;
                            acc.z += Li.z * n_dot_l;
                            total_weight += n_dot_l;
                        }
                    }
                    if (total_weight > 0.0F)
                    {
                        acc.x /= total_weight;
                        acc.y /= total_weight;
                        acc.z /= total_weight;
                    }
                    out.mips[mip].store(face, x, y, acc);
                }
            }
        }
    }
    return out;
}

}  // namespace cd::ibl

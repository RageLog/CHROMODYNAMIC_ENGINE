// =============================================================================
// CHROMODYNAMIC — cd/texture_synth/Earth.hpp
//
// Procedural Earth-like maps (albedo, normal-from-height, metallic-
// roughness-AO) used by hello_engine when no external glTF asset is
// available. All bakes return tightly-packed RGBA8 buffers ready for
// upload via the host's `create_texture_rgba8` (or equivalent).
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>
#include <cd/texture_synth/Noise.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cd::texture_synth
{

/// Bake an Earth-like RGBA8 albedo: ocean blue gradient + warm earth-
/// tone continents + matte snow at the poles. `size` is the side
/// length; output has `size * size * 4` bytes (RGBA, A=255).
[[nodiscard]] inline std::vector<std::uint8_t>
bake_earth_albedo_rgba8(std::uint32_t size)
{
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size) * size * 4);
    for (std::uint32_t py = 0; py < size; ++py)
    {
        const float v   = static_cast<float>(py) / static_cast<float>(size);
        const float lat = (v - 0.5F) * 3.14159265F;
        const float pole_falloff = std::cos(lat);
        for (std::uint32_t px = 0; px < size; ++px)
        {
            const float u = static_cast<float>(px) / static_cast<float>(size);
            float n = fbm2(u, v, 6.0F);
            n = n * pole_falloff + 0.15F * (1.0F - pole_falloff);
            const bool is_land = n > 0.48F;
            cd::math::Vec3f col {};
            if (is_land)
            {
                const float t = std::clamp((n - 0.48F) / 0.52F, 0.0F, 1.0F);
                cd::math::Vec3f low  { 0.30F, 0.55F, 0.18F };
                cd::math::Vec3f mid  { 0.55F, 0.45F, 0.20F };
                cd::math::Vec3f high { 0.90F, 0.88F, 0.82F };
                if (t < 0.5F)
                {
                    const float k = t * 2.0F;
                    col = { low.x + (mid.x - low.x) * k,
                            low.y + (mid.y - low.y) * k,
                            low.z + (mid.z - low.z) * k };
                }
                else
                {
                    const float k = (t - 0.5F) * 2.0F;
                    col = { mid.x + (high.x - mid.x) * k,
                            mid.y + (high.y - mid.y) * k,
                            mid.z + (high.z - mid.z) * k };
                }
            }
            else
            {
                const float ocean_depth = std::clamp((0.48F - n) / 0.48F, 0.0F, 1.0F);
                col = { 0.08F + (0.20F - 0.08F) * (1 - ocean_depth),
                        0.25F + (0.50F - 0.25F) * (1 - ocean_depth),
                        0.50F + (0.78F - 0.50F) * (1 - ocean_depth) };
            }
            if (pole_falloff < 0.18F)
                col = { 0.92F, 0.94F, 0.97F };
            const std::size_t i = (static_cast<std::size_t>(py) * size + px) * 4;
            rgba[i + 0] = static_cast<std::uint8_t>(std::clamp(col.x * 255.0F, 0.0F, 255.0F));
            rgba[i + 1] = static_cast<std::uint8_t>(std::clamp(col.y * 255.0F, 0.0F, 255.0F));
            rgba[i + 2] = static_cast<std::uint8_t>(std::clamp(col.z * 255.0F, 0.0F, 255.0F));
            rgba[i + 3] = 255;
        }
    }
    return rgba;
}

/// Bake a tangent-space normal map derived from the same height field
/// the Earth albedo uses. Result is RGBA8 packed (xyz = (n*0.5+0.5),
/// a = 255).
[[nodiscard]] inline std::vector<std::uint8_t>
bake_earth_normal_rgba8(std::uint32_t size, float strength = 4.0F)
{
    std::vector<std::uint8_t> nrm(static_cast<std::size_t>(size) * size * 4);
    const float eps = 1.0F / static_cast<float>(size);
    for (std::uint32_t py = 0; py < size; ++py)
    {
        const float v = static_cast<float>(py) / static_cast<float>(size);
        for (std::uint32_t px = 0; px < size; ++px)
        {
            const float u  = static_cast<float>(px) / static_cast<float>(size);
            const float hL = fbm2(u - eps, v, 6.0F);
            const float hR = fbm2(u + eps, v, 6.0F);
            const float hU = fbm2(u, v - eps, 6.0F);
            const float hD = fbm2(u, v + eps, 6.0F);
            cd::math::Vec3f n {
                (hL - hR) * strength,
                (hU - hD) * strength,
                1.0F };
            const float l = std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z);
            if (l > 1e-6F) { n.x/=l; n.y/=l; n.z/=l; }
            const std::size_t i = (static_cast<std::size_t>(py) * size + px) * 4;
            nrm[i + 0] = static_cast<std::uint8_t>(std::lround((n.x * 0.5F + 0.5F) * 255.0F));
            nrm[i + 1] = static_cast<std::uint8_t>(std::lround((n.y * 0.5F + 0.5F) * 255.0F));
            nrm[i + 2] = static_cast<std::uint8_t>(std::lround((n.z * 0.5F + 0.5F) * 255.0F));
            nrm[i + 3] = 255;
        }
    }
    return nrm;
}

/// Bake a metallic-roughness-AO map matching glTF 2.0's MR packing:
///   R unused, G = roughness, B = metallic, A = ambient occlusion.
[[nodiscard]] inline std::vector<std::uint8_t>
bake_earth_mr_rgba8(std::uint32_t size)
{
    std::vector<std::uint8_t> mr(static_cast<std::size_t>(size) * size * 4);
    for (std::uint32_t py = 0; py < size; ++py)
    {
        const float v = static_cast<float>(py) / static_cast<float>(size);
        const float lat = (v - 0.5F) * 3.14159265F;
        const float pole_falloff = std::cos(lat);
        for (std::uint32_t px = 0; px < size; ++px)
        {
            const float u = static_cast<float>(px) / static_cast<float>(size);
            float n = fbm2(u, v, 6.0F);
            n = n * pole_falloff + 0.15F * (1.0F - pole_falloff);
            const bool is_ocean = n <= 0.48F;
            float roughness, metallic, ao;
            if (pole_falloff < 0.18F) {
                roughness = 0.85F; metallic = 0.0F; ao = 0.95F;
            } else if (is_ocean) {
                roughness = 0.45F; metallic = 0.05F; ao = 0.95F;
            } else {
                const float t = std::clamp((n - 0.48F) / 0.52F, 0.0F, 1.0F);
                roughness = 0.65F + 0.25F * t;
                metallic  = 0.05F;
                ao        = 0.85F + 0.15F * (1.0F - t);
            }
            const std::size_t i = (static_cast<std::size_t>(py) * size + px) * 4;
            mr[i + 0] = 0;
            mr[i + 1] = static_cast<std::uint8_t>(roughness * 255.0F);
            mr[i + 2] = static_cast<std::uint8_t>(metallic * 255.0F);
            mr[i + 3] = static_cast<std::uint8_t>(ao * 255.0F);
        }
    }
    return mr;
}

}  // namespace cd::texture_synth

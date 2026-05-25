// =============================================================================
// CHROMODYNAMIC — cd/ibl/EquirectToCube.hpp
// Phase 155-full / v0.99.92 — HDR equirectangular → cubemap (CPU bake).
//
// Converts a 2D HDR equirectangular environment map (the format
// every IBL asset ships in — sIBL archive, HDRI Haven, Poly Haven)
// into a 6-face cubemap. The equirect → spherical projection is the
// standard:
//
//     phi   = atan2(dir.z, dir.x)        ∈ [-π, π]
//     theta = asin(dir.y)                ∈ [-π/2, π/2]
//     u     = 0.5 + phi / (2π)
//     v     = 0.5 - theta / π
//
// Bilinear sample in the equirect for each cube texel.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ibl/Cubemap.hpp>
#include <cd/math/Vector.hpp>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>

namespace cd::ibl
{

/// Equirectangular HDR image. Row-major float RGB.
struct EquirectImage
{
    std::uint32_t width  { 0 };
    std::uint32_t height { 0 };
    std::span<const float> rgb;  // length = width * height * 3, top row first
};

/// Sample the equirect at a unit world direction.
[[nodiscard]] inline cd::math::Vec3f
sample_equirect(const EquirectImage& eq, cd::math::Vec3f dir) noexcept
{
    if (eq.width == 0 || eq.height == 0) return { 0, 0, 0 };
    const float phi   = std::atan2(dir.z, dir.x);
    const float theta = std::asin(std::clamp(dir.y, -1.0F, 1.0F));
    const float u = 0.5F + phi / (2.0F * std::numbers::pi_v<float>);
    const float v = 0.5F - theta / std::numbers::pi_v<float>;
    const float fx = u * static_cast<float>(eq.width)  - 0.5F;
    const float fy = v * static_cast<float>(eq.height) - 0.5F;
    auto wrap_x = [&](int x) {
        x %= static_cast<int>(eq.width);
        if (x < 0) x += static_cast<int>(eq.width);
        return x;
    };
    auto clamp_y = [&](int y) {
        if (y < 0) return 0;
        if (y >= static_cast<int>(eq.height)) return static_cast<int>(eq.height - 1);
        return y;
    };
    const int x0 = wrap_x(static_cast<int>(std::floor(fx)));
    const int x1 = wrap_x(x0 + 1);
    const int y0 = clamp_y(static_cast<int>(std::floor(fy)));
    const int y1 = clamp_y(y0 + 1);
    const float tx = fx - std::floor(fx);
    const float ty = fy - std::floor(fy);
    auto px = [&](int x, int y) {
        const auto i = (static_cast<std::size_t>(y) * eq.width + static_cast<std::size_t>(x)) * 3;
        return cd::math::Vec3f { eq.rgb[i], eq.rgb[i+1], eq.rgb[i+2] };
    };
    const auto a = px(x0, y0);
    const auto b = px(x1, y0);
    const auto c = px(x0, y1);
    const auto d = px(x1, y1);
    const float ix = 1.0F - tx;
    const float iy = 1.0F - ty;
    return {
        (a.x*ix + b.x*tx) * iy + (c.x*ix + d.x*tx) * ty,
        (a.y*ix + b.y*tx) * iy + (c.y*ix + d.y*tx) * ty,
        (a.z*ix + b.z*tx) * iy + (c.z*ix + d.z*tx) * ty,
    };
}

/// Bake an HDR equirect into a cubemap of `face_size × face_size`.
[[nodiscard]] inline CubeMapRgbF
equirect_to_cube(const EquirectImage& eq, std::uint32_t face_size)
{
    auto cube = CubeMapRgbF::allocate(face_size);
    for (std::uint8_t f = 0; f < kCubeFaceCount; ++f)
    {
        const auto face = static_cast<CubeFace>(f);
        for (std::uint32_t y = 0; y < face_size; ++y)
        {
            for (std::uint32_t x = 0; x < face_size; ++x)
            {
                const float u = (static_cast<float>(x) + 0.5F) / static_cast<float>(face_size);
                const float v = (static_cast<float>(y) + 0.5F) / static_cast<float>(face_size);
                const auto dir = cube_uv_to_world_dir(face, u, v);
                cube.store(face, x, y, sample_equirect(eq, dir));
            }
        }
    }
    return cube;
}

}  // namespace cd::ibl

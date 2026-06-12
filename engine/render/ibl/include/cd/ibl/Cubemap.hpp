// =============================================================================
// CHROMODYNAMIC — cd/ibl/Cubemap.hpp
// Phase 155-full / v0.99.92 — CPU cubemap data + face math.
//
// Plain-old-data cubemap: 6 faces of `face_size × face_size` RGB
// floats (HDR). Face order matches Vulkan / D3D / OpenGL cubemap
// layer index (+X, -X, +Y, -Y, +Z, -Z).
//
// Coordinate convention: cube space is right-handed; +Y is up. The
// `cube_uv_to_world_dir` helper converts a face index + face-local
// (u,v) ∈ [0,1] into the unit world direction the texel represents
// — the same math the GPU uses to sample a cubemap, written in CPU
// code so all the convolutions in this module index the same way.
//
// Header-only.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace cd::ibl
{

enum class CubeFace : std::uint8_t
{
    kPosX = 0, kNegX = 1, kPosY = 2, kNegY = 3, kPosZ = 4, kNegZ = 5,
};

inline constexpr std::uint32_t kCubeFaceCount = 6;

/// RGB float cubemap. faces[f] holds face_size² × 3 floats, row-major.
struct CubeMapRgbF
{
    std::uint32_t face_size { 0 };
    std::array<std::vector<float>, kCubeFaceCount> faces {};

    [[nodiscard]] static CubeMapRgbF allocate(std::uint32_t size)
    {
        CubeMapRgbF c;
        c.face_size = size;
        for (auto& f : c.faces) f.assign(static_cast<std::size_t>(size) * size * 3, 0.0F);
        return c;
    }

    void store(CubeFace face, std::uint32_t x, std::uint32_t y,
               const cd::math::Vec3f& rgb) noexcept
    {
        auto& vec = faces[static_cast<std::size_t>(face)];
        const auto i = (static_cast<std::size_t>(y) * face_size + x) * 3;
        vec[i + 0] = rgb.x;
        vec[i + 1] = rgb.y;
        vec[i + 2] = rgb.z;
    }

    [[nodiscard]] cd::math::Vec3f sample_texel(CubeFace face,
                                               std::uint32_t x,
                                               std::uint32_t y) const noexcept
    {
        const auto& vec = faces[static_cast<std::size_t>(face)];
        const auto i = (static_cast<std::size_t>(y) * face_size + x) * 3;
        return { vec[i + 0], vec[i + 1], vec[i + 2] };
    }
};

/// Map (face, u, v) ∈ ({0..5}, [0,1], [0,1]) to the unit-length
/// world direction the texel represents. Matches the GL / Vulkan
/// cubemap convention. Note: GL cubemaps are left-handed; we
/// negate Z so the engine convention stays right-handed.
[[nodiscard]] inline cd::math::Vec3f
cube_uv_to_world_dir(CubeFace face, float u, float v) noexcept
{
    // u, v ∈ [0, 1] → s, t ∈ [-1, 1]
    const float s = 2.0F * u - 1.0F;
    const float t = 1.0F - 2.0F * v;  // flip y so v=0 is top
    cd::math::Vec3f d {};
    switch (face)
    {
        case CubeFace::kPosX: d = {  1.0F,    t,   -s }; break;
        case CubeFace::kNegX: d = { -1.0F,    t,    s }; break;
        case CubeFace::kPosY: d = {    s, 1.0F,    t }; break;
        case CubeFace::kNegY: d = {    s,-1.0F,   -t }; break;
        case CubeFace::kPosZ: d = {    s,    t, 1.0F }; break;
        case CubeFace::kNegZ: d = {   -s,    t,-1.0F }; break;
    }
    const float len = std::sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
    if (len > 1e-6F) { d.x/=len; d.y/=len; d.z/=len; }
    return d;
}

/// Build an environment cubemap by sampling an arbitrary sky function
/// at every cube texel's world direction. Caller passes any
/// `Vec3f -> Vec3f` callable; useful for materialising procedural
/// skies (analytical hemisphere, Hosek-Wilkie, atmospheric scatter
/// LUTs) into the IBL-ready CubeMapRgbF for prefilter + irradiance.
template<typename SkySampler>
[[nodiscard]] inline CubeMapRgbF
bake_sky_cube(std::uint32_t face_size, SkySampler sample)
{
    auto cm = CubeMapRgbF::allocate(face_size);
    for (std::uint8_t f = 0; f < kCubeFaceCount; ++f)
    {
        const auto face = static_cast<CubeFace>(f);
        for (std::uint32_t y = 0; y < face_size; ++y)
        {
            for (std::uint32_t x = 0; x < face_size; ++x)
            {
                const float u = (static_cast<float>(x) + 0.5F) /
                                static_cast<float>(face_size);
                const float v = (static_cast<float>(y) + 0.5F) /
                                static_cast<float>(face_size);
                const auto dir = cube_uv_to_world_dir(face, u, v);
                cm.store(face, x, y, sample(dir));
            }
        }
    }
    return cm;
}

/// Sample a unit direction with trilinear-ish lookup against the
/// cubemap. Used by the irradiance + specular convolutions. Picks
/// the dominant axis to choose a face, then maps the remaining two
/// axes to (u, v) and bilinearly interpolates within the face.
[[nodiscard]] inline cd::math::Vec3f
sample_cubemap_dir(const CubeMapRgbF& cm, cd::math::Vec3f dir) noexcept
{
    if (cm.face_size == 0) return { 0, 0, 0 };
    const float ax = std::abs(dir.x);
    const float ay = std::abs(dir.y);
    const float az = std::abs(dir.z);
    CubeFace face = CubeFace::kPosX;
    float sc = 0.0F;
    float tc = 0.0F;
    float ma = 0.0F;
    if (ax >= ay && ax >= az)
    {
        face = dir.x > 0 ? CubeFace::kPosX : CubeFace::kNegX;
        ma = ax;
        sc = (dir.x > 0) ? -dir.z : dir.z;
        tc = dir.y;
    }
    else if (ay >= ax && ay >= az)
    {
        face = dir.y > 0 ? CubeFace::kPosY : CubeFace::kNegY;
        ma = ay;
        sc = dir.x;
        tc = (dir.y > 0) ? dir.z : -dir.z;
    }
    else
    {
        face = dir.z > 0 ? CubeFace::kPosZ : CubeFace::kNegZ;
        ma = az;
        sc = (dir.z > 0) ? dir.x : -dir.x;
        tc = dir.y;
    }
    const float u = 0.5F * (sc / ma + 1.0F);
    const float v = 0.5F * (1.0F - tc / ma);
    // Bilinear within face.
    const float fx = u * static_cast<float>(cm.face_size) - 0.5F;
    const float fy = v * static_cast<float>(cm.face_size) - 0.5F;
    const auto clamp_i = [&](int i) {
        if (i < 0) return 0;
        if (std::cmp_greater_equal(i, cm.face_size)) return static_cast<int>(cm.face_size - 1);
        return i;
    };
    const int x0 = clamp_i(static_cast<int>(std::floor(fx)));
    const int y0 = clamp_i(static_cast<int>(std::floor(fy)));
    const int x1 = clamp_i(x0 + 1);
    const int y1 = clamp_i(y0 + 1);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const auto a = cm.sample_texel(face, static_cast<std::uint32_t>(x0), static_cast<std::uint32_t>(y0));
    const auto b = cm.sample_texel(face, static_cast<std::uint32_t>(x1), static_cast<std::uint32_t>(y0));
    const auto c = cm.sample_texel(face, static_cast<std::uint32_t>(x0), static_cast<std::uint32_t>(y1));
    const auto d = cm.sample_texel(face, static_cast<std::uint32_t>(x1), static_cast<std::uint32_t>(y1));
    const float inv_tx = 1.0F - tx;
    const float inv_ty = 1.0F - ty;
    return {
        (a.x*inv_tx + b.x*tx) * inv_ty + (c.x*inv_tx + d.x*tx) * ty,
        (a.y*inv_tx + b.y*tx) * inv_ty + (c.y*inv_tx + d.y*tx) * ty,
        (a.z*inv_tx + b.z*tx) * inv_ty + (c.z*inv_tx + d.z*tx) * ty,
    };
}

}  // namespace cd::ibl

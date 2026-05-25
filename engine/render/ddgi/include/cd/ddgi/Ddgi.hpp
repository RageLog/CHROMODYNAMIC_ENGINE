// =============================================================================
// CHROMODYNAMIC — cd/ddgi/Ddgi.hpp
// Day 29 — Dynamic Diffuse Global Illumination (Majercik 2019).
//
// 3D grid of irradiance probes; each probe stores irradiance + depth
// octahedral encodings. Per-frame the engine traces N rays per probe
// to update the encoding; per-shading-point the FS samples 8 nearest
// probes with depth-test gating.
//
// Reference: Majercik, Guertin, Nowrouzezahrai, McGuire — "Dynamic
// Diffuse Global Illumination with Ray-Traced Irradiance Fields"
// (JCGT 2019).
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace cd::ddgi
{

struct GridConfig
{
    cd::math::Vec3f origin   { 0, 0, 0 };
    cd::math::Vec3f spacing  { 1, 1, 1 };
    std::uint32_t   probes_x { 8 };
    std::uint32_t   probes_y { 4 };
    std::uint32_t   probes_z { 8 };
    /// Octahedral probe texture face size; total irradiance texture
    /// is (probes_x * probes_z * size) × (probes_y * size).
    std::uint32_t   probe_face_size { 8 };
    /// Rays per probe per frame (Majercik recommends 64-256).
    std::uint32_t   rays_per_probe  { 64 };
};

/// World position of probe at integer grid coordinate (px, py, pz).
[[nodiscard]] inline cd::math::Vec3f
probe_world_pos(const GridConfig& g,
                std::uint32_t px,
                std::uint32_t py,
                std::uint32_t pz) noexcept
{
    return {
        g.origin.x + static_cast<float>(px) * g.spacing.x,
        g.origin.y + static_cast<float>(py) * g.spacing.y,
        g.origin.z + static_cast<float>(pz) * g.spacing.z };
}

/// Trilinear weights for 8 nearest probes at a world-space sample
/// point. Caller fills `weights[0..7]` (in xyz lo/hi order:
/// 000 / 100 / 010 / 110 / 001 / 101 / 011 / 111). Out-of-bounds
/// samples return all-zero weights so the lookup falls back to sky.
inline void
trilinear_probe_weights(const GridConfig& g,
                        cd::math::Vec3f p,
                        std::array<float, 8>& weights,
                        std::array<std::array<std::uint32_t, 3>, 8>& corners) noexcept
{
    const float gx = (p.x - g.origin.x) / g.spacing.x;
    const float gy = (p.y - g.origin.y) / g.spacing.y;
    const float gz = (p.z - g.origin.z) / g.spacing.z;
    const auto x0 = static_cast<std::int32_t>(std::floor(gx));
    const auto y0 = static_cast<std::int32_t>(std::floor(gy));
    const auto z0 = static_cast<std::int32_t>(std::floor(gz));
    const float fx = gx - static_cast<float>(x0);
    const float fy = gy - static_cast<float>(y0);
    const float fz = gz - static_cast<float>(z0);
    int idx = 0;
    for (int dz = 0; dz <= 1; ++dz)
    for (int dy = 0; dy <= 1; ++dy)
    for (int dx = 0; dx <= 1; ++dx)
    {
        const float wx = (dx == 0) ? (1.0F - fx) : fx;
        const float wy = (dy == 0) ? (1.0F - fy) : fy;
        const float wz = (dz == 0) ? (1.0F - fz) : fz;
        weights[static_cast<std::size_t>(idx)] = wx * wy * wz;
        const std::int32_t cx = x0 + dx;
        const std::int32_t cy = y0 + dy;
        const std::int32_t cz = z0 + dz;
        if (cx < 0 || cy < 0 || cz < 0 ||
            cx >= static_cast<std::int32_t>(g.probes_x) ||
            cy >= static_cast<std::int32_t>(g.probes_y) ||
            cz >= static_cast<std::int32_t>(g.probes_z))
        {
            weights[static_cast<std::size_t>(idx)] = 0.0F;
        }
        corners[static_cast<std::size_t>(idx)] = {
            static_cast<std::uint32_t>(std::max(0, cx)),
            static_cast<std::uint32_t>(std::max(0, cy)),
            static_cast<std::uint32_t>(std::max(0, cz)) };
        ++idx;
    }
}

// ---- GLSL helpers -----------------------------------------------------------

constexpr std::string_view kProbeUpdateCS = R"glsl(
#version 460
#extension GL_EXT_ray_query : require
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform accelerationStructureEXT scene;
layout(set = 0, binding = 1, rgba16f) uniform image2D irradiance_atlas;
layout(set = 0, binding = 2, r16f)    uniform image2D depth_atlas;
layout(push_constant) uniform PC {
  vec3  grid_origin;  float padding0;
  vec3  grid_spacing; float padding1;
  uvec4 probes_dim;     // xyz=count, w=rays
  uint  probe_face_size;
  uint  frame_index;
  uint  padding2;
  uint  padding3;
} pc;

void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  uint probes_per_row = pc.probes_dim.x * pc.probes_dim.z;
  if (p.x >= probes_per_row * pc.probe_face_size ||
      p.y >= pc.probes_dim.y * pc.probe_face_size) return;
  // (Full Majercik probe update: octahedral-decode + accumulate +
  // hysteresis blend — skipping in this trimmed library skeleton.)
  imageStore(irradiance_atlas, ivec2(p), vec4(0));
  imageStore(depth_atlas,      ivec2(p), vec4(0));
}
)glsl";

}  // namespace cd::ddgi

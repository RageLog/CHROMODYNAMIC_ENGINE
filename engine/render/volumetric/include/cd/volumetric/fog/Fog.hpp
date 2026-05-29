// =============================================================================
// CHROMODYNAMIC — cd/volumetric/fog/Fog.hpp
// Day 16 — Frostbite volumetric fog (Wronski 2014).
//
// 3D froxel grid (view-space sliced) holding scattering + extinction
// per cell. Two compute passes:
//   1) kFogInjectionCS — accumulate scattering / extinction from
//      analytic lights + density volumes into the 3D froxel texture.
//   2) kFogIntegrationCS — front-to-back depth integration along the
//      view ray, writing the final RGBA accumulator (RGB = in-
//      scattered light, A = transmittance).
//
// References:
//   * Wronski 2014 — "Volumetric Fog: Unified, Compute-Shader Based
//     Solution" (Frostbite / SIGGRAPH 2014).
//   * Hillaire 2015 — "Towards Unified and Physically-Based Volumetric
//     Lighting" (subsequent quality refinements).
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cd::volumetric::fog
{

struct GridConfig
{
    std::uint32_t x { 160 };   ///< Tile columns. Wronski: 160 @ 1080p.
    std::uint32_t y { 90 };    ///< Tile rows.
    std::uint32_t z { 128 };   ///< Depth slices (logarithmic).
    float near_z { 0.1F };
    float far_z  { 64.0F };
};

/// Convert a 0..1 slice fraction to view-space Z using Wronski's
/// quadratic distribution (denser sampling close to camera, sparser
/// far away — matches eye sensitivity).
[[nodiscard]] inline float
slice_to_view_z(float slice, const GridConfig& g) noexcept
{
    const float t = slice * slice;
    return g.near_z + (g.far_z - g.near_z) * t;
}

/// Inverse: view-space Z to slice fraction.
[[nodiscard]] inline float
view_z_to_slice(float view_z, const GridConfig& g) noexcept
{
    const float t = std::clamp((view_z - g.near_z) /
                               (g.far_z - g.near_z), 0.0F, 1.0F);
    return std::sqrt(t);
}

/// Beer-Lambert transmittance over a slab of depth `dt` with
/// extinction coefficient `sigma`.
[[nodiscard]] inline float beer_lambert(float sigma, float dt) noexcept
{
    return std::exp(-sigma * dt);
}

/// CPU froxel accumulator. `cells.size() == config.x * config.y * config.z`.
/// Each cell is RGBA: RGB = in-scattering, A = extinction (km^-1).
struct FroxelGrid
{
    GridConfig config {};
    std::vector<cd::math::Vec4f> cells;

    void resize()
    {
        cells.assign(static_cast<std::size_t>(config.x) * config.y * config.z,
                     { 0, 0, 0, 0 });
    }

    [[nodiscard]] std::size_t
    index(std::uint32_t x, std::uint32_t y, std::uint32_t z) const noexcept
    {
        return (static_cast<std::size_t>(z) * config.y + y) * config.x + x;
    }
};

/// Front-to-back integrate along the Z axis of a froxel grid for the
/// (x, y) tile. Output `dst[slice]` holds the accumulated RGB in-
/// scattering and the A channel holds the transmittance from camera
/// to that slice.
inline void
integrate_view_ray(const FroxelGrid& src,
                   std::uint32_t x,
                   std::uint32_t y,
                   std::span<cd::math::Vec4f> dst) noexcept
{
    cd::math::Vec4f accum { 0, 0, 0, 1 };
    for (std::uint32_t z = 0; z < src.config.z && z < dst.size(); ++z)
    {
        const auto& cell = src.cells[src.index(x, y, z)];
        const float slice0 = static_cast<float>(z)     / static_cast<float>(src.config.z);
        const float slice1 = static_cast<float>(z + 1) / static_cast<float>(src.config.z);
        const float dt = slice_to_view_z(slice1, src.config) -
                         slice_to_view_z(slice0, src.config);
        const float trans = beer_lambert(cell.w, dt);
        accum.x += cell.x * dt * accum.w;
        accum.y += cell.y * dt * accum.w;
        accum.z += cell.z * dt * accum.w;
        accum.w *= trans;
        dst[z] = accum;
    }
}

// ---- GLSL compute kernels ---------------------------------------------------

constexpr std::string_view kFogInjectionCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8, local_size_z = 4) in;
layout(set = 0, binding = 0, rgba16f) uniform writeonly image3D froxel;
layout(push_constant) uniform PC {
  uvec4 size;          // x, y, z, _
  vec4  near_far;      // near_z, far_z, _, _
  vec4  sun_dir;       // xyz=dir, w=intensity
  vec4  sun_color;     // rgb=col, a=phase_g
  vec4  density_base;  // rgb=scatter colour, a=extinction
} pc;

float slice_to_z(float s) {
  return pc.near_far.x + (pc.near_far.y - pc.near_far.x) * s * s;
}

void main() {
  uvec3 p = gl_GlobalInvocationID.xyz;
  if (p.x >= pc.size.x || p.y >= pc.size.y || p.z >= pc.size.z) return;
  float slice = (float(p.z) + 0.5) / float(pc.size.z);
  float dt    = slice_to_z(slice + 1.0 / float(pc.size.z)) - slice_to_z(slice);
  // Constant analytical fog (height-independent) — first cut. Real
  // path looks up density volumes + cascaded shadow occlusion here.
  vec3  scat = pc.density_base.rgb;
  float ext  = pc.density_base.a;
  // Forward HG phase along sun direction (assume camera-space y = up).
  float g    = pc.sun_color.a;
  float cosT = -pc.sun_dir.y;
  float g2   = g * g;
  float ph   = (1.0 - g2) /
               (4.0 * 3.14159265 * pow(1.0 + g2 - 2.0 * g * cosT, 1.5));
  scat *= pc.sun_color.rgb * pc.sun_dir.w * ph;
  imageStore(froxel, ivec3(p), vec4(scat * dt, ext));
}
)glsl";

constexpr std::string_view kFogIntegrationCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler3D froxel;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image3D integrated;
layout(push_constant) uniform PC {
  uvec4 size;        // x, y, z, _
  vec4  near_far;
} pc;

void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  if (p.x >= pc.size.x || p.y >= pc.size.y) return;
  vec4 accum = vec4(0, 0, 0, 1);
  for (uint z = 0; z < pc.size.z; ++z) {
    vec3 uvw = (vec3(p.x, p.y, z) + 0.5) / vec3(pc.size);
    vec4 cell = texture(froxel, uvw);
    float slice0 = float(z)     / float(pc.size.z);
    float slice1 = float(z + 1) / float(pc.size.z);
    float dt = (pc.near_far.y - pc.near_far.x) * (slice1 * slice1 - slice0 * slice0);
    float trans = exp(-cell.a * dt);
    accum.rgb += cell.rgb * dt * accum.a;
    accum.a   *= trans;
    imageStore(integrated, ivec3(p.x, p.y, int(z)), accum);
  }
}
)glsl";

}  // namespace cd::volumetric::fog

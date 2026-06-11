// =============================================================================
// CHROMODYNAMIC — cd/decal/Decal.hpp
// Day 22 — Deferred screen-space decals.
//
// Decal = OBB volume + projection matrix + atlas slice. The GBuffer
// pass projects each pixel back into the OBB's local space; if the
// pixel lies inside the volume, sample the decal atlas and modulate
// the GBuffer's albedo / normal / roughness channels.
//
// References:
//   * Persson 2009 — "Volume Decals" (Avalanche / GDC 2009).
//   * Filion 2012 — "Cluster Decal Rendering" (Ubisoft).
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace cd::decal
{

enum class BlendMode : std::uint8_t
{
    kAlbedoOnly  = 0,
    kNormalOnly  = 1,
    kAlbedoNormal = 2,
    kFull         = 3,  ///< albedo + normal + roughness
};

struct Decal
{
    /// Centre of the decal OBB in world space.
    cd::math::Vec3f position { 0, 0, 0 };
    /// Orthonormal OBB axes — projection direction is `-forward`.
    cd::math::Vec3f right    { 1, 0, 0 };
    cd::math::Vec3f up       { 0, 1, 0 };
    cd::math::Vec3f forward  { 0, 0, 1 };
    /// Half-extents along each axis (world units).
    cd::math::Vec3f half_extents { 0.5F, 0.5F, 0.5F };
    /// Atlas UV rect: (u0, v0, u1, v1).
    std::array<float, 4> atlas_uv_rect { 0.0F, 0.0F, 1.0F, 1.0F };
    /// Per-decal opacity (multiplied with atlas alpha).
    float opacity { 1.0F };
    BlendMode blend { BlendMode::kFull };
};

/// World-space point P -> decal-local NDC ([-1, 1]^3). Returns true if
/// the point lies inside the OBB; UV is filled with the [0,1]^2
/// projection along (right, up).
[[nodiscard]] inline bool
project_world_to_decal(const Decal& d,
                       cd::math::Vec3f p,
                       cd::math::Vec3f& out_local,
                       std::array<float, 2>& out_uv) noexcept
{
    const cd::math::Vec3f rel { p.x - d.position.x,
                                p.y - d.position.y,
                                p.z - d.position.z };
    const float lx = rel.x * d.right.x   + rel.y * d.right.y   + rel.z * d.right.z;
    const float ly = rel.x * d.up.x      + rel.y * d.up.y      + rel.z * d.up.z;
    const float lz = rel.x * d.forward.x + rel.y * d.forward.y + rel.z * d.forward.z;
    out_local = { lx, ly, lz };
    if (std::abs(lx) > d.half_extents.x ||
        std::abs(ly) > d.half_extents.y ||
        std::abs(lz) > d.half_extents.z) return false;
    const float u = d.atlas_uv_rect[0] +
                    (d.atlas_uv_rect[2] - d.atlas_uv_rect[0]) *
                    (lx / d.half_extents.x * 0.5F + 0.5F);
    const float v = d.atlas_uv_rect[1] +
                    (d.atlas_uv_rect[3] - d.atlas_uv_rect[1]) *
                    (ly / d.half_extents.y * 0.5F + 0.5F);
    out_uv = { u, v };
    return true;
}

/// AABB-vs-frustum quick test for tile-based decal binning.
[[nodiscard]] inline bool
decal_intersects_aabb(const Decal& d,
                      cd::math::Vec3f aabb_min,
                      cd::math::Vec3f aabb_max) noexcept
{
    const cd::math::Vec3f c = d.position;
    // Compute the decal's world-space AABB via the 8 OBB corners.
    cd::math::Vec3f mn { c.x, c.y, c.z };
    cd::math::Vec3f mx { c.x, c.y, c.z };
    for (int sx = -1; sx <= 1; sx += 2)
    {
        for (int sy = -1; sy <= 1; sy += 2)
        {
            for (int sz = -1; sz <= 1; sz += 2)
            {
                const cd::math::Vec3f corner {
                    c.x + d.right.x * d.half_extents.x * static_cast<float>(sx) +
                          d.up.x    * d.half_extents.y * static_cast<float>(sy) +
                          d.forward.x * d.half_extents.z * static_cast<float>(sz),
                    c.y + d.right.y * d.half_extents.x * static_cast<float>(sx) +
                          d.up.y    * d.half_extents.y * static_cast<float>(sy) +
                          d.forward.y * d.half_extents.z * static_cast<float>(sz),
                    c.z + d.right.z * d.half_extents.x * static_cast<float>(sx) +
                          d.up.z    * d.half_extents.y * static_cast<float>(sy) +
                          d.forward.z * d.half_extents.z * static_cast<float>(sz) };
                mn.x = std::min(mn.x, corner.x); mn.y = std::min(mn.y, corner.y); mn.z = std::min(mn.z, corner.z);
                mx.x = std::max(mx.x, corner.x); mx.y = std::max(mx.y, corner.y); mx.z = std::max(mx.z, corner.z);
            }
        }
    }
    // NaN-preserving negated form (equivalent to the prior
    // !(... || ...) separating-axis rejection).
    return !(mx.x < aabb_min.x) && !(mn.x > aabb_max.x) &&
           !(mx.y < aabb_min.y) && !(mn.y > aabb_max.y) &&
           !(mx.z < aabb_min.z) && !(mn.z > aabb_max.z);
}

// ---- GLSL helper for the deferred decal pass --------------------------------

constexpr std::string_view kDecalGlsl = R"glsl(
struct Decal {
  vec3  pos;       float opacity;
  vec3  right;     float _pad0;
  vec3  up;        float _pad1;
  vec3  forward;   float _pad2;
  vec3  half_ext;  float _pad3;
  vec4  uv_rect;
};
bool decal_project(Decal d, vec3 world_p, out vec2 uv) {
  vec3 rel = world_p - d.pos;
  float lx = dot(rel, d.right);
  float ly = dot(rel, d.up);
  float lz = dot(rel, d.forward);
  if (abs(lx) > d.half_ext.x ||
      abs(ly) > d.half_ext.y ||
      abs(lz) > d.half_ext.z) return false;
  uv.x = d.uv_rect.x + (d.uv_rect.z - d.uv_rect.x) * (lx / d.half_ext.x * 0.5 + 0.5);
  uv.y = d.uv_rect.y + (d.uv_rect.w - d.uv_rect.y) * (ly / d.half_ext.y * 0.5 + 0.5);
  return true;
}
)glsl";

}  // namespace cd::decal

// =============================================================================
// CHROMODYNAMIC — cd/light/CascadedShadow.hpp
// Phase 165 / v0.99.87 — Cascaded Shadow Maps (CSM) helpers.
//
// CSM splits the directional light's shadow frustum into N sub-frusta
// along view-space depth, each rendered at independent texture
// resolution. Classic technique from Engel 2007 / Zhang et al. 2006
// ("Sample Distribution Shadow Maps" — Practical Split Scheme).
//
// This header computes:
//   * Split distances using the Practical Split Scheme — a
//     λ-weighted blend of uniform + logarithmic splits.
//   * Per-cascade tight-fit light-space ortho frustum from the
//     view frustum corners.
//   * The light-space view-projection matrix the shadow pass uses
//     to render each cascade.
//
// All CPU-side; the GPU consumes a flat array of view-projection
// matrices + split distances via UBO.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cd::light
{

constexpr std::uint32_t kMaxCascades = 4;

struct CascadeSplit
{
    float near_distance { 0.0F };   // view-space near (positive)
    float far_distance  { 0.0F };   // view-space far
    cd::math::Mat4f light_view_proj { cd::math::Mat4f::identity() };
};

/// Practical Split Scheme: blend uniform + logarithmic splits with
/// λ in [0, 1]. λ=0 → pure uniform, λ=1 → pure log. Doom Eternal
/// uses ~0.75; common production default is ~0.85.
[[nodiscard]] inline std::array<float, kMaxCascades + 1>
practical_split_distances(float near_z, float far_z,
                          std::uint32_t cascade_count = 4,
                          float lambda = 0.75F) noexcept
{
    std::array<float, kMaxCascades + 1> splits {};
    cascade_count = std::min(cascade_count, kMaxCascades);
    splits[0] = near_z;
    for (std::uint32_t i = 1; i <= cascade_count; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(cascade_count);
        const float uniform = near_z + (far_z - near_z) * t;
        const float logsplit = near_z * std::pow(far_z / std::max(near_z, 0.001F), t);
        splits[i] = lambda * logsplit + (1.0F - lambda) * uniform;
    }
    return splits;
}

/// Compute the 8 world-space corners of a slice of the camera frustum
/// between `near_z` and `far_z`. Used by the shadow pass to fit a
/// tight ortho box around the slice.
[[nodiscard]] inline std::array<cd::math::Vec3f, 8>
slice_frustum_corners_world(const cd::math::Mat4f& inv_view_proj,
                            float near_z, float far_z,
                            float full_near, float full_far) noexcept
{
    // Convert near/far slice distances to NDC z values: t_near, t_far.
    // For column-major OpenGL-style NDC z ∈ [-1, 1]: t = (z - near) / (far - near) * 2 - 1
    const float t_near = (near_z - full_near) / (full_far - full_near) * 2.0F - 1.0F;
    const float t_far  = (far_z  - full_near) / (full_far - full_near) * 2.0F - 1.0F;
    const std::array<cd::math::Vec3f, 8> ndc {{
        {-1.0F, -1.0F, t_near}, {1.0F, -1.0F, t_near},
        {-1.0F,  1.0F, t_near}, {1.0F,  1.0F, t_near},
        {-1.0F, -1.0F, t_far},  {1.0F, -1.0F, t_far},
        {-1.0F,  1.0F, t_far},  {1.0F,  1.0F, t_far},
    }};
    std::array<cd::math::Vec3f, 8> world {};
    for (std::size_t i = 0; i < 8; ++i)
    {
        const auto& n = ndc[i];
        // inv_view_proj is column-major; apply as 4x4 * vec4.
        const float wx = inv_view_proj[0][0]*n.x + inv_view_proj[1][0]*n.y + inv_view_proj[2][0]*n.z + inv_view_proj[3][0];
        const float wy = inv_view_proj[0][1]*n.x + inv_view_proj[1][1]*n.y + inv_view_proj[2][1]*n.z + inv_view_proj[3][1];
        const float wz = inv_view_proj[0][2]*n.x + inv_view_proj[1][2]*n.y + inv_view_proj[2][2]*n.z + inv_view_proj[3][2];
        const float ww = inv_view_proj[0][3]*n.x + inv_view_proj[1][3]*n.y + inv_view_proj[2][3]*n.z + inv_view_proj[3][3];
        const float inv_w = ww > 1e-6F ? 1.0F / ww : 0.0F;
        world[i] = { wx * inv_w, wy * inv_w, wz * inv_w };
    }
    return world;
}

/// Compute the cascade's light-space view-projection given the slice
/// corners + light direction. The matrix is `ortho(box) * lookAt(
/// box_center - light_dir * extent, box_center, world_up)`.
[[nodiscard]] inline cd::math::Mat4f
fit_cascade_light_matrix(const std::array<cd::math::Vec3f, 8>& slice_corners,
                         cd::math::Vec3f light_dir) noexcept
{
    // Center of the slice (used as light's lookAt target).
    cd::math::Vec3f center { 0, 0, 0 };
    for (const auto& c : slice_corners) { center.x += c.x; center.y += c.y; center.z += c.z; }
    center.x /= 8.0F; center.y /= 8.0F; center.z /= 8.0F;

    // Bounding sphere radius (texture-stable; using sphere radius
    // makes the cascade rotation-invariant per Persson 2009).
    float radius = 0.0F;
    for (const auto& c : slice_corners)
    {
        const float dx = c.x - center.x, dy = c.y - center.y, dz = c.z - center.z;
        radius = std::max(radius, std::sqrt(dx*dx + dy*dy + dz*dz));
    }
    // Snap radius to a stable increment to reduce shimmering.
    radius = std::ceil(radius * 16.0F) / 16.0F;

    // Light view = lookAt(center - light_dir*radius, center, up).
    auto norm = [](cd::math::Vec3f v) {
        const float l = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
        return l > 1e-6F ? cd::math::Vec3f { v.x/l, v.y/l, v.z/l } : v;
    };
    light_dir = norm(light_dir);
    cd::math::Vec3f eye {
        center.x - light_dir.x * radius * 2.0F,
        center.y - light_dir.y * radius * 2.0F,
        center.z - light_dir.z * radius * 2.0F,
    };
    cd::math::Vec3f up { 0.0F, 1.0F, 0.0F };
    if (std::abs(light_dir.y) > 0.99F) up = { 0.0F, 0.0F, 1.0F };

    // Right-handed lookAt.
    cd::math::Vec3f f = norm({ center.x - eye.x, center.y - eye.y, center.z - eye.z });
    cd::math::Vec3f s = norm({ f.y*up.z - f.z*up.y, f.z*up.x - f.x*up.z, f.x*up.y - f.y*up.x });
    cd::math::Vec3f u { s.y*f.z - s.z*f.y, s.z*f.x - s.x*f.z, s.x*f.y - s.y*f.x };

    cd::math::Mat4f view = cd::math::Mat4f::identity();
    view[0][0] = s.x; view[1][0] = s.y; view[2][0] = s.z; view[3][0] = -(s.x*eye.x + s.y*eye.y + s.z*eye.z);
    view[0][1] = u.x; view[1][1] = u.y; view[2][1] = u.z; view[3][1] = -(u.x*eye.x + u.y*eye.y + u.z*eye.z);
    view[0][2] = -f.x; view[1][2] = -f.y; view[2][2] = -f.z; view[3][2] = (f.x*eye.x + f.y*eye.y + f.z*eye.z);

    // Orthographic projection [-radius, +radius] cubed, near = 0, far = 2*radius*2.
    const float r = radius;
    cd::math::Mat4f proj = cd::math::Mat4f::identity();
    proj[0][0] =  1.0F / r;
    proj[1][1] =  1.0F / r;
    proj[2][2] = -1.0F / (2.0F * r * 2.0F);  // Vulkan Z [0,1] would scale this differently; OpenGL [-1,1] uses /-2r
    proj[3][2] =  0.0F;

    // Light-space VP = proj * view (column-major matrix product).
    cd::math::Mat4f vp {};
    for (std::size_t r2 = 0; r2 < 4; ++r2)
        for (std::size_t c = 0; c < 4; ++c)
            vp[c][r2] =
                proj[0][r2]*view[c][0] + proj[1][r2]*view[c][1]
              + proj[2][r2]*view[c][2] + proj[3][r2]*view[c][3];
    return vp;
}

/// Build the full cascade descriptor list. Returns one CascadeSplit
/// per cascade.
[[nodiscard]] inline std::vector<CascadeSplit>
build_cascades(const cd::math::Mat4f& inv_view_proj,
               float near_z, float far_z,
               cd::math::Vec3f light_dir,
               std::uint32_t cascade_count = 4,
               float lambda = 0.75F)
{
    cascade_count = std::min(cascade_count, kMaxCascades);
    const auto splits = practical_split_distances(near_z, far_z, cascade_count, lambda);
    std::vector<CascadeSplit> out;
    out.reserve(cascade_count);
    for (std::uint32_t i = 0; i < cascade_count; ++i)
    {
        CascadeSplit cs;
        cs.near_distance = splits[i];
        cs.far_distance  = splits[i + 1];
        const auto corners = slice_frustum_corners_world(
            inv_view_proj, cs.near_distance, cs.far_distance, near_z, far_z);
        cs.light_view_proj = fit_cascade_light_matrix(corners, light_dir);
        out.push_back(cs);
    }
    return out;
}

}  // namespace cd::light

// =============================================================================
// CHROMODYNAMIC — cd/camera/Camera.hpp
//
// Minimal viewing camera shared by every sample/editor that wants to look at
// something. Two responsibilities:
//
//   1. Geometric state: eye, target, up, fov, near/far clip planes.
//   2. Derive view / projection / view-projection matrices on demand.
//
// Deliberately NOT included here (kept separate for layering):
//   - Camera controllers (orbit / fps / dolly) — see OrbitController.hpp etc.
//   - Frustum culling — belongs in cd::scene once that lands.
//   - Cinematic curves, depth-of-field, etc. — application-layer concerns.
//
// Right-handed coordinate system, Vulkan/D3D clip space convention (depth in
// [0, 1]). Y-up world; the sample shaders flip Y at the end of the vertex
// program to satisfy Vulkan's NDC Y-down rule. This is the same convention
// cd::math::look_at / perspective produce.
// =============================================================================
#pragma once

#include <cd/math/Matrix.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>

namespace cd::camera
{

/// Plain-data viewing camera. Controllers mutate this struct; the renderer
/// only ever reads the derived matrices below.
struct Camera
{
    cd::math::Vec3f eye { 0.0F, 0.0F, 3.0F };
    cd::math::Vec3f target { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f up { 0.0F, 1.0F, 0.0F };

    /// Vertical field of view, RADIANS. ~57° is a calm walking-around default
    /// (1.0 rad). Use cd::math::Constants::pi/3 for the more cinematic 60°.
    float fov_y { 1.0F };

    /// Near/far clip planes in world units. The default values are
    /// intentionally crude — call `auto_frame()` or set them per-scene to
    /// avoid Z-fighting on large geometry.
    float near_z { 0.1F };
    float far_z { 100.0F };
};

/// World-space camera position. Sample shaders feed this into push constants
/// for view-direction-dependent shading (Fresnel, specular).
[[nodiscard]] inline cd::math::Vec3f world_position(const Camera& c) noexcept
{
    return c.eye;
}

/// View matrix (right-handed look-at). Identical to `cd::math::look_at` —
/// hoisted here so call sites don't have to reach across libraries.
[[nodiscard]] inline cd::math::Mat4f view_matrix(const Camera& c) noexcept
{
    return cd::math::look_at(c.eye, c.target, c.up);
}

/// Perspective projection. `aspect` = viewport width / height; if the
/// viewport is zero the function still returns a finite matrix (using a
/// 1:1 aspect fallback) so the renderer never propagates NaN to the GPU.
[[nodiscard]] inline cd::math::Mat4f projection_matrix(const Camera& c, float aspect) noexcept
{
    const float a = (aspect > 0.0F) ? aspect : 1.0F;
    return cd::math::perspective(c.fov_y, a, c.near_z, c.far_z);
}

/// Convenience: view-projection in one call. Most call sites need this and
/// nothing else.
[[nodiscard]] inline cd::math::Mat4f view_projection(const Camera& c, float aspect) noexcept
{
    return projection_matrix(c, aspect) * view_matrix(c);
}

/// Build a camera that frames an axis-aligned bounding box from the +X/+Y
/// front-quarter angle. The eye is placed `distance_scale * radius` away
/// from the box centre; near/far clip planes scale with that distance so a
/// scene loaded from a 0.1m miniature or a 1000m terrain both render
/// without Z-fighting or near-plane clipping.
///
/// `distance_scale` defaults to 2.5 — empirically a comfortable framing for
/// "viewer wants to see the whole thing without zooming". Pass a larger
/// value to back the camera off, smaller to push in.
[[nodiscard]] inline Camera
auto_frame_aabb(const cd::math::Vec3f& bbox_min, const cd::math::Vec3f& bbox_max, float distance_scale = 2.5F) noexcept
{
    const cd::math::Vec3f centre { (bbox_min[0] + bbox_max[0]) * 0.5F,
                                   (bbox_min[1] + bbox_max[1]) * 0.5F,
                                   (bbox_min[2] + bbox_max[2]) * 0.5F };
    const cd::math::Vec3f size { bbox_max[0] - bbox_min[0], bbox_max[1] - bbox_min[1], bbox_max[2] - bbox_min[2] };
    const float radius = 0.5F * std::sqrt(size[0] * size[0] + size[1] * size[1] + size[2] * size[2]);
    const float dist = std::max(radius * distance_scale, 1.5F);

    Camera c {};
    c.target = centre;
    // Position camera in the front-upper-right octant so depth + perspective
    // are immediately readable; controllers usually overwrite eye anyway.
    c.eye = { centre[0] + dist, centre[1] + dist * 0.45F, centre[2] + dist };
    c.up = { 0.0F, 1.0F, 0.0F };
    c.near_z = std::max(dist * 0.02F, 0.05F);
    c.far_z = std::max(dist * 10.0F, 50.0F);
    return c;
}

}  // namespace cd::camera

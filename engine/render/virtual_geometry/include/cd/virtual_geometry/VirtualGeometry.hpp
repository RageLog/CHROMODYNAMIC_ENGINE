// =============================================================================
// CHROMODYNAMIC — cd/virtual_geometry/VirtualGeometry.hpp
// Day 26 — Virtual geometry (Nanite-style LOD DAG).
//
// Hierarchical cluster (= meshlet) DAG with screen-space error-driven
// LOD pick on the GPU. The API ships the in-memory layout + the LOD
// pick logic; the actual builder (mesh simplification + cluster
// grouping) is large enough to deserve its own offline tool.
//
// Reference: Karis 2021 — "Nanite: A Deep Dive" (UE5 SIGGRAPH).
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cd::virtual_geometry
{

/// One DAG node = one cluster. `parent_error` is the screen-space
/// error that the cluster's parent introduces at the camera; pick the
/// parent (coarser) when our own error is acceptable.
struct ClusterNode
{
    cd::math::Vec4f bounds_sphere    { 0, 0, 0, 0 };
    float           self_error       { 0.0F };
    float           parent_error     { 0.0F };
    std::uint32_t   first_child      { 0 };
    std::uint32_t   child_count      { 0 };
    /// Index range into the cluster's meshlet data (see cd::mesh_shader).
    std::uint32_t   meshlet_offset   { 0 };
    std::uint32_t   meshlet_count    { 0 };
};

/// Project the screen-space error of `cluster_radius` at world position
/// `centre` for a given camera. Returns pixel error.
[[nodiscard]] inline float
projected_error_pixels(cd::math::Vec3f centre,
                       float            cluster_radius,
                       cd::math::Vec3f cam_eye,
                       float            half_fov_rad,
                       std::uint32_t    viewport_h_px) noexcept
{
    const float dx = centre.x - cam_eye.x;
    const float dy = centre.y - cam_eye.y;
    const float dz = centre.z - cam_eye.z;
    const float dist = std::max(std::sqrt(dx * dx + dy * dy + dz * dz), 1e-3F);
    const float angular = cluster_radius / dist;
    const float fov_px  = static_cast<float>(viewport_h_px) /
                          (2.0F * std::tan(half_fov_rad));
    return angular * fov_px;
}

/// Decide whether to draw `node`: yes if my error is acceptable but
/// my parent's isn't (= we sit at the right LOD frontier).
[[nodiscard]] inline bool
is_lod_frontier(const ClusterNode& node,
                float threshold_px,
                cd::math::Vec3f cam_eye,
                float half_fov_rad,
                std::uint32_t viewport_h_px) noexcept
{
    const cd::math::Vec3f c { node.bounds_sphere.x,
                              node.bounds_sphere.y,
                              node.bounds_sphere.z };
    const float self_px   = projected_error_pixels(c, node.self_error,
                                                    cam_eye, half_fov_rad,
                                                    viewport_h_px);
    const float parent_px = projected_error_pixels(c, node.parent_error,
                                                    cam_eye, half_fov_rad,
                                                    viewport_h_px);
    return self_px <= threshold_px && parent_px > threshold_px;
}

/// CPU-side LOD pick — host helper that produces the cluster list to
/// dispatch with a mesh shader. Tests + offline tooling use this;
/// production runs the equivalent compute kernel.
[[nodiscard]] inline std::vector<std::uint32_t>
pick_clusters(std::span<const ClusterNode> dag,
              float threshold_px,
              cd::math::Vec3f cam_eye,
              float half_fov_rad,
              std::uint32_t viewport_h_px)
{
    std::vector<std::uint32_t> out;
    out.reserve(dag.size() / 8);
    for (std::size_t i = 0; i < dag.size(); ++i)
    {
        if (is_lod_frontier(dag[i], threshold_px, cam_eye,
                            half_fov_rad, viewport_h_px))
            out.push_back(static_cast<std::uint32_t>(i));
    }
    return out;
}

// ---- GLSL pick kernel -------------------------------------------------------

constexpr std::string_view kLodPickCS = R"glsl(
#version 460
struct ClusterNode {
  vec4 bounds_sphere;
  float self_error, parent_error;
  uint  first_child, child_count;
  uint  meshlet_offset, meshlet_count;
  uint  pad0, pad1;
};
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) readonly buffer DAG { ClusterNode nodes[]; } D;
layout(set = 0, binding = 1) buffer Out {
  uint count;
  uint cluster_ids[];
} O;
layout(push_constant) uniform PC {
  vec3 cam_eye; float half_fov;
  uint count;
  uint vp_h;
  float threshold_px;
  float padding_;
} pc;
float projected_err(vec3 c, float r) {
  float d = max(distance(c, pc.cam_eye), 1e-3);
  float fov_px = float(pc.vp_h) / (2.0 * tan(pc.half_fov));
  return r / d * fov_px;
}
void main() {
  uint i = gl_GlobalInvocationID.x;
  if (i >= pc.count) return;
  ClusterNode n = D.nodes[i];
  vec3 c = n.bounds_sphere.xyz;
  float self_px   = projected_err(c, n.self_error);
  float parent_px = projected_err(c, n.parent_error);
  if (self_px <= pc.threshold_px && parent_px > pc.threshold_px) {
    uint slot = atomicAdd(O.count, 1u);
    O.cluster_ids[slot] = i;
  }
}
)glsl";

}  // namespace cd::virtual_geometry

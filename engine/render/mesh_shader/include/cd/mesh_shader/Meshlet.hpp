// =============================================================================
// CHROMODYNAMIC — cd/mesh_shader/Meshlet.hpp
// Day 25 — Mesh-shader meshlet API.
//
// Meshlet = ~64-vertex / ~124-triangle cluster (matches
// VK_EXT_mesh_shader minimum required limits). Per-cluster bounding
// sphere + cone for backface + frustum + occlusion culling on the
// task shader. Vertex / index buffers are storage SSBOs the mesh
// shader reads from.
//
// References:
//   * Akenine-Möller et al. 2018 ch. 10.5 — meshlets background.
//   * NVIDIA "Introduction to Mesh Shaders" 2018.
//   * Karis 2021 — Nanite ("clusters all the way down"); the
//     meshlet here is the leaf granularity Nanite operates on.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cd::mesh_shader
{

/// Limits aligned with VK_EXT_mesh_shader required minimums:
///   maxMeshOutputVertices    >= 64
///   maxMeshOutputPrimitives  >= 64 (we use 124 — common spec floor)
constexpr std::uint32_t kVerticesPerMeshlet  = 64;
constexpr std::uint32_t kTrianglesPerMeshlet = 124;

struct Meshlet
{
    std::uint32_t vertex_offset       { 0 };   ///< into vertex_indices[]
    std::uint32_t vertex_count        { 0 };
    std::uint32_t triangle_offset     { 0 };   ///< into triangle_indices[] (3 per tri)
    std::uint32_t triangle_count      { 0 };
    cd::math::Vec4f bounds_sphere     { 0, 0, 0, 0 };  // xyz=centre, w=radius
    cd::math::Vec4f cone_axis_cutoff  { 0, 0, 0, 0 };  // xyz=axis, w=cos cutoff
};

/// Per-mesh meshlet output. `vertex_indices` redirects meshlet-local
/// vertex slot to the source mesh vertex index. `triangle_indices`
/// stores 3 uint8 per triangle, each indexing into the meshlet's
/// vertex slot (0..63).
struct MeshletData
{
    std::vector<Meshlet>       meshlets;
    std::vector<std::uint32_t> vertex_indices;
    std::vector<std::uint8_t>  triangle_indices;  // 3 per triangle
};

/// Build meshlets from a flat triangle-list mesh. Greedy single-pass
/// clustering that respects the per-meshlet vertex + triangle caps;
/// this is the same shape Meshoptimizer's reference algorithm starts
/// with before its further cone / spatial optimisations.
[[nodiscard]] inline MeshletData
build_meshlets(std::span<const std::uint32_t> indices,
               std::span<const cd::math::Vec3f> positions)
{
    MeshletData out {};
    if (indices.empty() || positions.empty()) return out;
    Meshlet current {};
    std::vector<std::uint32_t> slot_to_global;  // current meshlet's local vertex slots
    slot_to_global.reserve(kVerticesPerMeshlet);

    auto flush = [&]() {
        if (current.triangle_count == 0) return;
        out.meshlets.push_back(current);
        current = Meshlet {};
        current.vertex_offset   = static_cast<std::uint32_t>(out.vertex_indices.size());
        current.triangle_offset = static_cast<std::uint32_t>(out.triangle_indices.size());
        slot_to_global.clear();
    };

    auto slot_for = [&](std::uint32_t global_index) -> std::int32_t {
        for (std::size_t i = 0; i < slot_to_global.size(); ++i)
            if (slot_to_global[i] == global_index)
                return static_cast<std::int32_t>(i);
        return -1;
    };

    for (std::size_t tri = 0; tri + 2 < indices.size(); tri += 3)
    {
        const std::array<std::uint32_t, 3> g {
            indices[tri], indices[tri + 1], indices[tri + 2] };
        // Figure out which of the three vertices are new in the
        // current meshlet's slot table.
        std::array<std::int32_t, 3> slots { -1, -1, -1 };
        std::uint32_t new_count = 0;
        for (std::size_t i = 0; i < 3; ++i)
        {
            slots[i] = slot_for(g[i]);
            if (slots[i] < 0) ++new_count;
        }
        // If adding this triangle would overflow either cap, flush.
        if (slot_to_global.size() + new_count > kVerticesPerMeshlet ||
            current.triangle_count + 1 > kTrianglesPerMeshlet)
        {
            flush();
            for (auto& s : slots) s = -1;
            new_count = 3;
        }
        // Allocate fresh slots for new vertices, then write the
        // triangle's per-vertex slot indices.
        for (std::size_t i = 0; i < 3; ++i)
        {
            if (slots[i] < 0)
            {
                slots[i] = static_cast<std::int32_t>(slot_to_global.size());
                slot_to_global.push_back(g[i]);
                out.vertex_indices.push_back(g[i]);
                current.vertex_count++;
            }
            out.triangle_indices.push_back(static_cast<std::uint8_t>(slots[i]));
        }
        current.triangle_count++;
    }
    flush();

    // Compute per-meshlet bounding sphere via Ritter's approximation.
    for (auto& m : out.meshlets)
    {
        if (m.vertex_count == 0) continue;
        const auto& first = positions[out.vertex_indices[m.vertex_offset]];
        cd::math::Vec3f mn = first;
        cd::math::Vec3f mx = first;
        for (std::uint32_t i = 0; i < m.vertex_count; ++i)
        {
            const auto& p = positions[out.vertex_indices[m.vertex_offset + i]];
            mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
            mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
        }
        const cd::math::Vec3f centre {
            (mn.x + mx.x) * 0.5F,
            (mn.y + mx.y) * 0.5F,
            (mn.z + mx.z) * 0.5F };
        float r2 = 0.0F;
        for (std::uint32_t i = 0; i < m.vertex_count; ++i)
        {
            const auto& p = positions[out.vertex_indices[m.vertex_offset + i]];
            const float dx = p.x - centre.x;
            const float dy = p.y - centre.y;
            const float dz = p.z - centre.z;
            r2 = std::max(r2, dx * dx + dy * dy + dz * dz);
        }
        m.bounds_sphere = { centre.x, centre.y, centre.z, std::sqrt(r2) };
    }
    return out;
}

/// Host mirror of the task-shader backface-cone cull predicate (Karis 2021
/// Nanite normal-cone test). A meshlet is culled when the view direction —
/// from the camera toward the cluster's bounding-sphere centre — points
/// sufficiently *against* the cluster's normal-cone axis, i.e. every triangle
/// in the meshlet faces away from the eye. This is byte-for-byte the same test
/// the `kMeshletTaskGlsl` skeleton runs on the GPU:
///   `view = normalize(centre - cam); cull = dot(view, -axis) > cutoff`.
///
/// The default `cone_axis_cutoff == {0,0,0,0}` sentinel (greedy v1 fills no
/// cone — see ADR-20260616 §2.3) makes this a guaranteed no-op: `dot` is 0
/// and `0 > 0` is false, so an un-fitted meshlet is never culled (safe).
/// Returns true when the meshlet should be discarded.
[[nodiscard]] inline bool
cone_cull(const Meshlet& m, const cd::math::Vec3f& cam_pos) noexcept
{
    const cd::math::Vec3f centre {
        m.bounds_sphere.x, m.bounds_sphere.y, m.bounds_sphere.z };
    const cd::math::Vec3f to_centre { centre.x - cam_pos.x,
                                      centre.y - cam_pos.y,
                                      centre.z - cam_pos.z };
    const cd::math::Vec3f view = cd::math::normalize(to_centre);
    const cd::math::Vec3f neg_axis { -m.cone_axis_cutoff.x,
                                     -m.cone_axis_cutoff.y,
                                     -m.cone_axis_cutoff.z };
    return cd::math::dot(view, neg_axis) > m.cone_axis_cutoff.w;
}

// ---- GLSL task / mesh shader skeleton ---------------------------------------

constexpr std::string_view kMeshletTaskGlsl = R"glsl(
#version 460
#extension GL_EXT_mesh_shader : require
layout(local_size_x = 32) in;
struct Meshlet {
  vec4 bounds_sphere;
  vec4 cone_axis_cutoff;
  uint vertex_offset, vertex_count, triangle_offset, triangle_count;
};
layout(set = 0, binding = 0) readonly buffer MeshletBuf { Meshlet meshlets[]; } M;
layout(push_constant) uniform PC { mat4 mvp; vec3 cam_pos; uint meshlet_count; } pc;
taskPayloadSharedEXT struct { uint id; } payload;
void main() {
  uint mi = gl_GlobalInvocationID.x;
  if (mi >= pc.meshlet_count) return;
  Meshlet m = M.meshlets[mi];
  // Backface-cone cull: if dot(view, -cone_axis) > cutoff, all
  // triangles in the meshlet face away from the camera.
  vec3 view = normalize(m.bounds_sphere.xyz - pc.cam_pos);
  if (dot(view, -m.cone_axis_cutoff.xyz) > m.cone_axis_cutoff.w) return;
  payload.id = mi;
  EmitMeshTasksEXT(1, 1, 1);
}
)glsl";

constexpr std::string_view kMeshletMeshGlsl = R"glsl(
#version 460
#extension GL_EXT_mesh_shader : require
layout(local_size_x = 32) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;
layout(set = 0, binding = 1) readonly buffer VBuf  { vec3 v[]; } V;
layout(set = 0, binding = 2) readonly buffer IBuf  { uint i[]; } I;
layout(set = 0, binding = 3) readonly buffer TBuf  { uint t[]; } T;  // packed uint8 triplets in uint32
layout(push_constant) uniform PC { mat4 mvp; vec3 cam_pos; uint meshlet_count; } pc;
struct Meshlet {
  vec4 bounds_sphere; vec4 cone_axis_cutoff;
  uint vertex_offset, vertex_count, triangle_offset, triangle_count;
};
layout(set = 0, binding = 0) readonly buffer MeshletBuf { Meshlet meshlets[]; } M;
taskPayloadSharedEXT struct { uint id; } payload;
void main() {
  Meshlet m = M.meshlets[payload.id];
  SetMeshOutputsEXT(m.vertex_count, m.triangle_count);
  if (gl_LocalInvocationIndex < m.vertex_count) {
    uint g = I.i[m.vertex_offset + gl_LocalInvocationIndex];
    gl_MeshVerticesEXT[gl_LocalInvocationIndex].gl_Position = pc.mvp * vec4(V.v[g], 1.0);
  }
  if (gl_LocalInvocationIndex < m.triangle_count) {
    uint packed = T.t[(m.triangle_offset + gl_LocalInvocationIndex * 3) / 4];
    uint shift = ((m.triangle_offset + gl_LocalInvocationIndex * 3) % 4) * 8;
    gl_PrimitiveTriangleIndicesEXT[gl_LocalInvocationIndex] = uvec3(
      (packed >> shift)        & 0xFFu,
      (packed >> (shift + 8))  & 0xFFu,
      (packed >> (shift + 16)) & 0xFFu);
  }
}
)glsl";

}  // namespace cd::mesh_shader

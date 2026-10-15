// =============================================================================
// CHROMODYNAMIC — cd/scene_renderer/GpuDrivenPass.hpp
// FAZ 4 Wave 4.1 — GPU-Driven Geometry, Frustum + Hi-Z Occlusion Culling,
// DrawIndexedIndirectCount Batching, Meshlet DAG LOD Cut & Texture Streaming.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/framegraph/FrameGraph.hpp>
#include <cd/rhi/Handles.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cd::scene_renderer
{

/// Standard Vulkan / D3D12 `VkDrawIndexedIndirectCommand` layout (20 bytes).
struct DrawIndexedIndirectCmd
{
    std::uint32_t index_count { 0 };
    std::uint32_t instance_count { 1 };
    std::uint32_t first_index { 0 };
    std::int32_t  vertex_offset { 0 };
    std::uint32_t first_instance { 0 };
};

/// Per-instance bounding sphere and LOD metadata fed into the GPU/CPU-mirror
/// culling & indirect batching pipeline.
struct CullableInstance
{
    float center[3] { 0.0F, 0.0F, 0.0F };
    float radius { 1.0F };
    std::uint32_t index_count { 0 };
    std::uint32_t first_index { 0 };
    std::int32_t  vertex_offset { 0 };
    std::uint32_t instance_id { 0 };
    float         min_depth_ndc { 0.0F };  ///< Closest NDC depth in [0, 1]
};

/// 6-plane frustum in world space (`nx*x + ny*y + nz*z + d >= 0` is inside)
/// plus optional Hi-Z occlusion depth pyramid summary.
struct GpuCullingConfig
{
    /// 6 planes: left, right, bottom, top, near, far; each `(nx, ny, nz, d)`.
    std::array<std::array<float, 4>, 6> frustum_planes {{
        {  1.0F,  0.0F,  0.0F, 1000.0F },
        { -1.0F,  0.0F,  0.0F, 1000.0F },
        {  0.0F,  1.0F,  0.0F, 1000.0F },
        {  0.0F, -1.0F,  0.0F, 1000.0F },
        {  0.0F,  0.0F,  1.0F, 1000.0F },
        {  0.0F,  0.0F, -1.0F, 1000.0F },
    }};
    bool enable_frustum_culling { true };
    bool enable_hiz_occlusion { true };
    /// Conservative furthest depth from the Hi-Z pyramid mip for the covered tile.
    float hiz_furthest_depth_ndc { 1.0F };
};

struct GpuCullingResult
{
    std::vector<DrawIndexedIndirectCmd> commands;
    std::uint32_t draw_count { 0 };
    std::uint32_t frustum_culled_count { 0 };
    std::uint32_t hiz_culled_count { 0 };
};

/// Execute Frustum + Hi-Z occlusion culling and emit compacted
/// `DrawIndexedIndirectCmd` records + `draw_count` (`DrawIndexedIndirectCount`).
[[nodiscard]] GpuCullingResult cull_and_batch_indirect(
    std::span<const CullableInstance> instances,
    const GpuCullingConfig& config
);

/// Virtual-geometry (Nanite-style) meshlet DAG node descriptor for LOD cut evaluation.
struct MeshletDagNode
{
    std::uint32_t meshlet_id { 0 };
    std::uint32_t triangle_count { 64 };
    float         lod_error_meters { 0.01F };
    float         parent_error_meters { 0.05F };
    float         distance_meters { 10.0F };
};

struct MeshletCutStats
{
    std::vector<std::uint32_t> selected_meshlets;
    std::uint32_t total_triangles { 0 };
    std::uint32_t task_shader_groups { 0 };  ///< 32 meshlets per task workgroup
};

/// Select the meshlet DAG cut where `projected_error <= error_threshold_px < projected_parent_error`.
[[nodiscard]] MeshletCutStats evaluate_meshlet_lod_cut(
    std::span<const MeshletDagNode> dag_nodes,
    float focal_length_px,
    float error_threshold_px = 1.0F
);

}  // namespace cd::scene_renderer

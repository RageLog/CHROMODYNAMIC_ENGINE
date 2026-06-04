// =============================================================================
// CHROMODYNAMIC — cd/render/lighting_clusters/DispatchPass.hpp
// phase672 — Sprint-2: GPU compute light-culling pass.
//
// Forward+ / clustered-forward Sprint-2: dispatch a compute shader that does
// the same per-cluster AABB-vs-sphere assignment the CPU Clusterer (Sprint-1)
// does, on the GPU. One thread per cluster walks the light array and writes
// the per-cluster (offset, count) pair plus the per-cluster light-index slab.
//
// MOMENT: a scene with 500 point lights renders at 60 fps because the GPU
// culls per-cluster in microseconds and each pixel queries only ~8 lights
// from its cluster (vs. n^2 CPU lookup).
//
// Parity with the CPU Clusterer: given the same ClusterGrid + light list
// the two paths produce the same (cluster -> light index set) assignment.
// The GPU path uses a fixed slab capacity per cluster
// (kMaxLightsPerCluster, default 64) so the layout is fully predictable
// for the consumer shader -- a parallel-prefix-sum compaction is a Sprint-3+
// optimization.
//
// Buffer layout (std430, written by kClusterCullCS):
//
//   * cluster_table SSBO: array<ClusterEntry, total_clusters>
//        ClusterEntry { uint offset; uint count; }
//        - offset = cluster_idx * kMaxLightsPerCluster  (where the slab begins
//          inside the light_indices SSBO).
//        - count  = number of overlapping lights (0 <= count <= kMaxLightsPerCluster).
//
//   * light_indices SSBO: array<uint, total_clusters * kMaxLightsPerCluster>
//        flat slab; indices [offset, offset + count) carry light IDs.
//
//   * lights SSBO (input): array<GpuPointLight, light_count>
//        GpuPointLight { vec4 position_radius; vec4 color_intensity; }
//
// Library-level only. Does NOT touch hello_engine (sample is FROZEN).
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>
#include <cd/render/lighting_clusters/LightingClusters.hpp>
#include <cd/rhi/Handles.hpp>

#include <cstdint>
#include <span>

namespace cd::rhi
{
class IDevice;
class ICommandBuffer;
}  // namespace cd::rhi

namespace cd::render::lighting_clusters
{

/// Maximum number of light indices a single cluster slab can hold. Fixed at
/// allocation time so the consumer shader can index the light_indices SSBO
/// without a per-cluster offset query. A typical Forward+ scene assigns
/// ~5-20 lights per cluster -- 64 leaves comfortable headroom while keeping
/// the per-cluster slab fits within an L1 cache line on the consumer side.
inline constexpr std::uint32_t kMaxLightsPerCluster = 64U;

/// std430-compatible per-cluster table entry. Matches the GLSL declaration
/// in kClusterCullCS byte-for-byte (2 x 4B uint32s).
struct ClusterEntry
{
    std::uint32_t offset;   ///< Index into light_indices SSBO where the slab starts.
    std::uint32_t count;    ///< Number of lights overlapping this cluster.
};
static_assert(sizeof(ClusterEntry) == 8U,
              "ClusterEntry must match the std430 layout in kClusterCullCS");

/// std430-compatible point-light layout matching the kClusterCullCS GLSL.
/// Two vec4s: (position.xyz, radius) + (color.rgb, intensity).
struct alignas(16) GpuPointLight
{
    float position[3];
    float radius;
    float color[3];
    float intensity;
};
static_assert(sizeof(GpuPointLight) == 32U,
              "GpuPointLight must match the std430 layout in kClusterCullCS");

/// GPU compute pass that runs the cluster-vs-light assignment on device.
///
/// Lifecycle:
///   * configure() -> store the grid; idempotent.
///   * prepare(device) -> compile shader + allocate SSBOs + descriptor set.
///   * execute_culling(cmd, lights) -> upload lights + dispatch + write
///     cluster_table + light_indices.
///   * shutdown() -> free GPU resources. Idempotent.
///
/// Single-device, single-threaded. Non-copyable, non-movable so the
/// destructor's RAII ordering is unambiguous.
class DispatchPass
{
public:
    DispatchPass() noexcept = default;
    ~DispatchPass();

    DispatchPass(const DispatchPass&)            = delete;
    DispatchPass& operator=(const DispatchPass&) = delete;
    DispatchPass(DispatchPass&&)                 = delete;
    DispatchPass& operator=(DispatchPass&&)      = delete;

    /// Cache the grid configuration. Must be called before prepare();
    /// changing the grid after prepare() requires shutdown() + prepare()
    /// since the cluster_table size depends on total_cluster_count().
    void configure(const ClusterGrid& grid) noexcept;

    /// Compile kClusterCullCS, build the descriptor-set / pipeline layouts +
    /// compute pipeline, and allocate the three SSBOs:
    ///   * lights         (host-visible, kStorage + kTransferDst)
    ///   * cluster_table  (host-visible, kStorage + kTransferSrc)
    ///   * light_indices  (host-visible, kStorage + kTransferSrc)
    ///
    /// `max_lights` sizes the lights SSBO -- subsequent execute_culling()
    /// calls must pass a span no larger than this. Returns kInvalidArgument
    /// when the grid has zero clusters or max_lights == 0, kBackendInitFailed
    /// when glslang is unavailable, or any RHI error verbatim.
    [[nodiscard]] cd::core::Result<void>
    prepare(cd::rhi::IDevice& device, std::uint32_t max_lights);

    /// Upload the `lights` span into the lights SSBO and record a dispatch
    /// of ceil(x_tiles/8) * ceil(y_tiles/8) * ceil(z_slices/8) groups into
    /// `cmd`. The compute shader writes the cluster_table + light_indices
    /// SSBOs in-place.
    ///
    /// `cmd` must be inside a begin() / end() pair. The caller is responsible
    /// for any matching pipeline barriers between this dispatch and downstream
    /// readers of cluster_table / light_indices.
    ///
    /// Returns kInvalidArgument when prepare() has not run, when lights.size()
    /// exceeds the max_lights passed to prepare(), or when the host-side
    /// lights upload fails. {} on success.
    [[nodiscard]] cd::core::Result<void>
    execute_culling(cd::rhi::ICommandBuffer& cmd,
                    std::span<const PointLight> lights);

    /// Free every GPU resource owned by the pass. Idempotent; called by the
    /// destructor.
    void shutdown() noexcept;

    /// True once prepare() succeeded and shutdown() has not been called.
    [[nodiscard]] bool is_ready() const noexcept { return ready_; }

    [[nodiscard]] const ClusterGrid& grid() const noexcept { return grid_; }

    [[nodiscard]] std::uint32_t total_cluster_count() const noexcept
    {
        return grid_.x_tiles * grid_.y_tiles * grid_.z_slices;
    }

    [[nodiscard]] std::uint32_t max_lights() const noexcept { return max_lights_; }

    // ---- Buffer accessors -------------------------------------------------

    /// SSBO that the GLSL kernel reads as `GpuPointLight lights[]`.
    [[nodiscard]] cd::rhi::BufferHandle lights_buffer() const noexcept { return lights_buffer_; }

    /// SSBO that the GLSL kernel writes as `ClusterEntry cluster_table[]`.
    /// Each entry stores (offset, count) for one cluster.
    [[nodiscard]] cd::rhi::BufferHandle cluster_table_buffer() const noexcept { return cluster_table_buffer_; }

    /// SSBO that the GLSL kernel writes as `uint light_indices[]`. Flat slab
    /// of total_clusters * kMaxLightsPerCluster uint32s; cluster k's indices
    /// live at [k * kMaxLightsPerCluster, k * kMaxLightsPerCluster + count_k).
    [[nodiscard]] cd::rhi::BufferHandle light_indices_buffer() const noexcept { return light_indices_buffer_; }

    // ---- Pipeline accessors (test introspection) --------------------------

    [[nodiscard]] cd::rhi::ComputePipelineHandle pipeline() const noexcept { return pipeline_; }
    [[nodiscard]] cd::rhi::PipelineLayoutHandle  pipeline_layout() const noexcept { return pipeline_layout_; }
    [[nodiscard]] cd::rhi::DescriptorSetHandle   descriptor_set() const noexcept { return descriptor_set_; }

    // ---- Static helpers ---------------------------------------------------

    /// Workgroup dispatch sizes -- ceil(dim / 8) for each axis. The GLSL
    /// kernel uses local_size = (8, 8, 8), so a 16x9x24 grid becomes
    /// (2, 2, 3) workgroups (with the kernel bounds-checking the trailing
    /// invocations against the actual grid dims via push-constants).
    [[nodiscard]] static constexpr std::uint32_t group_count(std::uint32_t dim) noexcept
    {
        constexpr std::uint32_t kLocalSize = 8U;
        return (dim + kLocalSize - 1U) / kLocalSize;
    }

    /// Total size in bytes of the cluster_table SSBO for a given cluster count.
    [[nodiscard]] static std::uint64_t cluster_table_size(std::uint32_t cluster_count) noexcept
    {
        return static_cast<std::uint64_t>(cluster_count) * sizeof(ClusterEntry);
    }

    /// Total size in bytes of the light_indices SSBO for a given cluster count.
    [[nodiscard]] static std::uint64_t light_indices_size(std::uint32_t cluster_count) noexcept
    {
        return static_cast<std::uint64_t>(cluster_count)
             * static_cast<std::uint64_t>(kMaxLightsPerCluster)
             * sizeof(std::uint32_t);
    }

private:
    cd::rhi::IDevice*                  device_ { nullptr };
    ClusterGrid                        grid_   {};
    std::uint32_t                      max_lights_ { 0 };
    bool                               ready_  { false };

    cd::rhi::ShaderModuleHandle        shader_module_   {};
    cd::rhi::DescriptorSetLayoutHandle dsl_             {};
    cd::rhi::PipelineLayoutHandle      pipeline_layout_ {};
    cd::rhi::ComputePipelineHandle     pipeline_        {};
    cd::rhi::DescriptorSetHandle       descriptor_set_  {};

    cd::rhi::BufferHandle              lights_buffer_        {};
    cd::rhi::BufferHandle              cluster_table_buffer_ {};
    cd::rhi::BufferHandle              light_indices_buffer_ {};
};

}  // namespace cd::render::lighting_clusters

// =============================================================================
// CHROMODYNAMIC — cd/cluster/gpu/GpuPipeline.hpp
// Phase 9 / Sprint 2 / Wave 101 — Vulkan compute pipeline wrapper for
// cluster_assign.comp.
//
// Drives the GPU side of the Forward+ light culling algorithm whose
// CPU baseline (cd::render::cluster::ClusterGrid) and parity-tested
// reference simulator (cd::render::cluster::run_reference_compute)
// landed in Phase 7 / Sprint 9 + Phase 8 / Sprint 10.
//
// Two-pass dispatch:
//   PASS 0 (PHASE = 0)  — compute shader counts overlapping lights
//                         per cluster → cluster_counts buffer.
//   CPU prefix-sum      — host reads counts, computes offsets, writes
//                         back to cluster_offsets buffer.
//   PASS 1 (PHASE = 1)  — compute shader writes light indices into
//                         light_indices at cluster_offsets slots.
//
// Buffer layout (std430, matching the GLSL):
//   binding 0: InLights        (uint count + LightData[])  — read-only
//   binding 1: ClusterCounts   (uint[])                    — read-write
//   binding 2: ClusterOffsets  (uint[])                    — read-only
//   binding 3: LightIndices    (uint[])                    — read-write
//
// Push constants: ClusterConfig fields + a phase flag (0 = count,
// 1 = write).
//
// Backend-agnostic — uses cd::rhi::IDevice. Works on any backend that
// supports compute pipelines + storage buffers (Vulkan today, Metal /
// D3D12 when Phase 9 Sprint 4-5 land).
//
// Build-validation guarantee: this header + the .cpp compile cleanly
// even when the host has no Vulkan ICD. Runtime success depends on
// IDevice availability; consumers gate on
// `create_vulkan_device().has_value()` (or equivalent for other
// backends).
// =============================================================================
#pragma once

#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/render/cluster/ClusterGrid.hpp>
#include <cd/render/cluster/ReferenceCompute.hpp>
#include <cd/rhi/IDevice.hpp>

#include <cstdint>
#include <memory>
#include <span>

namespace cd::cluster::gpu
{

namespace cluster_gpu_errors
{
inline constexpr std::uint32_t kDomain = 0x001D;

enum class Code : std::uint32_t
{
    kOk = 0,
    kShaderCompileFailed = 1,
    kPipelineCreationFailed = 2,
    kBufferCreationFailed = 3,
    kBufferUploadFailed = 4,
    kBufferReadbackFailed = 5,
    kInvalidArgument = 6,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace cluster_gpu_errors

class GpuPipeline
{
public:
    /// Construct from a live IDevice. Compiles the embedded
    /// cluster_assign.comp source, creates the descriptor-set layout +
    /// pipeline-layout + compute pipeline, and pre-allocates the four
    /// storage buffers sized for `max_lights` and the grid in `cfg`.
    ///
    /// Subsequent dispatch() calls assume the same config + max
    /// budget. Pass a recreate() call before bumping either.
    [[nodiscard]] static cd::core::Result<std::unique_ptr<GpuPipeline>>
    create(cd::rhi::IDevice& device,
           const cd::render::cluster::ClusterConfig& cfg,
           std::uint32_t max_lights);

    ~GpuPipeline();
    GpuPipeline(const GpuPipeline&) = delete;
    GpuPipeline& operator=(const GpuPipeline&) = delete;
    GpuPipeline(GpuPipeline&&) = delete;
    GpuPipeline& operator=(GpuPipeline&&) = delete;

    /// End-to-end dispatch + readback: upload lights, dispatch pass 0,
    /// prefix-sum CPU-side, dispatch pass 1, readback all output
    /// buffers into a ReferenceOutput that callers can compare bit-
    /// for-bit against the CPU reference simulator.
    [[nodiscard]] cd::core::Result<cd::render::cluster::ReferenceOutput>
    run(std::span<const cd::render::cluster::LightSphere> lights);

    [[nodiscard]] const cd::render::cluster::ClusterConfig& config() const noexcept;
    [[nodiscard]] std::uint32_t max_lights() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    explicit GpuPipeline(std::unique_ptr<Impl> impl) noexcept;
};

}  // namespace cd::cluster::gpu

// =============================================================================
// CHROMODYNAMIC — cd/restir_di/DispatchPass.hpp
// Phase 550 / Sprint-1 — ReSTIR DI initial-candidate dispatch (library level).
// Phase 561 / Sprint-2 — temporal + spatial reuse passes (library level).
//
// Standalone GPU pass that runs the ReSTIR DI compute kernels from
// Reservoir.hpp over the viewport. The pass owns four reservoir SSBOs that
// chain through the three sub-dispatches (sample -> temporal -> spatial):
//
//   * `reservoir_buffer()`           - current-frame initial candidates;
//                                      written by `record()` (Sprint-1).
//   * `previous_reservoir_buffer()`  - previous-frame survivor; the GPU-side
//                                      mirror of `cd::restir_di::TemporalBuffer
//                                      ::previous()` from Reservoir.hpp /
//                                      TemporalBuffer.hpp. The application
//                                      is responsible for ping-ponging the
//                                      contents (e.g. swap with the spatial
//                                      output every frame) before the next
//                                      temporal_reuse dispatch.
//   * `temporal_reservoir_buffer()`  - temporally-blended output of
//                                      `execute_temporal_reuse()`; consumed
//                                      as input by the spatial reuse pass.
//   * `spatial_reservoir_buffer()`   - final per-pixel survivor written by
//                                      `execute_spatial_reuse()`; intended
//                                      consumer is the denoiser (deferred).
//
// Sprint-1 scope (already shipped):
//   * compile kRestirDiSampleCS (GLSL -> SPIR-V via cd::shader::ICompiler)
//   * create shader module + descriptor set layout + pipeline layout +
//     compute pipeline + reservoir storage buffer
//   * record (bind pipeline / dispatch ceil(w/16) x ceil(h/16) x 1)
//   * teardown all GPU resources
//
// Sprint-2 scope (this revision):
//   * compile stripped kRestirDiTemporalReuseCS + kRestirDiSpatialReuseCS
//     variants (SSBO-only -- the motion-vector / G-buffer normal texture
//     bindings are deferred until the framegraph G-buffer ABI lands) so the
//     dispatches stay validation-clean on any Vulkan ICD without scene
//     state. Same reservoir byte layout as production so Sprint-3+ can swap
//     in the textured kernels without re-laying-out the SSBO.
//   * allocate the previous + temporal + spatial reservoir SSBOs alongside
//     the Sprint-1 current-frame SSBO.
//   * `execute_temporal_reuse(cb, frame_index)` -- bind temporal pipeline +
//     dispatch ceil(w/8) x ceil(h/8) x 1 (8x8 group size in the GLSL).
//   * `execute_spatial_reuse(cb, frame_index)` -- bind spatial pipeline +
//     dispatch ceil(w/8) x ceil(h/8) x 1.
//
// Sprint-3+ will reinstate motion-vector + G-buffer normal bindings (after
// the framegraph G-buffer ABI is finalised) and bind the visibility-ray
// shadow test against the scene TLAS. Denoiser integration is deferred.
//
// All RHI calls go through `cd::rhi::IDevice` — no backend-specific symbols
// leak through this header. Consumers link against `cd::restir_di` directly.
//
// hello_engine is *not* wired in this sprint; the pass is exercised only by
// tests/test_restir_di_dispatch.cpp which skips when no Vulkan ICD is
// installed (so CI machines without a GPU stay green).
// =============================================================================
#pragma once

#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/rhi/Handles.hpp>

#include <cstdint>
#include <memory>

namespace cd::rhi
{
class IDevice;
class ICommandBuffer;
}  // namespace cd::rhi

namespace cd::restir_di
{

/// Per-pixel reservoir as laid out on the GPU side. Mirrors the GLSL
/// `DiReservoir` struct in `kRestirDiSampleCS` byte-for-byte so the
/// allocation math is shared between host and device.
struct GpuReservoir
{
    std::uint32_t light_index;   ///< DiSample::light_index
    float         target_pdf;    ///< DiSample::target_pdf
    float         radiance_x;    ///< DiSample::radiance.x
    float         radiance_y;    ///< DiSample::radiance.y
    float         radiance_z;    ///< DiSample::radiance.z
    float         weight_sum;    ///< running WRS weight total
    std::uint32_t M;             ///< stream count
    std::uint32_t age;           ///< frames carried forward without a refresh
};

/// Configuration handed to `DispatchPass::prepare`. All required up-front so
/// that the pass can allocate device resources once and reuse them across
/// frames.
struct DispatchConfig
{
    std::uint32_t viewport_width  { 0 };
    std::uint32_t viewport_height { 0 };

    /// Total scene light count visible to the dispatch shader. Used only
    /// for the reservoir layout / debugging hand-off; the actual count is
    /// re-pushed every frame through the push-constant block (Sprint-2).
    std::uint32_t light_count { 0 };

    /// Number of initial candidates streamed through the WRS per pixel
    /// (Bitterli 2020 calls this `M_initial`, typical 32). Recorded into
    /// the pass for the upcoming push-constant wire-up; not consumed yet.
    std::uint32_t candidates { 32 };
};

/// Owning GPU-side ReSTIR DI initial-candidate pass. Single-device,
/// single-thread; non-copyable, non-movable so the destructor's RAII
/// ordering is unambiguous.
class DispatchPass
{
public:
    DispatchPass() = default;
    ~DispatchPass();

    DispatchPass(const DispatchPass&)            = delete;
    DispatchPass& operator=(const DispatchPass&) = delete;
    DispatchPass(DispatchPass&&)                 = delete;
    DispatchPass& operator=(DispatchPass&&)      = delete;

    /// Compile the sample shader, build the layouts + pipeline, and allocate
    /// the reservoir storage buffer sized to
    /// `viewport_width * viewport_height * sizeof(GpuReservoir)`.
    /// Returns kInvalidArgument when the viewport is zero, kCompileFailed
    /// when glslang is unavailable, or any other RHI error verbatim.
    [[nodiscard]] cd::core::Result<void>
    prepare(cd::rhi::IDevice& device, const DispatchConfig& cfg);

    /// Record the dispatch into the supplied command buffer. The caller is
    /// responsible for surrounding barriers (the pass writes the storage
    /// buffer; downstream readers must emit a kShaderResource->
    /// kShaderResource or kStorage->kShaderResource barrier as needed).
    /// No-op if `prepare()` has not been called successfully.
    void record(cd::rhi::ICommandBuffer& cb) const;

    /// Phase 561 / Sprint-2 -- Temporal reuse pass.
    ///
    /// Bind the temporal pipeline + descriptor set (current-frame reservoir
    /// SSBO @ binding=0, previous-frame reservoir SSBO @ binding=1,
    /// temporally-blended output SSBO @ binding=2) and dispatch
    /// ceil(viewport_w/8) x ceil(viewport_h/8) x 1.
    ///
    /// The caller is responsible for any barriers needed between this
    /// dispatch and the preceding `record()` (sample) call -- typically a
    /// kStorageBuffer storage-buffer barrier on the current-frame
    /// reservoir buffer. The pass writes
    /// `temporal_reservoir_buffer()`; downstream consumers
    /// (`execute_spatial_reuse()` or denoiser) must observe the same
    /// kStorage->kStorage or kStorage->kShaderResource barrier discipline.
    ///
    /// No-op if `prepare()` has not been called successfully.
    /// `frame_index` is forwarded to the shader's push-constant block so
    /// the on-GPU PCG seed varies frame-to-frame.
    void execute_temporal_reuse(cd::rhi::ICommandBuffer& cb,
                                std::uint32_t frame_index) const;

    /// Phase 561 / Sprint-2 -- Spatial reuse pass (5-tap disc kernel).
    ///
    /// Bind the spatial pipeline + descriptor set (temporal output SSBO
    /// @ binding=0, spatial output SSBO @ binding=1) and dispatch
    /// ceil(viewport_w/8) x ceil(viewport_h/8) x 1.
    ///
    /// The caller is responsible for emitting a storage-buffer barrier
    /// between the preceding `execute_temporal_reuse()` and this
    /// dispatch. The pass writes `spatial_reservoir_buffer()`; downstream
    /// readers (denoiser, integrator) must observe the matching
    /// kStorage->kShaderResource barrier.
    ///
    /// No-op if `prepare()` has not been called successfully.
    /// `frame_index` is forwarded to the shader's PCG seed.
    void execute_spatial_reuse(cd::rhi::ICommandBuffer& cb,
                               std::uint32_t frame_index) const;

    /// Tear down all device-owned resources. Idempotent. Called automatically
    /// by the destructor.
    void shutdown();

    /// True once a successful `prepare()` has produced a usable pipeline.
    [[nodiscard]] bool is_ready() const noexcept { return ready_; }

    /// Reservoir SSBO handle. Sprint-2 temporal-reuse pass binds this as
    /// its "current" reservoir source. Returns the null handle until prepare.
    [[nodiscard]] cd::rhi::BufferHandle reservoir_buffer() const noexcept { return reservoir_buffer_; }

    /// Phase 561 / Sprint-2 -- previous-frame reservoir SSBO. GPU-side mirror
    /// of `cd::restir_di::TemporalBuffer::previous()` (the CPU reference is
    /// in TemporalBuffer.hpp). Bound at temporal-reuse binding=1. Returns
    /// the null handle until prepare.
    [[nodiscard]] cd::rhi::BufferHandle previous_reservoir_buffer() const noexcept { return previous_reservoir_buffer_; }

    /// Phase 561 / Sprint-2 -- temporal-reuse output SSBO. Written by
    /// `execute_temporal_reuse()`, read by `execute_spatial_reuse()`.
    /// Returns the null handle until prepare.
    [[nodiscard]] cd::rhi::BufferHandle temporal_reservoir_buffer() const noexcept { return temporal_reservoir_buffer_; }

    /// Phase 561 / Sprint-2 -- spatial-reuse output SSBO. Written by
    /// `execute_spatial_reuse()`. The eventual denoiser/integrator reads
    /// this as the final per-pixel survivor reservoir. Returns the null
    /// handle until prepare.
    [[nodiscard]] cd::rhi::BufferHandle spatial_reservoir_buffer() const noexcept { return spatial_reservoir_buffer_; }

    /// Phase 561 / Sprint-2 -- group-count math for the reuse passes. The
    /// reuse shaders use an 8x8 work-group (vs. the Sprint-1 sample shader's
    /// 16x16), so the ceiling divisor differs.
    [[nodiscard]] static std::uint32_t reuse_group_count_x(std::uint32_t viewport_w) noexcept;
    [[nodiscard]] static std::uint32_t reuse_group_count_y(std::uint32_t viewport_h) noexcept;

    /// Group-count math exposed so tests + downstream callers can reason
    /// about the dispatch shape without reaching into the implementation.
    /// Matches the Sprint-1 spec: `viewport_w / 16` rounded up.
    [[nodiscard]] static std::uint32_t group_count_x(std::uint32_t viewport_w) noexcept;
    [[nodiscard]] static std::uint32_t group_count_y(std::uint32_t viewport_h) noexcept;

    /// Reservoir byte count for a given viewport size (host-side helper used
    /// by both the pass and its tests).
    [[nodiscard]] static std::uint64_t reservoir_buffer_size(std::uint32_t w, std::uint32_t h) noexcept;

private:
    cd::rhi::IDevice*                  device_ { nullptr };
    DispatchConfig                     cfg_ {};

    // ---- Sprint-1: sample pass --------------------------------------------
    cd::rhi::BufferHandle              reservoir_buffer_ {};
    cd::rhi::ShaderModuleHandle        shader_module_ {};
    cd::rhi::DescriptorSetLayoutHandle dsl_ {};
    cd::rhi::PipelineLayoutHandle      pipeline_layout_ {};
    cd::rhi::ComputePipelineHandle     pipeline_ {};
    cd::rhi::DescriptorSetHandle       descriptor_set_ {};

    // ---- Sprint-2: temporal reuse pass ------------------------------------
    cd::rhi::BufferHandle              previous_reservoir_buffer_ {};
    cd::rhi::BufferHandle              temporal_reservoir_buffer_ {};
    cd::rhi::ShaderModuleHandle        temporal_shader_module_ {};
    cd::rhi::DescriptorSetLayoutHandle temporal_dsl_ {};
    cd::rhi::PipelineLayoutHandle      temporal_pipeline_layout_ {};
    cd::rhi::ComputePipelineHandle     temporal_pipeline_ {};
    cd::rhi::DescriptorSetHandle       temporal_descriptor_set_ {};

    // ---- Sprint-2: spatial reuse pass -------------------------------------
    cd::rhi::BufferHandle              spatial_reservoir_buffer_ {};
    cd::rhi::ShaderModuleHandle        spatial_shader_module_ {};
    cd::rhi::DescriptorSetLayoutHandle spatial_dsl_ {};
    cd::rhi::PipelineLayoutHandle      spatial_pipeline_layout_ {};
    cd::rhi::ComputePipelineHandle     spatial_pipeline_ {};
    cd::rhi::DescriptorSetHandle       spatial_descriptor_set_ {};

    bool                               ready_ { false };
};

}  // namespace cd::restir_di

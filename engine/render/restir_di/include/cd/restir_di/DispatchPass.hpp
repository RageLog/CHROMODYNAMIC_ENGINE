// =============================================================================
// CHROMODYNAMIC — cd/restir_di/DispatchPass.hpp
// Phase 550 / Sprint-1 — ReSTIR DI initial-candidate dispatch (library level).
//
// Standalone GPU pass that runs the `kRestirDiSampleCS` compute kernel from
// Reservoir.hpp over the viewport, populating a per-pixel reservoir SSBO.
//
// Sprint-1 scope (this file):
//   * compile kRestirDiSampleCS (GLSL → SPIR-V via cd::shader::ICompiler)
//   * create shader module + descriptor set layout + pipeline layout +
//     compute pipeline + reservoir storage buffer
//   * record (bind pipeline / dispatch ceil(w/16) × ceil(h/16) × 1)
//   * teardown all GPU resources
//
// Sprint-2 will add `temporal_reuse` + `spatial_reuse` dispatch helpers and
// the matching reservoir ping-pong buffer + motion-vector binding.
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
    /// buffer; downstream readers must emit a kShaderResource→
    /// kShaderResource or kStorage→kShaderResource barrier as needed).
    /// No-op if `prepare()` has not been called successfully.
    void record(cd::rhi::ICommandBuffer& cb) const;

    /// Tear down all device-owned resources. Idempotent. Called automatically
    /// by the destructor.
    void shutdown();

    /// True once a successful `prepare()` has produced a usable pipeline.
    [[nodiscard]] bool is_ready() const noexcept { return ready_; }

    /// Reservoir SSBO handle. Sprint-2 temporal-reuse pass will bind this as
    /// its "current" reservoir source. Returns the null handle until prepare.
    [[nodiscard]] cd::rhi::BufferHandle reservoir_buffer() const noexcept { return reservoir_buffer_; }

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
    cd::rhi::BufferHandle              reservoir_buffer_ {};
    cd::rhi::ShaderModuleHandle        shader_module_ {};
    cd::rhi::DescriptorSetLayoutHandle dsl_ {};
    cd::rhi::PipelineLayoutHandle      pipeline_layout_ {};
    cd::rhi::ComputePipelineHandle     pipeline_ {};
    cd::rhi::DescriptorSetHandle       descriptor_set_ {};
    bool                               ready_ { false };
};

}  // namespace cd::restir_di

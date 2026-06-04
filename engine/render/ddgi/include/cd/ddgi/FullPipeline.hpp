// =============================================================================
// CHROMODYNAMIC — cd/ddgi/FullPipeline.hpp
// phase680 — Sprint-5: full DDGI pipeline compose
//            (trace -> blend_irradiance + blend_visibility -> sample).
//
// FullPipeline composes the four GPU passes of Dynamic Diffuse Global
// Illumination (Majercik et al. 2019) into a single `execute()` entry point.
// A graphics dev calls execute() once per frame; FullPipeline records the
// trace pass, the two blend passes (which read the trace pass output and
// write the irradiance + visibility atlases) and the sample pass (which
// reads both atlases + the caller's G-buffer to produce per-pixel indirect
// irradiance) — in the correct order with the correct memory barriers.
//
// The class is a thin orchestrator on top of `cd::ddgi::DispatchPass`. It
// owns one DispatchPass instance + emits the barriers / dispatches in a
// single ICommandBuffer recording. The view-projection matrix is accepted
// for forward-compatibility (future variants that project probe centres
// for relocation / culling) but is not consumed by the current shader set —
// the trace pass already reconstructs world-space probe positions from the
// grid push-constants.
//
// MOMENT (Source 2 / HL2 / Alyx culture):
//   one call per frame -> full indirect bounce GI in the user's scene.
//
// Sprint-5 scope:
//   * init(device, desc)        — forwards to DispatchPass::init().
//   * bind_sample_resources(...) — forwards to DispatchPass::bind_sample_resources().
//   * execute(cmd, view_proj_matrix, scene_tlas, frame_index)
//       1. bind_tlas (if needed) into the trace pass.
//       2. dispatch trace.
//       3. memory barrier: ray_radiance + ray_dir_dist  kUAV -> kUAV.
//       4. dispatch blend_irradiance + blend_visibility (parallel-OK; same
//          TLAS read, disjoint atlas writes).
//       5. memory barrier: irradiance_atlas + visibility_atlas  kUAV -> kUAV.
//       6. dispatch sample.
//   * shutdown(device) — forwards to DispatchPass::shutdown().
//
// References:
//   Majercik, Marrs, Spjut, McGuire (2019) — JCGT 8:2.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/Result.hpp>
#include <cd/ddgi/DispatchPass.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/rhi/Handles.hpp>

#include <cstdint>

namespace cd::rhi { class IDevice;        }
namespace cd::rhi { class ICommandBuffer; }

namespace cd::ddgi
{

/// Single-entry-point orchestrator for the four-pass DDGI pipeline.
///
/// Internally owns one `DispatchPass`; `execute()` records the trace + two
/// blend passes + sample pass in order with the required memory barriers.
class CD_NODISCARD FullPipeline
{
public:
    FullPipeline() noexcept = default;
    ~FullPipeline() = default;
    FullPipeline(const FullPipeline&) = delete;
    FullPipeline& operator=(const FullPipeline&) = delete;
    FullPipeline(FullPipeline&&) noexcept = default;
    FullPipeline& operator=(FullPipeline&&) noexcept = default;

    /// Initialise the underlying `DispatchPass`. Returns the same set of
    /// errors as `DispatchPass::init()` — kBackendInitFailed when glslang
    /// is missing, kCompileFailed when one of the four DDGI shaders fails
    /// to compile, kResourceCreationFailed on any device-create failure.
    /// On failure every partial allocation is rolled back.
    [[nodiscard]] cd::core::Result<void>
    init(cd::rhi::IDevice& device, const DispatchPassDesc& desc);

    /// Free every GPU resource owned by the pipeline. Safe to call on an
    /// uninitialised instance.
    void shutdown(cd::rhi::IDevice& device) noexcept;

    /// Wire the sample-pass G-buffer + output bindings. Must be called once
    /// before the first `execute()`. The image views must reference RGBA16F
    /// (or compatible) storage-capable textures the caller has transitioned
    /// to kUnorderedAccess.
    [[nodiscard]] cd::core::Result<void>
    bind_sample_resources(cd::rhi::IDevice&          device,
                          cd::rhi::TextureViewHandle output_view,
                          cd::rhi::TextureViewHandle world_pos_view,
                          cd::rhi::TextureViewHandle world_normal_view,
                          std::uint32_t              output_width,
                          std::uint32_t              output_height);

    /// Record the four-pass DDGI pipeline into `cmd`. The caller is
    /// responsible for transitioning the trace + atlas + G-buffer + output
    /// images to kUnorderedAccess before this call; FullPipeline inserts
    /// the inter-pass memory barriers itself (trace -> blend, blend ->
    /// sample).
    ///
    /// Arguments:
    ///   * `cmd`         — open command buffer in record state.
    ///   * `view_proj`   — main-view projection matrix; reserved for future
    ///                     probe-relocation variants. Not consumed by the
    ///                     current shader set (the trace pass reconstructs
    ///                     world-space probe positions from the grid push-
    ///                     constants).
    ///   * `scene_tlas`  — acceleration structure read by the trace pass.
    ///                     Ignored when the underlying DispatchPass was
    ///                     initialised with `needs_tlas = false` (smoke
    ///                     / no-RT variant — see DispatchPass).
    ///   * `frame_index` — per-frame counter; propagates to the trace + two
    ///                     blend passes for temporal hysteresis + Fibonacci
    ///                     ray rotation.
    ///
    /// Returns:
    ///   * std::unexpected(kInvalidArgument) when init() has not run or
    ///     bind_sample_resources() has not been called.
    ///   * {} on success — every dispatch was recorded into `cmd`.
    [[nodiscard]] cd::core::Result<void>
    execute(cd::rhi::ICommandBuffer&     cmd,
            const cd::math::Mat4f&       view_proj,
            cd::rhi::AccelStructureHandle scene_tlas,
            std::uint32_t                frame_index);

    /// True iff init() succeeded and shutdown() has not been called since.
    [[nodiscard]] bool initialised() const noexcept { return pass_.initialised(); }

    /// Access the underlying DispatchPass for direct resource queries
    /// (ray images, atlases, descriptor sets, etc).
    [[nodiscard]] DispatchPass&       pass()       noexcept { return pass_; }
    [[nodiscard]] const DispatchPass& pass() const noexcept { return pass_; }

    /// Number of times `execute()` has recorded a full pipeline successfully.
    [[nodiscard]] std::uint32_t execute_call_count() const noexcept { return execute_call_count_; }

private:
    DispatchPass  pass_                {};
    std::uint32_t execute_call_count_  { 0 };
};

}  // namespace cd::ddgi

// =============================================================================
// CHROMODYNAMIC -- cd/restir_di/FullPipelineDenoised.hpp
// Phase 681 / Sprint-6 -- ReSTIR DI full denoised pipeline (chained).
//
// Sprint-1 (phase 550) shipped the initial-candidate dispatch (`record`),
// Sprint-2 (phase 561) added the temporal + spatial reuse passes,
// Sprint-3 (phase 571) added the A-trous wavelet denoiser hookpoint,
// Sprint-4 (phase 616) promoted that into the three-pass SVGF skeleton, and
// Sprint-5 (phase 669) packaged the SVGF chain into the production-default
// FullSvgfPipeline (fixed 3 A-trous iterations).
//
// Sprint-6 closes the chain: a single owning facade that strings together
// all four library-level passes into one execute() call:
//
//   sample (Sprint-1)
//     -> temporal_reuse (Sprint-2)
//        -> spatial_reuse (Sprint-2)
//           -> SVGF (Sprint-4/5: moment + variance + 3x A-trous filter)
//              -> caller-supplied out_tex (denoised direct illumination)
//
// Why a distinct class rather than another `FullSvgfPipeline::execute(...)`
// overload?
//   * the SVGF wrapper denoises *one* reservoir buffer; it has no notion of
//     where that buffer came from. The denoised pipeline owns the full chain
//     -- including the DispatchPass and its four reservoir SSBOs -- so the
//     integrator gets one call per frame.
//   * the chain has a *temporal* dependency (the spatial-reuse output is the
//     "previous reservoir" the next frame's temporal_reuse pass reads). The
//     facade owns the per-frame `frame_index` advance + emits the
//     storage->storage barrier between sub-passes -- the integrator never
//     needs to know the topology.
//   * library consumers (and the eventual Metal / D3D12 ports) get a stable,
//     named "ReSTIR DI denoised" handle that the framegraph editor panel
//     can show.
//
// Sprint-6 SCOPE: library-level only.
//   * configure(device, viewport_w, viewport_h, light_count) builds the
//     four sub-passes (one DispatchPass + one FullSvgfPipeline).
//   * execute(cb, scene_lights, depth_tex, normal_tex, mesh_id_tex, out_tex)
//     records the four sub-dispatches in order with surrounding barriers
//     into the supplied command buffer and writes the denoised reservoir
//     into `out_tex`.
//   * `scene_lights`, `depth_tex`, `normal_tex`, `mesh_id_tex` are reserved
//     seam slots: this sprint passes them through to the sub-passes but the
//     underlying kernels still run the Sprint-2/3/4 stripped paths
//     (reservoir-luminance fallback) so the library-level test stays
//     runnable on any Vulkan ICD without scene state. Sprint-7+ wires the
//     proper G-buffer texture views and the scene-light SSBO through the
//     framegraph G-buffer ABI.
//   * hello_engine is NOT touched (per FROZEN constraint).
//
// MOMENT: a graphics dev enables ReSTIR DI in their scene with a single
// call per frame -- gets temporally-stable, noise-free direct illumination
// without having to wire the four sub-passes by hand.
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

class DispatchPass;       // forward-declare so consumers do not pull the
class FullSvgfPipeline;   // sub-pass headers through this facade interface.

/// Default per-pixel initial-candidate stream count (Bitterli 2020 `M_initial`).
/// Mirrors `DispatchConfig::candidates`; exposed here so callers do not have
/// to reach into DispatchPass to override it through the facade.
inline constexpr std::uint32_t kFullPipelineDenoisedDefaultCandidates = 32U;

/// Configuration for `FullPipelineDenoised::configure(...)`. Mirrors the
/// fields the underlying `DispatchConfig` consumes but flattens the
/// SvgfDenoiserConfig (the SVGF wrapper hard-codes the production defaults
/// per Sprint-5 -- callers do not tune them through this seam).
struct FullPipelineDenoisedConfig
{
    std::uint32_t viewport_width  { 0 };
    std::uint32_t viewport_height { 0 };

    /// Total scene light count visible to the sample dispatch. Forwarded to
    /// the inner `DispatchConfig::light_count` -- the sample shader still
    /// re-pushes the per-frame count via push-constant. Allowed to be 0
    /// (the library-level smoke runs without scene state).
    std::uint32_t light_count { 0 };

    /// Initial candidate stream count per pixel. Defaults to Bitterli 2020's
    /// 32 (matches `DispatchConfig::candidates`).
    std::uint32_t candidates { kFullPipelineDenoisedDefaultCandidates };
};

/// Opaque scene-light list seam. Sprint-6 keeps this an empty struct so the
/// chain compiles + tests pass without leaking the scene-light SSBO layout
/// through the library boundary. Sprint-7 promotes it to a typed handle
/// (probably `cd::rhi::BufferHandle scene_light_ssbo`) once the framegraph
/// G-buffer ABI lands.
struct SceneLightView
{
    /// Reserved field -- kept zero in Sprint-6. Sprint-7 will swap this for
    /// a typed buffer handle wired to the scene-light SSBO.
    std::uint64_t reserved { 0 };
};

/// Owning, single-device, non-copyable, non-movable orchestrator that runs
/// the full ReSTIR DI denoised chain (sample -> temporal_reuse ->
/// spatial_reuse -> SVGF) in one `execute(...)` call. Internally owns one
/// `DispatchPass` and one `FullSvgfPipeline`; the lifetimes are 1:1 with
/// this object.
///
/// Thread model: single producer thread; the caller is responsible for any
/// surrounding barriers between `execute(...)` and downstream readers of
/// `out_tex`.
class FullPipelineDenoised
{
public:
    FullPipelineDenoised();
    ~FullPipelineDenoised();

    FullPipelineDenoised(const FullPipelineDenoised&)            = delete;
    FullPipelineDenoised& operator=(const FullPipelineDenoised&) = delete;
    FullPipelineDenoised(FullPipelineDenoised&&)                 = delete;
    FullPipelineDenoised& operator=(FullPipelineDenoised&&)      = delete;

    /// Compile + allocate the four sub-passes. Drives:
    ///   * `DispatchPass::prepare(...)` with `{viewport_width,
    ///     viewport_height, light_count, candidates}`.
    ///   * `FullSvgfPipeline::configure(...)` with the same viewport extent.
    ///
    /// Returns kInvalidArgument when the viewport is zero, kBackendInitFailed
    /// when any sub-pass shader compile fails, or any other RHI error verbatim.
    [[nodiscard]] cd::core::Result<void>
    configure(cd::rhi::IDevice&                  device,
              const FullPipelineDenoisedConfig&  cfg);

    /// Convenience overload: defaults `light_count = 0` and
    /// `candidates = kFullPipelineDenoisedDefaultCandidates`. Useful for the
    /// library-level smoke + early integration when the integrator has not
    /// yet plumbed the scene-light SSBO.
    [[nodiscard]] cd::core::Result<void>
    configure(cd::rhi::IDevice& device,
              std::uint32_t     viewport_width,
              std::uint32_t     viewport_height);

    /// Run the full denoised chain. Internally records, in order:
    ///   1. sample dispatch (`DispatchPass::record`)
    ///   2. temporal reuse dispatch (`DispatchPass::execute_temporal_reuse`)
    ///   3. spatial reuse dispatch (`DispatchPass::execute_spatial_reuse`)
    ///   4. SVGF chain (`FullSvgfPipeline::execute` -- moment + variance +
    ///      3x A-trous filter)
    ///
    /// The output reservoir lands in `out_tex` (the SVGF chain's final
    /// destination). Per-frame `frame_index_` advance happens *after* the
    /// recording so each call uses a fresh PCG seed.
    ///
    /// `scene_lights`, `depth_tex`, `normal_tex`, `mesh_id_tex` are
    /// passed through to the sub-passes; per Sprint-6 the kernels run the
    /// reservoir-luminance fallback path so null handles are valid.
    ///
    /// Returns false (no-op) when the pipeline is not ready or `out_tex`
    /// is invalid.
    bool execute(cd::rhi::ICommandBuffer&   cb,
                 SceneLightView             scene_lights,
                 cd::rhi::TextureViewHandle depth_tex,
                 cd::rhi::TextureViewHandle normal_tex,
                 cd::rhi::TextureViewHandle mesh_id_tex,
                 cd::rhi::BufferHandle      out_tex);

    /// Tear down all device-owned resources. Idempotent. Called
    /// automatically by the destructor.
    void shutdown();

    /// True once a successful `configure(...)` has produced all sub-passes.
    [[nodiscard]] bool is_ready() const noexcept { return ready_; }

    /// Stashed viewport mirrors (read-only -- for tests + framegraph hook-up).
    [[nodiscard]] std::uint32_t viewport_width()  const noexcept { return viewport_width_; }
    [[nodiscard]] std::uint32_t viewport_height() const noexcept { return viewport_height_; }

    /// Stashed light_count + candidates (read-only mirrors).
    [[nodiscard]] std::uint32_t light_count() const noexcept { return light_count_; }
    [[nodiscard]] std::uint32_t candidates()  const noexcept { return candidates_; }

    /// Monotonically-advanced frame index forwarded to the reuse passes.
    /// Starts at 0 after configure(); incremented at the END of each
    /// successful `execute(...)`.
    [[nodiscard]] std::uint32_t frame_index() const noexcept { return frame_index_; }

    /// Sub-pass accessors -- read-only handles for the framegraph editor
    /// panel + tests to introspect the chain without owning it.
    [[nodiscard]] const DispatchPass*     dispatch_pass()     const noexcept { return dispatch_.get(); }
    [[nodiscard]] const FullSvgfPipeline* svgf_pipeline()     const noexcept { return svgf_.get(); }

private:
    std::unique_ptr<DispatchPass>     dispatch_ {};
    std::unique_ptr<FullSvgfPipeline> svgf_     {};

    std::uint32_t viewport_width_  { 0 };
    std::uint32_t viewport_height_ { 0 };
    std::uint32_t light_count_     { 0 };
    std::uint32_t candidates_      { kFullPipelineDenoisedDefaultCandidates };
    std::uint32_t frame_index_     { 0 };
    bool          ready_           { false };
};

}  // namespace cd::restir_di

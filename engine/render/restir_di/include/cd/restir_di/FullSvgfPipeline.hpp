// =============================================================================
// CHROMODYNAMIC -- cd/restir_di/FullSvgfPipeline.hpp
// Phase 669 / Sprint-5 -- ReSTIR DI SVGF full pipeline chain.
//
// Sprint-4 (phase 616) shipped the SVGF skeleton: three independently-
// configurable compute pipelines (moment estimate / variance estimate /
// edge-aware A-trous filter), all owned by `cd::restir_di::SvgfDenoiser`.
// That class exposes a *single* execute() that chains the three passes for a
// configurable number of filter iterations -- it is the workhorse.
//
// Sprint-5 promotes the three passes from "configurable building blocks" to a
// **named pipeline** with the production-ready iteration count (3 A-trous
// passes -- the number used by Schied 2017 Section 5 when the application
// already does a separate temporal reservoir reuse, which the ReSTIR DI
// dispatch chain provides). `FullSvgfPipeline` is the seam through which
// the integrator wires SVGF into the framegraph composite pass; the
// moment / variance / 3x A-trous chain becomes a single dispatch entry the
// caller does not need to understand the internals of.
//
// Why a distinct class instead of an `SvgfDenoiser::execute_full(...)`
// overload?
//   * the iteration count is a *production* choice -- it must not drift with
//     the per-test SvgfDenoiser knob tweaks (which legitimately exercise
//     filter_iterations = 1, 3, 5);
//   * the pipeline owns its own SvgfDenoiser internally and re-exposes the
//     argument order the integrator already uses
//     `(reservoir, normal, depth, mesh_id, out)` -- which matches the
//     framegraph G-buffer slot order, not the SVGF kernel internal order.
//   * library consumers (and the eventual Metal / D3D12 ports) get a stable,
//     named "SVGF DI denoiser" handle that the framegraph editor panel can
//     show without leaking the three-pass topology.
//
// Sprint-5 SCOPE: library-level only.
//   * The pipeline owns one SvgfDenoiser (fixed `filter_iterations = 3`).
//   * `configure(device, viewport_w, viewport_h)` is the entire prep API.
//   * `execute(cb, reservoir_buf, normal_tex, depth_tex, mesh_id_tex,
//      out_tex)` dispatches the chain.
//   * hello_engine is NOT touched (per FROZEN constraint); a Vulkan-gated
//     unit test allocates two host-visible reservoir buffers, fills the
//     input with a noisy luminance pattern, runs the chain, and verifies
//     the output luminance variance is lower than the input's.
//
// Sprint-6 will wire the proper depth / normal / mesh-id texture views from
// the G-buffer and hook the final filtered output into the framegraph
// composite path. The current sprint binds null views and the SvgfDenoiser
// kernel falls back to a reservoir-luminance edge stop (the Sprint-3-
// equivalent path) -- which is still enough to *demonstrably* lower the
// per-pixel luminance variance on a synthetic noisy input.
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

class SvgfDenoiser;  // forward-declare so consumers do not pull the full
                     // skeleton header through the pipeline interface.

/// Production-default filter iterations for the FullSvgfPipeline chain.
/// Matches the Schied 2017 Section 5 reference *when* the application
/// already runs a separate temporal reservoir reuse step ahead of the
/// denoiser -- the ReSTIR DI Sprint-2 temporal pass does exactly that, so
/// the per-pixel variance arriving at the denoiser is already reduced.
inline constexpr std::uint32_t kFullSvgfFilterIterations = 3U;

/// Owning, single-device, non-copyable, non-movable orchestrator that runs
/// the full SVGF denoiser chain (moment + variance + 3x edge-aware A-trous
/// filter) as a single named entry point. Internally owns an
/// `SvgfDenoiser`; the lifetimes are 1:1 with this object.
///
/// Thread model: same as `SvgfDenoiser` -- single producer thread; the
/// caller is responsible for any surrounding barriers between
/// `execute(...)` and downstream readers.
class FullSvgfPipeline
{
public:
    FullSvgfPipeline();
    ~FullSvgfPipeline();

    FullSvgfPipeline(const FullSvgfPipeline&)            = delete;
    FullSvgfPipeline& operator=(const FullSvgfPipeline&) = delete;
    FullSvgfPipeline(FullSvgfPipeline&&)                 = delete;
    FullSvgfPipeline& operator=(FullSvgfPipeline&&)      = delete;

    /// Compile the three SVGF compute pipelines (via the internally-owned
    /// `SvgfDenoiser::configure(...)`) and stash the viewport for the
    /// dispatch-time group-count math. The filter iteration count is fixed
    /// at `kFullSvgfFilterIterations` (3) -- the production default for the
    /// ReSTIR DI scenario (where a separate temporal reuse step already
    /// runs ahead of the denoiser).
    ///
    /// Returns kInvalidArgument when the viewport is zero, kBackendInitFailed
    /// when the SVGF shader compile fails, or any other RHI error verbatim.
    [[nodiscard]] cd::core::Result<void>
    configure(cd::rhi::IDevice& device,
              std::uint32_t     viewport_width,
              std::uint32_t     viewport_height);

    /// Run the full SVGF chain against `reservoir_buf` and write the
    /// filtered output into `out_tex`. `normal_tex`, `depth_tex`, and
    /// `mesh_id_tex` are the G-buffer view slots the Sprint-6 framegraph
    /// wiring will populate; pass null handles to fall back to the
    /// reservoir-luminance edge-stop path (Sprint-3-equivalent), which is
    /// the path the library-level smoke exercises.
    ///
    /// Argument order matches the framegraph G-buffer slot order
    /// `(reservoir, normal, depth, mesh_id, out)`, not the internal SVGF
    /// kernel argument order -- this is the public seam the integrator
    /// uses.
    ///
    /// Returns false (no-op) when the pipeline is not ready or any of the
    /// required buffer handles is invalid.
    bool execute(cd::rhi::ICommandBuffer&   cb,
                 cd::rhi::BufferHandle      reservoir_buf,
                 cd::rhi::TextureViewHandle normal_tex,
                 cd::rhi::TextureViewHandle depth_tex,
                 cd::rhi::TextureViewHandle mesh_id_tex,
                 cd::rhi::BufferHandle      out_tex) const;

    /// Tear down all device-owned resources. Idempotent. Called
    /// automatically by the destructor.
    void shutdown();

    /// True once a successful `configure(...)` has produced all three
    /// internal pipelines.
    [[nodiscard]] bool is_ready() const noexcept { return ready_; }

    /// Stashed viewport mirrors (read-only -- for tests + framegraph hook-up).
    [[nodiscard]] std::uint32_t viewport_width()  const noexcept { return viewport_width_; }
    [[nodiscard]] std::uint32_t viewport_height() const noexcept { return viewport_height_; }

    /// Compile-time filter iteration count exposed for downstream
    /// barrier / scheduling reasoning.
    [[nodiscard]] static constexpr std::uint32_t filter_iterations() noexcept
    {
        return kFullSvgfFilterIterations;
    }

private:
    std::unique_ptr<SvgfDenoiser> denoiser_ {};
    std::uint32_t                 viewport_width_  { 0 };
    std::uint32_t                 viewport_height_ { 0 };
    bool                          ready_           { false };
};

}  // namespace cd::restir_di

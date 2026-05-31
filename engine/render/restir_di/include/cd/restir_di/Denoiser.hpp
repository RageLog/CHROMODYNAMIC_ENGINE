// =============================================================================
// CHROMODYNAMIC -- cd/restir_di/Denoiser.hpp
// Phase 571 / Sprint-3 -- ReSTIR DI denoiser hookpoint (library level).
//
// Minimal A-trous wavelet edge-aware blur hookpoint -- the seam through which
// the eventual SVGF integrator (Sprint-4) will sit. Sprint-3 ships only the
// scaffolding: configure() + execute() + a stripped GLSL kernel that performs
// one A-trous wavelet pass per dispatch, with the caller iterating to obtain
// a 3-level pyramid (each iteration doubles the step width).
//
// Sprint-3 scope (this revision):
//   * `Denoiser::configure(iteration_count, step_width)` -- record the
//     iteration count + base step width. Defaults follow the Dammertz 2010
//     reference (5x5 kernel, 3 levels, step doubles each iteration).
//   * `Denoiser::execute(cb, reservoir_buffer, normal_view, depth_view,
//     output_buffer)` -- bind the compute pipeline + descriptor set and
//     dispatch `iteration_count` A-trous passes ping-ponging between
//     `reservoir_buffer` and `output_buffer` (working buffer).
//   * `kRestirAtrousCS` -- inline GLSL string that does one A-trous pass
//     with edge-aware weights derived from a 5x5 wavelet stencil. Sprint-3
//     keeps the kernel SSBO-only (reservoir-luminance edge stops) so the
//     library-level tests run without scene state. The normal_view +
//     depth_view parameters on execute() are reserved seam slots -- they
//     are accepted but unbound this sprint (matches the Sprint-2 stripping
//     of motion-vector + G-buffer bindings).
//
// Sprint-4 will:
//   * promote the kernel to full SVGF (variance estimate + per-pixel temporal
//     variance + luminance-driven sigma_l) on top of the same byte layout.
//   * surface a public iteration knob + an integration seam in the main
//     framegraph so the integrator picks the denoiser output up as the final
//     direct-illumination contribution.
//
// All RHI calls go through `cd::rhi::IDevice` -- no backend-specific symbols
// leak through this header. Consumers link against `cd::restir_di` directly.
// hello_engine is *not* wired in this sprint (per FROZEN constraint); the
// pass is exercised only by tests/test_restir_di_denoiser.cpp which skips
// when no Vulkan ICD is installed.
// =============================================================================
#pragma once

#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/rhi/Handles.hpp>

#include <cstdint>

namespace cd::rhi
{
class IDevice;
class ICommandBuffer;
}  // namespace cd::rhi

namespace cd::restir_di
{

/// Denoiser configuration. `iteration_count` controls how many A-trous passes
/// run per execute() invocation; `step_width` is the base wavelet step (in
/// pixels) used by the first iteration. Subsequent iterations double the step
/// per the standard Dammertz 2010 schedule.
struct DenoiserConfig
{
    std::uint32_t viewport_width  { 0 };
    std::uint32_t viewport_height { 0 };

    /// Number of A-trous iterations to issue per execute(). Sprint-3 brief:
    /// 3 levels (Dammertz reference). Sprint-4 SVGF typically uses 5.
    std::uint32_t iteration_count { 3 };

    /// Base wavelet step width in pixels. Doubled per iteration so that the
    /// 3-level chain covers a 1 / 2 / 4 pixel stencil.
    float step_width { 1.0F };
};

/// Owning GPU-side ReSTIR DI A-trous wavelet denoiser pass. Single-device,
/// single-thread; non-copyable, non-movable so the destructor's RAII
/// ordering is unambiguous. Mirrors `cd::restir_di::DispatchPass` lifecycle.
class Denoiser
{
public:
    Denoiser() = default;
    ~Denoiser();

    Denoiser(const Denoiser&)            = delete;
    Denoiser& operator=(const Denoiser&) = delete;
    Denoiser(Denoiser&&)                 = delete;
    Denoiser& operator=(Denoiser&&)      = delete;

    /// Compile the A-trous shader, build the layouts + pipeline, and stash
    /// the iteration count + step width for execute(). The descriptor set is
    /// allocated but its bindings are written lazily on the first execute()
    /// call (so the caller can hand in the reservoir + texture handles
    /// without pinning them to configure()).
    ///
    /// Returns kInvalidArgument when the viewport is zero or
    /// `iteration_count == 0`, kBackendInitFailed when glslang is
    /// unavailable, or any other RHI error verbatim.
    [[nodiscard]] cd::core::Result<void>
    configure(cd::rhi::IDevice& device, const DenoiserConfig& cfg);

    /// Bind the pipeline + descriptor set and issue `iteration_count`
    /// dispatches, ping-ponging between `reservoir_buffer` and
    /// `output_buffer`. Each dispatch runs at the configured viewport size;
    /// the kernel bounds-checks the trailing groups against the pushed
    /// resolution.
    ///
    /// `normal_view` and `depth_view` are reserved seam slots -- this sprint
    /// keeps them unbound on the GPU side (matches Sprint-2's motion-vector
    /// stripping). The kernel currently derives edge stops from the
    /// reservoir radiance directly. Sprint-4 will wire the views into a
    /// proper SVGF normal + depth-driven sigma_z weighting.
    ///
    /// The caller is responsible for surrounding barriers; the pass writes
    /// `output_buffer` on the final iteration so downstream readers must
    /// emit a kStorage->kShaderResource barrier as needed.
    void execute(cd::rhi::ICommandBuffer&  cb,
                 cd::rhi::BufferHandle     reservoir_buffer,
                 cd::rhi::TextureViewHandle normal_view,
                 cd::rhi::TextureViewHandle depth_view,
                 cd::rhi::BufferHandle     output_buffer) const;

    /// Tear down all device-owned resources. Idempotent. Called automatically
    /// by the destructor.
    void shutdown();

    /// True once a successful `configure()` has produced a usable pipeline.
    [[nodiscard]] bool is_ready() const noexcept { return ready_; }

    /// Stashed iteration count (read-only mirror for tests).
    [[nodiscard]] std::uint32_t iteration_count() const noexcept { return cfg_.iteration_count; }

    /// Stashed step width (read-only mirror for tests).
    [[nodiscard]] float step_width() const noexcept { return cfg_.step_width; }

    /// Group-count math (8x8 work group, same as Sprint-2 reuse passes).
    [[nodiscard]] static std::uint32_t group_count_x(std::uint32_t viewport_w) noexcept;
    [[nodiscard]] static std::uint32_t group_count_y(std::uint32_t viewport_h) noexcept;

private:
    cd::rhi::IDevice*                  device_ { nullptr };
    DenoiserConfig                     cfg_ {};

    cd::rhi::ShaderModuleHandle        shader_module_ {};
    cd::rhi::DescriptorSetLayoutHandle dsl_ {};
    cd::rhi::PipelineLayoutHandle      pipeline_layout_ {};
    cd::rhi::ComputePipelineHandle     pipeline_ {};
    cd::rhi::DescriptorSetHandle       descriptor_set_a_ {};
    cd::rhi::DescriptorSetHandle       descriptor_set_b_ {};

    bool                               ready_ { false };
};

}  // namespace cd::restir_di

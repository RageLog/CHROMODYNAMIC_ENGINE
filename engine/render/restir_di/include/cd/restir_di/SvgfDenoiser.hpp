// =============================================================================
// CHROMODYNAMIC -- cd/restir_di/SvgfDenoiser.hpp
// Phase 616 / Sprint-4 -- Spatiotemporally Variance-Guided Filter skeleton.
//
// SVGF (Schied 2017, "Spatiotemporal Variance-Guided Filtering: Real-Time
// Reconstruction for Path-Traced Global Illumination") promotes the Sprint-3
// A-trous hookpoint into a three-pass denoiser that drives the wavelet
// weights from per-pixel temporally-accumulated luminance variance instead
// of the static Dammertz luminance-only sigma.
//
// The three passes (one compute pipeline each):
//   1. Moment estimation pass (kRestirSvgfMomentEstimateCS)
//        - reprojects last-frame moments (luminance 1st + 2nd raw moment)
//        - accumulates the current-frame luminance into a temporal moment
//          buffer with a per-pixel exponential decay (alpha clamped by the
//          mesh-id consistency between last and current frame).
//        - emits a `history_length` counter so the variance pass can fall
//          back to a 7x7 spatial estimate when history is short.
//
//   2. Variance estimation pass (kRestirSvgfVarianceCS)
//        - reads the moment buffer + history length.
//        - when `history_length < kSvgfShortHistoryThreshold` (default 4),
//          replaces the temporal variance with a 7x7 bilateral spatial
//          estimate (per the original paper Section 4.1).
//        - otherwise uses the temporally-accumulated variance.
//        - writes the per-pixel variance into the filter input buffer.
//
//   3. Edge-aware filter pass (kRestirSvgfFilterCS)
//        - identical structure to the Sprint-3 A-trous kernel BUT the
//          luminance edge-stop sigma is now `phi_l = phi_color *
//          sqrt(g(variance))` where `g` is a 3x3 Gaussian prefilter (paper
//          Equation 5).
//        - depth + normal edge stops driven by `depth_phi` + `normal_phi`
//          from `SvgfDenoiser::configure(...)`.
//        - run `filter_iterations` times, ping-ponging between input +
//          output reservoir buffers (Dammertz step doubling preserved).
//
// Sprint-4 SCOPE: skeleton only -- the three pipelines are compiled and
// dispatched in the right order, the buffers are allocated with the right
// sizes, and the host-side ping-pong + push-constant book-keeping mirrors
// the production SVGF. The kernels themselves accept null
// depth/normal/mesh_id texture views (handed in as reserved seam slots) and
// fall back to a Sprint-3-equivalent reservoir-luminance edge stop in that
// degraded path -- this keeps the library-level tests runnable on any
// Vulkan ICD without scene state, mirroring the Sprint-3 strategy.
//
// Sprint-5 will:
//   * wire the proper depth + normal + mesh-id views (G-buffer route)
//   * promote the moment buffer + history buffer to long-lived,
//     framegraph-owned resources (Sprint-4 owns them internally for the
//     library smoke).
//   * hook the final filtered output into the framegraph composite path
//     so the integrator picks SVGF up as the direct-illumination input.
//
// hello_engine is NOT touched -- per FROZEN constraint.
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

/// SVGF tuning knobs. Defaults match the Schied 2017 paper Section 5
/// "Implementation" values; callers normally tweak only `depth_phi` +
/// `normal_phi` per-scene.
struct SvgfDenoiserConfig
{
    std::uint32_t viewport_width  { 0 };
    std::uint32_t viewport_height { 0 };

    /// Number of A-trous wavelet iterations in the filter pass. Paper
    /// recommends 5; Sprint-4 default matches.
    std::uint32_t filter_iterations { 5 };

    /// Depth edge-stop sigma denominator (`sigma_z` in paper Equation 3).
    /// Larger values relax the depth match; smaller tighten it.
    float depth_phi { 1.0F };

    /// Normal edge-stop power (`sigma_n` in paper Equation 3). Paper uses
    /// 128.0; smaller values relax the normal match.
    float normal_phi { 128.0F };

    /// Temporal alpha for the moment / colour history (paper alpha = 0.2).
    /// Clamped to (0, 1] at configure() time.
    float temporal_alpha { 0.2F };
};

/// History-length threshold below which the variance pass falls back to a
/// 7x7 spatial estimate (paper Section 4.1). Exposed for the unit tests
/// + so Sprint-5's framegraph wiring can match the constant.
inline constexpr std::uint32_t kSvgfShortHistoryThreshold = 4U;

/// Owning GPU-side ReSTIR DI SVGF denoiser. Single-device, single-thread;
/// non-copyable / non-movable so the destructor's RAII ordering is
/// unambiguous. Same lifecycle as `cd::restir_di::Denoiser` (Sprint-3
/// A-trous) -- the two classes can co-exist while the framegraph migrates.
class SvgfDenoiser
{
public:
    SvgfDenoiser() = default;
    ~SvgfDenoiser();

    SvgfDenoiser(const SvgfDenoiser&)            = delete;
    SvgfDenoiser& operator=(const SvgfDenoiser&) = delete;
    SvgfDenoiser(SvgfDenoiser&&)                 = delete;
    SvgfDenoiser& operator=(SvgfDenoiser&&)      = delete;

    /// Compile the three SVGF compute shaders, build their descriptor
    /// layouts + pipelines, allocate the internal moment / variance
    /// scratch SSBOs, and stash the tuning knobs for execute().
    ///
    /// Returns kInvalidArgument when the viewport is zero or
    /// `filter_iterations == 0`, kBackendInitFailed when glslang is
    /// unavailable, or any other RHI error verbatim.
    [[nodiscard]] cd::core::Result<void>
    configure(cd::rhi::IDevice& device, const SvgfDenoiserConfig& cfg);

    /// Convenience overload exposing the most-tuned three knobs without
    /// constructing a full config struct. Re-uses the currently-stashed
    /// viewport + `filter_iterations` (or sets `filter_iterations` if the
    /// caller never called the struct-taking overload).
    ///
    /// Returns kInvalidArgument when no prior configure() has supplied a
    /// viewport, or when `filter_iterations == 0`.
    [[nodiscard]] cd::core::Result<void>
    configure(int filter_iterations, float depth_phi, float normal_phi);

    /// Run the three SVGF passes against `reservoir_buf`, sampling
    /// `depth_tex` + `normal_tex` + `mesh_id_tex` for edge stops + temporal
    /// reprojection, and writing the final filtered reservoir to
    /// `out_tex`. All texture view handles are reserved seam slots: this
    /// sprint binds them when valid, otherwise falls back to a
    /// reservoir-luminance-only edge stop so the library-level smoke runs
    /// without G-buffer state.
    ///
    /// Returns false (no-op) when the denoiser is not ready or any of the
    /// required handles is invalid.
    bool execute(cd::rhi::ICommandBuffer&   cb,
                 cd::rhi::BufferHandle      reservoir_buf,
                 cd::rhi::TextureViewHandle depth_tex,
                 cd::rhi::TextureViewHandle normal_tex,
                 cd::rhi::TextureViewHandle mesh_id_tex,
                 cd::rhi::BufferHandle      out_tex) const;

    /// Tear down all device-owned resources. Idempotent. Called
    /// automatically by the destructor.
    void shutdown();

    /// True once a successful struct-taking configure() has produced all
    /// three pipelines + the scratch SSBOs.
    [[nodiscard]] bool is_ready() const noexcept { return ready_; }

    /// Stashed config mirrors (read-only -- for tests + framegraph wiring).
    [[nodiscard]] std::uint32_t filter_iterations() const noexcept
    {
        return cfg_.filter_iterations;
    }
    [[nodiscard]] float depth_phi() const noexcept { return cfg_.depth_phi; }
    [[nodiscard]] float normal_phi() const noexcept { return cfg_.normal_phi; }
    [[nodiscard]] float temporal_alpha() const noexcept { return cfg_.temporal_alpha; }
    [[nodiscard]] std::uint32_t viewport_width() const noexcept
    {
        return cfg_.viewport_width;
    }
    [[nodiscard]] std::uint32_t viewport_height() const noexcept
    {
        return cfg_.viewport_height;
    }

    /// Work-group dimension (8x8) -- matches Sprint-2 + Sprint-3.
    [[nodiscard]] static std::uint32_t group_count_x(std::uint32_t viewport_w) noexcept;
    [[nodiscard]] static std::uint32_t group_count_y(std::uint32_t viewport_h) noexcept;

private:
    cd::rhi::IDevice*                  device_ { nullptr };
    SvgfDenoiserConfig                 cfg_ {};

    // --- Pass 1: moment estimation ----------------------------------------
    cd::rhi::ShaderModuleHandle        moment_module_ {};
    cd::rhi::DescriptorSetLayoutHandle moment_dsl_ {};
    cd::rhi::PipelineLayoutHandle      moment_layout_ {};
    cd::rhi::ComputePipelineHandle     moment_pipeline_ {};
    cd::rhi::DescriptorSetHandle       moment_set_ {};

    // --- Pass 2: variance estimation --------------------------------------
    cd::rhi::ShaderModuleHandle        variance_module_ {};
    cd::rhi::DescriptorSetLayoutHandle variance_dsl_ {};
    cd::rhi::PipelineLayoutHandle      variance_layout_ {};
    cd::rhi::ComputePipelineHandle     variance_pipeline_ {};
    cd::rhi::DescriptorSetHandle       variance_set_ {};

    // --- Pass 3: edge-aware A-trous filter (ping-pong) --------------------
    cd::rhi::ShaderModuleHandle        filter_module_ {};
    cd::rhi::DescriptorSetLayoutHandle filter_dsl_ {};
    cd::rhi::PipelineLayoutHandle      filter_layout_ {};
    cd::rhi::ComputePipelineHandle     filter_pipeline_ {};
    cd::rhi::DescriptorSetHandle       filter_set_a_ {};
    cd::rhi::DescriptorSetHandle       filter_set_b_ {};

    // --- Internal scratch ssbos (moment + variance buffers) ---------------
    cd::rhi::BufferHandle              moment_buffer_ {};
    cd::rhi::BufferHandle              variance_buffer_ {};
    cd::rhi::BufferHandle              history_length_buffer_ {};

    bool                               ready_ { false };
};

}  // namespace cd::restir_di

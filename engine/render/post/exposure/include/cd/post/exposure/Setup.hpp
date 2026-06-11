// =============================================================================
// CHROMODYNAMIC — cd/post/exposure/Setup.hpp
//
// Phase 507 — Auto-exposure integration helper (composite + bloom wire).
//
// Combines the three independent pieces shipped earlier:
//   * Phase 454: CPU kernel (Reinhard log-avg + EMA smoothing)
//   * Phase 461: GpuReduction (compute log-luminance + readback SSBO)
//   * ADR-20260530-auto-exposure-pipeline: composite fx[1] + bloom EV
//
// Setup is the *single entry-point* a sample/engine integrator uses to
// drive scene EV → composite tonemap exposure multiplier:
//
//     // boot
//     auto setup_r = Setup::create(device, downsampled_hdr_extent);
//     if (!setup_r) abort();
//     Setup setup = std::move(*setup_r);
//
//     // per frame, BEFORE composite apply()
//     const float mul = setup.tick(cmd, hdr_view, dt_seconds);
//     composite_push.fx[1] = mul;
//
//     // optional: feed bloom prefilter
//     bloom_push.params[0] = bloom_threshold * setup.bloom_threshold_scale();
//
//     // shutdown
//     setup.destroy();
//
// The helper OWNS:
//   * GpuReduction (compute pipeline + readback SSBO)
//   * Settings (key / clamp range / EMA speeds)
//   * Frame-to-frame state: prev_ev (drives EMA), last_log_avg (debug),
//     last_multiplier (debug + UI readout)
//
// The helper does NOT own:
//   * HDR texture view (passed per tick)
//   * Command buffer (passed per tick)
//   * Composite push-constant buffer (caller writes the returned float)
//
// The optional manual EV bias is an *additive* offset in stops, layered on
// top of the auto-computed EV. The UI slider that historically drove
// `fx.exposure` directly becomes:
//
//     ImGui::SliderFloat("Exposure bias (stops)", &setup.ev_bias, -4, +4);
//     // setup.tick() reads ev_bias and applies it before
//     // compute_exposure_multiplier.
//
// Default ev_bias = 0 means "pure auto-exposure". The UI label changes
// from "Exposure (linear multiplier)" to "Exposure bias (stops)" — this is
// equivalent to a real camera's Exposure Compensation dial.
//
// References (live in `Exposure.hpp` already):
//   - Reinhard, Stark, Shirley, Ferwerda 2002 §2 (avg log-luma + 0.18 grey).
//   - Karis 2014 (UE4 EMA time-constant pattern).
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>
#include <cd/post/exposure/Exposure.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace cd::post::exposure
{

/// One-stop helper that owns the GPU reduction + the CPU EMA state.
/// Tick once per frame; returns the linear multiplier the composite
/// pass should write into its push-constant exposure slot (`fx[1]` in
/// hello_engine's CompositePush).
///
/// Lifecycle:
///   * `Setup::create(dev, hdr_extent)` allocates the GPU reduction.
///     The supplied extent should already be downsampled to <= 128x128
///     (256 partial slots, 8x8 tiles). The engine pre-downsamples its
///     scene HDR before feeding this; the helper does NOT do that
///     downsample itself — it is a small dedicated stage with its own
///     dispatch + readback.
///   * `tick(cmd, hdr_view, dt)` records the dispatch, reads back the
///     log-avg luminance, runs `update_ev` against the carried
///     `prev_ev`, applies optional `ev_bias`, and returns the linear
///     exposure multiplier ready for the composite push-constant.
///   * `bloom_threshold_scale()` returns exp2(-current_ev). Multiply
///     the bloom prefilter threshold (in linear HDR units) by this so
///     "threshold 1.0" means "1 stop over the scene's auto-exposed
///     mean" regardless of EV. The bloom prefilter shader will need a
///     matching ev push-constant to apply this scale per-pixel; until
///     that ship, callers can pre-scale the threshold CPU-side.
///   * `destroy()` releases the owned GPU reduction. Safe to call on
///     a default-constructed (uncreated) instance.
///
/// State carried across frames:
///   * `prev_ev`: the smoothed EV from the last frame, feeds the EMA.
///   * `last_log_avg`: the last successful log-avg readback (for UI /
///     debug overlays; equals `Settings::kSkipThreshold` when the
///     reduction had no valid tiles).
///   * `last_multiplier`: the last value returned by `tick()` (for UI
///     readout — equivalent to the old `fx.exposure` slider value).
///
/// User-tweakable inputs (writable any time):
///   * `settings`: clamp range + adaptation speeds + Reinhard key.
///   * `ev_bias`: ±N stops added on top of auto. Default 0 (pure auto).
struct Setup
{
    /// GPU reduction owned by the helper. Lazily created via `create()`.
    GpuReduction reduction {};

    /// EMA + clamp + key parameters (user-tweakable).
    Settings settings {};

    /// Manual exposure compensation, in stops. Added to the auto-EV
    /// before `compute_exposure_multiplier`. 0 = pure auto-exposure;
    /// +1 = scene appears 1 stop brighter; -1 = 1 stop darker.
    float ev_bias { 0.0F };

    /// State carried across frames so the EMA stays consistent.
    float prev_ev { 0.0F };

    /// Last log-avg luminance read back from the GPU. Equals
    /// `Settings::kSkipThreshold` when the reduction had no valid tiles
    /// (all-black scene, NullDevice, or first frame before submit).
    float last_log_avg { Settings::kSkipThreshold };

    /// Last linear multiplier returned by `tick()`. Mirrors the old
    /// `fx.exposure` UI value; useful for editor read-only displays.
    float last_multiplier { 1.0F };

    /// Build a Setup with the GPU reduction allocated for `hdr_extent`.
    /// The extent should already be a downsampled HDR target (engine
    /// pre-downsamples to keep the partial-slot budget intact). Returns
    /// an error if `GpuReduction::create` fails.
    [[nodiscard]] static cd::core::Result<Setup>
    create(cd::rhi::IDevice& dev, cd::rhi::Extent2D hdr_extent)
    {
        auto red_r = GpuReduction::create(dev, hdr_extent);
        if (!red_r.has_value())
        {
            return std::unexpected(red_r.error());
        }
        Setup out {};
        out.reduction = *red_r;
        return out;
    }

    /// Per-frame tick. Records the reduction dispatch into `cmd`,
    /// reads back the log-avg luminance, runs the EMA, applies the
    /// manual bias, and returns the linear multiplier the composite
    /// push-constant should consume.
    ///
    /// `cmd` MUST already be in the recording state. The caller is
    /// responsible for command-buffer submission + GPU synchronisation
    /// before the readback inside `dispatch_and_readback` returns the
    /// freshest data; this mirrors the contract of GpuReduction
    /// itself. The skeleton on NullDevice gracefully returns the
    /// previous multiplier (kSkipThreshold path) so headless tests +
    /// editor smoke modes do not stall.
    ///
    /// `dt_seconds` is the wall-clock delta the smoothing applies. Use
    /// the engine's frame_dt (clamped at the call site if necessary).
    [[nodiscard]] float
    tick(cd::rhi::ICommandBuffer& cmd,
         cd::rhi::TextureViewHandle hdr_view,
         float dt_seconds) noexcept
    {
        // 1) GPU reduction + host gather -> single log-avg luminance.
        last_log_avg = reduction.dispatch_and_readback(cmd, hdr_view);

        // 2) update_ev composes compute_target_ev + apply_smoothing,
        //    honouring kSkipThreshold (returns prev_ev unchanged when
        //    the reduction failed / NullDevice / all-black frame).
        prev_ev = update_ev(last_log_avg, prev_ev, dt_seconds, settings);

        // 3) Optional manual bias (Exposure Compensation dial).
        //    Clamp the *summed* EV to settings bounds so the user
        //    can't push the auto-exposed scene outside what we said
        //    the pipeline supports.
        const float biased_ev = std::clamp(prev_ev + ev_bias,
                                           settings.min_ev,
                                           settings.max_ev);

        // 4) Convert to linear multiplier — what composite_pass wants.
        last_multiplier = compute_exposure_multiplier(biased_ev, settings);
        return last_multiplier;
    }

    /// Scale factor for bloom prefilter threshold so the user-specified
    /// "threshold in linear HDR units" actually means "N stops above
    /// scene auto-exposed mean". Multiply the bloom threshold push-
    /// constant by this. A scene-mean = 0.18 grey (EV 0) returns 1.0;
    /// a 1-stop-brighter scene (EV +1) returns 0.5 (threshold halves
    /// so bloom still triggers at the same relative brightness).
    ///
    /// The bloom prefilter shader can also apply this scaling itself by
    /// receiving the current EV via push-constant — that path is the
    /// long-term home, but pre-scaling on the CPU keeps the shader
    /// untouched for the SCOPED-DOWN ship of phase 507.
    [[nodiscard]] float bloom_threshold_scale() const noexcept
    {
        const float biased_ev = std::clamp(prev_ev + ev_bias,
                                           settings.min_ev,
                                           settings.max_ev);
        return std::exp2(-biased_ev);
    }

    /// Current smoothed EV (auto + bias, clamped). Convenience accessor
    /// for editor overlays. `prev_ev` is the raw auto-EV before bias.
    [[nodiscard]] float current_ev() const noexcept
    {
        return std::clamp(prev_ev + ev_bias, settings.min_ev, settings.max_ev);
    }

    /// Release the GPU reduction. Safe to call on a default-constructed
    /// (uncreated) instance; in that case the carried GpuReduction is
    /// itself empty and destroy() is a no-op.
    void destroy() noexcept
    {
        reduction.destroy();
    }
};

}  // namespace cd::post::exposure

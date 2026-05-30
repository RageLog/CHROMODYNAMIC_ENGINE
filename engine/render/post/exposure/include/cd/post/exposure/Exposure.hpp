// =============================================================================
// CHROMODYNAMIC — cd/post/exposure/Exposure.hpp
//
// Phase 454 — auto-exposure helper (Reinhard log-avg luminance + EMA
// smoothing). Architectural fix for the "every scene needs manual
// exposure tweaking" pain that surfaced through phases 449-450:
//   - bloom_threshold tuning depended on scene EV
//   - exposure default 3.0 was over-bright for some scenes, under for others
//   - cylinder/torus saturated to white in one scene, looked correct in another
//
// Design (CPU-testable kernel; GPU integration queued):
//   1. Reduction pass (compute or per-mip downsample) produces ONE float
//      = avg log2(luminance(c)) across all pixels of the HDR target.
//   2. compute_target_ev() maps that average to a target EV using the
//      Reinhard 2002 "key" idea: EV = log2(avg_lum) - log2(0.18).
//   3. apply_smoothing() EMA-blends the previous frame's EV with the
//      newly-computed target, using a configurable time-constant.
//   4. compute_exposure_multiplier() converts the smoothed EV back to a
//      linear scalar that the composite tonemap pre-multiplies by.
//
// This library is GPU-free: callers feed the reduction result (single
// float, calc'd via their own compute / mip-downsample / CPU readback)
// and consume the smoothed exposure multiplier. The reduction kernel
// itself is the GPU work the cd::post_composite pass will land in
// follow-up. Cleanly testing the smoothing + curve fitting in isolation
// avoids the typical "tweaked once on dev machine, wrong on next scene"
// failure mode.
//
// References:
//   - Reinhard, Stark, Shirley, Ferwerda (2002): Photographic Tone
//     Reproduction for Digital Images. SIGGRAPH 2002 §2 — average log-
//     luminance as the key + the 0.18 grey-card reference.
//   - Karis (2014): Tone Mapping — Filmic Tonemapping with Piecewise
//     Power Curves. SIGGRAPH course notes on the EMA time-constant
//     pattern used by Unreal Engine 4.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>

namespace cd::post::exposure
{

/// Time-step for the EMA smoothing. 60 fps targets ~0.5s adaptation
/// when speed = 1.0; doubling speed halves the time. Real eyes adapt
/// on the order of seconds for moderate luma changes; we err toward
/// game-snappy (sub-second).
struct Settings
{
    /// Reference "middle grey" the auto-exposure aims to map to. The
    /// Reinhard 2002 paper uses 0.18 for photo film; we keep the same
    /// for compatibility with the rest of the PBR pipeline.
    float key { 0.18F };

    /// Hard clamp on EV adjustment. ±4 stops covers most scene
    /// transitions (night -> noon = ~14 stops in reality; in-game we
    /// cap so a single dark corner doesn't blow out the rest of the
    /// scene).
    float min_ev { -4.0F };
    float max_ev {  4.0F };

    /// EMA speed (1.0 = ~0.5s adaptation @ 60 fps, 2.0 = ~0.25s, ...).
    float adapt_speed_up   { 2.0F };   // bright -> dark (eye contracts faster)
    float adapt_speed_down { 1.0F };   // dark -> bright (slower bias)

    /// Sentinel for log-avg luminance to skip when the reduction
    /// produced an invalid result (e.g. all-black frame). When the
    /// caller passes `log_avg_luminance < kSkipThreshold` to
    /// `compute_target_ev`, the function returns the previous EV
    /// unchanged.
    static constexpr float kSkipThreshold = -100.0F;
};

/// Compute the target EV given the current frame's log-avg luminance.
/// Returns `prev_ev` unchanged when the log-avg sentinel says "skip".
/// EV is in stops (log2 scale): +1 = double, -1 = half.
[[nodiscard]] float
compute_target_ev(float log_avg_luminance, float prev_ev, const Settings& s) noexcept;

/// Blend `prev_ev` toward `target_ev` using the EMA time-constant
/// implied by `dt_seconds` and the settings' adapt speeds. Returns the
/// new smoothed EV, clamped to settings' [min_ev, max_ev] range.
[[nodiscard]] float
apply_smoothing(float prev_ev, float target_ev, float dt_seconds, const Settings& s) noexcept;

/// Convert an EV value (in stops) to the linear exposure multiplier the
/// composite pass should pre-multiply the HDR sample by. exposure =
/// 2^EV / key. The /key term keeps "EV 0" mapping the scene to 18% grey
/// (the Reinhard key).
[[nodiscard]] float
compute_exposure_multiplier(float ev, const Settings& s) noexcept;

/// One-shot helper that takes a fresh log-avg luminance + the previous
/// EV + dt + settings and returns the new EV (smoothed, clamped) ready
/// for downstream conversion to a multiplier. Composes the three steps
/// above.
[[nodiscard]] float
update_ev(float log_avg_luminance, float prev_ev, float dt_seconds, const Settings& s) noexcept;

/// Compute the log-avg luminance of an in-memory pixel array (RGBA,
/// floats, linear, row-major). Skips pixels at exactly zero luma to
/// avoid -inf in the log; returns `Settings::kSkipThreshold` when no
/// valid pixels exist. Useful for unit tests + CPU paths; the GPU
/// reduction is the production producer.
[[nodiscard]] float
log_avg_luminance(const float* rgba_pixels, std::uint32_t pixel_count) noexcept;

}  // namespace cd::post::exposure

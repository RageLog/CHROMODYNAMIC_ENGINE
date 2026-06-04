// =============================================================================
// CHROMODYNAMIC — cd/ui/ToastAnim.hpp
// phase724 — slide-in / fade-out animation math for toast notifications.
//
// Pure-header, zero-dependency utility.  The caller passes elapsed time in
// milliseconds; the function returns the current alpha (0..1) and horizontal
// x-offset (pixels, positive = shifted right / off-screen).
//
// Lifecycle (default 2000 ms total):
//   0  …  150 ms  — slide-in:  cubic-ease-out  x_offset 50→0, alpha 0→1
//   150 … 1850 ms — steady:    alpha = 1.0, x_offset = 0
//   1850 … 2000 ms — fade-out: linear alpha 1→0, x_offset = 0
//
// MOMENT: toasts slide in from the right with a smooth ease, sit for ~1.7 s,
// fade gracefully — Source 2 SDK polish.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cd::ui
{

/// Animation constants for a single toast notification.
struct ToastAnimCfg
{
    double lifetime_ms   { 2000.0 };  ///< Total visible lifetime (ms).
    double slide_ms      {  150.0 };  ///< Slide-in phase duration (ms).
    double fadeout_ms    {  150.0 };  ///< Fade-out phase duration (ms).
    float  slide_max_px  {   50.0F};  ///< Maximum x-offset at start of slide-in (px).
};

/// Result of the animation computation.
struct ToastAnimResult
{
    float alpha    { 1.0F };  ///< Opacity 0..1.
    float x_offset { 0.0F };  ///< Rightward pixel offset (0 = fully on-screen).
};

/// Compute the current animation state for a toast at a given age.
///
/// @param age_ms      Elapsed time since the toast was pushed (milliseconds).
/// @param cfg         Animation timing constants.
/// @return            alpha and x_offset for this frame.
///
/// The toast is considered expired when age_ms >= cfg.lifetime_ms; callers
/// should evict it beforehand, but the function still returns {0,0} gracefully.
[[nodiscard]] inline ToastAnimResult
toast_anim(double age_ms, const ToastAnimCfg& cfg = {}) noexcept
{
    if (age_ms <= 0.0)
    {
        // Not yet visible (clock lag etc.).
        return { 0.0F, cfg.slide_max_px };
    }

    const double steady_start = cfg.slide_ms;
    const double fade_start   = cfg.lifetime_ms - cfg.fadeout_ms;

    if (age_ms < steady_start)
    {
        // --- Slide-in phase: cubic ease-out (t = age / slide_ms) ---
        // ease_out_cubic(t) = 1 - (1-t)^3
        const double t   = age_ms / cfg.slide_ms;
        const double tc  = std::clamp(t, 0.0, 1.0);
        const double inv = 1.0 - tc;
        const double ease = 1.0 - (inv * inv * inv);  // ease-out-cubic

        const auto alpha    = static_cast<float>(ease);
        const auto x_offset = static_cast<float>((1.0 - ease) * static_cast<double>(cfg.slide_max_px));
        return { alpha, x_offset };
    }

    if (age_ms < fade_start)
    {
        // --- Steady phase ---
        return { 1.0F, 0.0F };
    }

    // --- Fade-out phase: linear ---
    const double t_fade = (age_ms - fade_start) / cfg.fadeout_ms;
    const double tc     = std::clamp(t_fade, 0.0, 1.0);
    const auto   alpha  = static_cast<float>(1.0 - tc);
    return { alpha, 0.0F };
}

}  // namespace cd::ui

// =============================================================================
// CHROMODYNAMIC -- cd/ui/ToastAnim.hpp
// phase724 -- slide-in / fade-out animation math for toast notifications.
// phase774 -- 4-direction slide support (kFromRight/Left/Top/Bottom).
//
// Pure-header, zero-dependency utility.  The caller passes elapsed time in
// milliseconds and the slide direction; the function returns the current
// alpha (0..1) and a 2D pixel offset (x_offset, y_offset).
//
// Lifecycle (default 2000 ms total):
//   0  ...  150 ms  -- slide-in:  cubic-ease-out  offset slide_max_px->0, alpha 0->1
//   150 ... 1850 ms -- steady:    alpha = 1.0, offset = 0
//   1850 ... 2000 ms -- fade-out: linear alpha 1->0, offset = 0
//
// Direction mapping (slide_max_px > 0 means away from screen centre):
//   kFromRight  -- x_offset > 0 (shifted right), y_offset = 0
//   kFromLeft   -- x_offset < 0 (shifted left),  y_offset = 0
//   kFromTop    -- x_offset = 0, y_offset < 0 (shifted up)
//   kFromBottom -- x_offset = 0, y_offset > 0 (shifted down)
//
// MOMENT: toasts slide in from any edge with a smooth cubic ease, sit for
// ~1.7 s, fade gracefully -- Source 2 SDK polish.
// =============================================================================
#pragma once

#include <cd/ui/Toast.hpp>

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
    float  slide_max_px  {   50.0F};  ///< Maximum offset magnitude at start of slide-in (px).
};

/// Result of the animation computation.
struct ToastAnimResult
{
    float alpha    { 1.0F };  ///< Opacity 0..1.
    float x_offset { 0.0F };  ///< Horizontal pixel offset (positive = right).
    float y_offset { 0.0F };  ///< Vertical pixel offset (positive = down).
};

/// Compute the current animation state for a toast at a given age and direction.
///
/// @param age_ms      Elapsed time since the toast was pushed (milliseconds).
/// @param direction   Which edge the toast slides in from.
/// @param cfg         Animation timing constants.
/// @return            alpha, x_offset, and y_offset for this frame.
///
/// The toast is considered expired when age_ms >= cfg.lifetime_ms; callers
/// should evict it beforehand, but the function still returns {0,0,0} gracefully.
[[nodiscard]] inline ToastAnimResult
toast_anim(double              age_ms,
           ToastDirection      direction = ToastDirection::kFromRight,
           const ToastAnimCfg& cfg       = {}) noexcept
{
    auto make_offset = [&](float slide_dist) -> ToastAnimResult
    {
        switch (direction)
        {
            case ToastDirection::kFromRight:
                return { 1.0F,  slide_dist, 0.0F };
            case ToastDirection::kFromLeft:
                return { 1.0F, -slide_dist, 0.0F };
            case ToastDirection::kFromTop:
                return { 1.0F, 0.0F, -slide_dist };
            case ToastDirection::kFromBottom:
                return { 1.0F, 0.0F,  slide_dist };
        }
        return { 1.0F, slide_dist, 0.0F };
    };

    if (age_ms <= 0.0)
    {
        auto r   = make_offset(cfg.slide_max_px);
        r.alpha  = 0.0F;
        return r;
    }

    const double steady_start = cfg.slide_ms;
    const double fade_start   = cfg.lifetime_ms - cfg.fadeout_ms;

    if (age_ms < steady_start)
    {
        // Slide-in phase: cubic ease-out (t = age / slide_ms)
        const double t    = age_ms / cfg.slide_ms;
        const double tc   = std::clamp(t, 0.0, 1.0);
        const double inv  = 1.0 - tc;
        const double ease = 1.0 - (inv * inv * inv);  // ease-out-cubic

        const auto slide_dist =
            static_cast<float>((1.0 - ease) * static_cast<double>(cfg.slide_max_px));
        auto r   = make_offset(slide_dist);
        r.alpha  = static_cast<float>(ease);
        return r;
    }

    if (age_ms < fade_start)
    {
        return { 1.0F, 0.0F, 0.0F };
    }

    // Fade-out phase: linear
    const double t_fade = (age_ms - fade_start) / cfg.fadeout_ms;
    const double tc     = std::clamp(t_fade, 0.0, 1.0);
    const auto   alpha  = static_cast<float>(1.0 - tc);
    return { alpha, 0.0F, 0.0F };
}

/// Backward-compatible overload: direction defaults to kFromRight.
/// Callers that only pass age_ms continue to work without changes.
[[nodiscard]] inline ToastAnimResult
toast_anim(double age_ms, const ToastAnimCfg& cfg) noexcept
{
    return toast_anim(age_ms, ToastDirection::kFromRight, cfg);
}

}  // namespace cd::ui
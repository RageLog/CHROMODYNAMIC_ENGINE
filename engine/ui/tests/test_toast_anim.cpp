// =============================================================================
// CHROMODYNAMIC — cd::ui toast animation tests
// phase724 — slide-in + fade-out animation math (cd/ui/ToastAnim.hpp)
//
// Test plan:
//   ToastAnim_SlideIn_AtMidpoint     — t=75ms is in slide-in phase (0..150ms):
//                                      alpha > 0 and alpha < 1, x_offset > 0.
//   ToastAnim_Steady_AlphaOneOffset0 — t=500ms is in steady phase:
//                                      alpha == 1.0, x_offset == 0.
//   ToastAnim_FadeOut_AtMidpoint     — t=1925ms is in fade-out phase (1850..2000):
//                                      alpha > 0 and alpha < 1, x_offset == 0.
//   ToastAnim_PostLifetime_Evicted   — t=2100ms >= lifetime: alpha == 0 (clamped),
//                                      indicating the toast should be removed.
// =============================================================================
#include <cd/ui/ToastAnim.hpp>
#include <gtest/gtest.h>

namespace
{

// Default config: lifetime=2000ms, slide=150ms, fadeout=150ms, slide_max=50px.
// Steady window: 150ms .. 1850ms.

TEST(ToastAnim, SlideIn_AtMidpoint)
{
    // t = 75ms is exactly halfway through the 150ms slide-in window.
    // cubic ease-out at t_norm=0.5: ease = 1 - (1-0.5)^3 = 1 - 0.125 = 0.875.
    // Alpha should be > 0 (started animating) and < 1 (not yet steady).
    // x_offset should be > 0 (not yet fully on-screen).
    const auto result = cd::ui::toast_anim(75.0);

    EXPECT_GT(result.alpha,    0.0F) << "slide-in at 75ms must have positive alpha";
    EXPECT_LT(result.alpha,    1.0F) << "slide-in at 75ms must not be fully opaque yet";
    EXPECT_GT(result.x_offset, 0.0F) << "slide-in at 75ms must have positive x_offset";
    // Cubic ease-out at t=0.5 gives 0.875; verify we're in a sensible band.
    EXPECT_GE(result.alpha, 0.5F)   << "cubic ease-out at midpoint should be above 0.5";
}

TEST(ToastAnim, Steady_AlphaOneOffset0)
{
    // t = 500ms is well inside the steady window (150ms .. 1850ms).
    // Expect full alpha and zero offset.
    const auto result = cd::ui::toast_anim(500.0);

    EXPECT_FLOAT_EQ(result.alpha,    1.0F) << "steady phase must be fully opaque";
    EXPECT_FLOAT_EQ(result.x_offset, 0.0F) << "steady phase must have zero x_offset";
}

TEST(ToastAnim, FadeOut_AtMidpoint)
{
    // t = 1925ms is 75ms into the 150ms fade-out window (1850..2000ms).
    // linear fade: t_norm = 75/150 = 0.5 → alpha = 0.5.
    // x_offset must remain 0 (no slide during fade-out).
    const auto result = cd::ui::toast_anim(1925.0);

    EXPECT_GT(result.alpha,    0.0F) << "fade-out at 1925ms still has some opacity";
    EXPECT_LT(result.alpha,    1.0F) << "fade-out at 1925ms is not fully opaque";
    EXPECT_FLOAT_EQ(result.x_offset, 0.0F) << "fade-out phase must have zero x_offset";
    // Linear fade at t_norm=0.5 → alpha exactly 0.5.
    EXPECT_NEAR(result.alpha, 0.5F, 0.01F) << "linear fade at midpoint should be ~0.5";
}

TEST(ToastAnim, PostLifetime_IsFullyTransparent)
{
    // t = 2100ms is 100ms past the default 2000ms lifetime.
    // toast_anim clamps t_fade to 1.0 → alpha = 0.
    // Callers should evict the toast, but the function must not return garbage.
    const auto result = cd::ui::toast_anim(2100.0);

    EXPECT_FLOAT_EQ(result.alpha,    0.0F) << "post-lifetime toast must be transparent";
    EXPECT_FLOAT_EQ(result.x_offset, 0.0F) << "post-lifetime toast must have zero offset";
}

}  // namespace

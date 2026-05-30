// =============================================================================
// CHROMODYNAMIC -- cd::ui::widgets animation wiring tests
//
// Phase 4.77 (phase477-ui-widgets-animation) -- validates that the
// per-widget Tweener<float> threads on Button / Toggle / Checkbox /
// Slider correctly smooth hover / press / focus transitions instead of
// snapping instantly. Pixel rendering remains tested by test_widgets.cpp
// (state machines) and the renderer's golden-image suite.
//
// Reference rate: WidgetAnimation::speed_up defaults to 10/sec, so the
// 0->1 transition completes in ~100 ms.
// =============================================================================
#include <cd/ui/animation/Animation.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <string>

namespace w   = cd::ui::widgets;
namespace ua  = cd::ui::animation;

namespace
{

constexpr w::Rect kRect100 { 10.0F, 10.0F, 100.0F, 30.0F };

// Mouse position inside the widget rect (centre).
constexpr float kInsideX = 50.0F;
constexpr float kInsideY = 25.0F;

// Mouse position well outside.
constexpr float kOutsideX = 500.0F;
constexpr float kOutsideY = 500.0F;

[[nodiscard]] w::InputState hover_input(float mx, float my,
                                        bool down     = false,
                                        bool pressed  = false,
                                        bool released = false,
                                        bool focused  = false)
{
    w::InputState in;
    in.pointer.mouse_x       = mx;
    in.pointer.mouse_y       = my;
    in.pointer.left_down     = down;
    in.pointer.left_pressed  = pressed;
    in.pointer.left_released = released;
    in.focused               = focused;
    return in;
}

}  // namespace

// =============================================================================
// 1. Initial state -- hover_amount / press_amount / focus_amount are 0
// =============================================================================

TEST(UiWidgetsAnimation, ButtonInitialAmountsAreZero)
{
    w::Button btn { "Go" };
    btn.set_rect(kRect100);
    EXPECT_FLOAT_EQ(btn.hover_amount(), 0.0F);
    EXPECT_FLOAT_EQ(btn.press_amount(), 0.0F);
    EXPECT_FLOAT_EQ(btn.focus_amount(), 0.0F);
}

TEST(UiWidgetsAnimation, AllWidgetsInitialAmountsAreZero)
{
    w::Button   btn;
    w::Toggle   tog;
    w::Checkbox cbx;
    w::Slider   sld;
    btn.set_rect(kRect100);
    tog.set_rect(kRect100);
    cbx.set_rect(kRect100);
    sld.set_rect(kRect100);

    EXPECT_FLOAT_EQ(btn.hover_amount(), 0.0F);
    EXPECT_FLOAT_EQ(tog.hover_amount(), 0.0F);
    EXPECT_FLOAT_EQ(cbx.hover_amount(), 0.0F);
    EXPECT_FLOAT_EQ(sld.hover_amount(), 0.0F);

    EXPECT_FLOAT_EQ(btn.press_amount(), 0.0F);
    EXPECT_FLOAT_EQ(tog.press_amount(), 0.0F);
    EXPECT_FLOAT_EQ(cbx.press_amount(), 0.0F);
    EXPECT_FLOAT_EQ(sld.press_amount(), 0.0F);
}

// =============================================================================
// 2. Hover ramp-up -- after 50ms tick at speed_up=10/sec, hover_amount > 0
// =============================================================================

TEST(UiWidgetsAnimation, ButtonHoverAmountRampsUpFromTickDt)
{
    w::Button btn { "Go" };
    btn.set_rect(kRect100);

    // Bring the cursor inside (boolean target -> true) and advance 50 ms.
    const float dt_50ms = 0.050F;
    btn.tick(hover_input(kInsideX, kInsideY), dt_50ms);

    EXPECT_TRUE(btn.state().hovered);
    EXPECT_GT(btn.hover_amount(), 0.0F);
    EXPECT_LT(btn.hover_amount(), 1.0F);  // not yet fully arrived
}

TEST(UiWidgetsAnimation, ToggleHoverAmountRampsUpFromTickDt)
{
    w::Toggle tog;
    tog.set_rect(kRect100);

    tog.tick(hover_input(kInsideX, kInsideY), 0.050F);
    EXPECT_TRUE(tog.state().hovered);
    EXPECT_GT(tog.hover_amount(), 0.0F);
    EXPECT_LT(tog.hover_amount(), 1.0F);
}

TEST(UiWidgetsAnimation, CheckboxHoverAmountRampsUpFromTickDt)
{
    w::Checkbox cbx;
    cbx.set_rect(kRect100);

    cbx.tick(hover_input(kInsideX, kInsideY), 0.050F);
    EXPECT_TRUE(cbx.state().hovered);
    EXPECT_GT(cbx.hover_amount(), 0.0F);
    EXPECT_LT(cbx.hover_amount(), 1.0F);
}

TEST(UiWidgetsAnimation, SliderHoverAmountRampsUpFromTickDt)
{
    w::Slider sld;
    sld.set_rect(kRect100);

    sld.tick(hover_input(kInsideX, kInsideY), 0.050F);
    EXPECT_TRUE(sld.state().hovered);
    EXPECT_GT(sld.hover_amount(), 0.0F);
    EXPECT_LT(sld.hover_amount(), 1.0F);
}

// =============================================================================
// 3. After hover + 1s tick, hover_amount ~ 1.0 (within 0.05)
// =============================================================================

TEST(UiWidgetsAnimation, ButtonHoverAmountSaturatesAtOneSecond)
{
    w::Button btn { "Go" };
    btn.set_rect(kRect100);

    btn.tick(hover_input(kInsideX, kInsideY), 1.0F);  // 1 second
    EXPECT_NEAR(btn.hover_amount(), 1.0F, 0.05F);
}

TEST(UiWidgetsAnimation, AllWidgetsHoverAmountSaturatesAtOneSecond)
{
    w::Button   btn;
    w::Toggle   tog;
    w::Checkbox cbx;
    w::Slider   sld;
    btn.set_rect(kRect100);
    tog.set_rect(kRect100);
    cbx.set_rect(kRect100);
    sld.set_rect(kRect100);

    btn.tick(hover_input(kInsideX, kInsideY), 1.0F);
    tog.tick(hover_input(kInsideX, kInsideY), 1.0F);
    cbx.tick(hover_input(kInsideX, kInsideY), 1.0F);
    sld.tick(hover_input(kInsideX, kInsideY), 1.0F);

    EXPECT_NEAR(btn.hover_amount(), 1.0F, 0.05F);
    EXPECT_NEAR(tog.hover_amount(), 1.0F, 0.05F);
    EXPECT_NEAR(cbx.hover_amount(), 1.0F, 0.05F);
    EXPECT_NEAR(sld.hover_amount(), 1.0F, 0.05F);
}

// =============================================================================
// 4. Unhover after saturation -- 200ms tick, hover_amount decreasing
// =============================================================================

TEST(UiWidgetsAnimation, ButtonHoverAmountDecreasesAfterUnhover)
{
    w::Button btn { "Go" };
    btn.set_rect(kRect100);

    // Saturate to 1.0 first.
    btn.tick(hover_input(kInsideX, kInsideY), 1.0F);
    const float at_peak = btn.hover_amount();
    EXPECT_NEAR(at_peak, 1.0F, 0.05F);

    // Move pointer outside; advance 200ms (>= duration at default 0.1s, so
    // it should be fully down to 0 -- which is decreasing relative to 1.0).
    btn.tick(hover_input(kOutsideX, kOutsideY), 0.200F);
    EXPECT_FALSE(btn.state().hovered);
    EXPECT_LT(btn.hover_amount(), at_peak);
}

TEST(UiWidgetsAnimation, ButtonHoverAmountDecreasingMidTransition)
{
    // Mid-transition unhover: saturate halfway, then unhover with a short
    // dt -- amount should drop but not hit 0 immediately.
    w::Button btn { "Go" };
    btn.set_rect(kRect100);

    // Bring partway up: 0.03s tick (~30% under the 0.1s duration window).
    btn.tick(hover_input(kInsideX, kInsideY), 0.030F);
    const float partway = btn.hover_amount();
    EXPECT_GT(partway, 0.0F);
    EXPECT_LT(partway, 1.0F);

    // Pull the target back to 0 and advance a tiny dt -- value must shrink.
    btn.tick(hover_input(kOutsideX, kOutsideY), 0.010F);
    EXPECT_LT(btn.hover_amount(), partway);
}

// =============================================================================
// 5. Press during hover -- press_amount animates upward
// =============================================================================

TEST(UiWidgetsAnimation, ButtonPressAmountAnimatesWhilePressed)
{
    w::Button btn { "Go" };
    btn.set_rect(kRect100);

    // First reach a hover-saturated state so press doesn't race with hover.
    btn.tick(hover_input(kInsideX, kInsideY), 1.0F);
    EXPECT_FLOAT_EQ(btn.press_amount(), 0.0F);

    // Press inside + short dt -- press_amount must ramp.
    btn.tick(hover_input(kInsideX, kInsideY,
                         /*down=*/true, /*pressed=*/true, /*released=*/false),
             0.030F);
    EXPECT_TRUE(btn.state().pressed);
    EXPECT_GT(btn.press_amount(), 0.0F);
    EXPECT_LE(btn.press_amount(), 1.0F);
}

TEST(UiWidgetsAnimation, ToggleSliderCheckboxPressAmountAnimatesWhilePressed)
{
    w::Toggle   tog;
    w::Checkbox cbx;
    w::Slider   sld;
    tog.set_rect(kRect100);
    cbx.set_rect(kRect100);
    sld.set_rect(kRect100);

    // Hover-saturate first.
    tog.tick(hover_input(kInsideX, kInsideY), 1.0F);
    cbx.tick(hover_input(kInsideX, kInsideY), 1.0F);
    sld.tick(hover_input(kInsideX, kInsideY), 1.0F);

    EXPECT_FLOAT_EQ(tog.press_amount(), 0.0F);
    EXPECT_FLOAT_EQ(cbx.press_amount(), 0.0F);
    EXPECT_FLOAT_EQ(sld.press_amount(), 0.0F);

    const w::InputState press = hover_input(kInsideX, kInsideY,
                                            /*down=*/true,
                                            /*pressed=*/true,
                                            /*released=*/false);
    tog.tick(press, 0.030F);
    cbx.tick(press, 0.030F);
    sld.tick(press, 0.030F);

    EXPECT_TRUE(tog.state().pressed);
    EXPECT_TRUE(cbx.state().pressed);
    EXPECT_TRUE(sld.state().pressed);
    EXPECT_GT(tog.press_amount(), 0.0F);
    EXPECT_GT(cbx.press_amount(), 0.0F);
    EXPECT_GT(sld.press_amount(), 0.0F);
}

// =============================================================================
// 6. Focus animation -- focus_amount tween threads work for keyboard nav
// =============================================================================

TEST(UiWidgetsAnimation, ButtonFocusAmountAnimatesOnFocusGain)
{
    w::Button btn { "Go" };
    btn.set_rect(kRect100);
    EXPECT_FLOAT_EQ(btn.focus_amount(), 0.0F);

    // Become focused (pointer somewhere neutral).
    btn.tick(hover_input(kOutsideX, kOutsideY,
                         /*down=*/false, /*pressed=*/false, /*released=*/false,
                         /*focused=*/true),
             0.050F);
    EXPECT_TRUE(btn.state().focused);
    EXPECT_GT(btn.focus_amount(), 0.0F);
    EXPECT_LT(btn.focus_amount(), 1.0F);

    // After a full second the focus tween should saturate.
    btn.tick(hover_input(kOutsideX, kOutsideY,
                         /*down=*/false, /*pressed=*/false, /*released=*/false,
                         /*focused=*/true),
             1.0F);
    EXPECT_NEAR(btn.focus_amount(), 1.0F, 0.05F);
}

// =============================================================================
// 7. Custom animation policy -- faster speed compresses ramp time
// =============================================================================

TEST(UiWidgetsAnimation, FasterSpeedReachesSaturationSooner)
{
    w::Button slow;
    w::Button fast;
    slow.set_rect(kRect100);
    fast.set_rect(kRect100);

    w::WidgetAnimation slow_policy;
    slow_policy.speed_up = 2.0F;   // duration 0.5s
    slow.set_animation(slow_policy);

    w::WidgetAnimation fast_policy;
    fast_policy.speed_up = 40.0F;  // duration 0.025s
    fast.set_animation(fast_policy);

    // Tick both for 30 ms while hovered -- fast must outrun slow.
    slow.tick(hover_input(kInsideX, kInsideY), 0.030F);
    fast.tick(hover_input(kInsideX, kInsideY), 0.030F);

    EXPECT_GT(fast.hover_amount(), slow.hover_amount());
    EXPECT_NEAR(fast.hover_amount(), 1.0F, 0.05F);  // already saturated
}

// =============================================================================
// 8. Legacy tick(input) -- no dt = instant-snap fallback still works
// =============================================================================

TEST(UiWidgetsAnimation, LegacyTickWithoutDtDoesNotCrashAndPreservesState)
{
    w::Button btn { "Go" };
    btn.set_rect(kRect100);

    // No-dt tick should still update boolean state_ as before.
    btn.tick(hover_input(kInsideX, kInsideY));
    EXPECT_TRUE(btn.state().hovered);

    // hover_amount stays at 0 because dt was 0 (no animation advance).
    EXPECT_FLOAT_EQ(btn.hover_amount(), 0.0F);
}

// =============================================================================
// 9. Cross-widget headless draw smoke -- animated path emits no asserts
// =============================================================================

TEST(UiWidgetsAnimation, AnimatedDrawEmitsWithoutFontWithoutAsserts)
{
    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();

    const w::Theme theme {};

    w::Button   btn { "Go" };
    w::Toggle   tog;
    w::Checkbox cbx { false, "On" };
    w::Slider   sld { 0.4F };
    btn.set_rect(kRect100);
    tog.set_rect(kRect100);
    cbx.set_rect(kRect100);
    sld.set_rect(kRect100);

    // Drive each into a partial hover state mid-transition.
    btn.tick(hover_input(kInsideX, kInsideY), 0.040F);
    tog.tick(hover_input(kInsideX, kInsideY), 0.040F);
    cbx.tick(hover_input(kInsideX, kInsideY), 0.040F);
    sld.tick(hover_input(kInsideX, kInsideY), 0.040F);

    btn.draw(batcher, nullptr, theme);
    tog.draw(batcher, nullptr, theme);
    cbx.draw(batcher, nullptr, theme);
    sld.draw(batcher, nullptr, theme);

    EXPECT_GT(batcher.vertex_count(),  0U);
    EXPECT_GT(batcher.index_count(),   0U);
    EXPECT_GT(batcher.command_count(), 0U);
}

// =============================================================================
// CHROMODYNAMIC — cd::ui::widgets::CurveEditor tests
//
// Phase 5.17 coverage per ADR-20260530-ui-widget-library. CPU-only — no
// GPU, no font. Tests cover Curve::evaluate (all tangent modes), keyframe
// add / remove, multi-curve overlay isolation, and out-of-range clamping.
// CurveEditor interaction state is exercised without the draw path (same
// headless pattern as test_widgets.cpp).
//
// Test list (8+ cases):
//   1. evaluate at keyframe time returns keyframe value
//   2. Linear tangent gives straight segments between keys
//   3. Auto tangent uses Catmull-Rom-like smoothing (midpoint check)
//   4. Stepped tangent holds previous value until next key
//   5. Multi-curve overlay does not interfere with single-curve eval
//   6. Add keyframe via API (count increases, sorted by time)
//   7. Remove keyframe by index (count decreases, correct key removed)
//   8. Out-of-range t clamped to first / last keyframe value
//   9. CurveEditor::tick click-to-add keyframe on active curve
//  10. Hermite cubic passes through endpoints (kFree tangents zero => cubic arc)
// =============================================================================
#include <cd/ui/widgets/CurveEditor.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>

namespace w = cd::ui::widgets;

namespace
{

constexpr float kEps = 1e-4F;

// Build a simple two-keyframe Curve for convenience.
[[nodiscard]] w::Curve make_two_key(float t0, float v0,
                                    float t1, float v1,
                                    w::TangentMode mode = w::TangentMode::kAuto)
{
    w::Curve c{"test"};
    w::KeyFrame k0;
    k0.time  = t0;  k0.value = v0;  k0.mode = mode;
    w::KeyFrame k1;
    k1.time  = t1;  k1.value = v1;  k1.mode = mode;
    (void)c.add_keyframe(k0);
    (void)c.add_keyframe(k1);
    return c;
}

// Make a pointer-pressed InputState at the given pixel position.
[[nodiscard]] w::InputState press_at(float px, float py)
{
    w::InputState in;
    in.pointer.mouse_x      = px;
    in.pointer.mouse_y      = py;
    in.pointer.left_down    = true;
    in.pointer.left_pressed = true;
    return in;
}

// Make a no-input (idle) InputState.
[[nodiscard]] w::InputState idle_input()
{
    return w::InputState{};
}

}  // namespace

// =============================================================================
// 1. evaluate at keyframe time returns keyframe value (exact hit)
// =============================================================================
TEST(CurveEditor, EvaluateAtKeyframeTime)
{
    w::Curve c = make_two_key(0.0F, 3.0F, 1.0F, 7.0F);

    EXPECT_NEAR(c.evaluate(0.0F), 3.0F, kEps);
    EXPECT_NEAR(c.evaluate(1.0F), 7.0F, kEps);
}

// =============================================================================
// 2. Linear tangent gives straight segment between keys
// =============================================================================
TEST(CurveEditor, LinearTangentGivesStraightSegment)
{
    w::Curve c = make_two_key(0.0F, 0.0F, 1.0F, 1.0F, w::TangentMode::kLinear);

    // At t = 0.5 the linear interpolant must equal 0.5.
    EXPECT_NEAR(c.evaluate(0.5F), 0.5F, kEps);

    // At t = 0.25 must equal 0.25.
    EXPECT_NEAR(c.evaluate(0.25F), 0.25F, kEps);

    // At t = 0.75 must equal 0.75.
    EXPECT_NEAR(c.evaluate(0.75F), 0.75F, kEps);
}

// =============================================================================
// 3. Auto tangent uses Catmull-Rom-like smoothing
//    For a symmetric three-key curve with equal spacing, the midpoint
//    interior knot has slope = 0 (Catmull-Rom central diff over equal
//    intervals with equal rise). The curve at the midpoint should be
//    smooth (value equals the midpoint keyframe value exactly, and
//    the derivative sign matches the outer keys).
// =============================================================================
TEST(CurveEditor, AutoTangentSmoothing)
{
    // Three keys: (0,0), (0.5, 1), (1,0) -- a symmetric arch.
    w::Curve c{"arch"};
    {
        w::KeyFrame k0; k0.time = 0.0F; k0.value = 0.0F; k0.mode = w::TangentMode::kAuto;
        w::KeyFrame k1; k1.time = 0.5F; k1.value = 1.0F; k1.mode = w::TangentMode::kAuto;
        w::KeyFrame k2; k2.time = 1.0F; k2.value = 0.0F; k2.mode = w::TangentMode::kAuto;
        (void)c.add_keyframe(k0);
        (void)c.add_keyframe(k1);
        (void)c.add_keyframe(k2);
    }

    // The curve must pass through the apex at t=0.5.
    EXPECT_NEAR(c.evaluate(0.5F), 1.0F, kEps);

    // With auto Catmull-Rom, the slope at the interior knot (index 1) is
    //   0.5 * (v[2] - v[0]) / (t[2] - t[0]) = 0.5 * (0 - 0) / 1 = 0.
    // So the cubic is flat at the apex. The value at t=0.25 should be
    // above 0 (the curve bows upward, not straight).
    const float mid_left = c.evaluate(0.25F);
    EXPECT_GT(mid_left, 0.0F);
    EXPECT_LT(mid_left, 1.0F);

    // Symmetry: value at t=0.75 should mirror t=0.25.
    const float mid_right = c.evaluate(0.75F);
    EXPECT_NEAR(mid_left, mid_right, kEps);
}

// =============================================================================
// 4. Stepped tangent holds previous value until next key
// =============================================================================
TEST(CurveEditor, SteppedTangentHoldsPreviousValue)
{
    w::Curve c = make_two_key(0.0F, 5.0F, 1.0F, 10.0F, w::TangentMode::kStepped);

    // Throughout [0, 1) the value must equal the left keyframe value (5.0).
    EXPECT_NEAR(c.evaluate(0.0F),   5.0F, kEps);
    EXPECT_NEAR(c.evaluate(0.5F),   5.0F, kEps);
    EXPECT_NEAR(c.evaluate(0.999F), 5.0F, kEps);

    // At the right endpoint the clamp returns the right keyframe value (10.0).
    EXPECT_NEAR(c.evaluate(1.0F), 10.0F, kEps);
}

// =============================================================================
// 5. Multi-curve overlay does not interfere with single-curve eval
// =============================================================================
TEST(CurveEditor, MultiCurveOverlayIsolation)
{
    // Build two independent curves, place them both in a CurveEditor.
    w::Curve c0 = make_two_key(0.0F, 0.0F, 1.0F, 1.0F, w::TangentMode::kLinear);
    w::Curve c1 = make_two_key(0.0F, 10.0F, 1.0F, 20.0F, w::TangentMode::kLinear);

    w::CurveEditor editor;
    (void)editor.add_curve(std::move(c0));
    (void)editor.add_curve(std::move(c1));

    // Each curve evaluates independently.
    EXPECT_NEAR(editor.curve(0U).evaluate(0.5F),  0.5F,  kEps);
    EXPECT_NEAR(editor.curve(1U).evaluate(0.5F), 15.0F, kEps);

    // Modifying curve 1 must not affect curve 0.
    w::KeyFrame extra; extra.time = 0.5F; extra.value = 999.0F;
    extra.mode = w::TangentMode::kLinear;
    (void)editor.curve(1U).add_keyframe(extra);

    EXPECT_NEAR(editor.curve(0U).evaluate(0.5F), 0.5F, kEps);
}

// =============================================================================
// 6. Add keyframe via API — count increases, order maintained
// =============================================================================
TEST(CurveEditor, AddKeyframe)
{
    w::Curve c{"seq"};
    EXPECT_EQ(c.keyframe_count(), 0U);

    w::KeyFrame k0; k0.time = 0.0F; k0.value = 1.0F;
    w::KeyFrame k1; k1.time = 1.0F; k1.value = 2.0F;
    w::KeyFrame km; km.time = 0.5F; km.value = 1.5F;  // inserted in middle

    (void)c.add_keyframe(k0);
    EXPECT_EQ(c.keyframe_count(), 1U);

    (void)c.add_keyframe(k1);
    EXPECT_EQ(c.keyframe_count(), 2U);

    // Insert out-of-order — the array must re-sort.
    const std::size_t mid_idx = c.add_keyframe(km);
    EXPECT_EQ(c.keyframe_count(), 3U);

    // The returned index must be sorted (not 0 or 2).
    EXPECT_EQ(mid_idx, 1U);

    // Time ordering invariant.
    for (std::size_t i = 1U; i < c.keyframe_count(); ++i)
    {
        EXPECT_LE(c.keyframes()[i - 1U].time, c.keyframes()[i].time);
    }
}

// =============================================================================
// 7. Remove keyframe by index — count decreases, correct key removed
// =============================================================================
TEST(CurveEditor, RemoveKeyframe)
{
    w::Curve c{"rem"};
    w::KeyFrame k0; k0.time = 0.0F; k0.value = 0.0F;
    w::KeyFrame k1; k1.time = 0.5F; k1.value = 5.0F;
    w::KeyFrame k2; k2.time = 1.0F; k2.value = 0.0F;
    (void)c.add_keyframe(k0);
    (void)c.add_keyframe(k1);
    (void)c.add_keyframe(k2);
    ASSERT_EQ(c.keyframe_count(), 3U);

    // Remove the middle keyframe (index 1, time = 0.5).
    c.remove_keyframe(1U);
    EXPECT_EQ(c.keyframe_count(), 2U);

    // Remaining keys should be at t=0 and t=1.
    EXPECT_NEAR(c.keyframes()[0U].time, 0.0F, kEps);
    EXPECT_NEAR(c.keyframes()[1U].time, 1.0F, kEps);

    // Out-of-bounds remove is a no-op.
    c.remove_keyframe(999U);
    EXPECT_EQ(c.keyframe_count(), 2U);
}

// =============================================================================
// 8. Out-of-range t clamped to first / last keyframe value
// =============================================================================
TEST(CurveEditor, OutOfRangeClamped)
{
    w::Curve c = make_two_key(0.2F, 3.0F, 0.8F, 7.0F);

    // Before the first keyframe -> returns first value.
    EXPECT_NEAR(c.evaluate(0.0F),  3.0F, kEps);
    EXPECT_NEAR(c.evaluate(-5.0F), 3.0F, kEps);

    // After the last keyframe -> returns last value.
    EXPECT_NEAR(c.evaluate(1.0F),  7.0F, kEps);
    EXPECT_NEAR(c.evaluate(99.0F), 7.0F, kEps);

    // Empty curve returns 0.
    w::Curve empty{"empty"};
    EXPECT_NEAR(empty.evaluate(0.5F), 0.0F, kEps);

    // Single-key curve always returns that value.
    w::Curve single{"single"};
    w::KeyFrame ks; ks.time = 0.5F; ks.value = 42.0F;
    (void)single.add_keyframe(ks);
    EXPECT_NEAR(single.evaluate(0.0F), 42.0F, kEps);
    EXPECT_NEAR(single.evaluate(0.5F), 42.0F, kEps);
    EXPECT_NEAR(single.evaluate(1.0F), 42.0F, kEps);
}

// =============================================================================
// 9. CurveEditor::tick click-to-add adds a keyframe on the active curve
// =============================================================================
TEST(CurveEditor, TickClickAddsKeyframe)
{
    w::CurveEditor editor;
    w::Curve c{"click"};
    (void)editor.add_curve(std::move(c));
    editor.set_rect(w::Rect { 0.0F, 0.0F, 200.0F, 100.0F });
    editor.set_view_range(0.0F, 1.0F, 0.0F, 1.0F);

    ASSERT_EQ(editor.curve(0U).keyframe_count(), 0U);

    // Click at pixel (100, 50) — centre of the rect — should map to
    // curve space (0.5, 0.5).
    const w::InputState press = press_at(100.0F, 50.0F);
    const bool mutated = editor.tick(press);

    EXPECT_TRUE(mutated);
    EXPECT_EQ(editor.curve(0U).keyframe_count(), 1U);

    // Release to end drag.
    (void)editor.tick(idle_input());
}

// =============================================================================
// 10. kFree tangents zero -> Hermite cubic passes through both endpoints
//     and the midpoint is above the chord (bowing arc).
// =============================================================================
TEST(CurveEditor, FreeZeroTangentsHermiteArc)
{
    // kFree with in/out_tangent = 0 gives the "natural" cubic that passes
    // through both endpoints and is tangentially flat at each end.
    // For v0=0, v1=1 and zero tangents, the Hermite formula simplifies to:
    //   p(t) = 3t^2 - 2t^3  (the classic smoothstep).
    // At t=0.5:  3*(0.25) - 2*(0.125) = 0.75 - 0.25 = 0.5.
    w::Curve c{"hermite"};
    w::KeyFrame k0; k0.time = 0.0F; k0.value = 0.0F;
    k0.in_tangent = 0.0F; k0.out_tangent = 0.0F;
    k0.mode = w::TangentMode::kFree;

    w::KeyFrame k1; k1.time = 1.0F; k1.value = 1.0F;
    k1.in_tangent = 0.0F; k1.out_tangent = 0.0F;
    k1.mode = w::TangentMode::kFree;

    c.set_keyframes({ k0, k1 });

    EXPECT_NEAR(c.evaluate(0.0F), 0.0F, kEps);
    EXPECT_NEAR(c.evaluate(0.5F), 0.5F, kEps);   // smoothstep midpoint
    EXPECT_NEAR(c.evaluate(1.0F), 1.0F, kEps);

    // Verify arc shape: value at t=0.25 should equal smoothstep(0.25)
    //   = 3*(0.0625) - 2*(0.015625) = 0.1875 - 0.03125 = 0.15625.
    EXPECT_NEAR(c.evaluate(0.25F), 0.15625F, kEps);
}

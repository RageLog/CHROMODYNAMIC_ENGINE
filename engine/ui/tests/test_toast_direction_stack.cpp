// =============================================================================
// CHROMODYNAMIC -- cd::ui toast direction + multi-stack tests
// phase774 -- Direction enum + vertical stacking
//
// Test plan:
//   ToastDirection_RoundTrip_AllFour    -- push 4 toasts with each Direction;
//                                         verify the stored direction field matches.
//   ToastDirection_Anim_OffsetSign      -- direction determines sign/axis of
//                                         slide offset at slide midpoint.
//   ToastStack_ThreeToasts_Vertical     -- 3-toast vertical stack offsets are
//                                         monotonically increasing with 4px gap.
//   ToastStack_GapIsCorrect_DefaultFour -- default gap = 4 px arithmetic check.
// =============================================================================
#include <cd/ui/Toast.hpp>
#include <cd/ui/ToastAnim.hpp>
#include <gtest/gtest.h>

namespace
{

// ---------------------------------------------------------------------------
// Direction round-trip via ToastQueue::add_toast
// ---------------------------------------------------------------------------
TEST(ToastDirection, RoundTrip_AllFour)
{
    cd::ui::ToastQueue q;

    q.add_toast("right",  1.0, 0.0,
                cd::ui::ToastSeverity::kInfo, cd::ui::ToastDirection::kFromRight);
    q.add_toast("left",   1.0, 0.0,
                cd::ui::ToastSeverity::kInfo, cd::ui::ToastDirection::kFromLeft);
    q.add_toast("top",    1.0, 0.0,
                cd::ui::ToastSeverity::kInfo, cd::ui::ToastDirection::kFromTop);
    q.add_toast("bottom", 1.0, 0.0,
                cd::ui::ToastSeverity::kInfo, cd::ui::ToastDirection::kFromBottom);

    ASSERT_EQ(q.size(), 4u);
    const auto& a = q.active();
    EXPECT_EQ(a[0].direction, cd::ui::ToastDirection::kFromRight)
        << "first toast must be kFromRight";
    EXPECT_EQ(a[1].direction, cd::ui::ToastDirection::kFromLeft)
        << "second toast must be kFromLeft";
    EXPECT_EQ(a[2].direction, cd::ui::ToastDirection::kFromTop)
        << "third toast must be kFromTop";
    EXPECT_EQ(a[3].direction, cd::ui::ToastDirection::kFromBottom)
        << "fourth toast must be kFromBottom";
}

// ---------------------------------------------------------------------------
// Direction drives correct axis/sign of slide offset at midpoint (75 ms).
// ---------------------------------------------------------------------------
TEST(ToastDirection, Anim_OffsetSign_AtSlideMidpoint)
{
    constexpr double kMid = 75.0;

    {
        const auto r = cd::ui::toast_anim(kMid, cd::ui::ToastDirection::kFromRight);
        EXPECT_GT(r.x_offset, 0.0F) << "kFromRight: x_offset must be positive";
        EXPECT_FLOAT_EQ(r.y_offset, 0.0F) << "kFromRight: y_offset must be zero";
    }
    {
        const auto r = cd::ui::toast_anim(kMid, cd::ui::ToastDirection::kFromLeft);
        EXPECT_LT(r.x_offset, 0.0F) << "kFromLeft: x_offset must be negative";
        EXPECT_FLOAT_EQ(r.y_offset, 0.0F) << "kFromLeft: y_offset must be zero";
    }
    {
        const auto r = cd::ui::toast_anim(kMid, cd::ui::ToastDirection::kFromTop);
        EXPECT_FLOAT_EQ(r.x_offset, 0.0F) << "kFromTop: x_offset must be zero";
        EXPECT_LT(r.y_offset, 0.0F) << "kFromTop: y_offset must be negative";
    }
    {
        const auto r = cd::ui::toast_anim(kMid, cd::ui::ToastDirection::kFromBottom);
        EXPECT_FLOAT_EQ(r.x_offset, 0.0F) << "kFromBottom: x_offset must be zero";
        EXPECT_GT(r.y_offset, 0.0F) << "kFromBottom: y_offset must be positive";
    }
}

// ---------------------------------------------------------------------------
// Multi-stack: 3 toasts produce monotonically increasing Y offsets (4 px gap).
// ---------------------------------------------------------------------------
TEST(ToastStack, ThreeToasts_VerticalOffsets_Monotonic)
{
    constexpr float kH   = 40.0F;
    constexpr float kGap =  4.0F;

    const float y0 = cd::ui::toast_stack_y_offset(0, kH, kGap);
    const float y1 = cd::ui::toast_stack_y_offset(1, kH, kGap);
    const float y2 = cd::ui::toast_stack_y_offset(2, kH, kGap);

    EXPECT_FLOAT_EQ(y0, 0.0F)
        << "slot 0 must be at zero offset";
    EXPECT_FLOAT_EQ(y1, kH + kGap)
        << "slot 1 must be exactly one (height+gap) above slot 0";
    EXPECT_FLOAT_EQ(y2, 2.0F * (kH + kGap))
        << "slot 2 must be exactly two (height+gap) above slot 0";
    EXPECT_LT(y0, y1) << "offsets must be strictly increasing";
    EXPECT_LT(y1, y2) << "offsets must be strictly increasing";
}

// ---------------------------------------------------------------------------
// Default gap (4 px) arithmetic.
// ---------------------------------------------------------------------------
TEST(ToastStack, GapIsCorrect_DefaultFourPx)
{
    constexpr float kH = 30.0F;
    EXPECT_FLOAT_EQ(cd::ui::toast_stack_y_offset(1, kH), 34.0F)
        << "height=30 + gap=4 = 34";
    EXPECT_FLOAT_EQ(cd::ui::toast_stack_y_offset(2, kH), 68.0F)
        << "2 * (height=30 + gap=4) = 68";
}

}  // namespace
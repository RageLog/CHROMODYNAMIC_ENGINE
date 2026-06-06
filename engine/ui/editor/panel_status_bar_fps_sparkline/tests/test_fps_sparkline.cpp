// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_status_bar_fps_sparkline/tests/
//                 test_fps_sparkline.cpp
//
// phase787 — unit tests for cd::editor::status_bar::FpsSparkline.
//
// All tests are headless (no RHI, no ImGui).
//
//  1.  DefaultState           — fresh sparkline: filled() == 0, avg_ms() == 0.
//  2.  PushOneFrame           — after push(16.0) filled() == 1, avg ~= 16.
//  3.  PushIgnoresNonPositive — push(0) and push(-5) are silently ignored.
//  4.  RingOverflow           — pushing 121 samples keeps filled() == 120.
//  5.  AvgMsCorrect           — arithmetic mean is computed correctly.
//  6.  Reset                  — reset() brings filled() back to 0.
//  7.  DrawEmptyNoOp          — draw() with 0 samples emits background only.
//  8.  DrawZeroBoundsNoOp     — draw() with w==0 or h==0 emits 0 vertices.
//  9.  DrawGreenAvg           — avg < 16.67 ms → vertex count > 0.
// 10.  DrawAmberAvg           — avg in [16.67, 33.33) → vertex count > 0.
// 11.  DrawRedAvg             — avg >= 33.33 ms → vertex count > 0.
// 12.  DrawEmitsMoreVertsWhenFull — 120 samples emit more verts than 1.
// =============================================================================
#include <cd/editor/panel_status_bar_fps_sparkline/FpsSparkline.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

namespace spark = cd::editor::status_bar;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{

[[nodiscard]] cd::ui::widgets::Rect standard_bounds() noexcept
{
    // A typical status-bar strip for the sparkline.
    return { 0.0F, 0.0F, spark::FpsSparkline::kWidth, 16.0F };
}

[[nodiscard]] cd::ui::widgets::Theme default_theme() noexcept
{
    return cd::ui::widgets::Theme {};  // default warm-dark tokens
}

/// Draw into a fresh batcher and return the vertex count.
[[nodiscard]] std::size_t draw_count(
    const spark::FpsSparkline& s,
    const cd::ui::widgets::Rect& bounds = standard_bounds())
{
    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();
    s.draw(batcher, default_theme(), bounds);
    return batcher.vertex_count();
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// TEST 1 — DefaultState
// ---------------------------------------------------------------------------
TEST(FpsSparkline, DefaultState)
{
    const spark::FpsSparkline s;
    EXPECT_EQ(s.filled(), 0U);
    EXPECT_FLOAT_EQ(s.avg_ms(), 0.0F);
}

// ---------------------------------------------------------------------------
// TEST 2 — PushOneFrame
// ---------------------------------------------------------------------------
TEST(FpsSparkline, PushOneFrame)
{
    spark::FpsSparkline s;
    s.push(16.0F);
    EXPECT_EQ(s.filled(), 1U);
    EXPECT_NEAR(s.avg_ms(), 16.0F, 0.001F);
}

// ---------------------------------------------------------------------------
// TEST 3 — PushIgnoresNonPositive
// ---------------------------------------------------------------------------
TEST(FpsSparkline, PushIgnoresNonPositive)
{
    spark::FpsSparkline s;
    s.push(0.0F);
    s.push(-5.0F);
    EXPECT_EQ(s.filled(), 0U);
    EXPECT_FLOAT_EQ(s.avg_ms(), 0.0F);
}

// ---------------------------------------------------------------------------
// TEST 4 — RingOverflow
// ---------------------------------------------------------------------------
TEST(FpsSparkline, RingOverflow)
{
    spark::FpsSparkline s;
    // Push one more than capacity — filled() must not exceed kCapacity.
    for (std::size_t i = 0U; i <= spark::FpsSparkline::kCapacity; ++i)
    {
        s.push(16.0F);
    }
    EXPECT_EQ(s.filled(), spark::FpsSparkline::kCapacity);
}

// ---------------------------------------------------------------------------
// TEST 5 — AvgMsCorrect
// ---------------------------------------------------------------------------
TEST(FpsSparkline, AvgMsCorrect)
{
    spark::FpsSparkline s;
    s.push(10.0F);
    s.push(20.0F);
    s.push(30.0F);
    // avg = (10+20+30)/3 = 20
    EXPECT_NEAR(s.avg_ms(), 20.0F, 0.001F);
}

// ---------------------------------------------------------------------------
// TEST 6 — Reset
// ---------------------------------------------------------------------------
TEST(FpsSparkline, Reset)
{
    spark::FpsSparkline s;
    s.push(16.0F);
    ASSERT_EQ(s.filled(), 1U);

    s.reset();
    EXPECT_EQ(s.filled(), 0U);
    EXPECT_FLOAT_EQ(s.avg_ms(), 0.0F);
}

// ---------------------------------------------------------------------------
// TEST 7 — DrawEmptyNoOp (background only, no bar quads)
// ---------------------------------------------------------------------------
TEST(FpsSparkline, DrawEmptyNoOp)
{
    // An empty sparkline draws only the background fill (4 verts) and no bars.
    const spark::FpsSparkline s;
    const std::size_t verts = draw_count(s);
    // Background fill = 4 vertices; bar quads = 0; separator = 0 (empty).
    EXPECT_GT(verts, 0U);   // at least the background must be emitted
}

// ---------------------------------------------------------------------------
// TEST 8 — DrawZeroBoundsNoOp
// ---------------------------------------------------------------------------
TEST(FpsSparkline, DrawZeroBoundsNoOp)
{
    spark::FpsSparkline s;
    s.push(16.0F);

    const cd::ui::widgets::Rect zero_w { 0.0F, 0.0F,   0.0F, 16.0F };
    const cd::ui::widgets::Rect zero_h { 0.0F, 0.0F, 200.0F,  0.0F };

    EXPECT_EQ(draw_count(s, zero_w), 0U);
    EXPECT_EQ(draw_count(s, zero_h), 0U);
}

// ---------------------------------------------------------------------------
// TEST 9 — DrawGreenAvg (< 16.67 ms)
// ---------------------------------------------------------------------------
TEST(FpsSparkline, DrawGreenAvg)
{
    spark::FpsSparkline s;
    s.push(10.0F);  // avg = 10 ms → green tier
    EXPECT_LT(s.avg_ms(), spark::FpsSparkline::kThresholdGreen);
    EXPECT_GT(draw_count(s), 0U);
}

// ---------------------------------------------------------------------------
// TEST 10 — DrawAmberAvg (16.67 <= avg < 33.33 ms)
// ---------------------------------------------------------------------------
TEST(FpsSparkline, DrawAmberAvg)
{
    spark::FpsSparkline s;
    s.push(25.0F);  // avg = 25 ms → amber tier
    EXPECT_GE(s.avg_ms(), spark::FpsSparkline::kThresholdGreen);
    EXPECT_LT(s.avg_ms(), spark::FpsSparkline::kThresholdAmber);
    EXPECT_GT(draw_count(s), 0U);
}

// ---------------------------------------------------------------------------
// TEST 11 — DrawRedAvg (>= 33.33 ms)
// ---------------------------------------------------------------------------
TEST(FpsSparkline, DrawRedAvg)
{
    spark::FpsSparkline s;
    s.push(50.0F);  // avg = 50 ms → red tier
    EXPECT_GE(s.avg_ms(), spark::FpsSparkline::kThresholdAmber);
    EXPECT_GT(draw_count(s), 0U);
}

// ---------------------------------------------------------------------------
// TEST 12 — DrawEmitsMoreVertsWhenFull
// ---------------------------------------------------------------------------
TEST(FpsSparkline, DrawEmitsMoreVertsWhenFull)
{
    spark::FpsSparkline s_one;
    s_one.push(16.0F);
    const std::size_t verts_one = draw_count(s_one);

    spark::FpsSparkline s_full;
    for (std::size_t i = 0U; i < spark::FpsSparkline::kCapacity; ++i)
    {
        s_full.push(16.0F);
    }
    const std::size_t verts_full = draw_count(s_full);

    // More samples must produce more bar quads (hence more vertices).
    EXPECT_GT(verts_full, verts_one);
}

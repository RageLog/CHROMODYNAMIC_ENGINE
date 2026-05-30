// =============================================================================
// CHROMODYNAMIC — tests/test_time.cpp
// Phase 459 — cd::gameplay::time::TimeKeeper unit tests.
// =============================================================================
#include <cd/gameplay/time/Time.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::gameplay::time::GameTime;
using cd::gameplay::time::TimeKeeper;
using cd::gameplay::time::TimerCategory;

// 1) Initial state — fresh keeper reports zeros and unit scale.
TEST(GameplayTime, InitialStateIsZero)
{
    TimeKeeper clock;
    const GameTime t = clock.get();
    EXPECT_DOUBLE_EQ(t.elapsed_seconds, 0.0);
    EXPECT_DOUBLE_EQ(t.delta_seconds, 0.0);
    EXPECT_EQ(t.frame_index, 0U);
    EXPECT_DOUBLE_EQ(t.time_scale, 1.0);
    EXPECT_FALSE(clock.is_paused());
    EXPECT_EQ(clock.frame_index(), 0U);
}

// 2) Tick advances elapsed and delta on unscaled gameplay channel.
TEST(GameplayTime, TickAdvancesTime)
{
    TimeKeeper clock;
    ASSERT_TRUE(clock.tick(0.016));
    ASSERT_TRUE(clock.tick(0.016));
    ASSERT_TRUE(clock.tick(0.018));
    const GameTime t = clock.get();
    EXPECT_NEAR(t.elapsed_seconds, 0.050, 1e-12);
    EXPECT_NEAR(t.delta_seconds, 0.018, 1e-12);
    EXPECT_EQ(t.frame_index, 3U);
}

// 3) Pause freezes elapsed_seconds but real delta still reports; resume
//    continues accumulating.
TEST(GameplayTime, PauseHaltsTimeResumeContinues)
{
    TimeKeeper clock;
    clock.tick(1.0);             // elapsed = 1.0
    clock.pause();
    EXPECT_TRUE(clock.is_paused());

    clock.tick(2.0);             // paused — elapsed unchanged
    const GameTime mid = clock.get();
    EXPECT_DOUBLE_EQ(mid.elapsed_seconds, 1.0);
    EXPECT_DOUBLE_EQ(mid.delta_seconds, 2.0);  // real dt still surfaced
    EXPECT_EQ(mid.frame_index, 2U);            // frame index still bumped

    clock.resume();
    EXPECT_FALSE(clock.is_paused());
    clock.tick(0.5);             // elapsed += 0.5
    const GameTime end = clock.get();
    EXPECT_DOUBLE_EQ(end.elapsed_seconds, 1.5);
    EXPECT_EQ(end.frame_index, 3U);
}

// 4) time_scale = 2.0 doubles gameplay elapsed.
TEST(GameplayTime, TimeScaleTwoDoublesElapsed)
{
    TimeKeeper clock;
    clock.set_time_scale(2.0);
    clock.tick(1.0);
    clock.tick(1.0);
    const GameTime t = clock.get();
    EXPECT_DOUBLE_EQ(t.elapsed_seconds, 4.0);
    EXPECT_DOUBLE_EQ(t.delta_seconds, 1.0);    // real dt unchanged
    EXPECT_DOUBLE_EQ(t.time_scale, 2.0);
}

// 5) time_scale = 0.5 halves gameplay elapsed (slow-mo).
TEST(GameplayTime, TimeScaleHalfHalvesElapsed)
{
    TimeKeeper clock;
    clock.set_time_scale(0.5);
    clock.tick(1.0);
    clock.tick(1.0);
    const GameTime t = clock.get();
    EXPECT_DOUBLE_EQ(t.elapsed_seconds, 1.0);
    EXPECT_DOUBLE_EQ(t.delta_seconds, 1.0);
    EXPECT_DOUBLE_EQ(t.time_scale, 0.5);
}

// 6) Frame index increments per tick regardless of pause / scale.
TEST(GameplayTime, FrameIndexIncrementsEvenWhenPaused)
{
    TimeKeeper clock;
    clock.pause();
    clock.tick(0.1);
    clock.tick(0.1);
    clock.set_time_scale(0.0);
    clock.tick(0.1);
    EXPECT_EQ(clock.frame_index(), 3U);
    EXPECT_EQ(clock.get().frame_index, 3U);
    EXPECT_DOUBLE_EQ(clock.get().elapsed_seconds, 0.0);  // paused or 0-scale
}

// 7) Multiple channels are independent — UI keeps running while gameplay
//    is paused; simulation channel can use its own scale.
TEST(GameplayTime, MultipleChannelsIndependent)
{
    TimeKeeper clock;
    clock.set_time_scale(TimerCategory::kSimulation, 4.0);

    clock.tick(0.25);
    clock.pause();                                      // pause gameplay only
    clock.tick(0.25);
    clock.tick(0.25);

    const GameTime gameplay   = clock.get(TimerCategory::kGameplay);
    const GameTime ui         = clock.get(TimerCategory::kUi);
    const GameTime simulation = clock.get(TimerCategory::kSimulation);

    // Gameplay accumulated only the first 0.25 (then paused).
    EXPECT_NEAR(gameplay.elapsed_seconds, 0.25, 1e-12);
    // UI ignores gameplay pause — full 0.75 elapsed.
    EXPECT_NEAR(ui.elapsed_seconds, 0.75, 1e-12);
    // Simulation runs at 4x — 0.75 * 4 = 3.0 elapsed.
    EXPECT_NEAR(simulation.elapsed_seconds, 3.0, 1e-12);
    // All channels share the same frame counter.
    EXPECT_EQ(gameplay.frame_index, 3U);
    EXPECT_EQ(ui.frame_index, 3U);
    EXPECT_EQ(simulation.frame_index, 3U);
}

// 8) Negative dt is rejected defensively — no state change, no frame bump,
//    tick() returns false.
TEST(GameplayTime, NegativeDtRejected)
{
    TimeKeeper clock;
    ASSERT_TRUE(clock.tick(1.0));
    const std::uint64_t before_frame   = clock.frame_index();
    const double        before_elapsed = clock.get().elapsed_seconds;
    const double        before_delta   = clock.get().delta_seconds;

    EXPECT_FALSE(clock.tick(-0.001));
    EXPECT_FALSE(clock.tick(-1e9));

    EXPECT_EQ(clock.frame_index(), before_frame);
    EXPECT_DOUBLE_EQ(clock.get().elapsed_seconds, before_elapsed);
    EXPECT_DOUBLE_EQ(clock.get().delta_seconds, before_delta);

    // A valid tick after rejection still works.
    ASSERT_TRUE(clock.tick(0.5));
    EXPECT_EQ(clock.frame_index(), before_frame + 1U);
    EXPECT_DOUBLE_EQ(clock.get().elapsed_seconds, before_elapsed + 0.5);
}

// 9) Bonus: negative scale clamped to zero (no time travel).
TEST(GameplayTime, NegativeScaleClampedToZero)
{
    TimeKeeper clock;
    clock.set_time_scale(-2.0);
    EXPECT_DOUBLE_EQ(clock.time_scale(), 0.0);
    clock.tick(1.0);
    EXPECT_DOUBLE_EQ(clock.get().elapsed_seconds, 0.0);
    EXPECT_EQ(clock.frame_index(), 1U);
}

// 10) Bonus: reset() returns to construction state.
TEST(GameplayTime, ResetReturnsToInitialState)
{
    TimeKeeper clock;
    clock.set_time_scale(3.0);
    clock.pause();
    clock.tick(1.0);
    clock.tick(1.0);

    clock.reset();
    const GameTime t = clock.get();
    EXPECT_DOUBLE_EQ(t.elapsed_seconds, 0.0);
    EXPECT_DOUBLE_EQ(t.delta_seconds, 0.0);
    EXPECT_DOUBLE_EQ(t.time_scale, 1.0);
    EXPECT_EQ(t.frame_index, 0U);
    EXPECT_FALSE(clock.is_paused());
}

}  // namespace

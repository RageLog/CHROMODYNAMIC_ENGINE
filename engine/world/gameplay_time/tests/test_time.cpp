// =============================================================================
// CHROMODYNAMIC — tests/test_time.cpp
// Phase 459 — cd::gameplay::time::TimeKeeper unit tests.
// Phase 1253 — extended edge/negative coverage + FixedStepAccumulator tests.
// =============================================================================
#include <cd/gameplay/time/Time.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::gameplay::time::FixedStepAccumulator;
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

// =============================================================================
// Phase 1253 extended tests — edge, negative, accumulator.
// =============================================================================

// 11) Zero dt tick is accepted (returns true), no elapsed change, frame bumps.
TEST(GameplayTime, ZeroDtAcceptedFrameStillBumps)
{
    TimeKeeper clock;
    ASSERT_TRUE(clock.tick(0.0));
    EXPECT_DOUBLE_EQ(clock.get().elapsed_seconds, 0.0);
    EXPECT_DOUBLE_EQ(clock.get().delta_seconds,   0.0);
    EXPECT_EQ(clock.frame_index(), 1U);
}

// 12) Very large dt is accepted (no artificial cap at TimeKeeper level).
//     The spiral-of-death clamp lives in FixedStepAccumulator, not here.
TEST(GameplayTime, VeryLargeDtAccepted)
{
    TimeKeeper clock;
    ASSERT_TRUE(clock.tick(1e6));
    EXPECT_DOUBLE_EQ(clock.get().elapsed_seconds, 1e6);
    EXPECT_EQ(clock.frame_index(), 1U);
}

// 13) time_scale == 0.0 freezes elapsed (equivalent to pause but via scale).
//     delta_seconds still reflects real dt; frame_index still bumps.
TEST(GameplayTime, TimeScaleZeroFreezesElapsed)
{
    TimeKeeper clock;
    clock.set_time_scale(0.0);
    EXPECT_DOUBLE_EQ(clock.time_scale(), 0.0);

    clock.tick(1.0);
    clock.tick(1.0);
    const GameTime t = clock.get();
    EXPECT_DOUBLE_EQ(t.elapsed_seconds, 0.0);   // frozen
    EXPECT_DOUBLE_EQ(t.delta_seconds,   1.0);   // real dt
    EXPECT_EQ(t.frame_index, 2U);
    EXPECT_DOUBLE_EQ(t.time_scale, 0.0);
}

// 14) Per-channel pause: pausing kUi does not affect kGameplay or kSimulation.
TEST(GameplayTime, PerChannelPauseUi)
{
    TimeKeeper clock;
    clock.set_paused(TimerCategory::kUi, true);
    EXPECT_TRUE (clock.is_paused(TimerCategory::kUi));
    EXPECT_FALSE(clock.is_paused(TimerCategory::kGameplay));
    EXPECT_FALSE(clock.is_paused(TimerCategory::kSimulation));

    clock.tick(1.0);
    EXPECT_DOUBLE_EQ(clock.get(TimerCategory::kUi).elapsed_seconds,         0.0);
    EXPECT_DOUBLE_EQ(clock.get(TimerCategory::kGameplay).elapsed_seconds,   1.0);
    EXPECT_DOUBLE_EQ(clock.get(TimerCategory::kSimulation).elapsed_seconds, 1.0);
}

// 15) Pause/resume idempotency — double-pause must not cause double-resume.
TEST(GameplayTime, PauseResumeIdempotency)
{
    TimeKeeper clock;
    clock.pause();
    clock.pause();   // second pause — still paused
    EXPECT_TRUE(clock.is_paused());

    clock.tick(1.0);
    EXPECT_DOUBLE_EQ(clock.get().elapsed_seconds, 0.0);

    clock.resume();
    clock.resume();  // double-resume — still running
    EXPECT_FALSE(clock.is_paused());

    clock.tick(1.0);
    EXPECT_DOUBLE_EQ(clock.get().elapsed_seconds, 1.0);
}

// 16) reset() restores ALL channels' time_scale to 1.0 and clears pause.
TEST(GameplayTime, ResetRestoresAllChannelScaleAndPause)
{
    TimeKeeper clock;
    clock.set_time_scale(TimerCategory::kGameplay,   5.0);
    clock.set_time_scale(TimerCategory::kUi,         0.1);
    clock.set_time_scale(TimerCategory::kSimulation, 10.0);
    clock.set_paused(TimerCategory::kUi, true);
    clock.tick(1.0);

    clock.reset();

    EXPECT_DOUBLE_EQ(clock.time_scale(TimerCategory::kGameplay),   1.0);
    EXPECT_DOUBLE_EQ(clock.time_scale(TimerCategory::kUi),         1.0);
    EXPECT_DOUBLE_EQ(clock.time_scale(TimerCategory::kSimulation),  1.0);
    EXPECT_FALSE(clock.is_paused(TimerCategory::kUi));
    EXPECT_EQ(clock.frame_index(), 0U);
}

// 17) scaled_delta() returns real_dt * time_scale for active channel; 0 when paused.
TEST(GameplayTime, ScaledDeltaReflectsScaleAndPause)
{
    TimeKeeper clock;
    clock.set_time_scale(TimerCategory::kGameplay, 2.0);
    clock.set_time_scale(TimerCategory::kUi,       0.5);
    clock.tick(1.0);

    EXPECT_DOUBLE_EQ(clock.scaled_delta(TimerCategory::kGameplay),   2.0);
    EXPECT_DOUBLE_EQ(clock.scaled_delta(TimerCategory::kUi),         0.5);
    EXPECT_DOUBLE_EQ(clock.scaled_delta(TimerCategory::kSimulation),  1.0);

    clock.pause();
    clock.tick(1.0);
    EXPECT_DOUBLE_EQ(clock.scaled_delta(TimerCategory::kGameplay), 0.0);  // paused
    EXPECT_DOUBLE_EQ(clock.scaled_delta(TimerCategory::kUi),       0.5);  // unaffected
}

// 18) get() time_scale field always matches set_time_scale, even after reset.
TEST(GameplayTime, GetTimeScaleFieldMatchesSetter)
{
    TimeKeeper clock;
    clock.set_time_scale(TimerCategory::kSimulation, 3.5);
    EXPECT_DOUBLE_EQ(clock.get(TimerCategory::kSimulation).time_scale, 3.5);
    clock.reset();
    EXPECT_DOUBLE_EQ(clock.get(TimerCategory::kSimulation).time_scale, 1.0);
}

// 19) Accumulate+resume: elapsed continues from freeze point, not from zero.
TEST(GameplayTime, ResumeAccumulatesFromFreezePoint)
{
    TimeKeeper clock;
    clock.tick(0.5);              // elapsed = 0.5
    clock.pause();
    clock.tick(100.0);            // elapsed frozen
    clock.tick(100.0);
    EXPECT_DOUBLE_EQ(clock.get().elapsed_seconds, 0.5);

    clock.resume();
    clock.tick(0.3);              // elapsed = 0.5 + 0.3
    EXPECT_NEAR(clock.get().elapsed_seconds, 0.8, 1e-12);
}

// =============================================================================
// FixedStepAccumulator tests
// =============================================================================

// 20) Single sub-step: 0.5 of a 1-second step yields 0 steps + residual 0.5.
TEST(FixedStepAccumulator, SubStepNoFullStepYet)
{
    FixedStepAccumulator accum { 1.0 };
    const int steps = accum.accumulate(0.5);
    EXPECT_EQ(steps, 0);
    EXPECT_NEAR(accum.residual(), 0.5, 1e-12);
}

// 21) Exactly one step's worth of dt produces exactly 1 step, zero residual.
TEST(FixedStepAccumulator, ExactlyOneStep)
{
    FixedStepAccumulator accum { 1.0 / 60.0 };
    const int steps = accum.accumulate(1.0 / 60.0);
    EXPECT_EQ(steps, 1);
    EXPECT_NEAR(accum.residual(), 0.0, 1e-12);
}

// 22) Multi-step catch-up: 2.5 steps worth → 2 steps + residual 0.5*step.
TEST(FixedStepAccumulator, MultiStepCatchUp)
{
    FixedStepAccumulator accum { 0.1 };
    const int steps = accum.accumulate(0.25);   // 2.5 * 0.1 steps
    EXPECT_EQ(steps, 2);
    EXPECT_NEAR(accum.residual(), 0.05, 1e-9);
}

// 23) Spiral-of-death clamp: huge dt capped at max_steps * step_seconds.
TEST(FixedStepAccumulator, SpiralOfDeathClamp)
{
    // max_steps = 4, step = 0.016 → max catchup = 0.064s regardless of dt.
    FixedStepAccumulator accum { 0.016, 4 };
    const int steps = accum.accumulate(10.0);   // 625 steps worth!
    EXPECT_EQ(steps, 4);
    // Residual after clamped accumulation: 4 * 0.016 - 4 * 0.016 = 0.
    EXPECT_NEAR(accum.residual(), 0.0, 1e-12);
}

// 24) Residual carry persists across frames and accumulates toward next step.
TEST(FixedStepAccumulator, ResidualCarryAcrossFrames)
{
    // step = 1.0; feed 0.4 three times → 0.4+0.4=0.8 → 0.8+0.4=1.2 → 1 step
    FixedStepAccumulator accum { 1.0 };
    EXPECT_EQ(accum.accumulate(0.4), 0);
    EXPECT_EQ(accum.accumulate(0.4), 0);
    EXPECT_NEAR(accum.residual(), 0.8, 1e-12);
    const int steps = accum.accumulate(0.4);
    EXPECT_EQ(steps, 1);
    EXPECT_NEAR(accum.residual(), 0.2, 1e-9);
}

// 25) reset() clears residual; subsequent accumulation restarts from zero.
TEST(FixedStepAccumulator, ResetClearsResidual)
{
    FixedStepAccumulator accum { 1.0 };
    const int steps_before_reset = accum.accumulate(0.9);  // residual = 0.9
    EXPECT_EQ(steps_before_reset, 0);
    EXPECT_NEAR(accum.residual(), 0.9, 1e-12);

    accum.reset();
    EXPECT_DOUBLE_EQ(accum.residual(), 0.0);

    // After reset, accumulate again from scratch.
    EXPECT_EQ(accum.accumulate(0.5), 0);
    EXPECT_NEAR(accum.residual(), 0.5, 1e-12);
}

// 26) Negative / zero dt to accumulate() yields 0 steps, residual unchanged.
TEST(FixedStepAccumulator, NegativeOrZeroDtProducesZeroSteps)
{
    FixedStepAccumulator accum { 1.0 };
    const int seed_steps = accum.accumulate(0.3);  // residual = 0.3
    EXPECT_EQ(seed_steps, 0);

    EXPECT_EQ(accum.accumulate(0.0),   0);
    EXPECT_EQ(accum.accumulate(-1.0),  0);
    EXPECT_NEAR(accum.residual(), 0.3, 1e-12);  // unchanged
}

// 27) Invalid step_seconds (≤0) is clamped to 1/60 internally; max_steps ≤0 clamped to 1.
TEST(FixedStepAccumulator, InvalidConstructorArgsClamped)
{
    FixedStepAccumulator accum { -5.0, -3 };
    EXPECT_GT(accum.step_seconds(), 0.0);
    EXPECT_GE(accum.max_steps(), 1);
}

// 28) Accumulator integrated with TimeKeeper scaled_delta — full usage pattern.
TEST(FixedStepAccumulator, IntegratedWithTimeKeeperScaledDelta)
{
    TimeKeeper            clock;
    FixedStepAccumulator  accum { 1.0 / 60.0 };  // 60 Hz physics

    clock.set_time_scale(2.0);   // 2× fast-forward
    clock.tick(1.0 / 60.0);      // real dt = 1/60 → scaled = 2/60

    const double s_dt  = clock.scaled_delta();
    const int    steps = accum.accumulate(s_dt);
    // 2/60 ÷ 1/60 = 2 steps
    EXPECT_EQ(steps, 2);
    EXPECT_NEAR(accum.residual(), 0.0, 1e-12);
}

// 29) Paused clock → scaled_delta == 0 → accumulator produces 0 steps,
//     residual carries from previous frame (simulation stays deterministic).
TEST(FixedStepAccumulator, PausedClockZeroSteps)
{
    TimeKeeper            clock;
    FixedStepAccumulator  accum { 1.0 / 60.0 };

    // Frame 1: normal — build up 0.5 residual of a step.
    clock.tick(1.0 / 120.0);   // half a step
    EXPECT_EQ(accum.accumulate(clock.scaled_delta()), 0);
    EXPECT_NEAR(accum.residual(), 1.0 / 120.0, 1e-12);

    // Frame 2: gameplay paused — no steps, residual preserved.
    clock.pause();
    clock.tick(1.0);
    EXPECT_DOUBLE_EQ(clock.scaled_delta(), 0.0);
    EXPECT_EQ(accum.accumulate(clock.scaled_delta()), 0);
    EXPECT_NEAR(accum.residual(), 1.0 / 120.0, 1e-12);  // still carrying
}

}  // namespace

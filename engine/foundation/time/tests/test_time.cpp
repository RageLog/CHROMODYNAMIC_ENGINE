// =============================================================================
// CHROMODYNAMIC — cd::time tests (Sprint S2.1.d)
// =============================================================================
#include <cd/time/FramePacer.hpp>
#include <cd/time/HiResClock.hpp>
#include <cd/time/IClock.hpp>
#include <cd/time/RateLimiter.hpp>
#include <cd/time/SimClock.hpp>
#include <cd/time/SteadyClock.hpp>
#include <cd/time/TimerQueue.hpp>
#include <cd/time/Types.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

namespace
{

using cd::time::Duration;
using cd::time::Milliseconds;
using cd::time::Nanoseconds;
using cd::time::Seconds;

// --- Types + conversions ---------------------------------------------------
TEST(TimeTypes, ToSeconds)
{
    EXPECT_DOUBLE_EQ(cd::time::to_seconds(std::chrono::seconds { 1 }), 1.0);
    EXPECT_DOUBLE_EQ(cd::time::to_seconds(std::chrono::milliseconds { 500 }), 0.5);
}

TEST(TimeTypes, Elapsed)
{
    cd::time::TimePoint a {};
    cd::time::TimePoint b = a + Milliseconds { 100 };
    EXPECT_EQ(cd::time::elapsed<Milliseconds>(a, b).count(), 100);
}

// --- SteadyClock ----------------------------------------------------------
TEST(SteadyClock, NowIsMonotonic)
{
    cd::time::SteadyClock c;
    auto t0 = c.now();
    std::this_thread::sleep_for(std::chrono::milliseconds { 2 });
    auto t1 = c.now();
    EXPECT_GT(t1, t0);
    EXPECT_EQ(c.mode(), cd::time::TimeMode::WallClock);
    EXPECT_FALSE(c.is_simulation());
}

TEST(SteadyClock, FreeFunctionMatchesInstance)
{
    auto t = cd::time::now();
    auto u = cd::time::SteadyClock::instance().now();
    EXPECT_LE(t, u);
}

// --- HiResClock -----------------------------------------------------------
TEST(HiResClock, MonotonicAndPositive)
{
    std::uint64_t a = cd::time::hires_now_ns();
    std::uint64_t b = cd::time::hires_now_ns();
    EXPECT_GT(a, 0u);
    EXPECT_GE(b, a);
}

TEST(HiResClock, SubMillisecondResolution)
{
    std::uint64_t a = cd::time::hires_now_ns();
    // Busy-loop briefly to ensure we observe sub-ms differences.
    volatile int sink = 0;
    for (int i = 0; i < 1000; ++i)
        sink += i;
    std::uint64_t b = cd::time::hires_now_ns();
    EXPECT_GT(b, a);
    EXPECT_LT(b - a, 1'000'000ull);  // should be sub-millisecond on any modern CPU
    (void)sink;
}

// --- SimClock --------------------------------------------------------------
TEST(SimClock, StartsAtZero)
{
    cd::time::SimClock sc;
    EXPECT_EQ(sc.elapsed(), Duration::zero());
    EXPECT_EQ(sc.tick_count(), 0u);
    EXPECT_FALSE(sc.is_paused());
    EXPECT_DOUBLE_EQ(sc.scale(), 1.0);
    EXPECT_EQ(sc.mode(), cd::time::TimeMode::Simulation);
}

TEST(SimClock, TickAdvancesByScaledDelta)
{
    cd::time::SimClock sc;
    auto advanced = sc.tick(Milliseconds { 100 });
    EXPECT_EQ(std::chrono::duration_cast<Milliseconds>(advanced).count(), 100);
    EXPECT_EQ(std::chrono::duration_cast<Milliseconds>(sc.elapsed()).count(), 100);
    EXPECT_EQ(sc.tick_count(), 1u);
}

TEST(SimClock, PauseStopsAdvancement)
{
    cd::time::SimClock sc;
    sc.pause();
    EXPECT_TRUE(sc.is_paused());
    auto advanced = sc.tick(Milliseconds { 500 });
    EXPECT_EQ(advanced, Duration::zero());
    EXPECT_EQ(sc.elapsed(), Duration::zero());
    sc.resume();
    sc.tick(Milliseconds { 100 });
    EXPECT_EQ(std::chrono::duration_cast<Milliseconds>(sc.elapsed()).count(), 100);
}

TEST(SimClock, ScaleHalvesDelta)
{
    cd::time::SimClock sc;
    sc.set_scale(0.5);
    EXPECT_DOUBLE_EQ(sc.scale(), 0.5);
    auto advanced = sc.tick(Milliseconds { 200 });
    EXPECT_EQ(std::chrono::duration_cast<Milliseconds>(advanced).count(), 100);
}

TEST(SimClock, ScaleClampedToValidRange)
{
    cd::time::SimClock sc;
    sc.set_scale(-1.0);
    EXPECT_DOUBLE_EQ(sc.scale(), cd::time::SimClock::kMinScale);
    sc.set_scale(1000.0);
    EXPECT_DOUBLE_EQ(sc.scale(), cd::time::SimClock::kMaxScale);
}

TEST(SimClock, StepBypassesPause)
{
    cd::time::SimClock sc { Milliseconds { 20 } };
    sc.pause();
    auto advanced = sc.step();
    EXPECT_EQ(std::chrono::duration_cast<Milliseconds>(advanced).count(), 20);
    EXPECT_EQ(std::chrono::duration_cast<Milliseconds>(sc.elapsed()).count(), 20);
}

TEST(SimClock, ResetClearsAccumulatedAndCount)
{
    cd::time::SimClock sc;
    sc.tick(Milliseconds { 500 });
    sc.tick(Milliseconds { 500 });
    EXPECT_NE(sc.elapsed(), Duration::zero());
    EXPECT_EQ(sc.tick_count(), 2u);
    sc.reset();
    EXPECT_EQ(sc.elapsed(), Duration::zero());
    EXPECT_EQ(sc.tick_count(), 0u);
}

TEST(SimClock, DeterministicSequence)
{
    cd::time::SimClock a;
    cd::time::SimClock b;
    for (int i = 0; i < 100; ++i)
    {
        a.tick(Milliseconds { 16 });
        b.tick(Milliseconds { 16 });
    }
    EXPECT_EQ(a.elapsed(), b.elapsed());
    EXPECT_EQ(a.tick_count(), b.tick_count());
}

// --- FramePacer ------------------------------------------------------------
TEST(FramePacer, NoStepsWhenDeltaBelowSimStep)
{
    cd::time::FramePacer fp { cd::time::FramePacerOptions { Milliseconds { 16 } } };
    auto r = fp.update(Milliseconds { 8 });
    EXPECT_EQ(r.sim_steps, 0u);
    EXPECT_GT(r.alpha, 0.0);
    EXPECT_LT(r.alpha, 1.0);
}

TEST(FramePacer, OneStepWhenAccumulatorMatches)
{
    cd::time::FramePacer fp { cd::time::FramePacerOptions { Milliseconds { 16 } } };
    auto r = fp.update(Milliseconds { 16 });
    EXPECT_EQ(r.sim_steps, 1u);
}

TEST(FramePacer, MultipleStepsWithinBudget)
{
    cd::time::FramePacer fp {
        cd::time::FramePacerOptions { Milliseconds { 10 }, 4 /*max_substeps*/ }
    };
    auto r = fp.update(Milliseconds { 35 });
    EXPECT_EQ(r.sim_steps, 3u);
    // 5 ms remainder → alpha = 0.5
    EXPECT_NEAR(r.alpha, 0.5, 1e-9);
}

TEST(FramePacer, SpiralOfDeathClampToMaxSubsteps)
{
    cd::time::FramePacer fp {
        cd::time::FramePacerOptions { Milliseconds { 10 }, 4 /*max_substeps*/ }
    };
    auto r = fp.update(Milliseconds { 500 });  // huge stall
    EXPECT_LE(r.sim_steps, 4u);
    EXPECT_EQ(fp.accumulator(), Duration::zero());
}

// --- TimerQueue ------------------------------------------------------------
TEST(TimerQueue, StartStopIdempotent)
{
    cd::time::TimerQueue tq;
    EXPECT_FALSE(tq.is_running());
    tq.start();
    EXPECT_TRUE(tq.is_running());
    tq.start();  // idempotent
    tq.stop();
    EXPECT_FALSE(tq.is_running());
    tq.stop();  // idempotent
}

TEST(TimerQueue, ScheduledCallbackFires)
{
    cd::time::TimerQueue tq;
    tq.start();
    std::atomic<int> fired { 0 };
    std::atomic<bool> was_cancelled { false };
    tq.schedule_after(
        std::chrono::milliseconds { 20 },
        [&](bool c)
        {
            fired.fetch_add(1);
            was_cancelled.store(c);
        }
    );
    std::this_thread::sleep_for(std::chrono::milliseconds { 150 });
    EXPECT_EQ(fired.load(), 1);
    EXPECT_FALSE(was_cancelled.load());
    tq.stop();
}

TEST(TimerQueue, StopCancelsPendingCallbacks)
{
    cd::time::TimerQueue tq;
    tq.start();
    std::atomic<int> cancelled { 0 };
    for (int i = 0; i < 5; ++i)
    {
        tq.schedule_after(
            std::chrono::seconds { 60 },
            [&](bool c)
            {
                if (c)
                    cancelled.fetch_add(1);
            }
        );
    }
    tq.stop();
    EXPECT_EQ(cancelled.load(), 5);
}

// --- RateLimiter / IntervalTicker — Wave 57 --------------------------------

TEST(RateLimiter, UncappedIsNoOp)
{
    cd::time::RateLimiter rl { 0 };
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; ++i)
        rl.await_next_frame();
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    EXPECT_LT(elapsed, std::chrono::milliseconds { 50 });
}

TEST(RateLimiter, PeriodMatchesTargetHz)
{
    cd::time::RateLimiter rl { 60 };
    EXPECT_EQ(rl.target_hz(), 60U);
    EXPECT_GT(rl.period().count(), 0);
    // Period should be ~16.67 ms for 60 Hz.
    const auto ms = std::chrono::duration_cast<std::chrono::microseconds>(rl.period()).count();
    EXPECT_NEAR(static_cast<double>(ms), 16'667.0, 50.0);
}

TEST(RateLimiter, AwaitsTargetPeriodOnAverage)
{
    // 100 Hz target → 10 ms per frame. Verify the average over 5
    // frames is at least the target floor (no busy-spin runaway).
    // Upper bound is generous (3× target) because parallel test
    // execution can starve the spin tail on noisy CI hosts.
    cd::time::RateLimiter rl { 100 };
    const auto t0 = std::chrono::steady_clock::now();
    constexpr int kFrames = 5;
    for (int i = 0; i < kFrames; ++i)
        rl.await_next_frame();
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    const auto expected = std::chrono::milliseconds { 10 } * kFrames;
    EXPECT_GE(elapsed, expected - std::chrono::milliseconds { 5 });
    EXPECT_LT(elapsed, expected * 3);
}

TEST(IntervalTicker, FiresAtFixedCadenceWithSimClock)
{
    cd::time::SimClock sim;
    cd::time::IntervalTicker tick { std::chrono::milliseconds { 100 }, &sim };

    // First call primes the deadline → false.
    EXPECT_FALSE(tick.tick());
    sim.tick(std::chrono::milliseconds { 50 });
    EXPECT_FALSE(tick.tick());  // still before deadline
    sim.tick(std::chrono::milliseconds { 60 });  // total 110 ms
    EXPECT_TRUE(tick.tick());   // fires
    EXPECT_EQ(tick.fire_count(), 1U);
    sim.tick(std::chrono::milliseconds { 100 });
    EXPECT_TRUE(tick.tick());   // second interval
    EXPECT_EQ(tick.fire_count(), 2U);
}

TEST(IntervalTicker, ResetClearsCounter)
{
    cd::time::SimClock sim;
    cd::time::IntervalTicker tick { std::chrono::milliseconds { 10 }, &sim };
    EXPECT_FALSE(tick.tick());
    sim.tick(std::chrono::milliseconds { 20 });
    EXPECT_TRUE(tick.tick());
    EXPECT_EQ(tick.fire_count(), 1U);
    tick.reset();
    EXPECT_EQ(tick.fire_count(), 0U);
}

// --- SimClock edge / negative / set_time -----------------------------------

TEST(SimClock, ZeroDtTickIncrementsCount)
{
    cd::time::SimClock sc;
    sc.tick(Duration::zero());
    // Zero real-dt should still count as a tick but add no time.
    EXPECT_EQ(sc.tick_count(), 1U);
    EXPECT_EQ(sc.elapsed(), Duration::zero());
}

TEST(SimClock, NegativeDtIsIgnoredByZeroScale)
{
    // When scale is 0 the scaled duration is always zero regardless of real dt.
    cd::time::SimClock sc;
    sc.set_scale(0.0);
    const auto advanced = sc.tick(Milliseconds { -100 });
    EXPECT_EQ(advanced, Duration::zero());
    EXPECT_EQ(sc.elapsed(), Duration::zero());
}

TEST(SimClock, LargeDeltaDoesNotOverflow)
{
    // Advance by ~1 year in one tick; no UB / wrap.
    cd::time::SimClock sc;
    const auto one_year = std::chrono::hours { 365 * 24 };
    sc.tick(std::chrono::duration_cast<Duration>(one_year));
    EXPECT_GT(sc.elapsed(), Duration::zero());
    EXPECT_EQ(sc.tick_count(), 1U);
}

TEST(SimClock, ScaleZeroEffectivelyPauses)
{
    cd::time::SimClock sc;
    sc.set_scale(0.0);
    const auto advanced = sc.tick(Milliseconds { 100 });
    EXPECT_EQ(advanced, Duration::zero());
    EXPECT_EQ(sc.elapsed(), Duration::zero());
}

TEST(SimClock, SetTimeJumpsAccumulated)
{
    cd::time::SimClock sc;
    sc.tick(Milliseconds { 50 });
    const auto jump = std::chrono::duration_cast<Duration>(Milliseconds { 999 });
    sc.set_time(jump);
    EXPECT_EQ(sc.elapsed(), jump);
    // tick_count is NOT reset by set_time
    EXPECT_EQ(sc.tick_count(), 1U);
}

TEST(SimClock, SetTimeNegativeClampsToZero)
{
    cd::time::SimClock sc;
    sc.tick(Milliseconds { 200 });
    sc.set_time(Milliseconds { -50 });
    EXPECT_EQ(sc.elapsed(), Duration::zero());
}

TEST(SimClock, ResumeAfterPauseDoesNotLooseScale)
{
    cd::time::SimClock sc;
    sc.set_scale(2.0);
    sc.pause();
    sc.resume();
    EXPECT_DOUBLE_EQ(sc.scale(), 2.0);
    const auto advanced = sc.tick(Milliseconds { 100 });
    EXPECT_EQ(std::chrono::duration_cast<Milliseconds>(advanced).count(), 200);
}

// --- FramePacer edge / boundary / reset ------------------------------------

TEST(FramePacer, AlphaIsZeroAtExactMultiple)
{
    // 3 × 10 ms real dt with 10 ms step → 3 substeps, 0 leftover → alpha==0.
    cd::time::FramePacer fp {
        cd::time::FramePacerOptions { Milliseconds { 10 }, 8 }
    };
    const auto r = fp.update(Milliseconds { 30 });
    EXPECT_EQ(r.sim_steps, 3U);
    EXPECT_NEAR(r.alpha, 0.0, 1e-12);
    EXPECT_EQ(r.leftover, Duration::zero());
}

TEST(FramePacer, ResetClearsAccumulator)
{
    cd::time::FramePacer fp { cd::time::FramePacerOptions { Milliseconds { 16 } } };
    fp.update(Milliseconds { 8 });
    EXPECT_NE(fp.accumulator(), Duration::zero());
    fp.reset();
    EXPECT_EQ(fp.accumulator(), Duration::zero());
}

TEST(FramePacer, SetOptionsUpdatesStep)
{
    cd::time::FramePacer fp { cd::time::FramePacerOptions { Milliseconds { 16 } } };
    fp.set_options(cd::time::FramePacerOptions { Milliseconds { 8 }, 8 });
    EXPECT_EQ(fp.options().sim_step, std::chrono::duration_cast<Duration>(Milliseconds { 8 }));
    EXPECT_EQ(fp.options().max_substeps, 8U);
}

TEST(FramePacer, CatchUpAtMaxSubstepsDropsResidue)
{
    // Accumulator already past max*step — the clamp must reset accumulator to zero.
    cd::time::FramePacer fp {
        cd::time::FramePacerOptions { Milliseconds { 10 }, 2 }
    };
    fp.update(Milliseconds { 100 });  // way over budget
    // After clamp, accumulator should be zero (no persistent backlog).
    EXPECT_EQ(fp.accumulator(), Duration::zero());
}

TEST(FramePacer, LeftoverMatchesAccumulator)
{
    cd::time::FramePacer fp {
        cd::time::FramePacerOptions { Milliseconds { 10 }, 4 }
    };
    const auto r = fp.update(Milliseconds { 15 });
    // 15 ms → 1 step consumed (10 ms), 5 ms left.
    EXPECT_EQ(r.leftover, fp.accumulator());
    EXPECT_EQ(std::chrono::duration_cast<Milliseconds>(r.leftover).count(), 5);
}

// --- RateLimiter injected-clock edges --------------------------------------

TEST(RateLimiter, SetTargetHzReinitialisesPeriod)
{
    cd::time::RateLimiter rl { 60 };
    rl.set_target_hz(120);
    EXPECT_EQ(rl.target_hz(), 120U);
    const auto us =
        std::chrono::duration_cast<std::chrono::microseconds>(rl.period()).count();
    // 120 Hz → ~8333 µs
    EXPECT_NEAR(static_cast<double>(us), 8'333.0, 100.0);
}

TEST(RateLimiter, ResetClearsLastWake)
{
    cd::time::RateLimiter rl { 60 };
    rl.reset();
    // After reset, last_frame_duration() should be zero.
    EXPECT_EQ(rl.last_frame_duration(), Duration::zero());
}

// --- TimerQueue fire-order + past-deadline + cancel token ------------------

TEST(TimerQueue, FiresInEarliestDeadlineOrder)
{
    // Schedule two timers: 40 ms then 10 ms. The 10 ms one must fire first.
    cd::time::TimerQueue tq;
    tq.start();

    std::vector<int> order;
    std::mutex order_mutex;
    std::condition_variable order_cv;

    tq.schedule_after(
        std::chrono::milliseconds { 40 },
        [&](bool)
        {
            {
                std::scoped_lock lk { order_mutex };
                order.push_back(2);
            }
            order_cv.notify_all();
        }
    );
    tq.schedule_after(
        std::chrono::milliseconds { 10 },
        [&](bool)
        {
            {
                std::scoped_lock lk { order_mutex };
                order.push_back(1);
            }
            order_cv.notify_all();
        }
    );

    // Wait until both have fired (or timeout after 300 ms).
    {
        std::unique_lock lk { order_mutex };
        order_cv.wait_for(lk, std::chrono::milliseconds { 300 }, [&] { return order.size() >= 2U; });
    }

    ASSERT_EQ(order.size(), 2U);
    EXPECT_EQ(order[0], 1);  // shorter delay fires first
    EXPECT_EQ(order[1], 2);
    tq.stop();
}

TEST(TimerQueue, PastDeadlineFiresImmediately)
{
    // A timer scheduled with 0 ms delay should fire on the very next loop iteration.
    cd::time::TimerQueue tq;
    tq.start();
    std::atomic<bool> fired { false };
    std::condition_variable cv;
    [[maybe_unused]] std::mutex cv_mutex;

    tq.schedule_after(
        std::chrono::milliseconds { 0 },
        [&](bool)
        {
            fired.store(true);
            cv.notify_all();
        }
    );

    std::unique_lock lk { cv_mutex };
    cv.wait_for(lk, std::chrono::milliseconds { 200 }, [&] { return fired.load(); });
    EXPECT_TRUE(fired.load());
    tq.stop();
}

TEST(TimerQueue, CancelViaStopToken)
{
    // Schedule a long-running timer but cancel it before it fires via stop_token.
    cd::time::TimerQueue tq;
    tq.start();

    std::stop_source src;
    std::atomic<bool> called { false };
    std::atomic<bool> was_cancelled { false };
    std::condition_variable cv;
    [[maybe_unused]] std::mutex cv_mutex;

    tq.schedule_after(
        std::chrono::seconds { 60 },
        [&](bool c)
        {
            called.store(true);
            was_cancelled.store(c);
            cv.notify_all();
        },
        src.get_token()
    );

    // Request cancellation immediately.
    src.request_stop();

    // The callback should fire (with cancelled=true) only when tq.stop() drains.
    tq.stop();

    EXPECT_TRUE(called.load());
    EXPECT_TRUE(was_cancelled.load());
}

TEST(TimerQueue, SequenceIdsMonotonicallyIncreasing)
{
    cd::time::TimerQueue tq;
    tq.start();
    const auto id1 = tq.schedule_after(std::chrono::milliseconds { 100 }, [](bool) {});
    const auto id2 = tq.schedule_after(std::chrono::milliseconds { 100 }, [](bool) {});
    EXPECT_LT(id1, id2);
    tq.stop();
}

// Regression: schedule_after must return the id of the timer JUST scheduled, not
// the min-heap top. Scheduling a NEAR-deadline timer first makes it the heap top;
// a subsequent FAR-deadline timer's id must be its own sequence (pre-fix both
// calls aliased to the near timer's sequence via entries_.top()).
TEST(TimerQueue, ScheduleAfterReturnsScheduledIdNotHeapTop)
{
    cd::time::TimerQueue tq;
    tq.start();
    const auto id_near = tq.schedule_after(std::chrono::milliseconds { 10 }, [](bool) {});
    const auto id_far  = tq.schedule_after(std::chrono::seconds { 3600 },    [](bool) {});
    EXPECT_NE(id_near, id_far) << "far-deadline timer id must not alias the heap-top id";
    EXPECT_LT(id_near, id_far);
    tq.stop();
}

// --- HiResClock IClock interface -------------------------------------------

TEST(HiResClock, ModeIsHiResRaw)
{
    cd::time::HiResClock c;
    EXPECT_EQ(c.mode(), cd::time::TimeMode::HiResRaw);
    EXPECT_FALSE(c.is_simulation());
}

TEST(HiResClock, NowViaInterfaceIsMonotonic)
{
    cd::time::IClock& clk = cd::time::HiResClock::instance();
    const auto t0 = clk.now();
    volatile int sink = 0;
    for (int i = 0; i < 10'000; ++i)
        sink += i;
    const auto t1 = clk.now();
    EXPECT_GE(t1, t0);
    (void)sink;
}

// --- IClock now_as<> template ----------------------------------------------

TEST(IClockNowAs, SimClockNowAsMilliseconds)
{
    cd::time::SimClock sc;
    sc.tick(Milliseconds { 250 });
    const auto ms = sc.now_as<Milliseconds>();
    EXPECT_EQ(ms.count(), 250);
}

}  // namespace

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

#include <chrono>
#include <thread>

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

}  // namespace

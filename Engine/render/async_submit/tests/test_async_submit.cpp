// =============================================================================
// CHROMODYNAMIC — test_async_submit.cpp
//
// Stress + correctness tests for cd::render::AsyncSubmit. Covers:
//   * Single enqueue → job runs on worker thread (different TID)
//   * Sequential enqueues block correctly (single-slot)
//   * 1000-iteration stress with simulated frame work
//   * Clean shutdown (destructor joins, no deadlock)
//   * is_busy / wait_idle / counters
// =============================================================================
#include <cd/render/AsyncSubmit.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

TEST(AsyncSubmit, EnqueueRunsOnWorkerThread)
{
    cd::render::AsyncSubmit async;
    const auto main_tid = std::this_thread::get_id();
    std::thread::id worker_tid;
    bool ran = false;
    async.enqueue([&] {
        worker_tid = std::this_thread::get_id();
        ran = true;
    });
    async.wait_idle();
    EXPECT_TRUE(ran);
    EXPECT_NE(worker_tid, main_tid);
    EXPECT_EQ(async.enqueue_count(), 1u);
    EXPECT_EQ(async.completion_count(), 1u);
}

TEST(AsyncSubmit, SequentialEnqueueBlocksSingleSlot)
{
    cd::render::AsyncSubmit async;
    std::atomic<int> counter { 0 };
    constexpr int kJobs = 16;
    for (int i = 0; i < kJobs; ++i)
    {
        async.enqueue([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds { 1 });
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }
    async.wait_idle();
    EXPECT_EQ(counter.load(), kJobs);
    EXPECT_EQ(async.enqueue_count(), static_cast<std::uint64_t>(kJobs));
    EXPECT_EQ(async.completion_count(), static_cast<std::uint64_t>(kJobs));
}

TEST(AsyncSubmit, FrameLoopStressOneThousandIterations)
{
    // Simulate the "wait_idle + record + enqueue end_frame" loop a real
    // sample would run. Each iteration: bump a shared counter on the
    // worker, mimic ~50µs of "GPU work" via a tight loop. 1000 iters.
    cd::render::AsyncSubmit async;
    std::atomic<std::uint64_t> frames_completed { 0 };

    auto t_start = std::chrono::steady_clock::now();
    constexpr int kFrames = 1000;
    for (int i = 0; i < kFrames; ++i)
    {
        async.wait_idle();  // ensure previous frame done
        async.enqueue([&] {
            // Simulate frame work — busy-wait a fixed integer amount.
            volatile std::uint64_t spin = 0;
            for (std::uint64_t k = 0; k < 256; ++k)
                spin += k * k;
            (void)spin;
            frames_completed.fetch_add(1, std::memory_order_release);
        });
    }
    async.wait_idle();
    auto t_end = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
    EXPECT_EQ(frames_completed.load(), static_cast<std::uint64_t>(kFrames));
    EXPECT_EQ(async.completion_count(), static_cast<std::uint64_t>(kFrames));
    // Sanity: 1000 frames should complete in well under 10 seconds even
    // on a heavily loaded CI runner.
    EXPECT_LT(ms, 10'000);
}

TEST(AsyncSubmit, IsBusyTracksState)
{
    cd::render::AsyncSubmit async;
    std::atomic<bool> release { false };
    async.enqueue([&] {
        while (!release.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::microseconds { 50 });
    });
    // Busy almost immediately. The worker may not have picked up the job
    // yet, but busy_ is set inside enqueue() under the lock, so this is
    // strictly observable as true.
    EXPECT_TRUE(async.is_busy());
    release.store(true, std::memory_order_release);
    async.wait_idle();
    EXPECT_FALSE(async.is_busy());
}

TEST(AsyncSubmit, DestructorJoinsCleanly)
{
    // Construct + destruct a few async submitters with pending work
    // to exercise the join path. No assertion needed beyond "doesn't
    // deadlock" — if this test returns the destructor unwound cleanly.
    for (int i = 0; i < 10; ++i)
    {
        cd::render::AsyncSubmit async;
        async.enqueue([] { std::this_thread::sleep_for(std::chrono::milliseconds { 1 }); });
        // Let it drop without explicit wait_idle — destructor handles it.
    }
    SUCCEED();
}

TEST(AsyncSubmit, EmptyJobIsNoop)
{
    cd::render::AsyncSubmit async;
    async.enqueue({});  // null function
    async.wait_idle();
    EXPECT_EQ(async.completion_count(), 1u);  // counted as completed.
}

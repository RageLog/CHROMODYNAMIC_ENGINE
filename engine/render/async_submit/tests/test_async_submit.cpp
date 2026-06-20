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
#include <cstddef>
#include <mutex>
#include <stdexcept>
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

// =============================================================================
// AsyncSubmitN (Wave 30) — N-deep ring queue pipelining
// =============================================================================

#include <cd/render/AsyncSubmitN.hpp>

TEST(AsyncSubmitN, ProducerStaysAheadByCapacity)
{
    cd::render::AsyncSubmitN q { 3 };
    std::atomic<int> done { 0 };
    auto slow_job = [&] {
        std::this_thread::sleep_for(std::chrono::milliseconds { 5 });
        done.fetch_add(1, std::memory_order_release);
    };
    // 3 enqueues should not block (capacity = 3, worker just starting).
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 3; ++i)
        q.enqueue(slow_job);
    auto t1 = std::chrono::steady_clock::now();
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count(), 10);
    q.wait_idle();
    EXPECT_EQ(done.load(), 3);
    EXPECT_EQ(q.completion_count(), 3u);
}

TEST(AsyncSubmitN, EnqueueBlocksWhenFull)
{
    cd::render::AsyncSubmitN q { 2 };
    std::atomic<bool> release { false };
    // Worker stuck on the first job.
    q.enqueue([&] {
        while (!release.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::microseconds { 100 });
    });
    q.enqueue([] {});  // capacity-1 slot
    // Third enqueue must block until worker releases first job.
    std::thread t([&] { q.enqueue([] {}); });
    std::this_thread::sleep_for(std::chrono::milliseconds { 20 });
    EXPECT_EQ(q.completion_count(), 0u);  // first job still spinning
    release.store(true, std::memory_order_release);
    t.join();
    q.wait_idle();
    EXPECT_EQ(q.completion_count(), 3u);
}

TEST(AsyncSubmitN, OneThousandFrameStress)
{
    cd::render::AsyncSubmitN q { 3 };
    std::atomic<std::uint64_t> frames { 0 };
    for (int i = 0; i < 1000; ++i)
    {
        q.enqueue([&] {
            volatile std::uint64_t s = 0;
            for (std::uint64_t k = 0; k < 64; ++k)
                s += k * k;
            (void)s;
            frames.fetch_add(1, std::memory_order_release);
        });
    }
    q.wait_idle();
    EXPECT_EQ(frames.load(), 1000u);
    EXPECT_EQ(q.completion_count(), 1000u);
}

TEST(AsyncSubmitN, DestructorDrainsAndJoins)
{
    for (int i = 0; i < 5; ++i)
    {
        cd::render::AsyncSubmitN q { 4 };
        for (int k = 0; k < 4; ++k)
            q.enqueue([] { std::this_thread::sleep_for(std::chrono::milliseconds { 1 }); });
        // Let destructor handle drain + stop + join.
    }
    SUCCEED();
}

// =============================================================================
// BAND 3 — genuinely-untested AsyncSubmitN ring edges. The minimal-primitive
// scope (no frame-fence integration) is sealed in
// ADR-20260616-band3-render-core-scope.md §2.2; these pin the constructor +
// ring-wrap branches the existing tests never exercised.
// =============================================================================

TEST(AsyncSubmitN, ZeroCapacityDefaultsToTwo)
{
    // The `capacity == 0 ? 2 : capacity` constructor branch was never hit —
    // every other test passes an explicit capacity. Default ctor + explicit 0
    // must both land on 2.
    cd::render::AsyncSubmitN q_default;
    EXPECT_EQ(q_default.capacity(), 2u);
    cd::render::AsyncSubmitN q_zero { 0 };
    EXPECT_EQ(q_zero.capacity(), 2u);
}

TEST(AsyncSubmitN, CapacityOneSerializesAndWrapsRing)
{
    // capacity == 1 forces the head_ = (head_+1) % capacity_ wrap to fire on
    // every job (modulo-1 is the degenerate ring). Many jobs through a 1-slot
    // queue must all complete in FIFO with no lost slot.
    cd::render::AsyncSubmitN q { 1 };
    EXPECT_EQ(q.capacity(), 1u);
    std::atomic<int> order { 0 };
    std::vector<int> seen;
    std::mutex seen_mu;
    constexpr int kJobs = 20;
    for (int i = 0; i < kJobs; ++i)
    {
        q.enqueue([&, i] {
            const int rank = order.fetch_add(1, std::memory_order_acq_rel);
            (void)rank;
            std::scoped_lock guard { seen_mu };
            seen.push_back(i);
        });
    }
    q.wait_idle();
    ASSERT_EQ(seen.size(), static_cast<std::size_t>(kJobs));
    for (int i = 0; i < kJobs; ++i)
        EXPECT_EQ(seen[static_cast<std::size_t>(i)], i);  // strict FIFO
    EXPECT_EQ(q.completion_count(), static_cast<std::uint64_t>(kJobs));
    EXPECT_EQ(q.enqueue_count(), static_cast<std::uint64_t>(kJobs));
    EXPECT_EQ(q.size(), 0u);
}

TEST(AsyncSubmitN, WaitIdleOnFreshQueueReturnsImmediately)
{
    // wait_idle() with size_==0 && !running_ must not block (the predicate is
    // already satisfied) — the "nothing enqueued yet" branch.
    cd::render::AsyncSubmitN q { 3 };
    q.wait_idle();
    EXPECT_EQ(q.size(), 0u);
    EXPECT_EQ(q.completion_count(), 0u);
    EXPECT_EQ(q.enqueue_count(), 0u);
}

// ----- AsyncSubmit (single-slot) untested counters/empty edges --------------

TEST(AsyncSubmit, FreshSubmitterHasZeroCounters)
{
    cd::render::AsyncSubmit async;
    EXPECT_EQ(async.enqueue_count(), 0u);
    EXPECT_EQ(async.completion_count(), 0u);
    EXPECT_FALSE(async.is_busy());
    async.wait_idle();  // no-op on an idle fresh submitter
    EXPECT_FALSE(async.is_busy());
}

// =============================================================================
// DEPTH PASS — per-submit CompletionToken (renderer-agnostic completion signal,
// the "small clean testable hook" from the brief) + worker exception isolation
// + shutdown/drain edge & negative coverage. Anti-flakiness: every wait is on a
// cv/atomic/token — NO sleep_for in the new paths.
// =============================================================================

// ----- CompletionToken: empty / default-constructed contract ----------------

TEST(CompletionToken, DefaultConstructedReportsCompleteAndInvalid)
{
    // Arrange / Act
    const cd::render::CompletionToken tok;
    // Assert: an empty token has nothing to wait for → complete, no exception.
    EXPECT_FALSE(tok.valid());
    EXPECT_TRUE(tok.is_complete());
    EXPECT_FALSE(tok.has_exception());
    tok.wait();                 // must not block / crash
    tok.rethrow_if_exception(); // must not throw
    SUCCEED();
}

// ----- AsyncSubmit (single-slot) tracked submission -------------------------

TEST(AsyncSubmit, EnqueueTrackedTokenCompletesAfterJob)
{
    // Arrange
    cd::render::AsyncSubmit async;
    std::atomic<bool> ran { false };

    // Act
    auto tok = async.enqueue_tracked([&] { ran.store(true, std::memory_order_release); });
    EXPECT_TRUE(tok.valid());
    tok.wait();  // block on THIS submission, not global idle

    // Assert: token completion happens-after the job body + the worker's
    // completion bookkeeping (signal fires last in the loop iteration).
    EXPECT_TRUE(ran.load(std::memory_order_acquire));
    EXPECT_TRUE(tok.is_complete());
    EXPECT_FALSE(tok.has_exception());
    EXPECT_EQ(async.completion_count(), 1u);
}

TEST(AsyncSubmit, EnqueueTrackedEmptyJobCompletesToken)
{
    // Negative-ish: a null job is a no-op but still completes its token.
    cd::render::AsyncSubmit async;
    auto tok = async.enqueue_tracked({});
    tok.wait();
    EXPECT_TRUE(tok.is_complete());
    EXPECT_FALSE(tok.has_exception());
    EXPECT_EQ(async.completion_count(), 1u);
}

TEST(AsyncSubmit, UntrackedEnqueueDoesNotAffectTrackedToken)
{
    // A tracked submit followed by plain enqueue: the first token tracks ONLY
    // its own job. Single-slot semantics mean the second enqueue blocks until
    // the first completes, so by the time both return the token is complete.
    cd::render::AsyncSubmit async;
    std::atomic<int> hits { 0 };
    auto tok = async.enqueue_tracked([&] { hits.fetch_add(1, std::memory_order_acq_rel); });
    async.enqueue([&] { hits.fetch_add(1, std::memory_order_acq_rel); });
    async.wait_idle();
    EXPECT_TRUE(tok.is_complete());
    EXPECT_EQ(hits.load(), 2);
    EXPECT_EQ(async.completion_count(), 2u);
}

TEST(AsyncSubmit, DiscardedTokenSignalsWithoutCrash)
{
    // Lifetime edge: drop the token immediately. The worker still signals the
    // (now orphaned) shared state — shared_ptr keeps it alive until signalled.
    cd::render::AsyncSubmit async;
    std::atomic<bool> ran { false };
    {
        auto tok = async.enqueue_tracked([&] { ran.store(true, std::memory_order_release); });
        (void)tok;  // immediately goes out of scope
    }
    async.wait_idle();
    EXPECT_TRUE(ran.load(std::memory_order_acquire));
    EXPECT_EQ(async.completion_count(), 1u);
}

TEST(AsyncSubmit, WorkerSurvivesThrowingJobAndCapturesException)
{
    // Worker exception isolation: a throwing job must NOT terminate the worker.
    // The token captures the exception; a subsequent good job still runs.
    cd::render::AsyncSubmit async;
    auto bad = async.enqueue_tracked([] { throw std::runtime_error { "boom" }; });
    bad.wait();
    EXPECT_TRUE(bad.is_complete());
    EXPECT_TRUE(bad.has_exception());
    EXPECT_THROW(bad.rethrow_if_exception(), std::runtime_error);

    // Worker is still alive: a fresh submission completes normally.
    std::atomic<bool> ran { false };
    auto good = async.enqueue_tracked([&] { ran.store(true, std::memory_order_release); });
    good.wait();
    EXPECT_TRUE(ran.load(std::memory_order_acquire));
    EXPECT_FALSE(good.has_exception());
    EXPECT_EQ(async.completion_count(), 2u);  // both counted as completed
}

TEST(AsyncSubmit, RethrowIsNoopWhenJobReturnsNormally)
{
    cd::render::AsyncSubmit async;
    auto tok = async.enqueue_tracked([] { /* no throw */ });
    tok.wait();
    EXPECT_FALSE(tok.has_exception());
    tok.rethrow_if_exception();  // must NOT throw
    SUCCEED();
}

TEST(AsyncSubmit, DoubleWaitIdleIsIdempotent)
{
    // Draining twice with nothing pending must not block or corrupt state.
    cd::render::AsyncSubmit async;
    async.enqueue([] {});
    async.wait_idle();
    async.wait_idle();  // second drain — predicate already satisfied
    EXPECT_FALSE(async.is_busy());
    EXPECT_EQ(async.completion_count(), 1u);
}

// ----- AsyncSubmitN (N-slot ring) tracked submission + ordering -------------

TEST(AsyncSubmitN, EnqueueTrackedTokensCompleteInFifoOrder)
{
    // Ring + tokens: N tracked submits; each token completes when its own job
    // finishes. With capacity 3 and FIFO execution the tokens complete in
    // submission order. We gate each token on the next via the worker's order.
    cd::render::AsyncSubmitN q { 3 };
    std::atomic<int> order { 0 };
    std::vector<cd::render::CompletionToken> toks;
    std::vector<int> ranks(8, -1);
    std::mutex ranks_mu;
    constexpr int kJobs = 8;
    toks.reserve(kJobs);
    for (int i = 0; i < kJobs; ++i)
    {
        toks.push_back(q.enqueue_tracked([&, i] {
            const int r = order.fetch_add(1, std::memory_order_acq_rel);
            std::scoped_lock guard { ranks_mu };
            ranks[static_cast<std::size_t>(i)] = r;
        }));
    }
    for (auto& t : toks)
        t.wait();
    // Every token completed; FIFO means job i ran at rank i.
    for (int i = 0; i < kJobs; ++i)
    {
        EXPECT_TRUE(toks[static_cast<std::size_t>(i)].is_complete());
        EXPECT_EQ(ranks[static_cast<std::size_t>(i)], i);
    }
    EXPECT_EQ(q.completion_count(), static_cast<std::uint64_t>(kJobs));
}

TEST(AsyncSubmitN, TrackedTokenBackpressureOnFullRing)
{
    // Backpressure: capacity 1 forces the producer to block on enqueue_tracked
    // until the worker drains the prior slot. We synchronize the first job's
    // release via an atomic flag set FROM the test thread — no sleep_for.
    cd::render::AsyncSubmitN q { 1 };
    std::atomic<bool> release { false };
    std::atomic<bool> first_started { false };

    auto t0 = q.enqueue_tracked([&] {
        first_started.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire))
            std::this_thread::yield();  // spin, not sleep — released by the test
    });
    // Spin until the worker has actually picked up the first job so the ring
    // is genuinely occupied (capacity 1).
    while (!first_started.load(std::memory_order_acquire))
        std::this_thread::yield();

    // The second tracked enqueue must block until the first job is released.
    std::atomic<bool> second_enqueued { false };
    std::thread producer([&] {
        auto t1 = q.enqueue_tracked([] {});
        second_enqueued.store(true, std::memory_order_release);
        t1.wait();
    });
    // First job still running → second enqueue blocked → no completion yet.
    EXPECT_FALSE(second_enqueued.load(std::memory_order_acquire));
    EXPECT_FALSE(t0.is_complete());

    release.store(true, std::memory_order_release);
    t0.wait();
    producer.join();
    q.wait_idle();
    EXPECT_TRUE(second_enqueued.load(std::memory_order_acquire));
    EXPECT_EQ(q.completion_count(), 2u);
}

TEST(AsyncSubmitN, WorkerSurvivesThrowingJobMidRing)
{
    // A throwing job in the middle of the ring is isolated: prior + later jobs
    // complete, the worker survives, and the bad token captures the exception.
    cd::render::AsyncSubmitN q { 3 };
    std::atomic<int> good_runs { 0 };
    auto a = q.enqueue_tracked([&] { good_runs.fetch_add(1, std::memory_order_acq_rel); });
    auto bad = q.enqueue_tracked([] { throw std::runtime_error { "mid" }; });
    auto c = q.enqueue_tracked([&] { good_runs.fetch_add(1, std::memory_order_acq_rel); });
    a.wait();
    bad.wait();
    c.wait();
    EXPECT_FALSE(a.has_exception());
    EXPECT_TRUE(bad.has_exception());
    EXPECT_THROW(bad.rethrow_if_exception(), std::runtime_error);
    EXPECT_FALSE(c.has_exception());
    EXPECT_EQ(good_runs.load(), 2);
    EXPECT_EQ(q.completion_count(), 3u);  // all three counted, including the thrower
}

TEST(AsyncSubmitN, ShutdownWithPendingDrainsAllTrackedTokens)
{
    // Destructor must drain (not drop) every pending job. We capture the
    // tokens' shared state so we can assert post-destruction completion.
    std::vector<cd::render::CompletionToken> toks;
    std::atomic<int> ran { 0 };
    {
        cd::render::AsyncSubmitN q { 4 };
        toks.reserve(4);
        for (int i = 0; i < 4; ++i)
            toks.push_back(q.enqueue_tracked([&] { ran.fetch_add(1, std::memory_order_acq_rel); }));
        // ~q here: wait_idle() drains all 4, then stop + join.
    }
    // After the queue is destroyed, every tracked job ran and every token is
    // complete (the shared state outlives the queue).
    EXPECT_EQ(ran.load(), 4);
    for (auto& t : toks)
    {
        EXPECT_TRUE(t.is_complete());
        EXPECT_FALSE(t.has_exception());
    }
}

TEST(AsyncSubmitN, EmptyDrainOnFreshTrackedQueueReturnsImmediately)
{
    // wait_idle() on a fresh queue + a token taken from an empty (null) job.
    cd::render::AsyncSubmitN q { 2 };
    q.wait_idle();  // nothing enqueued — must not block
    auto tok = q.enqueue_tracked({});
    tok.wait();
    EXPECT_TRUE(tok.is_complete());
    EXPECT_FALSE(tok.has_exception());
    EXPECT_EQ(q.completion_count(), 1u);
}

TEST(AsyncSubmitN, CapacityOneTrackedTokensWrapRingFifo)
{
    // capacity 1 + tracked tokens exercises the ring wrap on every job while
    // also signalling each token. 12 jobs, strict FIFO, no lost slot.
    cd::render::AsyncSubmitN q { 1 };
    std::vector<cd::render::CompletionToken> toks;
    std::atomic<int> order { 0 };
    std::vector<int> seen;
    std::mutex seen_mu;
    constexpr int kJobs = 12;
    toks.reserve(kJobs);
    for (int i = 0; i < kJobs; ++i)
    {
        toks.push_back(q.enqueue_tracked([&, i] {
            (void)order.fetch_add(1, std::memory_order_acq_rel);
            std::scoped_lock guard { seen_mu };
            seen.push_back(i);
        }));
    }
    for (auto& t : toks)
        t.wait();
    ASSERT_EQ(seen.size(), static_cast<std::size_t>(kJobs));
    for (int i = 0; i < kJobs; ++i)
        EXPECT_EQ(seen[static_cast<std::size_t>(i)], i);
    EXPECT_EQ(q.completion_count(), static_cast<std::uint64_t>(kJobs));
    EXPECT_EQ(q.size(), 0u);
}

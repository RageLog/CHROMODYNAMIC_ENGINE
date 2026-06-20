// =============================================================================
// CHROMODYNAMIC — cd/render/AsyncSubmit.hpp
// Phase 4 / Sprint S4.4 — minimal render thread primitive.
//
// One worker thread + one job slot. The owning thread calls
//   wait_idle();                  // drain previous frame
//   auto frame = renderer.begin_frame();
//   // ... record into frame.command_buffer ...
//   async.enqueue([&]{ renderer.end_frame(); });
//
// This is the "one frame deep" producer/consumer pattern. It does NOT
// pipeline beyond a single frame — that requires N-deep ring + per-
// frame fences (Phase 5). What we get from this minimal version:
//
//   * Game logic for frame N+1 overlaps with GPU submit/present of N.
//   * Single-slot semantics → no races between game and submit on the
//     same Frame resources.
//   * Predictable shutdown: destructor joins the worker after draining.
//
// Concurrency invariants:
//   * enqueue() must be called from the SAME thread that owns the
//     surrounding renderer (single-producer). The class does not gate
//     multi-producer; that's a programming error.
//   * The job function runs on the worker thread. Anything it touches
//     must either be thread-safe or owned exclusively by the worker
//     between enqueue() and wait_idle().
//   * wait_idle() blocks until the last-enqueued job has completed.
//   * Destructor calls wait_idle() then joins the thread cleanly.
//
// Phase 4 closure ADR pre-conditions for the "real" render thread are
// captured separately (ADR-20260522-wave20-render-thread-deferral).
// This primitive is the building block that those conditions, once
// satisfied, would build on top of.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace cd::render
{

// -----------------------------------------------------------------------------
// Per-submit completion token (the "small clean testable hook" — primitive-
// internal, renderer-agnostic). A caller that needs to know when ONE specific
// submitted job has finished (rather than blocking on global idle via
// wait_idle()) takes a CompletionToken from enqueue_tracked() and either polls
// is_complete() or blocks on wait(). The token is a self-contained completion
// signal: it does NOT bind to any Renderer fence ring, so the primitive stays
// fence-agnostic and the layer boundary is preserved.
//
// Lifetime: the shared state outlives both the token and the in-flight job
// (shared_ptr); the worker signals it via an RAII guard so completion fires on
// normal return AND on exception unwind. A token may be discarded freely — the
// worker still signals its (now orphaned) state with no UB.
//
// Exception isolation: if the tracked job throws, the worker CAPTURES the
// exception into the token (std::future semantics) instead of letting it kill
// the worker thread. The capturing caller can observe it via has_exception()
// or re-throw it on the caller's thread via wait() / rethrow_if_exception().
// -----------------------------------------------------------------------------
class CompletionToken
{
public:
    CompletionToken() = default;  // empty/invalid token (valid() == false).

    CompletionToken(const CompletionToken&) = delete;
    CompletionToken& operator=(const CompletionToken&) = delete;
    CompletionToken(CompletionToken&&) noexcept = default;
    CompletionToken& operator=(CompletionToken&&) noexcept = default;
    ~CompletionToken() = default;

    /// True if this token tracks a real submission (non-empty state).
    [[nodiscard]] bool valid() const noexcept { return state_ != nullptr; }

    /// True once the tracked job has finished on the worker. An empty token
    /// (default-constructed) reports complete — there is nothing to wait for.
    [[nodiscard]] bool is_complete() const
    {
        if (!state_)
            return true;
        std::scoped_lock guard { state_->mu };
        return state_->done;
    }

    /// True if the tracked job finished by throwing. Only meaningful once
    /// is_complete() — before completion it reports false.
    [[nodiscard]] bool has_exception() const
    {
        if (!state_)
            return false;
        std::scoped_lock guard { state_->mu };
        return state_->error != nullptr;
    }

    /// Block until the tracked job has finished. No-op on an empty token. Does
    /// NOT re-throw a captured exception — use rethrow_if_exception() for that.
    void wait() const
    {
        if (!state_)
            return;
        std::unique_lock guard { state_->mu };
        state_->cv.wait(guard, [this] { return state_->done; });
    }

    /// Block until complete, then re-throw the captured job exception (if any)
    /// on the calling thread — std::future-style. No-op on an empty token or a
    /// job that returned normally.
    void rethrow_if_exception() const
    {
        if (!state_)
            return;
        std::exception_ptr err;
        {
            std::unique_lock guard { state_->mu };
            state_->cv.wait(guard, [this] { return state_->done; });
            err = state_->error;
        }
        if (err)
            std::rethrow_exception(err);
    }

private:
    friend class AsyncSubmit;
    friend class AsyncSubmitN;

    struct State
    {
        std::mutex mu;
        std::condition_variable cv;
        bool done { false };
        std::exception_ptr error {};
    };

    explicit CompletionToken(std::shared_ptr<State> state) noexcept
        : state_ { std::move(state) }
    {
    }

    /// Worker-side: capture the in-flight job's exception (if any) into the
    /// state before it is marked complete. Called from the worker's catch
    /// block. Safe on a null state (untracked job).
    static void capture_exception(const std::shared_ptr<State>& state,
                                  std::exception_ptr err)
    {
        if (!state)
            return;
        std::scoped_lock guard { state->mu };
        state->error = err;  // std::exception_ptr assign is by const-ref; no move
    }

    /// Worker-side: mark the state complete and wake every waiter. Static so it
    /// can be invoked from an RAII guard holding only the shared_ptr (the token
    /// itself may already be gone). Safe on a null state (untracked job).
    static void signal(const std::shared_ptr<State>& state)
    {
        if (!state)
            return;
        {
            std::scoped_lock guard { state->mu };
            state->done = true;
        }
        state->cv.notify_all();
    }

    std::shared_ptr<State> state_ {};
};

class AsyncSubmit
{
public:
    using Job = std::function<void()>;

    AsyncSubmit()
        : thread_ { [this] { worker_loop_(); } }
    {
    }

    ~AsyncSubmit()
    {
        // Drain any pending job, then signal stop and join.
        wait_idle();
        {
            std::scoped_lock guard { mu_ };
            stop_ = true;
        }
        have_job_.notify_all();
        if (thread_.joinable())
            thread_.join();
    }

    AsyncSubmit(const AsyncSubmit&) = delete;
    AsyncSubmit& operator=(const AsyncSubmit&) = delete;
    AsyncSubmit(AsyncSubmit&&) = delete;
    AsyncSubmit& operator=(AsyncSubmit&&) = delete;

    /// Enqueue a job. If a previous job is still running, this blocks
    /// until it completes (single-slot pipeline). On return, the new
    /// job has been handed to the worker; it may not have started yet.
    void enqueue(Job job)
    {
        std::unique_lock guard { mu_ };
        // Wait for any currently-running job to finish.
        job_done_.wait(guard, [this] { return !busy_; });
        pending_ = std::move(job);
        pending_token_ = nullptr;  // untracked submission.
        busy_ = true;
        ++enqueue_count_;
        guard.unlock();
        have_job_.notify_one();
    }

    /// Like enqueue(), but returns a CompletionToken that becomes complete
    /// exactly when THIS job has finished on the worker (including the empty-
    /// job no-op and exception-unwind paths). Lets a caller wait on one
    /// specific submission rather than global wait_idle().
    [[nodiscard]] CompletionToken enqueue_tracked(Job job)
    {
        auto state = std::make_shared<CompletionToken::State>();
        {
            std::unique_lock guard { mu_ };
            job_done_.wait(guard, [this] { return !busy_; });
            pending_ = std::move(job);
            pending_token_ = state;
            busy_ = true;
            ++enqueue_count_;
            guard.unlock();
            have_job_.notify_one();
        }
        return CompletionToken { std::move(state) };
    }

    /// Block until the worker is idle (no pending or running job).
    void wait_idle()
    {
        std::unique_lock guard { mu_ };
        job_done_.wait(guard, [this] { return !busy_; });
    }

    /// True when the worker is currently running a job (or has one
    /// queued that has not yet been picked up). Cheap; suitable for
    /// busy-waiting in tests but use `wait_idle()` in production.
    [[nodiscard]] bool is_busy() const
    {
        std::scoped_lock guard { mu_ };
        return busy_;
    }

    /// Total enqueue calls since construction. Test introspection.
    [[nodiscard]] std::uint64_t enqueue_count() const noexcept
    {
        return enqueue_count_;
    }

    /// Total jobs completed by the worker. Test introspection.
    [[nodiscard]] std::uint64_t completion_count() const noexcept
    {
        return completion_count_.load(std::memory_order_acquire);
    }

private:
    void worker_loop_()
    {
        while (true)
        {
            Job job;
            std::shared_ptr<CompletionToken::State> token;
            {
                std::unique_lock guard { mu_ };
                have_job_.wait(guard, [this] { return busy_ || stop_; });
                if (stop_ && !busy_)
                    return;
                job = std::move(pending_);
                pending_ = nullptr;
                token = std::move(pending_token_);
                pending_token_ = nullptr;
            }
            // Exception isolation: a throwing job must not terminate the worker
            // thread (which would leave busy_ stuck → wait_idle() deadlock). The
            // RAII FinalSignal marks the token complete on every exit path; if
            // the job threw, the exception is CAPTURED into the token (not
            // swallowed) so the tracking caller can observe / re-throw it.
            struct FinalSignal
            {
                std::shared_ptr<CompletionToken::State> st;
                ~FinalSignal() { CompletionToken::signal(st); }
            } final_signal { token };
            if (job)
            {
                // run outside the lock — must not access shared state.
                try
                {
                    job();
                }
                catch (...)
                {
                    CompletionToken::capture_exception(token, std::current_exception());
                }
            }
            {
                std::scoped_lock guard { mu_ };
                busy_ = false;
                completion_count_.fetch_add(1, std::memory_order_release);
            }
            job_done_.notify_all();
        }
    }

    mutable std::mutex mu_;
    std::condition_variable have_job_;
    std::condition_variable job_done_;
    Job pending_ {};
    std::shared_ptr<CompletionToken::State> pending_token_ {};
    bool busy_ { false };
    bool stop_ { false };
    std::uint64_t enqueue_count_ { 0 };
    std::atomic<std::uint64_t> completion_count_ { 0 };
    std::thread thread_;
};

}  // namespace cd::render

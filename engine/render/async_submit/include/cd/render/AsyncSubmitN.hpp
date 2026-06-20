// =============================================================================
// CHROMODYNAMIC — cd/render/AsyncSubmitN.hpp
// Phase 5 / S4.4.b — N-frame pipelined render thread.
//
// Sibling of cd::render::AsyncSubmit (1-frame deep). Holds a bounded
// ring of pending jobs so the producer can stay AHEAD of the worker by
// up to N frames. enqueue() only blocks when the ring is FULL —
// otherwise it returns immediately and the producer keeps building the
// next frame.
//
// Usage pattern:
//   AsyncSubmitN<3> q;                          // 3-deep
//   while (loop) {
//       // q.enqueue() may block if 3 frames are already in flight
//       record_frame_n();
//       q.enqueue([&]{ renderer.end_frame(); });
//       // No wait_idle() — the producer immediately starts frame n+1
//   }
//
// Safety invariants:
//   * Bounded queue → no unbounded latency between record and present.
//   * SPSC (single-producer-single-consumer) only. Multi-producer is
//     a programming error and the class does not gate it.
//   * Destructor: drain → stop → join. No teardown deadlock.
//
// Required for proper render-thread pipelining: per-frame fence array
// on the Renderer side so frame N's begin_frame doesn't smash frame N-1's
// resources. The Renderer already supports this via its
// frames_in_flight slot count (typically 2-3). Set N here ≤ frames_in_flight.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/render/AsyncSubmit.hpp>  // CompletionToken (shared per-submit signal)

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace cd::render
{

class AsyncSubmitN
{
public:
    using Job = std::function<void()>;

    /// `capacity` is the max number of in-flight jobs (i.e. frames the
    /// producer is allowed to be ahead). 0 → defaults to 2.
    explicit AsyncSubmitN(std::size_t capacity = 2)
        : capacity_ { capacity == 0 ? std::size_t { 2 } : capacity }
        , queue_(capacity_)
        , tokens_(capacity_)
        , thread_ { [this] { worker_loop_(); } }
    {
    }

    ~AsyncSubmitN()
    {
        wait_idle();
        {
            std::scoped_lock guard { mu_ };
            stop_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
        if (thread_.joinable())
            thread_.join();
    }

    AsyncSubmitN(const AsyncSubmitN&) = delete;
    AsyncSubmitN& operator=(const AsyncSubmitN&) = delete;
    AsyncSubmitN(AsyncSubmitN&&) = delete;
    AsyncSubmitN& operator=(AsyncSubmitN&&) = delete;

    /// Enqueue a job. Blocks if the queue is FULL (the worker is
    /// `capacity` jobs behind). Returns when the slot has been reserved.
    void enqueue(Job job)
    {
        std::unique_lock guard { mu_ };
        not_full_.wait(guard, [this] { return size_ < capacity_; });
        const auto idx = (head_ + size_) % capacity_;
        queue_[idx] = std::move(job);
        tokens_[idx] = nullptr;  // untracked submission.
        ++size_;
        ++enqueue_count_;
        guard.unlock();
        not_empty_.notify_one();
    }

    /// Like enqueue(), but returns a CompletionToken that becomes complete
    /// exactly when THIS job has finished on the worker (including the empty-
    /// job no-op and exception-unwind paths). Lets a caller wait on one
    /// specific in-flight frame rather than draining the whole ring via
    /// wait_idle().
    [[nodiscard]] CompletionToken enqueue_tracked(Job job)
    {
        auto state = std::make_shared<CompletionToken::State>();
        {
            std::unique_lock guard { mu_ };
            not_full_.wait(guard, [this] { return size_ < capacity_; });
            const auto idx = (head_ + size_) % capacity_;
            queue_[idx] = std::move(job);
            tokens_[idx] = state;
            ++size_;
            ++enqueue_count_;
            guard.unlock();
            not_empty_.notify_one();
        }
        return CompletionToken { std::move(state) };
    }

    /// Block until the queue is empty AND the worker is not running a job.
    void wait_idle()
    {
        std::unique_lock guard { mu_ };
        idle_.wait(guard, [this] { return size_ == 0 && !running_; });
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    [[nodiscard]] std::size_t size() const
    {
        std::scoped_lock guard { mu_ };
        return size_;
    }

    [[nodiscard]] std::uint64_t enqueue_count() const noexcept { return enqueue_count_; }
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
                not_empty_.wait(guard, [this] { return size_ > 0 || stop_; });
                if (stop_ && size_ == 0)
                    return;
                job = std::move(queue_[head_]);
                queue_[head_] = nullptr;
                token = std::move(tokens_[head_]);
                tokens_[head_] = nullptr;
                head_ = (head_ + 1) % capacity_;
                --size_;
                running_ = true;
            }
            not_full_.notify_one();
            // Exception isolation + token signal (see AsyncSubmit::worker_loop_).
            // The RAII FinalSignal marks the token complete on every exit path;
            // a throwing job has its exception CAPTURED into the token instead
            // of killing the worker (which would strand running_ →
            // wait_idle() deadlock).
            struct FinalSignal
            {
                std::shared_ptr<CompletionToken::State> st;
                ~FinalSignal() { CompletionToken::signal(st); }
            } final_signal { token };
            if (job)
            {
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
                running_ = false;
                completion_count_.fetch_add(1, std::memory_order_release);
            }
            idle_.notify_all();
        }
    }

    mutable std::mutex mu_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::condition_variable idle_;
    std::size_t capacity_;
    std::vector<Job> queue_;
    std::vector<std::shared_ptr<CompletionToken::State>> tokens_;
    std::size_t head_ { 0 };
    std::size_t size_ { 0 };
    bool running_ { false };
    bool stop_ { false };
    std::uint64_t enqueue_count_ { 0 };
    std::atomic<std::uint64_t> completion_count_ { 0 };
    std::thread thread_;
};

}  // namespace cd::render

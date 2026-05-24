// =============================================================================
// CHROMODYNAMIC — cd/asset/AsyncStreamer.hpp
// Phase 114 / Wave 278 — async asset streamer worker thread.
//
// Wraps a cd::asset::StreamQueue with a single worker thread that:
//   * Sleeps on a condition variable until enqueue() wakes it.
//   * Pops the most-urgent pending request, invokes a caller-supplied
//     load function, and marks the request complete or failed.
//   * Exits cleanly on stop() (signals via stopping_ + cv notify).
//
// The load function is `bool(AssetId)`. Caller is responsible for any
// VFS / RHI work that load entails. The streamer is just the scheduler
// + worker thread — exactly what was missing from Phase 45.B's
// StreamQueue primitive.
//
// Thread-safety:
//   * `enqueue` and the state queries are mutex-guarded; safe from any
//     thread.
//   * `start` / `stop` are NOT thread-safe with concurrent enqueues —
//     call them from the owning thread only.
//
// Header-only because it's tiny and pulls in std::thread anyway.
// =============================================================================
#pragma once

#include <cd/asset/StreamRequest.hpp>
#include <cd/core/Defines.hpp>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace cd::asset
{

class AsyncStreamer
{
public:
    /// Function type the worker calls per request. Return true on
    /// success (asset is now resident); false to mark the request
    /// `kFailed`. Implementations typically call AssetRegistry::load
    /// or a VFS read + decode chain.
    using LoadFn = std::function<bool(AssetId)>;

    explicit AsyncStreamer(LoadFn load_fn) noexcept
        : load_fn_ { std::move(load_fn) }
    {
    }

    AsyncStreamer(const AsyncStreamer&)            = delete;
    AsyncStreamer& operator=(const AsyncStreamer&) = delete;
    AsyncStreamer(AsyncStreamer&&)                 = delete;
    AsyncStreamer& operator=(AsyncStreamer&&)      = delete;

    ~AsyncStreamer() { stop(); }

    /// Spawn the worker thread. Calling start() twice is a no-op.
    void start()
    {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) return;
        stopping_.store(false, std::memory_order_release);
        worker_ = std::thread { [this] { worker_loop_(); } };
    }

    /// Signal the worker to drain and exit. Blocks until joined.
    void stop()
    {
        if (!running_.load(std::memory_order_acquire)) return;
        {
            std::lock_guard<std::mutex> lk { mutex_ };
            stopping_.store(true, std::memory_order_release);
        }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        running_.store(false, std::memory_order_release);
    }

    /// Push a new request. Wakes the worker if it was idle.
    void enqueue(StreamRequest r)
    {
        {
            std::lock_guard<std::mutex> lk { mutex_ };
            queue_.push(r);
        }
        cv_.notify_one();
    }

    /// Inspect a request's current state.
    [[nodiscard]] StreamState state_of(AssetId id) const
    {
        std::lock_guard<std::mutex> lk { mutex_ };
        return queue_.state_of(id);
    }

    /// Pending heap size (not counting in-flight / complete entries).
    [[nodiscard]] std::size_t pending_count() const
    {
        std::lock_guard<std::mutex> lk { mutex_ };
        return queue_.size();
    }

    [[nodiscard]] bool is_running() const noexcept
    {
        return running_.load(std::memory_order_acquire);
    }

private:
    void worker_loop_()
    {
        for (;;)
        {
            StreamRequest r {};
            {
                std::unique_lock<std::mutex> lk { mutex_ };
                cv_.wait(lk, [this] {
                    return stopping_.load(std::memory_order_acquire) || !queue_.empty();
                });
                if (stopping_.load(std::memory_order_acquire) && queue_.empty())
                    return;
                if (queue_.empty()) continue;
                r = queue_.pop_top();
            }

            // Load runs outside the lock so other threads can enqueue
            // while we're working.
            const bool ok = load_fn_ ? load_fn_(r.id) : false;

            {
                std::lock_guard<std::mutex> lk { mutex_ };
                if (ok) queue_.mark_complete(r.id);
                else    queue_.mark_failed(r.id);
            }
        }
    }

    LoadFn                         load_fn_;
    StreamQueue                    queue_;
    mutable std::mutex             mutex_;
    std::condition_variable        cv_;
    std::thread                    worker_;
    std::atomic<bool>              running_  { false };
    std::atomic<bool>              stopping_ { false };
};

}  // namespace cd::asset

// =============================================================================
// CHROMODYNAMIC — cd/asset/audio_streamer/AsyncAudioPool.cpp
// Phase 756 — cd::asset::audio_streamer Sprint-2 (async path)
//
// Implementation strategy (mirrors AsyncTexturePool exactly):
//   * Worker threads block on work_cv_ until a pending request arrives or
//     stop_ is signalled.
//   * Each worker grabs ONE request under the lock, releases the lock, then
//     performs the (simulated) I/O outside the lock to maximise concurrency.
//   * On completion the worker pushes the asset path onto completed_queue_
//     under the lock and notifies idle_cv_ so join_all() can detect quiescence.
//   * poll_completed() swaps out completed_queue_ under the lock (O(1) swap).
//   * join_all() waits on idle_cv_ until pending_queue_ + inflight_ == 0,
//     then sets stop_ and joins all threads.
//
// No sleep_for: all blocking uses condition_variable::wait with a predicate.
// Anti-flakiness rule upheld.
//
// MOMENT: Soundtrack swap during gameplay loads in background — no audio dropout.
// =============================================================================

#include <cd/asset/audio_streamer/AudioStreamer.hpp>

#include <algorithm>
#include <cassert>

namespace cd::asset::audio_streamer
{

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------

AsyncAudioPool::~AsyncAudioPool() noexcept
{
    // If the user forgot to call join_all(), do it now.
    if (!workers_.empty())
    {
        {
            const std::scoped_lock lk { mutex_ };
            stop_ = true;
        }
        work_cv_.notify_all();
        for (auto& t : workers_)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// configure — start worker threads
// ---------------------------------------------------------------------------

void AsyncAudioPool::configure(const std::uint32_t worker_count)
{
    // Idempotent guard: only configure once.
    assert(workers_.empty() && "AsyncAudioPool::configure called twice");

    const std::uint32_t n = (worker_count == 0U) ? 1U : worker_count;
    workers_.reserve(static_cast<std::size_t>(n));
    for (std::uint32_t i = 0U; i < n; ++i)
    {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

// ---------------------------------------------------------------------------
// submit_async
// ---------------------------------------------------------------------------

void AsyncAudioPool::submit_async(StreamRequest request)
{
    {
        const std::scoped_lock lk { mutex_ };
        pending_queue_.push_back(PendingEntry{
            std::move(request.asset_path),
            request.channel_target,
            request.priority });
    }
    work_cv_.notify_one();
}

// ---------------------------------------------------------------------------
// poll_completed — non-blocking drain
// ---------------------------------------------------------------------------

std::vector<std::string> AsyncAudioPool::poll_completed()
{
    std::vector<std::string> out;
    {
        const std::scoped_lock lk { mutex_ };
        out.swap(completed_queue_);
    }
    return out;
}

// ---------------------------------------------------------------------------
// join_all — wait for quiescence, then shutdown
// ---------------------------------------------------------------------------

void AsyncAudioPool::join_all()
{
    // Wait until all pending work and in-flight work are done.
    {
        std::unique_lock lk { mutex_ };
        idle_cv_.wait(lk, [this] {
            return pending_queue_.empty() && (inflight_.load(std::memory_order_relaxed) == 0U);
        });
        stop_ = true;
    }
    work_cv_.notify_all();

    for (auto& t : workers_)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
    workers_.clear();
}

// ---------------------------------------------------------------------------
// completed_count
// ---------------------------------------------------------------------------

std::size_t AsyncAudioPool::completed_count() const noexcept
{
    return completed_count_.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// worker_loop (private)
// ---------------------------------------------------------------------------

void AsyncAudioPool::worker_loop()
{
    for (;;)
    {
        PendingEntry entry;

        // --- Wait for work or stop ---
        {
            std::unique_lock lk { mutex_ };
            work_cv_.wait(lk, [this] {
                return stop_ || !pending_queue_.empty();
            });

            if (stop_ && pending_queue_.empty())
            {
                return;  // clean shutdown
            }

            // Pick highest-priority entry (O(n); acceptable — queue is small
            // per-worker and this runs outside the main thread hot path).
            const auto best = std::max_element(
                pending_queue_.begin(),
                pending_queue_.end(),
                [](const PendingEntry& a, const PendingEntry& b) noexcept {
                    return a.priority < b.priority;
                });

            entry = std::move(*best);
            pending_queue_.erase(best);
            inflight_.fetch_add(1U, std::memory_order_relaxed);
        }

        // --- Simulate I/O outside the lock ---
        // Sprint-2 stub: real WAV/OGG decode is a future deliverable.
        // The path is used as the completion token; no actual file I/O here.
        const std::string completed_path = std::move(entry.path);

        // --- Publish completion ---
        {
            const std::scoped_lock lk { mutex_ };
            completed_queue_.push_back(completed_path);
            inflight_.fetch_sub(1U, std::memory_order_relaxed);
        }
        completed_count_.fetch_add(1U, std::memory_order_relaxed);
        idle_cv_.notify_all();  // wake join_all() if waiting
    }
}

}  // namespace cd::asset::audio_streamer

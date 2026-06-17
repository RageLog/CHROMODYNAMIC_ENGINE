// =============================================================================
// CHROMODYNAMIC — cd/asset/audio_streamer/AsyncAudioPool.cpp
// Phase 756 — cd::asset::audio_streamer async path
// Band 6   — workers run the real WAV decode; completions carry real PCM.
//
// Implementation strategy (mirrors AsyncTexturePool):
//   * Worker threads block on work_cv_ until a pending request arrives or
//     stop_ is signalled.
//   * Each worker grabs ONE request under the lock, releases the lock, then
//     runs the REAL decode (decode_audio_file) outside the lock to maximise
//     concurrency. CPU-only — no audio-device contact.
//   * A successful decode pushes a CompletedAudio (path + PCM) onto
//     completed_queue_ under the lock and notifies idle_cv_. A failed decode
//     (incl. any sealed .ogg) is dropped but still decrements inflight_ so
//     join_all() can reach quiescence.
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
#include <optional>
#include <utility>

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

std::vector<CompletedAudio> AsyncAudioPool::poll_completed()
{
    std::vector<CompletedAudio> out;
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
            const auto best = std::ranges::max_element(
                pending_queue_,
                [](const PendingEntry& a, const PendingEntry& b) noexcept {
                    return a.priority < b.priority;
                });

            entry = std::move(*best);
            pending_queue_.erase(best);
            inflight_.fetch_add(1U, std::memory_order_relaxed);
        }

        // --- Real CPU decode outside the lock ---
        // decode_audio_file runs the WAV decoder (or returns nullopt for a
        // sealed .ogg / IO failure → dropped, not completed).
        std::optional<DecodedAudio> decoded = decode_audio_file(entry.path);

        // --- Publish completion (only on a successful decode) ---
        {
            const std::scoped_lock lk { mutex_ };
            if (decoded.has_value())
            {
                completed_queue_.push_back(CompletedAudio{ std::move(entry.path), std::move(*decoded) });
            }
            inflight_.fetch_sub(1U, std::memory_order_relaxed);
        }
        if (decoded.has_value())
        {
            completed_count_.fetch_add(1U, std::memory_order_relaxed);
        }
        idle_cv_.notify_all();  // wake join_all() if waiting
    }
}

}  // namespace cd::asset::audio_streamer

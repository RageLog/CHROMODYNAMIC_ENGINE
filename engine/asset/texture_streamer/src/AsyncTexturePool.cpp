// =============================================================================
// CHROMODYNAMIC — cd/asset/texture_streamer/AsyncTexturePool.cpp
// Phase 714 — cd::asset::texture_streamer async path
// Band 6   — workers run the real CPU decode; completions carry decoded data.
//
// Implementation strategy:
//   * Worker threads block on work_cv_ until a pending request arrives or
//     stop_ is signalled.
//   * Each worker grabs ONE request under the lock, releases the lock, then
//     runs the REAL decode (decode_texture_file) outside the lock to maximise
//     concurrency. CPU-only — workers never touch IDevice.
//   * A successful decode pushes a CompletedTexture (path + decoded payload)
//     onto completed_queue_ under the lock and notifies idle_cv_. A failed
//     decode is dropped (not reported as completed) but still decrements
//     inflight_ so join_all() can reach quiescence.
//   * poll_completed() swaps out completed_queue_ under the lock (O(1) swap).
//   * join_all() waits on idle_cv_ until pending_queue_ + inflight_ == 0,
//     then sets stop_ and joins all threads.
//
// No sleep_for: all blocking uses condition_variable::wait / wait_for with a
// predicate.  Anti-flakiness rule upheld.
// =============================================================================

#include <cd/asset/texture_streamer/TextureStreamer.hpp>

#include <algorithm>
#include <cassert>
#include <optional>
#include <utility>

namespace cd::asset::texture_streamer
{

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------

AsyncTexturePool::~AsyncTexturePool() noexcept
{
    // If the user forgot to call join_all(), do it now.
    // join_all() is noexcept-tolerant: it sets stop_ then joins workers.
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

void AsyncTexturePool::configure(const std::uint32_t worker_count)
{
    // Idempotent guard: only configure once.
    assert(workers_.empty() && "AsyncTexturePool::configure called twice");

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

void AsyncTexturePool::submit_async(StreamRequest request)
{
    {
        const std::scoped_lock lk { mutex_ };
        pending_queue_.push_back(PendingEntry{
            std::move(request.asset_path),
            request.mip_target,
            request.priority });
    }
    work_cv_.notify_one();
}

// ---------------------------------------------------------------------------
// poll_completed — non-blocking drain
// ---------------------------------------------------------------------------

std::vector<CompletedTexture> AsyncTexturePool::poll_completed()
{
    std::vector<CompletedTexture> out;
    {
        const std::scoped_lock lk { mutex_ };
        out.swap(completed_queue_);
    }
    return out;
}

// ---------------------------------------------------------------------------
// join_all — wait for quiescence, then shutdown
// ---------------------------------------------------------------------------

void AsyncTexturePool::join_all()
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

std::size_t AsyncTexturePool::completed_count() const noexcept
{
    return completed_count_.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// worker_loop (private)
// ---------------------------------------------------------------------------

void AsyncTexturePool::worker_loop()
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
        // decode_texture_file dispatches cdtex vs. stb image. CPU-only; no
        // IDevice contact. A failure yields nullopt → dropped (not completed).
        std::optional<DecodedTexture> decoded = decode_texture_file(entry.path);

        // --- Publish completion (only on a successful decode) ---
        {
            const std::scoped_lock lk { mutex_ };
            if (decoded.has_value())
            {
                completed_queue_.push_back(CompletedTexture{ std::move(entry.path), std::move(*decoded) });
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

}  // namespace cd::asset::texture_streamer

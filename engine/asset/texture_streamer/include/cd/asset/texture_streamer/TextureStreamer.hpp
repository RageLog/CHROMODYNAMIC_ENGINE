// =============================================================================
// CHROMODYNAMIC — cd/asset/texture_streamer/TextureStreamer.hpp
// Phase 599 — cd::asset::texture_streamer (Sprint-1: synchronous)
// Phase 714 — cd::asset::texture_streamer (Sprint-2: opt-in async path)
//
// GPU-side texture streaming coordinator. Accepts enqueue/cancel requests
// keyed by asset path and drives upload via cd::rhi::IDevice on each tick().
//
// Sprint-1: synchronous — highest-priority pending request processed inline.
// Sprint-2: opt-in async via AsyncTexturePool + TextureStreamerConfig.
//   * Set use_async = true (and optionally worker_count > 0) to offload I/O
//     to background threads while tick() only drains the completion queue.
//   * Priority: higher uint8 value = higher priority (255 = highest, 0 = lowest).
//
// Namespace: cd::asset::texture_streamer
// =============================================================================
#pragma once

#include <cd/rhi/Handles.hpp>
#include <cd/rhi/IDevice.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace cd::asset::texture_streamer
{

// ---- Public types -----------------------------------------------------------

/// Caller-facing load request.
///   `mip_target`  — target mip level to stream up to (0 = full mip chain).
///   `priority`    — 255 = highest, 0 = lowest.
struct StreamRequest
{
    std::string  asset_path;
    std::uint8_t mip_target { 0U };
    std::uint8_t priority   { 128U };
};

// ---- AsyncTexturePool -------------------------------------------------------

/// Thread-pool that executes StreamRequests on background worker threads.
///
/// Usage (standalone or via TextureStreamer when use_async == true):
///   AsyncTexturePool pool;
///   pool.configure(2);                          // start 2 workers
///   pool.submit_async({ "tex.cdtex", 0, 200 }); // enqueue a request
///   auto done = pool.poll_completed();           // drain finished paths
///   pool.join_all();                             // wait + shutdown
///
/// Thread safety: submit_async / poll_completed / join_all are each
///   individually thread-safe via internal mutex + condition_variable.
///   Do NOT call join_all while another thread holds a lock on the pool.
///
/// The pool simulates I/O completion via a lightweight stub (same 1x1 GPU
/// placeholder strategy as Sprint-1) — real cdtex decode is a future Sprint.
class AsyncTexturePool
{
public:
    AsyncTexturePool()  = default;
    ~AsyncTexturePool() noexcept;

    // Non-copyable, non-movable (owns threads + mutex).
    AsyncTexturePool(const AsyncTexturePool&)            = delete;
    AsyncTexturePool& operator=(const AsyncTexturePool&) = delete;
    AsyncTexturePool(AsyncTexturePool&&)                 = delete;
    AsyncTexturePool& operator=(AsyncTexturePool&&)      = delete;

    /// Start `worker_count` background worker threads (idempotent if called
    /// once; must NOT be called after join_all()).
    void configure(std::uint32_t worker_count = 2U);

    /// Enqueue a request for async processing. Thread-safe.
    void submit_async(StreamRequest request);

    /// Drain and return asset paths that have been fully processed since the
    /// last poll_completed() call. Thread-safe. Returns an empty vector if
    /// nothing has finished yet. Does NOT block.
    [[nodiscard]] std::vector<std::string> poll_completed();

    /// Signal all workers to finish outstanding work, then join them.
    /// Blocks until all submitted requests are processed. Safe to call from
    /// the owner thread. Must not be called concurrently with submit_async().
    void join_all();

    /// Number of requests successfully completed (cumulative, thread-safe).
    [[nodiscard]] std::size_t completed_count() const noexcept;

private:
    // ---- Worker loop ---------------------------------------------------
    void worker_loop();

    // ---- State ---------------------------------------------------------
    struct PendingEntry
    {
        std::string  path;
        std::uint8_t mip_target { 0U };
        std::uint8_t priority   { 0U };
    };

    mutable std::mutex      mutex_;
    std::condition_variable work_cv_;    // workers wait for work or stop
    std::condition_variable idle_cv_;    // join_all waits for quiescence

    std::vector<PendingEntry>  pending_queue_;   // protected by mutex_
    std::vector<std::string>   completed_queue_; // protected by mutex_

    std::atomic<std::size_t>   completed_count_ { 0U };
    std::atomic<std::uint32_t> inflight_        { 0U }; // jobs in-progress

    bool                       stop_ { false };         // protected by mutex_

    std::vector<std::thread>   workers_;
};

// ---- TextureStreamerConfig --------------------------------------------------

/// Optional configuration for TextureStreamer Sprint-2.
///   `use_async`    — false (default) = Sprint-1 sync path; true = async pool.
///   `worker_count` — number of worker threads when use_async == true.
struct TextureStreamerConfig
{
    bool         use_async    { false };
    std::uint32_t worker_count { 2U };
};

// ---- TextureStreamer ---------------------------------------------------------

/// Texture streamer — Sprint-1 (sync) or Sprint-2 (opt-in async).
///
/// Lifecycle (sync, Sprint-1 default):
///   cd::rhi::NullDevice device;
///   TextureStreamer ts;
///   ts.enqueue({ "textures/albedo.cdtex", 0, 200 });
///   while (!ts.is_loaded("textures/albedo.cdtex")) { ts.tick(dt, device); }
///   auto h = ts.get_loaded("textures/albedo.cdtex"); // has value
///
/// Lifecycle (async, Sprint-2):
///   TextureStreamer ts({ .use_async = true, .worker_count = 4 });
///   ts.enqueue({ "textures/albedo.cdtex", 0, 200 });
///   ts.tick(dt, device);   // delegates to AsyncTexturePool
///   ts.join_pending();      // optional — wait for all async loads to finish
///
/// Thread safety: not thread-safe for concurrent calls — all public methods
///   must come from the same (owner) thread.
class TextureStreamer
{
public:
    /// Construct with optional Sprint-2 config. Default = Sprint-1 sync path.
    explicit TextureStreamer(TextureStreamerConfig cfg = {});
    ~TextureStreamer();

    // Non-copyable; non-movable (owns AsyncTexturePool which is non-movable).
    TextureStreamer(const TextureStreamer&)            = delete;
    TextureStreamer& operator=(const TextureStreamer&) = delete;
    TextureStreamer(TextureStreamer&&)                 = delete;
    TextureStreamer& operator=(TextureStreamer&&)      = delete;

    // ---- Mutation -----------------------------------------------------------

    /// Enqueue a load request. If the path is already pending or loaded,
    /// this is a no-op (idempotent). Priority on duplicate is NOT updated.
    void enqueue(StreamRequest request);

    /// Cancel a pending request. If the path is already loaded or unknown,
    /// this is a no-op.  NOTE: in async mode cancel() only removes the path
    /// from the local pending_map_ dedup set; a request already dispatched to
    /// the AsyncTexturePool will still complete (its result is accepted on the
    /// next poll inside tick()).
    void cancel(std::string_view asset_path);

    /// Drive loading.
    ///   Sync mode  (use_async == false): process the highest-priority pending
    ///              request synchronously — at most one upload per tick() call.
    ///   Async mode (use_async == true):  submit ALL pending requests to the
    ///              AsyncTexturePool, then drain the pool's completion queue
    ///              into completed_paths_.
    /// `dt` is reserved for future budget / time-slicing.
    /// `device` is used in sync mode; in async mode a NullDevice is used
    /// internally by the worker (real decode is a future Sprint deliverable).
    void tick(float dt, cd::rhi::IDevice& device);

    /// Block until the AsyncTexturePool has processed all outstanding requests.
    /// No-op in sync mode. Safe to call from the owner thread.
    void join_pending();

    // ---- Queries ------------------------------------------------------------

    /// True if the asset has been successfully loaded and its GPU handle is valid.
    [[nodiscard]] bool is_loaded(std::string_view asset_path) const;

    /// Returns the TextureHandle if loaded, std::nullopt otherwise
    /// (pending / failed / unknown).
    [[nodiscard]] std::optional<cd::rhi::TextureHandle>
    get_loaded(std::string_view asset_path) const;

    /// Number of enqueued requests not yet loaded or failed.
    [[nodiscard]] std::size_t pending_count() const noexcept;

    /// Number of successfully completed uploads since construction.
    [[nodiscard]] std::size_t completed_count() const noexcept;

private:
    // ---- Internal state -----------------------------------------------------

    // Pending queue entry: path + mip_target + priority.
    struct PendingEntry
    {
        std::string  path;
        std::uint8_t mip_target { 0U };
        std::uint8_t priority   { 0U };
    };

    // Loaded texture record stored in completion table.
    struct LoadedRecord
    {
        cd::rhi::TextureHandle handle {};
        std::uint8_t           mip_target { 0U };
    };

    // Configuration (set at construction, immutable thereafter).
    TextureStreamerConfig config_;

    // Sprint-2 async pool — only constructed when config_.use_async == true.
    // Heap-allocated so we can forward-declare and avoid pulling thread headers
    // into every translation unit that includes TextureStreamer.hpp.
    // (AsyncTexturePool is defined above in the same header so the unique_ptr
    // destructor is reachable.)
    std::unique_ptr<AsyncTexturePool> async_pool_;

    // Pending set: path → entry (fast cancel + dedup).
    std::unordered_map<std::string, PendingEntry> pending_map_;

    // Completed table indexed by insertion order.
    std::vector<LoadedRecord> completed_;

    // Reverse lookup: path → TextureHandle for completed assets.
    std::unordered_map<std::string, cd::rhi::TextureHandle> completed_paths_;

    // ---- Helpers ------------------------------------------------------------

    /// Accept a batch of completed asset paths from the async pool and register
    /// placeholder handles in completed_ / completed_paths_.
    void accept_async_completions(std::vector<std::string> paths);

    /// Sprint-1 sync tick: process the single highest-priority pending entry.
    void tick_sync_impl(cd::rhi::IDevice& device);

    /// Sprint-2 async tick: drain pending_map_ into the AsyncTexturePool.
    void tick_async_impl();
};

}  // namespace cd::asset::texture_streamer

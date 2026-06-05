// =============================================================================
// CHROMODYNAMIC — cd/asset/audio_streamer/AudioStreamer.hpp
// Phase 619 — cd::asset::audio_streamer (Sprint-1: synchronous)
// Phase 756 — cd::asset::audio_streamer (Sprint-2: opt-in async path)
//
// Audio clip streaming coordinator. Accepts enqueue/cancel requests keyed by
// asset path and drives "load" on each tick() call. Resolved clips are
// identified by cd::asset::AssetId derived from the asset path.
//
// Sprint-1: synchronous — highest-priority pending request processed inline.
// Sprint-2: opt-in async via AsyncAudioPool + AudioStreamerConfig.
//   * Set use_async = true (and optionally worker_count > 0) to offload I/O
//     to background threads while tick() only drains the completion queue.
//   * Priority: higher uint8 value = higher priority (255 = highest, 0 = lowest).
//   * channel_target: target audio bus index; reserved for routing; carried
//     through but not acted on in Sprint-1 or Sprint-2 stub.
//
// Sibling to:
//   cd::asset::scene_streamer   (Phase 586)
//   cd::asset::texture_streamer (Phase 599 + 714)
//
// Namespace: cd::asset::audio_streamer
// =============================================================================
#pragma once

#include <cd/asset/AssetId.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace cd::asset::audio_streamer
{

// ---- Public types -----------------------------------------------------------

/// Caller-facing load request.
///   `asset_path`     — VFS path to the audio clip (e.g. "audio/footstep.wav").
///   `channel_target` — target audio bus index; reserved for Sprint-2 routing.
///   `priority`       — 255 = highest, 0 = lowest.
struct StreamRequest
{
    std::string  asset_path;
    std::uint8_t channel_target { 0U };
    std::uint8_t priority       { 128U };
};

// ---- AsyncAudioPool ---------------------------------------------------------

/// Thread-pool that executes StreamRequests on background worker threads.
///
/// Usage (standalone or via AudioStreamer when use_async == true):
///   AsyncAudioPool pool;
///   pool.configure(2);                            // start 2 workers
///   pool.submit_async({ "audio/music.wav", 0, 200 }); // enqueue a request
///   auto done = pool.poll_completed();             // drain finished paths
///   pool.join_all();                               // wait + shutdown
///
/// Thread safety: submit_async / poll_completed / join_all are each
///   individually thread-safe via internal mutex + condition_variable.
///   Do NOT call join_all while another thread holds a lock on the pool.
///
/// The pool simulates I/O completion via a lightweight stub (same AssetId
/// hash strategy as Sprint-1) — real WAV/OGG decode is a future Sprint.
class AsyncAudioPool
{
public:
    AsyncAudioPool()  = default;
    ~AsyncAudioPool() noexcept;

    // Non-copyable, non-movable (owns threads + mutex).
    AsyncAudioPool(const AsyncAudioPool&)            = delete;
    AsyncAudioPool& operator=(const AsyncAudioPool&) = delete;
    AsyncAudioPool(AsyncAudioPool&&)                 = delete;
    AsyncAudioPool& operator=(AsyncAudioPool&&)      = delete;

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
        std::uint8_t channel_target { 0U };
        std::uint8_t priority       { 0U };
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

// ---- AudioStreamerConfig ----------------------------------------------------

/// Optional configuration for AudioStreamer Sprint-2.
///   `use_async`    — false (default) = Sprint-1 sync path; true = async pool.
///   `worker_count` — number of worker threads when use_async == true.
struct AudioStreamerConfig
{
    bool          use_async    { false };
    std::uint32_t worker_count { 2U };
};

// ---- AudioStreamer -----------------------------------------------------------

/// Audio clip streamer — Sprint-1 (sync) or Sprint-2 (opt-in async).
///
/// Lifecycle (sync, Sprint-1 default):
///   AudioStreamer as;
///   as.enqueue({ "audio/music.wav", 0, 200 });
///   while (!as.is_loaded("audio/music.wav")) { as.tick(dt); }
///   auto id = as.get_loaded("audio/music.wav"); // has value
///
/// Lifecycle (async, Sprint-2):
///   AudioStreamer as({ .use_async = true, .worker_count = 2 });
///   as.enqueue({ "audio/music.wav", 0, 200 });
///   as.tick(dt);        // delegates to AsyncAudioPool
///   as.join_pending();  // optional — wait for all async loads to finish
///
/// Sprint-1 load strategy: the asset path is hashed to an AssetId via
/// cd::asset::AssetId::from_path(). This is always deterministic and never
/// fails, modelling "resident in the registry". Actual WAV decode,
/// sample-rate conversion and device upload are deferred to a future Sprint.
///
/// Thread safety: not thread-safe for concurrent calls — all public methods
///   must come from the same (owner) thread.
class AudioStreamer
{
public:
    /// Construct with optional Sprint-2 config. Default = Sprint-1 sync path.
    explicit AudioStreamer(AudioStreamerConfig cfg = {});
    ~AudioStreamer();

    // Non-copyable; non-movable (owns AsyncAudioPool which is non-movable).
    AudioStreamer(const AudioStreamer&)            = delete;
    AudioStreamer& operator=(const AudioStreamer&) = delete;
    AudioStreamer(AudioStreamer&&)                 = delete;
    AudioStreamer& operator=(AudioStreamer&&)      = delete;

    // ---- Mutation -----------------------------------------------------------

    /// Enqueue a load request. If the path is already pending or loaded,
    /// this is a no-op (idempotent). Priority on duplicate is NOT updated.
    void enqueue(StreamRequest request);

    /// Cancel a pending request. If the path is already loaded or unknown,
    /// this is a no-op.  NOTE: in async mode cancel() only removes the path
    /// from the local pending_map_ dedup set; a request already dispatched to
    /// the AsyncAudioPool will still complete (its result is accepted on the
    /// next poll inside tick()).
    void cancel(std::string_view asset_path);

    /// Drive loading.
    ///   Sync mode  (use_async == false): process the highest-priority pending
    ///              request synchronously — at most one load per tick() call.
    ///   Async mode (use_async == true):  submit ALL pending requests to the
    ///              AsyncAudioPool, then drain the pool's completion queue
    ///              into completed_paths_.
    /// `dt` is reserved for future budget / time-slicing.
    void tick(float dt);

    /// Block until the AsyncAudioPool has processed all outstanding requests.
    /// No-op in sync mode. Safe to call from the owner thread.
    void join_pending();

    // ---- Queries ------------------------------------------------------------

    /// True if the asset has been successfully loaded.
    [[nodiscard]] bool is_loaded(std::string_view asset_path) const;

    /// Returns the AssetId if loaded, std::nullopt otherwise
    /// (pending / failed / unknown).
    [[nodiscard]] std::optional<cd::asset::AssetId>
    get_loaded(std::string_view asset_path) const;

    /// Number of enqueued requests not yet loaded or failed.
    [[nodiscard]] std::size_t pending_count() const noexcept;

    /// Number of successfully completed loads since construction.
    [[nodiscard]] std::size_t completed_count() const noexcept;

private:
    // ---- Internal state -----------------------------------------------------

    // Pending queue entry: path + channel_target + priority.
    struct PendingEntry
    {
        std::string  path;
        std::uint8_t channel_target { 0U };
        std::uint8_t priority       { 0U };
    };

    // Loaded clip record stored in completion table.
    struct LoadedRecord
    {
        cd::asset::AssetId id {};
        std::uint8_t       channel_target { 0U };
    };

    // Configuration (set at construction, immutable thereafter).
    AudioStreamerConfig config_;

    // Sprint-2 async pool — only constructed when config_.use_async == true.
    std::unique_ptr<AsyncAudioPool> async_pool_;

    // Pending set: path → entry (fast cancel + dedup).
    std::unordered_map<std::string, PendingEntry> pending_map_;

    // Completion table indexed by insertion order.
    std::vector<LoadedRecord> completed_;

    // Reverse lookup: path → AssetId for completed assets.
    std::unordered_map<std::string, cd::asset::AssetId> completed_paths_;

    // ---- Helpers ------------------------------------------------------------

    /// Accept a batch of completed asset paths from the async pool and register
    /// AssetId entries in completed_ / completed_paths_.
    void accept_async_completions(std::vector<std::string> paths);

    /// Sprint-1 sync tick: process the single highest-priority pending entry.
    void tick_sync_impl();

    /// Sprint-2 async tick: drain pending_map_ into the AsyncAudioPool.
    void tick_async_impl();
};

}  // namespace cd::asset::audio_streamer

// =============================================================================
// CHROMODYNAMIC — cd/asset/scene_streamer/SceneStreamer.hpp
// Phase 586 — cd::asset::scene_streamer (Sprint-1: synchronous)
// Phase 755 — cd::asset::scene_streamer (Sprint-2: opt-in async path)
// Band 6   — real glTF parse wired behind the async orchestration.
//
// High-level scene streaming coordinator. Accepts enqueue/cancel requests
// keyed by asset path and drives a load via cd::asset::gltf::load_scene().
//
// Sync mode  — highest-priority pending request parsed inline.
// Async mode — opt-in via AsyncScenePool + SceneStreamerConfig.
//   * Set use_async = true (and optionally worker_count > 0) to offload the
//     glTF parse to background threads while tick() only drains completions.
//   * Priority: higher uint8 value = higher priority (255 = highest, 0 = lowest).
//
// DECODE: BOTH paths run the real cd::asset::gltf::load_scene() parser, so a
//   completion carries a real owning LoadedScene (node/mesh/material/texture
//   tree + bounds), not a synthetic placeholder. load_scene() is a pure CPU
//   parse safe to run per-file on a worker thread; the GPU/ECS ingest of the
//   LoadedScene stays the render-side consumer's job.
//
// Namespace: cd::asset::scene_streamer
// =============================================================================
#pragma once

#include <cd/asset/gltf/SceneLoader.hpp>

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

namespace cd::asset::scene_streamer
{

// ---- Public types -----------------------------------------------------------

/// Opaque handle for a successfully loaded scene. Wraps the index into the
/// internal completed table; stable across further enqueue/cancel calls.
struct SceneId
{
    std::uint32_t index { 0U };

    [[nodiscard]] bool operator==(const SceneId&) const noexcept = default;
};

/// Caller-facing load request. `priority` 255 = highest, 0 = lowest.
struct StreamRequest
{
    std::string  asset_path;
    std::uint8_t priority { 128U };
};

/// Worker → owner completion record: the asset path plus the REAL parsed
/// scene produced by cd::asset::gltf::load_scene() on a worker thread.
struct CompletedScene
{
    std::string                  path;
    cd::asset::gltf::LoadedScene scene;
};

// ---- AsyncScenePool ---------------------------------------------------------

/// Thread-pool that executes StreamRequests on background worker threads.
///
/// Usage (standalone or via SceneStreamer when use_async == true):
///   AsyncScenePool pool;
///   pool.configure(2);                          // start 2 workers
///   pool.submit_async({ "level.glb", 200 });    // enqueue a request
///   auto done = pool.poll_completed();           // drain finished paths
///   pool.join_all();                             // wait + shutdown
///
/// Thread safety: submit_async / poll_completed / join_all are each
///   individually thread-safe via internal mutex + condition_variable.
///   Do NOT call join_all while another thread holds a lock on the pool.
///
/// Each worker runs the REAL cd::asset::gltf::load_scene() parser outside the
/// lock, so a completion carries a fully-parsed LoadedScene. A path that fails
/// to parse is NOT reported as completed (matches the sync silent-drop).
class AsyncScenePool
{
public:
    AsyncScenePool()  = default;
    ~AsyncScenePool() noexcept;

    // Non-copyable, non-movable (owns threads + mutex).
    AsyncScenePool(const AsyncScenePool&)            = delete;
    AsyncScenePool& operator=(const AsyncScenePool&) = delete;
    AsyncScenePool(AsyncScenePool&&)                 = delete;
    AsyncScenePool& operator=(AsyncScenePool&&)      = delete;

    /// Start `worker_count` background worker threads (idempotent if called
    /// once; must NOT be called after join_all()).
    void configure(std::uint32_t worker_count = 2U);

    /// Enqueue a request for async processing. Thread-safe.
    void submit_async(StreamRequest request);

    /// Drain and return scenes that have been fully parsed since the last
    /// poll_completed() call. Thread-safe. Returns an empty vector if nothing
    /// has finished yet. Does NOT block.
    [[nodiscard]] std::vector<CompletedScene> poll_completed();

    /// Signal all workers to finish outstanding work, then join them.
    /// Blocks until all submitted requests are processed. Safe to call from
    /// the owner thread. Must not be called concurrently with submit_async().
    void join_all();

    /// Number of requests successfully completed (cumulative, thread-safe).
    [[nodiscard]] std::size_t completed_count() const noexcept;

private:
    // ---- Worker loop --------------------------------------------------------
    void worker_loop();

    // ---- State --------------------------------------------------------------
    struct PendingEntry
    {
        std::string  path;
        std::uint8_t priority { 0U };
    };

    mutable std::mutex      mutex_;
    std::condition_variable work_cv_;    // workers wait for work or stop
    std::condition_variable idle_cv_;    // join_all waits for quiescence

    std::vector<PendingEntry>   pending_queue_;   // protected by mutex_
    std::vector<CompletedScene> completed_queue_; // protected by mutex_

    std::atomic<std::size_t>   completed_count_ { 0U };
    std::atomic<std::uint32_t> inflight_        { 0U }; // jobs in-progress

    bool                       stop_ { false };         // protected by mutex_

    std::vector<std::thread>   workers_;
};

// ---- SceneStreamerConfig ----------------------------------------------------

/// Optional configuration for SceneStreamer Sprint-2.
///   `use_async`    — false (default) = Sprint-1 sync path; true = async pool.
///   `worker_count` — number of worker threads when use_async == true.
struct SceneStreamerConfig
{
    bool          use_async    { false };
    std::uint32_t worker_count { 2U };
};

// ---- SceneStreamer -----------------------------------------------------------

/// Scene streamer — Sprint-1 (sync) or Sprint-2 (opt-in async).
///
/// Lifecycle (sync, Sprint-1 default):
///   SceneStreamer s;
///   s.enqueue({ "assets/sponza.glb", 200 });
///   while (!s.is_loaded("assets/sponza.glb")) { s.tick(dt); }
///   auto id = s.get_loaded("assets/sponza.glb"); // has value
///
/// Lifecycle (async, Sprint-2):
///   SceneStreamer s({ .use_async = true, .worker_count = 2 });
///   s.enqueue({ "level/city.glb", 200 });
///   s.tick(dt);       // delegates to AsyncScenePool
///   s.join_pending(); // optional — wait for all async loads to finish
///
/// Thread safety: not thread-safe for concurrent calls — all public methods
///   must come from the same (owner) thread.
class SceneStreamer
{
public:
    /// Construct with optional Sprint-2 config. Default = Sprint-1 sync path.
    explicit SceneStreamer(SceneStreamerConfig cfg = {});
    ~SceneStreamer();

    // Non-copyable; non-movable (owns AsyncScenePool which is non-movable).
    SceneStreamer(const SceneStreamer&)            = delete;
    SceneStreamer& operator=(const SceneStreamer&) = delete;
    SceneStreamer(SceneStreamer&&)                 = delete;
    SceneStreamer& operator=(SceneStreamer&&)      = delete;

    // ---- Mutation -----------------------------------------------------------

    /// Enqueue a load request. If the path is already pending or loaded,
    /// this is a no-op (idempotent). Priority on duplicate is NOT updated.
    void enqueue(StreamRequest request);

    /// Cancel a pending request. If the path is already loaded or unknown,
    /// this is a no-op.  NOTE: in async mode cancel() only removes the path
    /// from the local pending_map_ dedup set; a request already dispatched to
    /// the AsyncScenePool will still complete (its result is accepted on the
    /// next poll inside tick()).
    void cancel(std::string_view asset_path);

    /// Drive loading.
    ///   Sync mode  (use_async == false): process the highest-priority pending
    ///              request synchronously — at most one load per tick() call.
    ///   Async mode (use_async == true):  submit ALL pending requests to the
    ///              AsyncScenePool, then drain the pool's completion queue
    ///              into completed_paths_.
    /// `dt` is reserved for future budget / time-slicing.
    void tick(float dt);

    /// Block until the AsyncScenePool has processed all outstanding requests.
    /// No-op in sync mode. Safe to call from the owner thread.
    void join_pending();

    // ---- Queries ------------------------------------------------------------

    /// True if the asset has been successfully loaded.
    [[nodiscard]] bool is_loaded(std::string_view asset_path) const;

    /// Returns the SceneId if loaded, std::nullopt otherwise (pending/failed/
    /// unknown).
    [[nodiscard]] std::optional<SceneId>
    get_loaded(std::string_view asset_path) const;

    /// Read-only access to the REAL parsed scene for a loaded path, or nullptr
    /// if not loaded. Lets consumers + tests verify a genuine node/mesh tree
    /// was parsed (not an empty placeholder). Pointer is stable until the
    /// streamer is destroyed (completed_ never erases entries).
    [[nodiscard]] const cd::asset::gltf::LoadedScene*
    get_scene(std::string_view asset_path) const;

    /// Number of enqueued requests not yet loaded or failed.
    [[nodiscard]] std::size_t pending_count() const noexcept;

    /// Number of successfully completed loads since construction.
    [[nodiscard]] std::size_t completed_count() const noexcept;

private:
    // ---- Internal state -----------------------------------------------------

    // Pending queue entry: path + priority.
    struct PendingEntry
    {
        std::string  path;
        std::uint8_t priority { 0U };
    };

    // Loaded scene record stored in completion table.
    struct LoadedRecord
    {
        cd::asset::gltf::LoadedScene scene;
        SceneId                      id {};
    };

    // Configuration (set at construction, immutable thereafter).
    SceneStreamerConfig config_;

    // Sprint-2 async pool — only constructed when config_.use_async == true.
    std::unique_ptr<AsyncScenePool> async_pool_;

    // Pending set: path → entry (fast cancel + dedup).
    std::unordered_map<std::string, PendingEntry> pending_map_;

    // Completed table indexed by SceneId::index.
    std::vector<LoadedRecord> completed_;

    // Reverse lookup: path → SceneId for completed assets.
    std::unordered_map<std::string, SceneId> completed_paths_;

    // ---- Helpers ------------------------------------------------------------

    /// Accept a batch of parsed scenes from the async pool and register them
    /// (with real LoadedScene data) in completed_ / completed_paths_.
    void accept_async_completions(std::vector<CompletedScene> done);

    /// Sprint-1 sync tick: process the single highest-priority pending entry.
    void tick_sync_impl();

    /// Sprint-2 async tick: drain pending_map_ into the AsyncScenePool.
    void tick_async_impl();
};

}  // namespace cd::asset::scene_streamer

// =============================================================================
// CHROMODYNAMIC — cd/asset/streamer_pool/StreamerPool.hpp
// Phase 705 — cd::asset::streamer_pool — unified asset-streamer coordinator
//
// Coordinates four Sprint-1 streamers under a single tick() call:
//   cd::asset::scene_streamer::SceneStreamer
//   cd::asset::texture_streamer::TextureStreamer
//   cd::asset::audio_streamer::AudioStreamer
//   cd::asset::shader_cache::ShaderCache (cache-only; no async queue)
//
// Dispatch strategy (Sprint-1):
//   Priority-weighted round-robin: each call to tick() distributes up to
//   `max_concurrent_loads` token(s) across the attached streamers.  Tokens are
//   allocated in proportion to each streamer's configured priority weight
//   (scene > texture > audio > shader) using a Bresenham-style deficit
//   accumulator so that no pending streamer is ever starved.
//
// Sprint-2 plan (SEALED — multi-week):
//   Replace the synchronous inline calls with async job submissions into a
//   dedicated background thread pool owned by StreamerPool; max_concurrent_loads
//   becomes a true inflight-job cap with GPU-upload integration.
//   Requires: (a) IDevice async-upload API on the RHI layer, (b) GPU fence
//   signalling across the upload queue, (c) engine-side residency table.
//   See docs/PROJECT_COMPLETION_STATUS.md for the seal rationale.
//
// Namespace: cd::asset::streamer_pool
// =============================================================================
#pragma once

#include <cd/asset/audio_streamer/AudioStreamer.hpp>
#include <cd/asset/scene_streamer/SceneStreamer.hpp>
#include <cd/asset/shader_cache/ShaderCache.hpp>
#include <cd/asset/texture_streamer/TextureStreamer.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace cd::asset::streamer_pool
{

// =============================================================================
// PoolConfig
// =============================================================================

/// Construction-time configuration for StreamerPool.
///
/// `max_concurrent_loads` — upper bound on the number of load tokens
///   distributed per tick() call.  Sprint-1: each tick of a child streamer
///   consumes one token.
///
/// `*_priority` — relative scheduling weight.  Higher value = more tokens
///   allocated per StreamerPool::tick() when demand is equal.  Unused
///   streamers (nullptr) contribute 0 tokens.
struct PoolConfig
{
    std::uint32_t max_concurrent_loads { 4U };
    std::uint32_t scene_priority       { 10U };
    std::uint32_t texture_priority     { 8U };
    std::uint32_t audio_priority       { 6U };
    std::uint32_t shader_priority      { 4U };
};

// =============================================================================
// PoolStats
// =============================================================================

/// Snapshot of pending / completed counts across all attached streamers.
///
/// `shader_pending`   — always 0 (ShaderCache is on-demand, not queued).
/// `shader_completed` — maps to ShaderCache::entry_count().
struct PoolStats
{
    std::uint32_t scene_pending    { 0U };
    std::uint32_t scene_completed  { 0U };
    std::uint32_t texture_pending  { 0U };
    std::uint32_t texture_completed{ 0U };
    std::uint32_t audio_pending    { 0U };
    std::uint32_t audio_completed  { 0U };
    std::uint32_t shader_pending   { 0U };  ///< Always 0.
    std::uint32_t shader_completed { 0U };  ///< ShaderCache::entry_count().
};

// =============================================================================
// Domain tag used by CompletionCallback to identify which streamer completed.
// =============================================================================

enum class StreamerDomain : std::uint8_t
{
    Scene   = 0U,
    Texture = 1U,
    Audio   = 2U,
};

// =============================================================================
// CompletionCallback
// =============================================================================

/// Signature for the optional per-item completion notification.
///
/// `domain`     — which child streamer finished the item.
/// `asset_path` — the path that was enqueued (as passed to enqueue_*).
/// `completed`  — cumulative completed count on that streamer after this item.
///
/// The callback is called from the same thread as tick(); it must NOT call
/// back into StreamerPool (no re-entrant tick/cancel/enqueue).
using CompletionCallback =
    std::function<void(StreamerDomain domain,
                       const std::string& asset_path,
                       std::uint32_t      completed)>;

// =============================================================================
// StreamerPool
// =============================================================================

/// Unified coordinator that ticks multiple asset streamers in a single call.
///
/// Lifecycle:
///   StreamerPool pool;
///   pool.configure({ .max_concurrent_loads = 4 });
///   pool.attach_scene(&my_scene_streamer);
///   pool.attach_texture(&my_texture_streamer, &my_rhi_device);
///   pool.attach_audio(&my_audio_streamer);
///   // Optional: per-item completion notification.
///   pool.set_completion_callback([](auto domain, auto& path, auto n) { ... });
///   // In the engine loop:
///   pool.tick(dt);
///   if (pool.is_idle()) { /* all streamers drained */ }
///   auto s = pool.stats(); // unified pending/completed view
///   pool.pause();          // suppress dispatch without detaching
///   pool.resume();
///   pool.cancel_all();     // forward cancel to all attached streamers
///
/// Ownership: StreamerPool holds NON-OWNING pointers.  The caller is
/// responsible for ensuring the lifetime of attached objects outlasts the pool.
///
/// Thread safety: NOT thread-safe.  All calls must originate from the same
/// thread.
class StreamerPool
{
public:
    StreamerPool()  = default;
    ~StreamerPool() = default;

    // Non-copyable; movable.
    StreamerPool(const StreamerPool&)            = delete;
    StreamerPool& operator=(const StreamerPool&) = delete;
    StreamerPool(StreamerPool&&)                 = default;
    StreamerPool& operator=(StreamerPool&&)      = default;

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /// Apply pool configuration.  May be called before the first tick().
    /// Calling configure() resets the internal deficit accumulators.
    void configure(const PoolConfig& cfg);

    // -------------------------------------------------------------------------
    // Attachment
    // -------------------------------------------------------------------------

    /// Attach a scene streamer.  Pass nullptr to detach.
    void attach_scene(cd::asset::scene_streamer::SceneStreamer* streamer);

    /// Attach a texture streamer.  `device` must remain valid while attached.
    /// Pass nullptr for both to detach.
    void attach_texture(cd::asset::texture_streamer::TextureStreamer* streamer,
                        cd::rhi::IDevice*                             device);

    /// Attach an audio streamer.  Pass nullptr to detach.
    void attach_audio(cd::asset::audio_streamer::AudioStreamer* streamer);

    /// Attach a shader cache (stats-only; no async queue).  Pass nullptr to detach.
    void attach_shader(cd::asset::shader_cache::ShaderCache* cache);

    // -------------------------------------------------------------------------
    // Convenience enqueue facades
    // -------------------------------------------------------------------------

    /// Enqueue a scene load request via the attached scene streamer.
    /// No-op if no scene streamer is attached.
    void enqueue_scene(cd::asset::scene_streamer::StreamRequest request);

    /// Enqueue a texture load request via the attached texture streamer.
    /// No-op if no texture streamer is attached.
    void enqueue_texture(cd::asset::texture_streamer::StreamRequest request);

    /// Enqueue an audio load request via the attached audio streamer.
    /// No-op if no audio streamer is attached.
    void enqueue_audio(cd::asset::audio_streamer::StreamRequest request);

    // -------------------------------------------------------------------------
    // Backpressure
    // -------------------------------------------------------------------------

    /// Pause tick dispatch: subsequent tick() calls return without dispatching
    /// any tokens.  Streamers remain attached; stats() still reflects their
    /// current counts.  No-op if already paused.
    void pause() noexcept;

    /// Resume tick dispatch after a prior pause().  No-op if not paused.
    void resume() noexcept;

    /// True while dispatch is suppressed (after pause(), before resume()).
    [[nodiscard]] bool is_paused() const noexcept;

    // -------------------------------------------------------------------------
    // Cancellation
    // -------------------------------------------------------------------------

    /// Cancel all pending requests on every attached streamer.  Completed
    /// (already loaded) items are unaffected.  Shader cache has no pending
    /// queue and is not touched.
    void cancel_all();

    // -------------------------------------------------------------------------
    // Engine-loop integration
    // -------------------------------------------------------------------------

    /// Distribute load tokens across attached streamers using a priority-
    /// weighted round-robin.  Streamers with more pending work and higher
    /// priority receive more tokens per call.
    ///
    /// `dt` — frame delta time in seconds; forwarded to child streamers for
    ///         future time-budget slicing (Sprint-2).
    ///
    /// Fires the registered CompletionCallback (if any) for each item that
    /// completes during this tick — detected by comparing completed_count()
    /// before and after each child tick.
    ///
    /// No-op when paused.
    void tick(float dt);

    // -------------------------------------------------------------------------
    // Observation
    // -------------------------------------------------------------------------

    /// Return a unified snapshot of pending and completed counts.
    [[nodiscard]] PoolStats stats() const;

    /// True when every attached streamer has zero pending requests.
    /// Always true when no streamers are attached.
    [[nodiscard]] bool is_idle() const;

    /// Aggregate pending count across all attached async streamers
    /// (scene + texture + audio).  Equivalent to
    /// stats().scene_pending + stats().texture_pending + stats().audio_pending.
    [[nodiscard]] std::uint32_t total_pending() const;

    // -------------------------------------------------------------------------
    // Completion notification
    // -------------------------------------------------------------------------

    /// Register a callback invoked once per completed item inside tick().
    /// Pass an empty std::function to clear the callback.
    /// The callback is called on the tick() thread; re-entrant tick/cancel is
    /// forbidden from within the callback.
    void set_completion_callback(CompletionCallback cb);

private:
    // ---- Configuration -------------------------------------------------------
    PoolConfig cfg_ {};

    // ---- Attached streamers (non-owning) -------------------------------------
    cd::asset::scene_streamer::SceneStreamer*     scene_   { nullptr };
    cd::asset::texture_streamer::TextureStreamer* texture_ { nullptr };
    cd::rhi::IDevice*                             device_  { nullptr };
    cd::asset::audio_streamer::AudioStreamer*     audio_   { nullptr };
    cd::asset::shader_cache::ShaderCache*         shader_  { nullptr };

    // ---- Bresenham deficit accumulators (reset on configure()) ---------------
    std::int64_t deficit_scene_   { 0 };
    std::int64_t deficit_texture_ { 0 };
    std::int64_t deficit_audio_   { 0 };
    std::int64_t deficit_shader_  { 0 };

    // ---- Backpressure --------------------------------------------------------
    bool paused_ { false };

    // ---- Completion notification ----------------------------------------------
    CompletionCallback completion_cb_ {};

    // ---- Path tracking for cancel_all() -------------------------------------
    // Paths submitted via the pool's enqueue_* facades are recorded here so
    // cancel_all() can call cancel(path) on each child streamer without
    // requiring an iterate-pending-map API on the children.
    std::vector<std::string> tracked_scene_paths_   {};
    std::vector<std::string> tracked_texture_paths_ {};
    std::vector<std::string> tracked_audio_paths_   {};
};

}  // namespace cd::asset::streamer_pool

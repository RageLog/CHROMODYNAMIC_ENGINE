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
// Sprint-2 plan:
//   Replace the synchronous inline calls with async job submissions into a
//   thread pool; max_concurrent_loads becomes an inflight-job cap.
//
// Namespace: cd::asset::streamer_pool
// =============================================================================
#pragma once

#include <cd/asset/audio_streamer/AudioStreamer.hpp>
#include <cd/asset/scene_streamer/SceneStreamer.hpp>
#include <cd/asset/shader_cache/ShaderCache.hpp>
#include <cd/asset/texture_streamer/TextureStreamer.hpp>

#include <cstdint>

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
///   // In the engine loop:
///   pool.tick(dt);
///   auto s = pool.stats(); // unified pending/completed view
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
    // Engine-loop integration
    // -------------------------------------------------------------------------

    /// Distribute load tokens across attached streamers using a priority-
    /// weighted round-robin.  Streamers with more pending work and higher
    /// priority receive more tokens per call.
    ///
    /// `dt` — frame delta time in seconds; forwarded to child streamers for
    ///         future time-budget slicing (Sprint-2).
    void tick(float dt);

    // -------------------------------------------------------------------------
    // Observation
    // -------------------------------------------------------------------------

    /// Return a unified snapshot of pending and completed counts.
    [[nodiscard]] PoolStats stats() const;

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
};

}  // namespace cd::asset::streamer_pool

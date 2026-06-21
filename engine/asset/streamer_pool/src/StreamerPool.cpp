// =============================================================================
// CHROMODYNAMIC — engine/asset/streamer_pool/src/StreamerPool.cpp
// Phase 705 — cd::asset::streamer_pool implementation
//
// Priority-weighted round-robin dispatch (Sprint-1):
//
//   Total weight W = sum of priorities of streamers that have pending work.
//   Each streamer is allocated floor(priority/W * max_concurrent_loads) tokens,
//   with a Bresenham-style deficit accumulator to distribute any remainder.
//
//   If no streamer has pending work, tick() is a no-op.
//   ShaderCache is cache-only (on-demand); it never receives tick tokens.
// =============================================================================

#include <cd/asset/streamer_pool/StreamerPool.hpp>

#include <cstdint>
#include <string>
#include <utility>

namespace cd::asset::streamer_pool
{

// =============================================================================
// configure
// =============================================================================

void StreamerPool::configure(const PoolConfig& cfg)
{
    cfg_             = cfg;
    deficit_scene_   = 0;
    deficit_texture_ = 0;
    deficit_audio_   = 0;
    deficit_shader_  = 0;
}

// =============================================================================
// Attachment
// =============================================================================

void StreamerPool::attach_scene(cd::asset::scene_streamer::SceneStreamer* streamer)
{
    scene_ = streamer;
}

void StreamerPool::attach_texture(cd::asset::texture_streamer::TextureStreamer* streamer,
                                   cd::rhi::IDevice*                             device)
{
    texture_ = streamer;
    device_  = device;
}

void StreamerPool::attach_audio(cd::asset::audio_streamer::AudioStreamer* streamer)
{
    audio_ = streamer;
}

void StreamerPool::attach_shader(cd::asset::shader_cache::ShaderCache* cache)
{
    shader_ = cache;
}

// =============================================================================
// Convenience enqueue facades
// =============================================================================

void StreamerPool::enqueue_scene(cd::asset::scene_streamer::StreamRequest request)
{
    if (scene_ != nullptr)
    {
        tracked_scene_paths_.push_back(request.asset_path);
        scene_->enqueue(std::move(request));
    }
}

void StreamerPool::enqueue_texture(cd::asset::texture_streamer::StreamRequest request)
{
    if (texture_ != nullptr)
    {
        tracked_texture_paths_.push_back(request.asset_path);
        texture_->enqueue(std::move(request));
    }
}

void StreamerPool::enqueue_audio(cd::asset::audio_streamer::StreamRequest request)
{
    if (audio_ != nullptr)
    {
        tracked_audio_paths_.push_back(request.asset_path);
        audio_->enqueue(std::move(request));
    }
}

// =============================================================================
// Backpressure
// =============================================================================

void StreamerPool::pause() noexcept
{
    paused_ = true;
}

void StreamerPool::resume() noexcept
{
    paused_ = false;
}

bool StreamerPool::is_paused() const noexcept
{
    return paused_;
}

// =============================================================================
// Cancellation
// =============================================================================

void StreamerPool::cancel_all()
{
    // Cancel every path submitted via the pool's own enqueue_* facades.
    // Each child streamer's cancel(path) is idempotent for unknown or
    // already-loaded paths, so iterating the tracked lists is safe even if
    // some items completed between enqueue and cancel_all.
    //
    // Paths enqueued directly on the child streamer (bypassing the pool facade)
    // are NOT tracked here — callers that bypass the facade must cancel those
    // themselves.  This is the behaviour-preserving contract: only pool-mediated
    // enqueues are managed by the pool.

    if (scene_ != nullptr)
    {
        for (const std::string& path : tracked_scene_paths_)
        {
            scene_->cancel(path);
        }
    }
    tracked_scene_paths_.clear();

    if (texture_ != nullptr)
    {
        for (const std::string& path : tracked_texture_paths_)
        {
            texture_->cancel(path);
        }
    }
    tracked_texture_paths_.clear();

    if (audio_ != nullptr)
    {
        for (const std::string& path : tracked_audio_paths_)
        {
            audio_->cancel(path);
        }
    }
    tracked_audio_paths_.clear();
}

// =============================================================================
// set_completion_callback
// =============================================================================

void StreamerPool::set_completion_callback(CompletionCallback cb)
{
    completion_cb_ = std::move(cb);
}

// =============================================================================
// tick — priority-weighted round-robin
// =============================================================================

void StreamerPool::tick(const float dt)
{
    // Backpressure gate.
    if (paused_)
    {
        return;
    }

    // ---- Determine active streamers (attached + have pending work) -----------

    const bool scene_active   = (scene_   != nullptr) && (scene_->pending_count()   > 0U);
    const bool texture_active = (texture_ != nullptr) && (device_ != nullptr)
                                  && (texture_->pending_count() > 0U);
    const bool audio_active   = (audio_   != nullptr) && (audio_->pending_count()   > 0U);
    // ShaderCache: no async pending queue — never active for tick dispatch.

    const auto total_weight =
        (scene_active   ? cfg_.scene_priority   : 0U)
      + (texture_active ? cfg_.texture_priority : 0U)
      + (audio_active   ? cfg_.audio_priority   : 0U);

    if (total_weight == 0U)
    {
        return;  // Nothing pending.
    }

    // ---- Bresenham deficit accumulation -------------------------------------
    //
    // For each active streamer i:
    //   deficit_i += priority_i * max_concurrent_loads
    //   tokens_i   = deficit_i / total_weight
    //   deficit_i -= tokens_i  * total_weight
    //
    // This distributes exactly max_concurrent_loads tokens total across active
    // streamers in proportion to their priority weights, rounding favourably
    // toward higher-priority streamers without drift over time.

    const auto w = static_cast<std::int64_t>(total_weight);
    const auto b = static_cast<std::int64_t>(cfg_.max_concurrent_loads);

    auto compute_tokens = [&](std::int64_t& deficit,
                               std::uint32_t priority,
                               bool          active) -> std::uint32_t
    {
        if (!active)
        {
            return 0U;
        }
        deficit += static_cast<std::int64_t>(priority) * b;
        const std::int64_t tokens = deficit / w;
        deficit -= tokens * w;
        return static_cast<std::uint32_t>(tokens);
    };

    const std::uint32_t scene_tokens   = compute_tokens(deficit_scene_,   cfg_.scene_priority,   scene_active);
    const std::uint32_t texture_tokens = compute_tokens(deficit_texture_,  cfg_.texture_priority, texture_active);
    const std::uint32_t audio_tokens   = compute_tokens(deficit_audio_,    cfg_.audio_priority,   audio_active);

    // ---- Dispatch (with optional completion callback) -----------------------

    const bool has_cb = static_cast<bool>(completion_cb_);

    for (std::uint32_t i = 0U; i < scene_tokens; ++i)
    {
        if (scene_->pending_count() == 0U) { break; }
        const auto before = has_cb ? scene_->completed_count() : std::size_t{0U};
        scene_->tick(dt);
        if (has_cb)
        {
            const auto after = scene_->completed_count();
            if (after > before)
            {
                completion_cb_(StreamerDomain::Scene,
                               std::string{},
                               static_cast<std::uint32_t>(after));
            }
        }
    }

    for (std::uint32_t i = 0U; i < texture_tokens; ++i)
    {
        if (texture_->pending_count() == 0U) { break; }
        const auto before = has_cb ? texture_->completed_count() : std::size_t{0U};
        texture_->tick(dt, *device_);
        if (has_cb)
        {
            const auto after = texture_->completed_count();
            if (after > before)
            {
                completion_cb_(StreamerDomain::Texture,
                               std::string{},
                               static_cast<std::uint32_t>(after));
            }
        }
    }

    for (std::uint32_t i = 0U; i < audio_tokens; ++i)
    {
        if (audio_->pending_count() == 0U) { break; }
        const auto before = has_cb ? audio_->completed_count() : std::size_t{0U};
        audio_->tick(dt);
        if (has_cb)
        {
            const auto after = audio_->completed_count();
            if (after > before)
            {
                completion_cb_(StreamerDomain::Audio,
                               std::string{},
                               static_cast<std::uint32_t>(after));
            }
        }
    }
}

// =============================================================================
// stats
// =============================================================================

PoolStats StreamerPool::stats() const
{
    PoolStats s {};

    if (scene_ != nullptr)
    {
        s.scene_pending   = static_cast<std::uint32_t>(scene_->pending_count());
        s.scene_completed = static_cast<std::uint32_t>(scene_->completed_count());
    }

    if (texture_ != nullptr)
    {
        s.texture_pending   = static_cast<std::uint32_t>(texture_->pending_count());
        s.texture_completed = static_cast<std::uint32_t>(texture_->completed_count());
    }

    if (audio_ != nullptr)
    {
        s.audio_pending   = static_cast<std::uint32_t>(audio_->pending_count());
        s.audio_completed = static_cast<std::uint32_t>(audio_->completed_count());
    }

    if (shader_ != nullptr)
    {
        s.shader_pending   = 0U;  // ShaderCache has no async queue.
        s.shader_completed = static_cast<std::uint32_t>(shader_->entry_count());
    }

    return s;
}

// =============================================================================
// is_idle
// =============================================================================

bool StreamerPool::is_idle() const
{
    if (scene_   != nullptr && scene_->pending_count()   > 0U) { return false; }
    if (texture_ != nullptr && texture_->pending_count() > 0U) { return false; }
    if (audio_   != nullptr && audio_->pending_count()   > 0U) { return false; }
    return true;
}

// =============================================================================
// total_pending
// =============================================================================

std::uint32_t StreamerPool::total_pending() const
{
    std::uint32_t n = 0U;
    if (scene_   != nullptr) { n += static_cast<std::uint32_t>(scene_->pending_count());   }
    if (texture_ != nullptr) { n += static_cast<std::uint32_t>(texture_->pending_count()); }
    if (audio_   != nullptr) { n += static_cast<std::uint32_t>(audio_->pending_count());   }
    return n;
}

}  // namespace cd::asset::streamer_pool

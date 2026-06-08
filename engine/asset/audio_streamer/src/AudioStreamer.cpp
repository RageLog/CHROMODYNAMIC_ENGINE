// =============================================================================
// CHROMODYNAMIC — cd/asset/audio_streamer/AudioStreamer.cpp
// Phase 619 — cd::asset::audio_streamer implementation (Sprint-1: synchronous)
// Phase 756 — Sprint-2: opt-in async path via AsyncAudioPool
//
// Sprint-1 strategy (use_async == false, default):
//   * On tick(), pick the highest-priority pending entry via O(n) scan.
//     (Sprint-2 will replace with a priority_queue for O(log n).)
//   * Compute cd::asset::AssetId::from_path(path) as the "clip id". This hash
//     is always deterministic and never fails, modelling synchronous registration
//     in the asset registry without real I/O. This will be replaced with actual
//     WAV/OGG decode + audio-device upload in a future Sprint.
//   * channel_target is carried through to LoadedRecord for Sprint-2 routing.
//   * Failure scenarios (e.g. kNullAssetId sentinel) are propagated as a silent
//     drop — callers detect via is_loaded() remaining false.
//
// Sprint-2 strategy (use_async == true):
//   * AsyncAudioPool is started with config_.worker_count threads.
//   * tick() submits all pending requests to the pool, then drains the
//     completion queue into completed_paths_.
//   * AssetId registration happens on the owner thread from each completed
//     path — avoiding any concurrency issues with the id registry.
// =============================================================================

#include <cd/asset/audio_streamer/AudioStreamer.hpp>

#include <algorithm>
#include <utility>

namespace cd::asset::audio_streamer
{

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

AudioStreamer::AudioStreamer(AudioStreamerConfig cfg)
    : config_ { cfg }
{
    if (config_.use_async)
    {
        async_pool_ = std::make_unique<AsyncAudioPool>();
        async_pool_->configure(config_.worker_count);
    }
}

AudioStreamer::~AudioStreamer() = default;

// ---------------------------------------------------------------------------
// enqueue
// ---------------------------------------------------------------------------

void AudioStreamer::enqueue(StreamRequest request)
{
    // Idempotent: already loaded → no-op.
    if (completed_paths_.contains(request.asset_path))
    {
        return;
    }

    // Idempotent: already pending → no-op (do not update priority or channel).
    if (pending_map_.contains(request.asset_path))
    {
        return;
    }

    const std::uint8_t channel = request.channel_target;
    const std::uint8_t prio    = request.priority;
    const std::string  path    = std::move(request.asset_path);

    pending_map_.emplace(path, PendingEntry{ path, channel, prio });
}

// ---------------------------------------------------------------------------
// cancel
// ---------------------------------------------------------------------------

void AudioStreamer::cancel(std::string_view asset_path)
{
    // Only removes from the local dedup set — completed entries are immutable.
    // In async mode a request already in the pool will still complete; its
    // result is accepted on the next poll in tick().
    pending_map_.erase(std::string{ asset_path });
}

// ---------------------------------------------------------------------------
// tick — sync path implementation (Sprint-1 default)
// ---------------------------------------------------------------------------

void AudioStreamer::tick_sync_impl()
{
    if (pending_map_.empty())
    {
        return;
    }

    // Select the highest-priority pending entry (O(n), acceptable for Sprint-1).
    const auto best = std::ranges::max_element(
        pending_map_,
        [](const auto& lhs, const auto& rhs) noexcept {
            return lhs.second.priority < rhs.second.priority;
        });

    const std::string  path    = best->first;
    const std::uint8_t channel = best->second.channel_target;
    pending_map_.erase(best);

    // Sprint-1: derive AssetId from path (deterministic hash, never fails).
    // Sprint-2 will replace this with real WAV decode + audio-device upload.
    const cd::asset::AssetId id = cd::asset::AssetId::from_path(path);

    // Guard: a zero-valued AssetId (empty path) is treated as failure.
    if (!id.is_valid())
    {
        // Silently drop — path is consumed from pending but not added to
        // completed. Caller detects via is_loaded() returning false.
        return;
    }

    completed_.push_back(LoadedRecord{ id, channel });
    completed_paths_.emplace(path, id);
}

// ---------------------------------------------------------------------------
// tick — async path implementation (Sprint-2)
// ---------------------------------------------------------------------------

void AudioStreamer::tick_async_impl()
{
    // Submit all pending requests to the worker pool.
    for (auto& [path, entry] : pending_map_)
    {
        async_pool_->submit_async(StreamRequest{ entry.path, entry.channel_target, entry.priority });
    }
    pending_map_.clear();
}

// ---------------------------------------------------------------------------
// accept_async_completions (private helper)
// ---------------------------------------------------------------------------

void AudioStreamer::accept_async_completions(std::vector<std::string> paths)
{
    for (auto& path : paths)
    {
        if (completed_paths_.contains(path))
        {
            continue;  // async pool may complete a path that was already cancelled
        }

        // Derive AssetId on the owner thread — same strategy as Sprint-1.
        const cd::asset::AssetId id = cd::asset::AssetId::from_path(path);
        if (!id.is_valid())
        {
            continue;  // silently drop invalid paths
        }

        completed_.push_back(LoadedRecord{ id, 0U });
        completed_paths_.emplace(path, id);
    }
}

// ---------------------------------------------------------------------------
// tick (public)
// ---------------------------------------------------------------------------

void AudioStreamer::tick(float /*dt*/)
{
    if (config_.use_async)
    {
        // Submit all pending requests.
        tick_async_impl();

        // Drain whatever has already completed on worker threads.
        auto done = async_pool_->poll_completed();
        accept_async_completions(std::move(done));
    }
    else
    {
        tick_sync_impl();
    }
}

// ---------------------------------------------------------------------------
// join_pending
// ---------------------------------------------------------------------------

void AudioStreamer::join_pending()
{
    if (!config_.use_async || !async_pool_)
    {
        return;
    }
    async_pool_->join_all();

    // Drain any remaining completed paths into our table.
    auto done = async_pool_->poll_completed();
    accept_async_completions(std::move(done));
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

bool AudioStreamer::is_loaded(std::string_view asset_path) const
{
    return completed_paths_.contains(std::string{ asset_path });
}

std::optional<cd::asset::AssetId>
AudioStreamer::get_loaded(std::string_view asset_path) const
{
    const auto it = completed_paths_.find(std::string{ asset_path });
    if (it == completed_paths_.cend())
    {
        return std::nullopt;
    }
    return it->second;
}

std::size_t AudioStreamer::pending_count() const noexcept
{
    return pending_map_.size();
}

std::size_t AudioStreamer::completed_count() const noexcept
{
    if (config_.use_async && async_pool_)
    {
        // In async mode, completed_ may lag behind the pool's counter until
        // the next poll; report completed_paths_ size for consistency with
        // what the owner thread can actually observe.
        return completed_paths_.size();
    }
    return completed_.size();
}

}  // namespace cd::asset::audio_streamer

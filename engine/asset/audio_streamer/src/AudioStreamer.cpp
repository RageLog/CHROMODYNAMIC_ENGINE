// =============================================================================
// CHROMODYNAMIC — cd/asset/audio_streamer/AudioStreamer.cpp
// Phase 619 — cd::asset::audio_streamer implementation (Sprint-1: synchronous)
//
// Sprint-1 strategy:
//   * On tick(), pick the highest-priority pending entry via O(n) scan.
//     (Sprint-2 will replace with a priority_queue for O(log n).)
//   * Compute cd::asset::AssetId::from_path(path) as the "clip id". This hash
//     is always deterministic and never fails, modelling synchronous registration
//     in the asset registry without real I/O. This will be replaced with actual
//     WAV/OGG decode + audio-device upload in Sprint-2.
//   * channel_target is carried through to LoadedRecord for Sprint-2 routing.
//   * Failure scenarios (e.g. kNullAssetId sentinel) are propagated as a silent
//     drop — callers detect via is_loaded() remaining false.
// =============================================================================

#include <cd/asset/audio_streamer/AudioStreamer.hpp>

#include <algorithm>
#include <utility>

namespace cd::asset::audio_streamer
{

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

void AudioStreamer::cancel(std::string_view asset_path)
{
    // Only removes from the pending set — completed entries are immutable.
    pending_map_.erase(std::string{ asset_path });
}

void AudioStreamer::tick(float /*dt*/)
{
    if (pending_map_.empty())
    {
        return;
    }

    // Select the highest-priority pending entry (O(n), acceptable for Sprint-1).
    const auto best = std::max_element(
        pending_map_.cbegin(),
        pending_map_.cend(),
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
    return completed_.size();
}

}  // namespace cd::asset::audio_streamer

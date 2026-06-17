// =============================================================================
// CHROMODYNAMIC — cd/asset/audio_streamer/AudioStreamer.cpp
// Phase 619 — cd::asset::audio_streamer implementation (Sprint-1: synchronous)
// Phase 756 — opt-in async path via AsyncAudioPool
// Band 6   — real WAV PCM decode wired into BOTH paths; OGG SEALED.
//
// Decode dispatch (decode_audio_file):
//   * ".wav" → cd::asset::wav::load → DecodedAudio { channels, sample_rate,
//     bits_per_sample, frame_count, interleaved PCM bytes }.
//   * ".ogg" / any other extension → std::nullopt. There is no Vorbis decoder
//     at the asset layer yet; sealing it keeps the streamer honest (an .ogg
//     request fails to decode rather than fabricating a placeholder). The
//     trigger to lift the seal is a cd::asset::ogg loader — see the Band-6 ADR.
//
// Sync mode (use_async == false, default):
//   * Pick the highest-priority pending entry, run decode_audio_file().
//   * The clip id is cd::asset::AssetId::from_path(path); the record carries
//     the REAL decoded channels / sample_rate / frame_count.
//   * A failed decode silently drops the entry (is_loaded stays false).
//
// Async mode (use_async == true):
//   * AsyncAudioPool workers decode each request and hand back real PCM.
//   * tick() / join_pending() drain decoded clips on the owner thread.
// =============================================================================

#include <cd/asset/audio_streamer/AudioStreamer.hpp>

#include <cd/asset/wav/Wav.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

namespace cd::asset::audio_streamer
{

// ---------------------------------------------------------------------------
// decode_audio_file — real WAV CPU decode (shared by sync + worker paths)
// ---------------------------------------------------------------------------

std::optional<DecodedAudio> decode_audio_file(std::string_view path)
{
    // Only .wav is decodable at the asset layer today. .ogg is SEALED.
    const bool is_wav = path.size() >= 4U && path.substr(path.size() - 4U) == ".wav";
    if (!is_wav)
    {
        return std::nullopt;
    }

    auto r = cd::asset::wav::load(path);
    if (!r.has_value())
    {
        return std::nullopt;
    }

    DecodedAudio out;
    out.channels        = r->channels;
    out.sample_rate     = r->sample_rate;
    out.bits_per_sample = r->bits_per_sample;
    out.frame_count     = static_cast<std::uint64_t>(r->frame_count());

    // cd::asset::wav stores samples as std::byte; copy into the uint8 PCM view.
    out.pcm.resize(r->samples.size());
    if (!r->samples.empty())
    {
        std::memcpy(out.pcm.data(), r->samples.data(), r->samples.size());
    }
    return out;
}

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
    if (completed_index_.contains(request.asset_path))
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

    // Real CPU decode — WAV RIFF/WAVE (OGG sealed → nullopt).
    auto decoded = decode_audio_file(path);
    if (!decoded.has_value())
    {
        // Decode / IO failure (or sealed .ogg) — silently drop. Caller detects
        // via is_loaded() returning false.
        return;
    }

    register_clip(path, channel, *decoded);
}

// ---------------------------------------------------------------------------
// register_clip (private helper) — owner-thread record registration
// ---------------------------------------------------------------------------

void AudioStreamer::register_clip(const std::string& path,
                                  std::uint8_t        channel,
                                  const DecodedAudio& decoded)
{
    // The clip id is a stable hash of the path; the record carries the REAL
    // decoded format so consumers can route + size their audio buffers.
    const cd::asset::AssetId id = cd::asset::AssetId::from_path(path);
    if (!id.is_valid())
    {
        return;  // empty path → invalid id → drop
    }

    const std::size_t idx = completed_.size();
    completed_.push_back(LoadedRecord{ id, channel, decoded.channels,
                                       decoded.sample_rate, decoded.frame_count });
    completed_index_.emplace(path, idx);
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

void AudioStreamer::accept_async_completions(std::vector<CompletedAudio> done)
{
    // Each completion already carries the REAL decoded PCM + format from a
    // worker. We register the record on the owner thread.
    for (auto& item : done)
    {
        if (completed_index_.contains(item.path))
        {
            continue;  // async pool may complete a path that was already cancelled
        }
        register_clip(item.path, 0U, item.decoded);
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
    return completed_index_.contains(std::string{ asset_path });
}

std::optional<cd::asset::AssetId>
AudioStreamer::get_loaded(std::string_view asset_path) const
{
    const auto it = completed_index_.find(std::string{ asset_path });
    if (it == completed_index_.cend())
    {
        return std::nullopt;
    }
    return completed_.at(it->second).id;
}

std::optional<AudioStreamer::ClipFormat>
AudioStreamer::get_format(std::string_view asset_path) const
{
    const auto it = completed_index_.find(std::string{ asset_path });
    if (it == completed_index_.cend())
    {
        return std::nullopt;
    }
    const auto& rec = completed_.at(it->second);
    return ClipFormat{ rec.channels, rec.sample_rate, rec.frame_count };
}

std::size_t AudioStreamer::pending_count() const noexcept
{
    return pending_map_.size();
}

std::size_t AudioStreamer::completed_count() const noexcept
{
    // completed_index_ tracks what the owner thread has actually accepted in
    // both sync and async modes.
    return completed_index_.size();
}

}  // namespace cd::asset::audio_streamer

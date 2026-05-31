// =============================================================================
// CHROMODYNAMIC — cd/asset/audio_streamer/AudioStreamer.hpp
// Phase 619 — cd::asset::audio_streamer (Sprint-1: synchronous)
//
// Audio clip streaming coordinator. Accepts enqueue/cancel requests keyed by
// asset path and drives synchronous "load" on each tick() call. Resolved clips
// are identified by cd::asset::AssetId derived from the asset path.
//
// Sprint-1 constraints:
//   * Synchronous: load happens inline on the calling thread inside tick().
//   * Priority: higher uint8 value = higher priority. Requests are served in
//     descending priority order each tick (one per call in Sprint-1).
//   * channel_target: target audio channel/bus index (reserved for routing in
//     Sprint-2; carried through but not acted on in Sprint-1).
//   * Sprint-2 will replace the synchronous load stub with async job submission
//     and real PCM decode via cd::asset_wav (or a future cd::audio library).
//
// Sibling to:
//   cd::asset::scene_streamer   (Phase 586)
//   cd::asset::texture_streamer (Phase 599)
//
// Namespace: cd::asset::audio_streamer
// =============================================================================
#pragma once

#include <cd/asset/AssetId.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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

// ---- AudioStreamer -----------------------------------------------------------

/// Synchronous audio streamer (Sprint-1).
///
/// Lifecycle:
///   AudioStreamer as;
///   as.enqueue({ "audio/music.wav", 0, 200 });
///   while (!as.is_loaded("audio/music.wav")) { as.tick(dt); }
///   auto id = as.get_loaded("audio/music.wav"); // has value
///
/// Sprint-1 load strategy: the asset path is hashed to an AssetId via
/// cd::asset::AssetId::from_path(). This is always deterministic and never
/// fails, modelling "resident in the registry" for the synchronous sprint.
/// Actual WAV decode, sample-rate conversion and device upload are deferred
/// to Sprint-2.
///
/// Thread safety: not thread-safe — all calls must come from the same thread.
class AudioStreamer
{
public:
    AudioStreamer()  = default;
    ~AudioStreamer() = default;

    // Non-copyable, movable.
    AudioStreamer(const AudioStreamer&)            = delete;
    AudioStreamer& operator=(const AudioStreamer&) = delete;
    AudioStreamer(AudioStreamer&&)                 = default;
    AudioStreamer& operator=(AudioStreamer&&)      = default;

    // ---- Mutation -----------------------------------------------------------

    /// Enqueue a load request. If the path is already pending or loaded,
    /// this is a no-op (idempotent). Priority on duplicate is NOT updated.
    void enqueue(StreamRequest request);

    /// Cancel a pending request. If the path is already loaded or unknown,
    /// this is a no-op.
    void cancel(std::string_view asset_path);

    /// Drive loading: process the highest-priority pending request.
    /// Sprint-1: at most one load per tick() call, synchronous.
    /// `dt` is reserved for future budget / time-slicing (async Sprint-2).
    void tick(float dt);

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

    // Pending set: path → entry (fast cancel + dedup).
    std::unordered_map<std::string, PendingEntry> pending_map_;

    // Completion table indexed by insertion order.
    std::vector<LoadedRecord> completed_;

    // Reverse lookup: path → AssetId for completed assets.
    std::unordered_map<std::string, cd::asset::AssetId> completed_paths_;
};

}  // namespace cd::asset::audio_streamer

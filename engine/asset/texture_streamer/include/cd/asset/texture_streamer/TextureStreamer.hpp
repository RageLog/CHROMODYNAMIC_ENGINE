// =============================================================================
// CHROMODYNAMIC — cd/asset/texture_streamer/TextureStreamer.hpp
// Phase 599 — cd::asset::texture_streamer (Sprint-1: synchronous)
//
// GPU-side texture streaming coordinator. Accepts enqueue/cancel requests
// keyed by asset path and drives synchronous upload via cd::rhi::IDevice on
// each tick() call.
//
// Sprint-1 constraints:
//   * Synchronous: the highest-priority pending request is processed inline on
//     the calling thread inside tick().
//   * Priority: higher uint8 value = higher priority (255 = highest, 0 = lowest).
//     Requests are served in descending priority order one per tick().
//   * Sprint-2 will replace the synchronous upload with async job submission.
//
// Namespace: cd::asset::texture_streamer
// =============================================================================
#pragma once

#include <cd/rhi/Handles.hpp>
#include <cd/rhi/IDevice.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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

// ---- TextureStreamer ---------------------------------------------------------

/// Synchronous texture streamer (Sprint-1).
///
/// Lifecycle:
///   cd::rhi::NullDevice device;
///   TextureStreamer ts;
///   ts.enqueue({ "textures/albedo.cdtex", 0, 200 });
///   while (!ts.is_loaded("textures/albedo.cdtex")) { ts.tick(dt, device); }
///   auto h = ts.get_loaded("textures/albedo.cdtex"); // has value
///
/// Thread safety: not thread-safe — all calls must come from the same thread.
class TextureStreamer
{
public:
    TextureStreamer()  = default;
    ~TextureStreamer() = default;

    // Non-copyable, movable.
    TextureStreamer(const TextureStreamer&)            = delete;
    TextureStreamer& operator=(const TextureStreamer&) = delete;
    TextureStreamer(TextureStreamer&&)                 = default;
    TextureStreamer& operator=(TextureStreamer&&)      = default;

    // ---- Mutation -----------------------------------------------------------

    /// Enqueue a load request. If the path is already pending or loaded,
    /// this is a no-op (idempotent). Priority on duplicate is NOT updated.
    void enqueue(StreamRequest request);

    /// Cancel a pending request. If the path is already loaded or unknown,
    /// this is a no-op.
    void cancel(std::string_view asset_path);

    /// Drive loading: process the highest-priority pending request synchronously.
    /// Sprint-1: at most one upload per tick() call.
    /// `dt` is reserved for future budget / time-slicing (async Sprint-2).
    /// `device` is used to allocate the GPU texture resource.
    void tick(float dt, cd::rhi::IDevice& device);

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

    // Pending set: path → entry (fast cancel + dedup).
    std::unordered_map<std::string, PendingEntry> pending_map_;

    // Completed table indexed by insertion order.
    std::vector<LoadedRecord> completed_;

    // Reverse lookup: path → TextureHandle for completed assets.
    std::unordered_map<std::string, cd::rhi::TextureHandle> completed_paths_;
};

}  // namespace cd::asset::texture_streamer

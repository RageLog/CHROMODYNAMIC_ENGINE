// =============================================================================
// CHROMODYNAMIC — cd/asset/scene_streamer/SceneStreamer.hpp
// Phase 586 — cd::asset::scene_streamer (Sprint-1: synchronous)
//
// High-level scene streaming coordinator. Accepts enqueue/cancel requests
// keyed by asset path and drives synchronous load via cd::asset::gltf::
// load_scene() on each tick() call.
//
// Sprint-1 constraints:
//   * Synchronous: load happens inline on the calling thread inside tick().
//   * Priority: higher uint8 value = higher priority. Requests are served in
//     descending priority order each tick (one per call in Sprint-1).
//   * Sprint-2 will replace the load call with an async job submission.
//
// Namespace: cd::asset::scene_streamer
// =============================================================================
#pragma once

#include <cd/asset/gltf/SceneLoader.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
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

// ---- SceneStreamer -----------------------------------------------------------

/// Synchronous scene streamer (Sprint-1).
///
/// Lifecycle:
///   SceneStreamer s;
///   s.enqueue({ "assets/sponza.glb", 200 });
///   while (!s.is_loaded("assets/sponza.glb")) { s.tick(dt); }
///   auto id = s.get_loaded("assets/sponza.glb"); // has value
///
/// Thread safety: not thread-safe — all calls must come from the same thread.
class SceneStreamer
{
public:
    SceneStreamer()  = default;
    ~SceneStreamer() = default;

    // Non-copyable, movable.
    SceneStreamer(const SceneStreamer&)            = delete;
    SceneStreamer& operator=(const SceneStreamer&) = delete;
    SceneStreamer(SceneStreamer&&)                 = default;
    SceneStreamer& operator=(SceneStreamer&&)      = default;

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

    /// Returns the SceneId if loaded, std::nullopt otherwise (pending/failed/
    /// unknown).
    [[nodiscard]] std::optional<SceneId>
    get_loaded(std::string_view asset_path) const;

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

    // Pending set: path → priority (fast cancel + dedup).
    std::unordered_map<std::string, std::uint8_t> pending_map_;

    // Completed table indexed by SceneId::index.
    std::vector<LoadedRecord> completed_;

    // Reverse lookup: path → SceneId for completed assets.
    std::unordered_map<std::string, SceneId> completed_paths_;
};

}  // namespace cd::asset::scene_streamer

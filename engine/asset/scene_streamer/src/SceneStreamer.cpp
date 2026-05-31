// =============================================================================
// CHROMODYNAMIC — cd/asset/scene_streamer/SceneStreamer.cpp
// Phase 586 — cd::asset::scene_streamer implementation (Sprint-1: synchronous)
// =============================================================================

#include <cd/asset/scene_streamer/SceneStreamer.hpp>

#include <algorithm>
#include <utility>

namespace cd::asset::scene_streamer
{

void SceneStreamer::enqueue(StreamRequest request)
{
    // Idempotent: already loaded → no-op.
    if (completed_paths_.contains(request.asset_path))
    {
        return;
    }

    // Idempotent: already pending → no-op (do not update priority).
    if (pending_map_.contains(request.asset_path))
    {
        return;
    }

    pending_map_.emplace(std::move(request.asset_path), request.priority);
}

void SceneStreamer::cancel(std::string_view asset_path)
{
    // Only removes from the pending set — completed entries are immutable.
    pending_map_.erase(std::string{ asset_path });
}

void SceneStreamer::tick(float /*dt*/)
{
    if (pending_map_.empty())
    {
        return;
    }

    // Select the highest-priority pending entry.
    // std::max_element over an unordered_map range — O(n) per tick,
    // acceptable for Sprint-1. Sprint-2 will use a priority_queue.
    const auto best = std::max_element(
        pending_map_.cbegin(),
        pending_map_.cend(),
        [](const auto& lhs, const auto& rhs) noexcept {
            return lhs.second < rhs.second;
        });

    const std::string path  = best->first;
    pending_map_.erase(best);

    // Synchronous load.
    auto result = cd::asset::gltf::load_scene(path);
    if (!result.has_value())
    {
        // Failed load: silently dropped (path not in pending, not in
        // completed). Callers can detect via is_loaded() remaining false.
        return;
    }

    const auto new_id = SceneId{ static_cast<std::uint32_t>(completed_.size()) };
    completed_.push_back(LoadedRecord{ std::move(*result), new_id });
    completed_paths_.emplace(path, new_id);
}

bool SceneStreamer::is_loaded(std::string_view asset_path) const
{
    return completed_paths_.contains(std::string{ asset_path });
}

std::optional<SceneId>
SceneStreamer::get_loaded(std::string_view asset_path) const
{
    const auto it = completed_paths_.find(std::string{ asset_path });
    if (it == completed_paths_.cend())
    {
        return std::nullopt;
    }
    return it->second;
}

std::size_t SceneStreamer::pending_count() const noexcept
{
    return pending_map_.size();
}

std::size_t SceneStreamer::completed_count() const noexcept
{
    return completed_.size();
}

}  // namespace cd::asset::scene_streamer
